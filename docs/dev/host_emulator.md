# Host Emulator — running the firmware natively under spark's GUI

A *true* Deluge emulator: the real firmware (`deluge_app`) compiled natively via
the host BSP (`src/bsp/host`, the `<libdeluge/...>` boundary), driven through
[spark](https://github.com/stellar-aria/spark)'s polished desktop front panel
(OLED canvas, RGB pad grid, buttons, encoders). Unlike spark's stock simulator —
a Rust *reimplementation* of the Deluge UI — this runs the actual firmware code,
so it is faithful for testing UI, sequencing, and (later) audio behaviour.

## Architecture: IPC bridge over a Unix socket

The two processes already have the exact seams this needs, so we glue them with a
serialized wire rather than FFI (no ABI / 32-vs-64-bit / shared-crash hazards):

```
┌─ deluge_host (this repo, native build) ──────┐         ┌─ spark-deluge-simulator ─┐
│  deluge_app  (real firmware + scheduler)     │         │  Iced GUI (front panel)  │
│    │                                         │         │    │                     │
│    ▼  libdeluge BSP  (src/bsp/host)          │  Unix   │    ▼  panel mode         │
│  host_link.c  ── MessageToDeluge ───────────────socket───▶ render OLED/pads/LEDs  │
│               ◀── MessageFromDeluge ─────────────────────  emit input on click    │
└──────────────────────────────────────────────┘         └──────────────────────────┘
```

### Role reversal vs spark's normal protocol

spark's `MessageToDeluge` (`UpdateDisplay`/`SetPadRGB`/`SetLed` …) is normally the
**host→device** direction: in spark's world the *host* is the brain that computes
LED/OLED frames and pushes them to a dumb panel, and the device returns
`MessageFromDeluge` *input*.

Our emulator **reverses the roles** — the real firmware is the brain. So:

| | emits | consumes |
|---|---|---|
| `deluge_host` (firmware) | `MessageToDeluge` (display/LED) | `MessageFromDeluge` (input) |
| spark GUI (panel mode) | `MessageFromDeluge` (input) | `MessageToDeluge` (display/LED) |

i.e. `deluge_host` plays the *daemon/host* side of spark's wire, and spark's GUI
runs in a new *panel/terminal* mode that renders inbound frames and emits input,
bypassing its own engine + DelugeUI.

## Wire protocol (matches spark `apps/deluge/core/src/interface/protocol.rs`)

Length-prefixed frames: `[len_lo][len_hi][type][data…]`, `len = 1 + N` (type+data).

**Firmware → GUI (display/LEDs), the `MessageToDeluge` types:**

| type | name | payload | firmware seam |
|---|---|---|---|
| `0x20` | UpdateDisplay | 768-byte page-major OLED | `deluge_display_blit_oled(px,128,48)` |
| `0x21` | ClearDisplay | — | — |
| `0x22` | SetPadRGB | `col,row,r,g,b` | `deluge_control_set_pad{,_columns}` |
| `0x24` | SetLed | `index,on` | `deluge_control_set_led` |
| `0x28` | SetKnobIndicator | `which,l0,l1,l2,l3` | `deluge_control_set_indicator` |
| `0x29` | SetSyncedLed | `on` | (rear sync LED) |

The firmware's `oledCurrentImage` is **already** the 768-byte SSD1309 page-major
buffer spark's `update_display_from_buffer` expects (`buf[page*128+col]`, bit b =
panel row `page*8+b`); no conversion. Pad LEDs arrive column-pair-batched via
`deluge_control_set_pad_columns(idx, colours, count)` (`idx = x>>1`).

**GUI → firmware (input), the `MessageFromDeluge` types:**

| type | name | payload | firmware seam |
|---|---|---|---|
| `0x01`/`0x02` | PadPressed/Released | `col,row` | `deluge_control_poll_event` (PAD) |
| `0x03`/`0x04` | ButtonPressed/Released | `button_id` | `deluge_control_poll_event` (BUTTON) |
| `0x05` | EncoderRotated | `encoder_id, delta:i8` | `deluge_encoder_take_edges` |

Button IDs follow `hid/button.h`: `id = 9*(y + 2*kDisplayHeight) + x`,
`kDisplayHeight=8`. Encoder push-buttons are special IDs (144/153/157/162/171/175).
Encoder rotation IDs: 0=SCROLL_X,1=TEMPO,2=MOD_0,3=MOD_1,4=SCROLL_Y,5=SELECT.

## Milestones

1. **Visual loop** (current): OLED + pads + LEDs out, pad/button/encoder in, over
   the socket. Audio muted; the offline WAV-capture path (`host_audio.c`) is kept
   for the existing regression harness. ← *this milestone*
2. **Audio loop**: pace `deluge_audio_drive` against a real-time cpal stream on the
   spark side (second channel / shared ring); MIDI in/out over the same link.
3. **Polish**: SD-card image mounting, multi-instance, gdb/replay ergonomics.

## Running (target UX)

```bash
# firmware, native:
cmake -B build-sim -S sim -G Ninja && cmake --build build-sim --target deluge_host
DELUGE_HOST_LINK=/tmp/deluge.sock ./build-sim/deluge_host
# spark GUI as the panel:
cargo run -p spark-deluge-native --bin spark-deluge-simulator -- --emulate /tmp/deluge.sock
```
