# USB-MIDI transport relocation (libdeluge)

Status: **in progress** on `feat/libdeluge-midi-transport`. This is the last and
largest HAL↔app coupling in the libdeluge separation. It rewrites real-time
USB-MIDI I/O, so **"builds green" does not mean "works"** — it must be flashed and
tested with real USB MIDI devices (host *and* peripheral mode) before merge.

## The problem: the app co-implements the USB-MIDI driver

The USB-MIDI transport is smeared across both layers and the coupling runs **both
directions**:

- **HAL → app:** the `rohan_midi` pipe handlers in `RZA1/usb/.../r_usb_{h,p}libusbip.c`
  write the app's `ConnectedUSBMIDIDevice` fields (`numBytesReceived`,
  `currentlyWaitingToReceive`, `numBytesSendingNow`) and call the upcalls
  `usbSendCompleteAsHost` / `usbSendCompleteAsPeripheral`. USB receive DMA targets
  the app struct's `receiveData[64]` (zero-copy).
- **app → HAL:** `deluge/io/midi/midi_engine.cpp` *defines* the HAL USB transfer
  descriptors `g_usb_midi_recv_utr` / `g_usb_midi_send_utr` (type `usb_utr_t`), sets
  their DMA pointers / lengths / pipe keywords, and drives Renesas internals
  directly: `g_p_usb_pipe`, `usb_hstd_get_usb_ip_adr`, `usb_cstd_set_nak`,
  `usb_pstd_set_stall`, `USB_CFG_PMIDI_BULK_IN/OUT`, `g_usb_hmidi_tmp_ep_tbl`, …

So `midi_engine.cpp` is ~15 transport functions of USB driver code plus the actual
MIDI logic, all in one file.

### What is transport vs. logic

`ConnectedUSBMIDIDevice` (midi_device_manager.h) is almost entirely a transport
object that leaked into the app:

| Field | Tier |
|---|---|
| `receiveData[64]`, `dataSendingNow[]`, `sendDataRingBuf[]`, `ringBuf{Read,Write}Idx`, `numBytesReceived`, `numBytesSendingNow`, `currentlyWaitingToReceive`, `sq`, `canHaveMIDISent` | **transport → BSP** |
| `device[4]` (MICICableUSB cables), `maxPortConnected` | **logical → app** |

`midi_engine.cpp` functions:

- **transport → BSP:** `usbSendCompleteAsHost/AsPeripheral/PeripheralOrA1`,
  `usbReceiveComplete/PeripheralOrA1`, `brdyOccurred`, `flushUSBMIDIToHostedDevice`,
  `flushUSBMIDIOutput`, `setupUSBHostReceiveTransfer`, the descriptor setup in the
  `MidiEngine` ctor, the `g_usb_midi_*_utr` definitions.
- **logic → app:** `sendNote/CC/Clock/...`, `sendMidi`, `setupUSBMessage`,
  `sendUsbMidi` (MIDI→USB-MIDI-event-packet encoding), `checkIncomingUsbMidi`
  (USB-MIDI-event-packet → MIDI message decode), `checkIncomingUsbSysex`,
  `midiSysexReceived`, the DIN serial path.

The MIDI↔USB-event-packet packing (4-byte USB-MIDI events) is a grey area; keep it
app-side for now (it is MIDI-protocol framing, not USB transport).

## Target architecture

Three concerns that are tangled today, pulled apart into three owners:

```
app MIDI engine          musical logic only: routing (which cable wants a message),
   │                      MPE zones, song integration, sysex dispatch
   ├─────────────► deluge_midi  (pure MIDI protocol library, NO dependencies):
   │                      MIDIMessage; MIDI<->bytes (DIN, running status);
   │                      MIDI<->USB-MIDI 4-byte event packets (pack/unpack);
   │                      sysex framing; bytesPerStatusMessage
   │  deluge_midi_read(port, dst, max) / deluge_midi_write(port, src, len)
   ▼
BSP usb_midi             transport only: owns per-device RX/TX buffers + send ring,
   │                     the USB transfer descriptors, the receive/send state
   │                     machine; drives Renesas pipes; drains HAL completions.
   │                     Moves opaque byte buffers — does NOT parse MIDI.
   │  start transfer (down) ; poll completions (down)
   ▼
HAL Renesas USB core     generic bulk pipes; records pipe-complete events
```

Two libraries fall out, both dependency-free leaves:
- **`deluge_midi`** — the MIDI *protocol* (spec + serialization). No hardware, no
  transport, no app deps; usable anywhere (and FFI-friendly for the Rust side).
  Built around the existing near-pure `MIDIMessage` (model/midi/message.h).
- **`bsp_rza1` usb_midi** — the *transport* (already part of the BSP).

The key insight: the 4-byte USB-MIDI event packing is neither transport (the BSP
just moves the bytes) nor musical logic — it is pure USB-MIDI **spec**, so it lives
in `deluge_midi`. The BSP transport never looks inside the buffers it moves.

Key rule restored: **no upcalls.** Completion flows as a pull — the HAL records
`(pipe, bytes)` on BRDY/BEMP into a HAL-owned latch/queue; the BSP polls it (BSP→HAL
is downward). The BSP owns the buffers; it hands a buffer pointer *down* to the HAL
when starting a transfer (so the zero-copy DMA still works), and the HAL never names
app structures.

## The `deluge_midi` protocol library

Pure, dependency-free (libc only). Probable home `src/midi/` → `deluge_midi` static
lib, parallel to `deluge_foundation` (a leaf the app links; the BSP/HAL do not need
it). Contents, gathered from today's scattered protocol code:
- `MIDIMessage` + factories + `isSystemMessage()` (relocate/anchor from
  `model/midi/message.h`) and `bytesPerStatusMessage()`.
- USB-MIDI event pack: `setupUSBMessage()` (midi_engine.cpp:527) → e.g.
  `midi_usb_event_pack(MIDIMessage, cable) -> uint32_t`.
- USB-MIDI event unpack: the decode in `checkIncomingUsbMidi` (the
  `statusType/cable/channel/data1/data2` extraction from each 4-byte group) →
  `midi_usb_event_unpack(uint32_t) -> MIDIMessage (+ cable, validity)`.
- DIN serialize/deserialize + running status (from `sendSerialMidi` /
  `checkIncomingSerialMidi`).
- sysex framing helpers (the USB-MIDI sysex CIN cases).
What does NOT go here: routing (`wantsToOutputMIDIOnChannel`, cable selection),
which is musical/app logic and stays in the MIDI engine but *calls* this library.

## Boundary API (`include/libdeluge/midi_io.h`)

Already present: `deluge_midi_read/write/write_space/flush`, `DelugeMidiPortKind`,
the USB-host enumeration-event poll. To add for USB-MIDI transport:

- USB ports mapped onto `DelugeMidiPort` (DIN is port 0; upstream USB cables and
  hosted-device ports follow). `deluge_midi_port_count()` / `_kind()` already exist.
- `deluge_midi_read`/`write` implemented for the USB ports (today only DIN is wired
  in `bsp/rza1/midi_io.c`).
- A connection-state query so the app knows which USB-MIDI ports are live (replaces
  reading `cable[0]` / `g_usb_peri_connected` / `g_usb_host_connected`).
- `deluge_midi_service()` (or fold into existing routine) for the BSP to pump its
  state machine + drain HAL completions, called from the app's MIDI routine.

## Phased plan

Phase 1 is pure and **fully testable** (it's the right place to start cutting code);
phases 2–7 touch real-time USB and only the finished result is flash-testable.

1. **Extract the `deluge_midi` protocol library** (pure, low-risk, verifiable):
   create `src/midi/` → `deluge_midi`; move `MIDIMessage`/`bytesPerStatusMessage`,
   the USB-MIDI event pack (`setupUSBMessage`) + unpack, DIN serialize + running
   status, and sysex framing into it; the MIDI engine calls it for all
   serialization while keeping routing. No transport change — same bytes on the
   wire, so existing behaviour is preserved and unit-checkable.
2. **Foundation:** `src/bsp/rza1/usb_midi.{c,h}` + the boundary additions
   (declarations + stubs). No behaviour change.
3. **Move transport state down:** relocate the `g_usb_midi_*_utr` descriptor
   definitions and the RX/TX buffers + ring into the BSP module; split
   `ConnectedUSBMIDIDevice` (transport fields leave the app struct).
4. **Send path:** move `flushUSBMIDIOutput`, `flushUSBMIDIToHostedDevice`, the
   `usbSendComplete*` functions into the BSP; expose via `deluge_midi_write`/flush;
   convert send-complete to a polled count.
5. **Receive path:** move `setupUSBHostReceiveTransfer`, `usbReceiveComplete*`,
   `brdyOccurred` into the BSP; the app pulls bytes via `deluge_midi_read`, then
   decodes them with `deluge_midi`.
6. **HAL `rohan_midi` functions:** reduce to generic pipe-complete event recording;
   stop including app MIDI headers; the BSP drains the events.
7. **Strip + rewire:** remove transport from `midi_engine.cpp`; app uses only the
   boundary + `deluge_midi`; relocate `r_usb_pmidi_config.h` (USB-MIDI *config*)
   out of the app tree into the BSP.

## Risks / hardware test matrix

- USB **host** mode: connect a class-compliant USB MIDI controller (+ through a hub);
  verify note in/out, clock, sysex, hot-plug/unplug, multiple devices.
- USB **peripheral** mode: connect Deluge to a computer; verify both directions.
- Timing: MIDI clock jitter; sysex throughput; the send ring buffer not overflowing
  under heavy output. The receive path was zero-copy DMA into the app buffer — after
  the move it is zero-copy DMA into the BSP buffer + one copy on `deluge_midi_read`;
  confirm no added latency/loss at high message rates.
