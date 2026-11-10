/* Fast LHDC V5 entropy decoder (included by lhdc_entropy_dec.c after the reference code).
 *
 * Bit-exact with fac_decode + fac_model_update + s2_at + rice_read_quotient (checked by output hash
 * on the bench streams). The LX6 core has 16 address registers; the reference loops keep ~30 values
 * live and spill on every symbol. This version keeps the per-symbol state small:
 *
 *  - Symbols are decoded in tight runs (fs_run_*) that hold only the range coder and the model.
 *    Each symbol's frequency and rescale threshold share one word (freq << 16 | threshold), the
 *    cumulative table is two words (c1 | c2 << 16, c3), and the window-fill and sliding phases
 *    are separate loops.
 *  - Pass 2 is decoded ahead in chunks of 64 symbols into a ring (fs_ring_t, in the lazily
 *    allocated FAC state), recording the coder's byte position and range before each symbol. The
 *    Rice inverse then reads an array, and the coder state after exactly s2_used symbols (needed
 *    for the mantissa plane offset) is read back from the record. Decoding ahead is harmless: the
 *    eos/hard-cap rule is evaluated per symbol in the same order as the lazy reference, and
 *    symbols past s2_used are not used.
 *  - The rescale division 0x80000000/total is reused while the total is unchanged.
 */
#ifndef LHDC_ENTROPY_FAST_H
#define LHDC_ENTROPY_FAST_H

#define FS_CHUNK 64

typedef struct {
    const uint8_t *data;
    int len, p;
    uint32_t range, code;
    uint8_t *hist;
    int window, cnt, pos;
    uint32_t w0, w1, w2;   /* freq << 16 | threshold */
    uint32_t c12, c3;      /* c1 | c2 << 16 ; c3 */
    uint32_t total, scale;
} fs_t;

#ifdef LHDC_ENTROPY_PROBE
#include "esp_cpu.h"
uint32_t g_ep_cyc[4], g_ep_s1, g_ep_s2, g_ep_calls, g_ep_rescale, g_ep_div, g_ep_sec[16];
#define EP_NOW() esp_cpu_get_cycle_count()
#define EP_COUNT(x) ((x)++)
#else
#define EP_NOW() 0u
#define EP_COUNT(x) ((void)0)
#endif

/* Recompute thresholds and the 15-bit cumulative table from the three frequencies. */
#define FS_RESCALE() do {                                                          \
        EP_COUNT(g_ep_rescale);                                                    \
        const uint32_t f0_ = w0 >> 16, f1_ = w1 >> 16, f2_ = w2 >> 16;             \
        w0 = (f0_ << 16) | (f0_ + (f0_ >> 5));                                     \
        w1 = (f1_ << 16) | (f1_ + (f1_ >> 5));                                     \
        w2 = (f2_ << 16) | (f2_ + (f2_ >> 5));                                     \
        const uint32_t s2_ = f0_ + f1_, s3_ = s2_ + f2_;                           \
        if (s3_ != total) { total = s3_; scale = 0x80000000u / s3_; EP_COUNT(g_ep_div); } \
        c12 = ((f0_ * scale) >> 16) | (((s2_ * scale) >> 16) << 16);               \
        c3 = (s3_ * scale) >> 16;                                                  \
    } while (0)

/* Decode one symbol into `sym` (fac_decode). r*(hi-lo) == r*hi - r*lo in uint32 arithmetic. */
#define FS_DECODE() do {                                                           \
        const uint32_t r_ = range >> FAC_TOTAL_BITS;                               \
        const uint32_t t1_ = r_ * (c12 & 0xFFFFu);                                 \
        if (t1_ > code) { sym = 0; range = t1_; }                                  \
        else {                                                                     \
            const uint32_t t2_ = r_ * (c12 >> 16);                                 \
            if (t2_ > code) { sym = 1; code -= t1_; range = t2_ - t1_; }           \
            else { sym = 2; code -= t2_; range = r_ * c3 - t2_; }                  \
        }                                                                          \
        while (range < (1u << 24)) {                                               \
            code = (code << 8) | ((p < len) ? data[p] : 0u);                       \
            p++;                                                                   \
            range <<= 8;                                                           \
        }                                                                          \
    } while (0)

/* freq[sym]++ and rescale when it reaches its threshold. */
#define FS_INC() do {                                                              \
        if (sym == 0) { w0 += 0x10000u; if ((w0 >> 16) >= (w0 & 0xFFFFu)) FS_RESCALE(); } \
        else if (sym == 1) { w1 += 0x10000u; if ((w1 >> 16) >= (w1 & 0xFFFFu)) FS_RESCALE(); } \
        else { w2 += 0x10000u; if ((w2 >> 16) >= (w2 & 0xFFFFu)) FS_RESCALE(); }  \
    } while (0)

/* freq[old]-- when freq[old] >= 2. */
#define FS_DEC(old) do {                                                           \
        if ((old) == 0) { if (w0 >= 0x20000u) w0 -= 0x10000u; }                    \
        else if ((old) == 1) { if (w1 >= 0x20000u) w1 -= 0x10000u; }               \
        else { if (w2 >= 0x20000u) w2 -= 0x10000u; }                               \
    } while (0)

/* Decode up to n symbols. pass2: check the eos/hard-cap rule before every symbol and record the
 * coder state before each symbol in the ring. Returns the number decoded; *eos set when stopped. */
static LHDC_HOT LHDC_ALWAYS_INLINE int fs_run_body(fs_t *S, uint8_t *out, int n, const int pass2,
                                                   int produced, int hard_cap, fs_ring_t *ring, int *eos)
{
    const uint8_t *const data = S->data;
    const int len = S->len, window = S->window;
    uint8_t *const hist = S->hist;
    int p = S->p, cnt = S->cnt, pos = S->pos;
    uint32_t range = S->range, code = S->code;
    uint32_t w0 = S->w0, w1 = S->w1, w2 = S->w2, c12 = S->c12, c3 = S->c3;
    uint32_t total = S->total, scale = S->scale;
    int k = 0, sym;

    /* window-fill phase: append to the history, no decrement */
    while (k < n && cnt < window) {
        if (pass2) {
            if (p > len || produced + k >= hard_cap) { *eos = 1; goto out; }
            const unsigned slot = (unsigned)(produced + k) & (FS_RING - 1u);
            ring->p[slot] = (uint16_t)p; ring->range[slot] = range;
        }
        FS_DECODE();
        hist[cnt++] = (uint8_t)sym;
        FS_INC();
        if (pass2) ring->sym[(unsigned)(produced + k) & (FS_RING - 1u)] = (uint8_t)sym;
        else out[k] = (uint8_t)sym;
        k++;
    }
    /* sliding phase */
    while (k < n) {
        if (pass2) {
            if (p > len || produced + k >= hard_cap) { *eos = 1; goto out; }
            const unsigned slot = (unsigned)(produced + k) & (FS_RING - 1u);
            ring->p[slot] = (uint16_t)p; ring->range[slot] = range;
        }
        FS_DECODE();
        const int old = hist[pos];
        FS_DEC(old);
        hist[pos] = (uint8_t)sym;
        if (++pos >= window) pos = 0;
        FS_INC();
        if (pass2) ring->sym[(unsigned)(produced + k) & (FS_RING - 1u)] = (uint8_t)sym;
        else out[k] = (uint8_t)sym;
        k++;
    }
out:
    S->p = p; S->cnt = cnt; S->pos = pos; S->range = range; S->code = code;
    S->w0 = w0; S->w1 = w1; S->w2 = w2; S->c12 = c12; S->c3 = c3; S->total = total; S->scale = scale;
    return k;
}

static LHDC_HOT LHDC_NOINLINE void fs_run_pass1(fs_t *S, uint8_t *out, int n)
{
    int eos = 0;
    (void)fs_run_body(S, out, n, 0, 0, 0, NULL, &eos);
}

static LHDC_HOT LHDC_NOINLINE int fs_run_pass2(fs_t *S, int n, int produced, int hard_cap, fs_ring_t *ring, int *eos)
{
    return fs_run_body(S, NULL, n, 1, produced, hard_cap, ring, eos);
}

/* Cold: decode n symbols ignoring eos (the reference's s2_state_after past the end). */
static LHDC_NOINLINE void fs_skip(fs_t *S, int n)
{
    uint8_t scratch[16];
    while (n > 0) {
        int m = n > 16 ? 16 : n;
        fs_run_pass1(S, scratch, m);
        n -= m;
    }
}

static LHDC_HOT LHDC_ALWAYS_INLINE void fs_model_init(fs_t *S, const uint32_t *init, int window,
                                                      uint8_t *hist, int hist_cap)
{
    if (window > hist_cap) window = hist_cap;
    if (window < 1) window = 1;
    S->hist = hist;
    S->window = window;
    S->pos = 0;
    S->cnt = 0;
    memset(hist, 0, (size_t)window);
    uint32_t w0 = init[0] << 16, w1 = init[1] << 16, w2 = init[2] << 16, c12, c3, total = 0, scale = 0;
    FS_RESCALE();
    S->w0 = w0; S->w1 = w1; S->w2 = w2; S->c12 = c12; S->c3 = c3; S->total = total; S->scale = scale;
}

/* Pass-2 reader: symbols decoded ahead into the ring. */
typedef struct {
    fs_t S;
    fs_ring_t *ring;
    int produced, eos, hard_cap;
} fs_s2_t;

static LHDC_HOT LHDC_NOINLINE int fs_s2_more(fs_s2_t *z, int j)
{
    while (j >= z->produced && !z->eos)
        z->produced += fs_run_pass2(&z->S, FS_CHUNK, z->produced, z->hard_cap, z->ring, &z->eos);
    return j < z->produced ? z->ring->sym[(unsigned)j & (FS_RING - 1u)] : -1;
}

#define S2(j) ((j) < z->produced ? (int)ring_sym[(unsigned)(j) & (FS_RING - 1u)] : fs_s2_more(z, (j)))

/* rice_read_quotient() over the ring. */
static LHDC_HOT LHDC_ALWAYS_INLINE int fs_quotient(fs_s2_t *z, const uint8_t *ring_sym, int *jp, int *error)
{
    int j = *jp, symbol = S2(j);
    if (symbol == 2) {
        uint32_t low = 0; unsigned shift = 0;
        do {
            if (shift >= 32u) { *error = 1; *jp = j; return 0; }
            ++j;
            int v1 = S2(j); uint32_t b1 = v1 < 0 ? 0u : (uint32_t)v1; ++j;
            int v0 = S2(j); uint32_t b0 = v0 < 0 ? 0u : (uint32_t)v0; ++j;
            low |= ((b1 << 1) | b0) << shift; shift += 2u;
            symbol = S2(j);
        } while (symbol == 2);
        int ones = 0;
        while (symbol == 1) { ++ones; ++j; symbol = S2(j); }
        if (symbol == 0) ++j;
        *jp = j;
        if (shift >= 31u || (uint32_t)(ones + 1) > ((uint32_t)INT32_MAX >> shift)) { *error = 1; return 0; }
        uint32_t value = low | ((uint32_t)(ones + 1) << shift);
        if (value > (uint32_t)INT32_MAX) { *error = 1; return 0; }
        return (int)value;
    }
    int ones = 0;
    while (ones < 3 && symbol == 1) {
        ++ones; ++j;
        if (ones < 3) symbol = S2(j);
    }
    if (ones == 3) { *jp = j; return 3; }
    if (symbol == 0) ++j;
    *jp = j; return ones;
}

/* rice_decode_coeffs_used(); returns s2_used. */
static LHDC_HOT LHDC_NOINLINE int fs_rice(fs_s2_t *z, const uint8_t *s1, int count, int split, int32_t *coeff,
                                          int *error)
{
    const uint8_t *const ring_sym = z->ring->sym;
    int pivot = count - split;
    if (pivot < 0) pivot = 0;
    int run = 0;
    for (int i = 0; i < count; i++) if (s1[i] == 2) run = i;
    const int pivot2 = (run < pivot) ? count : run;
    int j = 0, i = 0;
    const int lim = pivot2 + 1 < count ? pivot2 + 1 : count;
    for (; i < lim; i++) {
        const int s = s1[i];
        if (s == 2) {
            int q = fs_quotient(z, ring_sym, &j, error);
            if (*error) return j;
            if (q > INT32_MAX - 2) { *error = 1; return j; }
            coeff[i] = q + 2;
        } else {
            coeff[i] = s;
        }
    }
    for (; i < count; i++) {
        const int s = s1[i];
        int q = fs_quotient(z, ring_sym, &j, error);
        if (*error) return j;
        if (q > (INT32_MAX - s) / 2) { *error = 1; return j; }
        coeff[i] = (int32_t)(((uint32_t)q << 1) | (uint32_t)s);
    }
    return j;
}
#undef S2

static LHDC_HOT lhdc_dec_ret_t lhdc_entropy_decode_fast(int32_t *quant_spectrum, int num_coeffs,
                                                        lhdc_dec_bit_reader_t *br, int count, int split,
                                                        int ma_win1, int ma_win2, int *fac_bytes_out,
                                                        uint8_t *facbuf, int fac_cap, uint8_t *s1)
{
    uint32_t e0 = EP_NOW();
    const int nb = dbt_lhdc_read_fac_bytes(br, facbuf, fac_cap);
    uint32_t e1 = EP_NOW();
    if (count < 1 || count > num_coeffs) count = num_coeffs;
    if (!s_fac_state) return LHDC_DEC_ERROR;
    const int slot = LHDC_DEC_SLOT();

    fs_s2_t z;
    fs_t *S = &z.S;
    S->data = facbuf; S->len = nb; S->p = 0;
    S->range = 0xFFFFFFFFu; S->code = 0;
    for (int i = 0; i < 4; i++) { S->code = (S->code << 8) | ((S->p < S->len) ? S->data[S->p] : 0); S->p++; }

    fs_model_init(S, FAC_FQ1, ma_win1 > 0 ? ma_win1 : 256, s_fac_hist1[slot], (int)sizeof(s_fac_hist1[slot]));
    uint32_t e2 = EP_NOW();
    fs_run_pass1(S, s1, count);
    uint32_t e3 = EP_NOW();

    fs_model_init(S, FAC_FQ2, ma_win2 > 0 ? ma_win2 : 256, s_fac_hist2[slot], (int)sizeof(s_fac_hist2[slot]));
    memset(quant_spectrum, 0, (size_t)num_coeffs * sizeof(int32_t));
    z.ring = &s_fac_state->ring[slot];
    z.produced = 0; z.eos = 0; z.hard_cap = count * 16;
    int error = 0;
    uint32_t e4 = EP_NOW();
    const int used = fs_rice(&z, s1, count, split, quant_spectrum, &error);
    uint32_t e5 = EP_NOW();
#ifdef LHDC_ENTROPY_PROBE
    g_ep_cyc[0] += e1 - e0; g_ep_cyc[1] += e3 - e2; g_ep_cyc[2] += e5 - e4; g_ep_cyc[3] += (e2 - e1) + (e4 - e3);
    g_ep_s1 += (uint32_t)count; g_ep_s2 += (uint32_t)z.produced; g_ep_calls++;
#else
    (void)e0; (void)e1; (void)e2; (void)e3; (void)e4; (void)e5;
#endif
    if (error) return LHDC_DEC_BITSTREAM_ERROR;

    if (fac_bytes_out) {
        int consumed;
        uint32_t final_range;
        if (used < z.produced) {
            /* the state recorded before symbol `used` is the state after exactly `used` symbols */
            const unsigned r = (unsigned)used & (FS_RING - 1u);
            consumed = z.ring->p[r]; final_range = z.ring->range[r];
        } else {
            if (used > z.produced) fs_skip(S, used - z.produced);
            consumed = S->p; final_range = S->range;
        }
        if (consumed < 0) consumed = 0;
        *fac_bytes_out = consumed;
        g_fac_final_range[slot] = final_range;
    }
    return LHDC_DEC_OK;
}
#endif
