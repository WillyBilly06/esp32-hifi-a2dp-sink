# Third-party code

The code written for this project (the Bluetooth host stack, codec adapters, firmware and the LHDC V5 and SSC
decoders) is under the BSD 3-Clause license in [LICENSE](LICENSE). The components below keep their own licenses;
their original notices are left in place in every file.

| Component | Where | License |
|---|---|---|
| ESP-IDF v6.1 (Espressif) | [`esp-idf/`](esp-idf) | Apache-2.0 |
| Helix AAC decoder (RealNetworks) | `esp-idf/components/bt/host/bluedroid/external/libaac-lc/libhelix_aac` | RealNetworks Public Source License (RPSL) |
| LDAC decoder (re-implementation of AOSP libldac) | `esp-idf/components/bt/host/bluedroid/external/libldac-dec` | see its sources; AOSP libldac is Apache-2.0 |
| libfreeaptx (aptX / aptX HD) | `esp-idf/components/bt/host/bluedroid/external/libfreeaptx` | LGPL-2.1-or-later |
| Opus | `esp-idf/components/bt/host/bluedroid/external/opus` | BSD 3-Clause (Xiph.Org) |
| SBC codec (Open Interface / AOSP) | `esp-idf/components/bt/host/bluedroid/external/sbc` | Apache-2.0 |
| CELT-derived parts of the SSC decoder | [`native_bt/codecs/ssc/decoder`](native_bt/codecs/ssc/decoder) | BSD (see `COPYING.opus`) |

LDAC, LHDC, aptX, AAC and Samsung Scalable Codec are trademarks of their owners. Using these codecs in a commercial
product may require licenses from them.
