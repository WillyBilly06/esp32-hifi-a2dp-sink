# pure_sink — BDK SPEAKER pure playback firmware

A minimal Bluetooth Classic A2DP speaker for the original ESP32 (4 MB flash, no
PSRAM, PCM5102A DAC) built on the in-house `native_bt` host stack. It only
plays audio: there is no DSP, BLE, sound effects, diagnostics or JSON output.

## Architecture

```
phone ──BR/EDR──> ESP32 controller (VHCI, core 1)
                     │ HCI
                     ▼
          host owner task (app_main, core 1, prio 20)  ── single event loop
          ├─ native_bt runtime: HCI, ACL, L2CAP, HCI flow control
          ├─ GAP: "BDK SPEAKER", CoD 0x240414, discoverable/connectable
          ├─ Classic security: SSP just works, link keys in NVS ("native_bonds")
          ├─ SDP server: A2DP Sink + AVRCP CT/TG records
          ├─ AVRCP: passthrough keys, absolute volume (target), metadata/play status
          └─ A2DP/AVDTP acceptor ── encoded media queue (codec worker)
                                          │
          decoder task (core 0, prio 19)  ▼  dbt_codec_worker_step()
          SBC (bitpool <= 250) / AAC / aptX / aptX HD / aptX LL / LDAC / LHDC V5 / Opus / Opus05 / SSC
                                          │ PCM (S16/S24/S32, mono/stereo), copied
                                          ▼
                PCM ring buffer (64 KB: S16 as is, 32-bit output as packed S24)
                                          │
          I2S writer task (core 1, prio 21)  ▼  convert to 32-bit stereo slots
                              × volume (one multiply, skipped at 100 %)
                              ± 1-frame drift slip
                                          ▼
                 I2S1 std Philips, master, APLL → PCM5102A
                 BCLK GPIO27, LRCK GPIO25, DATA GPIO26 (no MCLK)
```

* **Back-pressure**: the worker runs in deferred-PCM mode. The PCM hook copies
  one ≤256-frame chunk into the ring or returns `DBT_BUSY`; the writer wakes the
  decoder when it frees space. HCI receive credits are limited to free encoded
  queue slots while streaming, so a full ring
  throttles the phone instead of dropping packets.
* **Stream control**: the worker's stream hook (START/SUSPEND/CLOSE, decoder
  task) only publishes a request; the writer applies it (flush, re-create the
  I2S channel when the sample rate changes, 44.1/48/88.2/96 kHz) and
  acknowledges, and the producer gets `BUSY` until then.
* **Jitter buffer (150 ms)**: the buffer is the PCM ring plus the encoded
  packets still queued for the decoder. Encoded audio is 5-10x smaller than PCM,
  so most of the reserve stays compressed: the queue has 16 packet slots for
  AAC, LDAC and SSC and 8 for the lighter codecs. Playback (re)starts once
  150 ms are buffered in total and at least 40 ms are decoded. The ring holds
  371 ms of 44.1 kHz S16, 227 ms of 48 kHz S24 or 113 ms of 96 kHz S24. An
  empty ring is an underrun: the DMA plays zeros (`auto_clear`) and the writer
  re-buffers. The 150 ms is also the AVDTP delay report, sent right after the
  codec is configured, so video stays in sync.
* **Clock drift**: no resampler. The writer keeps a slow moving average of the
  total buffered audio (~2.5 s time constant, so Bluetooth bursts are ignored).
  Only when the average is more than 20 ms off its set-point does it drop or repeat one
  stereo frame, at most once per 4 I2S chunks (≈50 frames/s capacity, ~1000 ppm,
  versus <100 ppm real drift). Typical result: one inaudible single-sample slip
  every few seconds to minutes, no oscillation, no audible pitch change.
* **Volume**: AVRCP absolute volume 0..127 maps to mute / −50 dB … 0 dB
  (dB-linear), applied as one integer multiply per sample.
* **Pairing**: discoverable whenever no phone holds an encrypted link; always
  connectable so bonded phones reconnect. New pairing is accepted while no
  other phone is connected. Up to 8 bonds; the 9th replaces the oldest slot.
* **Button (GPIO0/BOOT)**: short press = AVRCP play/pause; hold 3 s = pairing
  mode for 120 s (disconnects the current phone and stays discoverable).

## Files

| File | Purpose |
| --- | --- |
| `CMakeLists.txt` | Project; pulls the shared components from `../esp32_probe/components` via `EXTRA_COMPONENT_DIRS`, sets `DBT_PROBE_SSC_BUILD=ON` |
| `sdkconfig.defaults` | Controller-only BTDM, core layout, 4 MB flash, UART0 921600, `I2S_ISR_IRAM_SAFE` |
| `partitions.csv` | nvs 0x9000/0x6000 (unchanged → bonds survive), phy_init 0xf000, factory app 0x10000 (3.9 MB) |
| `main/main.c` | Controller + VHCI transport, runtime/GAP setup, host owner loop, visibility policy, button, logging |
| `main/bt_security.c` | Classic SSP policy and NVS bond store (`native_bonds` namespace) |
| `main/bt_sdp.c` | SDP records (A2DP Sink, AVRCP CT/TG) |
| `main/a2dp_sink.c` | Endpoints, validators and codec ops, codec worker, decoder task, codec logging, credit policy, delay reports |
| `main/sink_avrcp.c` | AVRCP controller/target: keys, absolute volume, metadata |
| `main/audio_out.c` | PCM ring, I2S writer, volume, drift handling |
| `main/button.c` | GPIO0 short/long press |


## Build and flash

From the repository root (Windows PowerShell):

```powershell
firmware\esp-idf\install.ps1 esp32        # once
tools\build_pure_sink.ps1                  # builds and checks the image fits the factory partition
tools\flash_pure_sink.ps1 -Port COM7       # keeps NVS, so pairings survive
```

Linux / macOS:

```bash
./firmware/esp-idf/install.sh esp32        # once
. ./firmware/esp-idf/export.sh
idf.py -C firmware/native_bt/pure_sink build
idf.py -C firmware/native_bt/pure_sink -p /dev/ttyUSB0 flash monitor
```

The shared `native_bt` component points the SDK's size check at an `ota_1` slot this layout does not have. That
prints a harmless warning; the build script checks the image against the factory partition instead.

Do **not** use `erase_flash` if you want to keep pairings: NVS is at 0x9000–0xEFFF. There is no `otadata`
partition, so the bootloader always starts the factory app at 0x10000. Console: 921600 baud
(`idf.py monitor -b 921600`, or `python tools/serial_log.py COM7 60 log.txt`).

## Log lines

* boot / BT ready / codec open / codec close: `heap[...]: free, min, largest`
* connect, disconnect, pairing, bond stored, discoverability changes
* `configured:` / `codec changed:` / `reconfigured:` / `stream start:` with codec,
  rate, S16/S24/S32, channels, decoded fields and the raw CIE in hex
* in-band: SBC bitpool, LDAC frame size → kbps/quality, SSC bitrate index → kbps
  (logged on change, at most once per second)
* AVDTP state changes, I2S start/stop, underruns (rate limited)
* every AVDTP signal in both directions with its body in hex (`AVDTP <- SET_CONFIGURATION ...`,
  `AVDTP -> GET_ALL_CAPABILITIES accept ...`), rejects as warnings
* HCI link events: disconnect reason (`0x13` = the peer ended it, `0x16` = we did), failed connections
* AVRCP connect, absolute volume, play status, now playing
* every 10 s while streaming (60 s idle): heap, decoder/I2S/host stack free,
  codec working set, ring level/target, volume, underruns, drift slips, encoded
  audio queued (ms), media packets dropped/rejected, decoder core load

Using the speaker with a Windows PC (codec switching, AAC, troubleshooting): see
[docs/windows-pc.md](../../docs/windows-pc.md).

## Intentionally left out

BLE (advertising, GATT, LE SMP, BLE control/OTA), RFCOMM/SPP, HFP/SCO,
EQ/limiter/FIR/dither/volume ramps, DSP assist, startup sounds, LEDs, SPIFFS,
OTA and rollback, signal analysis, profilers, ingress/dispatch traces, JSON
diagnostics, speaker-initiated reconnection to the last phone (the phone
reconnects to the bonded speaker; tap it in the phone's Bluetooth list or let
the phone auto-connect), multi-point, and a resampler (drift is handled by
single-frame slips).

The controller still runs in dual mode (`BTDM`) because the host's controller
bring-up requires it (`DBT_FAULT_NOT_DUAL_MODE`) and the GAP setup writes LE
advertising data/parameters; advertising is never enabled.
