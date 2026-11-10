# LHDC V5 on the ESP32

The decoder in [`native_bt/codecs/lhdcv5/fast`](../native_bt/codecs/lhdcv5/fast) is an optimized build of
[LHDC-V5-Decoder](https://github.com/WillyBilly06/LHDC-V5-Decoder). Its output is **bit-identical** to that decoder:
the full PCM output of every test stream hashes the same.

## Measured on the ESP32

Original ESP32 at 240 MHz, one core, 1000 kbps streams from the LHDC V5 test encoder (5 ms frames):

| Stream | Before | After | Decoder RAM |
|---|---|---|---|
| 192 kHz / 24-bit, music | 3.77 ms (75 % of a core) | **3.24 ms (64 %)**, worst frame 3.30 ms | 52.4 → 50.5 KB |
| 192 kHz / 24-bit, full-band noise | 3.70 ms | 3.15 ms | 50.5 KB |
| 96 kHz / 24-bit, music | 2.08 ms | 1.79 ms | 26.9 KB |
| 48 kHz / 24-bit, music | 1.16 ms | 1.08 ms | 20.0 KB |

Live from a nubia phone at 192 kHz / 24-bit, the decoder core runs at 46–51 % with 0 underruns.

## What changed

* **Entropy decoder** (the largest stage): the range coder and adaptive model state are kept in registers. Each
  symbol's frequency and rescale threshold share one word, and the window-fill and sliding phases are separate loops.
  Pass 2 is decoded ahead in 64-symbol chunks into a ring that also records the coder position before each symbol, so
  the Rice inverse reads an array and the mantissa-plane offset is read back from the record.
* **Mantissa plane**: read through a 32-bit bit window refilled a byte at a time (about 25 % faster).
* **Dequantization, coefficient reversal and SNS shaping** are one pass instead of three (same float operations).
* A peak scan whose result was never read is gone (25k cycles per channel).
* The unused pass-2 symbol buffer is gone (1.9 KB at 192 kHz).

## Memory

Nothing is allocated until an LHDC stream opens. All buffers, tables and caches are freed when it closes; the heap is
the same after any number of open/close cycles. At 192 kHz the decoder uses 50.5 KB in 11 blocks, the largest
7.8 KB, so it fits a fragmented heap.

## Dual-core option

Build with `LHDC_DEC_DUAL_CORE=1` to decode the right channel on the other core:

| | One core (default) | Both cores |
|---|---|---|
| 192 kHz frame time | 3.24 ms | 2.26 ms |
| Decoder RAM at 192 kHz | 50.5 KB | 71.8 KB |
| Work on the Bluetooth core | none | ~1.5 ms per frame |

Each core gets its own scratch buffers, and the worker task is pinned to the core the caller is not on, so the output
is identical in both modes. The worker and its buffers are deleted when the stream closes. The default build uses one
core: core 0 belongs to the decoder, and 64 % leaves plenty of headroom.
