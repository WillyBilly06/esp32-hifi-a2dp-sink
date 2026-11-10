#ifndef DBT_LHDC_MANTISSA_H
#define DBT_LHDC_MANTISSA_H
#include <stdint.h>
/* Mantissa + sign plane for one channel (same results as the reference loop: bits past the
 * channel's byte slice read as zero; peak is the largest |magnitude| as an unsigned word).
 *
 * The bits are read through a 32-bit window `buf` (MSB first) holding `avail` valid bits, refilled
 * one byte at a time to at least 25 bits, so a shift of up to 24 bits plus the sign bit needs no
 * further refill. total_bits is the channel's byte count * 8. */
static __attribute__((always_inline)) inline int64_t dbt_lhdc_mantissa_fast(const int32_t *coeff, int32_t *out, int count,
    const uint8_t *payload, int total_bits, int start_bit, int pred_mode, int init_shift)
{
    const int total_bytes = total_bits >> 3;
    int next = start_bit >> 3;                 /* next byte to load */
    uint32_t buf = 0;
    int avail = 0;
#define MANT_REFILL()                                                                   \
    while (avail <= 24) {                                                               \
        const uint32_t b_ = next < total_bytes ? payload[next] : 0u;                    \
        next++;                                                                         \
        buf |= b_ << (24 - avail);                                                      \
        avail += 8;                                                                     \
    }
    MANT_REFILL();
    {   /* skip to the start bit inside the first byte */
        const int skip = start_bit & 7;
        buf <<= skip; avail -= skip;
    }
    int pred = init_shift << 7;
    uint32_t peak = 0;
    for (int k = 0; k < count; ++k) {
        MANT_REFILL();
        const int shift = (pred + 0x40) >> 7;
        uint32_t mant = 0;
        if (shift) {
            if (shift <= 24) {
                mant = buf >> (32 - shift);
                buf <<= shift; avail -= shift;
            } else {                           /* rare: read 24 + (shift - 24) bits */
                mant = buf >> 8;
                buf <<= 24; avail -= 24;
                MANT_REFILL();
                const int rest = shift - 24;
                mant = (mant << rest) | (buf >> (32 - rest));
                buf <<= rest; avail -= rest;
                MANT_REFILL();
            }
        }
        const uint32_t magnitude = ((uint32_t)coeff[k] << shift) | mant;
        uint32_t value = magnitude;
        if (magnitude) {
            if (buf >> 31) value = 0u - magnitude;
            buf <<= 1; avail -= 1;
        }
        out[k] = (int32_t)value;
        const uint32_t absolute = (int32_t)magnitude < 0 ? 0u - magnitude : magnitude;
        if (absolute > peak) peak = absolute;
        unsigned width = absolute ? 32u - (unsigned)__builtin_clz(absolute) : 0u;
        if (width > 31u) width = 31u;
        const int x = (int)width << 7;
        pred = pred_mode ? ((7 * pred + x) >> 3) : ((5 * x + 3 * pred) >> 3);
    }
#undef MANT_REFILL
    return (int64_t)peak;
}
#endif
