# Using the speaker with a Windows PC

The firmware in [`native_bt/pure_sink`](../native_bt/pure_sink) works with any A2DP source. This
page covers Windows PCs, where codec choice depends on the Bluetooth driver. It was tested with the
[Alternative A2DP Driver](https://www.bluetoothgoodies.com/a2dp/) 1.8.3 on Windows 11 (Realtek adapter).

## What the speaker offers

| Codec | Offered by the speaker |
|---|---|
| SBC | All modes, 44.1/48 kHz, bitpool 2–250 |
| AAC | MPEG-2/4 AAC-LC, 44.1/48 kHz, VBR, up to 320 kbps |
| aptX | 44.1/48 kHz stereo |
| aptX HD | 44.1/48 kHz stereo, 24-bit |
| LDAC | 44.1/48/88.2/96 kHz, stereo |
| LHDC V5 | 44.1 / 48 / 96 / 192 kHz, 16/24-bit, up to 1000 kbps (phones only; the Windows driver has no LHDC) |
| SSC | Samsung phones only |

Opus and aptX Low Latency endpoints are also present. The Alternative A2DP Driver did not offer them in testing.

## How a codec switch works

The driver applies new codec settings only when the speaker (re)connects. **(Re)connect device** in the driver's app
does this:

1. The PC closes the audio stream and ends the Bluetooth link (HCI reason `0x13`).
2. About 2 s later the PC connects again and configures the codec you picked.

The speaker's console shows both halves:

```
I (50887) a2dp: AVDTP <- CLOSE 20
I (50990) pure_sink: HCI disconnection complete: handle 0x080, status 0x00, reason 0x13 (peer ended it)
I (52939) security: incoming connection from E8:47:3A:EC:51:9A (bonded)
I (53497) a2dp: AVDTP <- SET_CONFIGURATION 14 04 01 00 07 09 00 ff 4f 00 00 00 01 00 12 08 00
I (53499) a2dp: configured: aptX 48000 Hz S32 2 ch, aptX, vendor 0x004f codec 0x0001, stereo
```

## Troubleshooting

### The speaker stays disconnected after a codec switch

In 15 switches between SBC, AAC, aptX, aptX HD and LDAC, 13 reconnected, most of them within 2–5 s. Twice, after
leaving aptX, the PC ended the link and never connected again: the console shows the `HCI disconnection complete
... reason 0x13` line and no `incoming connection` after it.

This happens on the PC side. Once the PC has ended the link, only the PC can start the new connection. The speaker
stays connectable the whole time (the console prints `discoverable, connectable` right after the disconnect), and
no connection attempt reaches it. Press **(Re)connect device** again, or click **Connect** for the speaker in
Windows' Bluetooth settings.

### Codecs are missing or a codec plays silence after a firmware change

The driver stores the speaker's codec list per Bluetooth address. On reconnects it often configures a codec
straight from that stored list without asking the speaker again. If you flash firmware with a different codec list
under the same address (another SBC bitpool limit, an extra codec), the stored list is out of date. Remove the
speaker in Windows' Bluetooth settings and pair it again, so the PC reads the new list.

### AAC is greyed out

The Alternative A2DP Driver sells two license editions, and AAC needs the "with AAC support" edition. With the
other edition AAC stays greyed out for every device, even though the driver detects it. To check, open **About**
in the driver's app: a **Start free AAC CODEC trial** link means your license has no AAC. The trial lasts one week
and adds short silences during that time.

### Choppy audio

Watch the speaker's status line (every 10 s):

```
pure_sink: heap free 68012 ... | codec LDAC 42724 B | ring 113/113 ms (target 140) playing | vol 127 |
underruns 0, slips -1174/+1203 | encoded 10 ms | media dropped 0, rejected 0 | decoder busy 26%
```

* `underruns` counts how often the buffer ran dry. Rising counts usually come from a busy radio (Wi-Fi on 2.4 GHz,
  distance); lower the LDAC quality or the SBC bitpool in the driver's app.
* `media dropped` counts packets that did not fit in the queue, and `rejected` counts packets the decoder could not
  read.
* `decoder busy` is the decoder core's load. 96 kHz LDAC and SSC UHQ are the heaviest, at about 25–50 %.
