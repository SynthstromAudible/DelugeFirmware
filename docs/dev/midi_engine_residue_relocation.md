# MIDI-engine HAL residue relocation (flash-test-gated)

**Status:** planned, not started. Branch it off `feat/libdeluge` (suggested
`feat/libdeluge-midi-residue`). The whole change touches the live real-time USB +
DIN/serial MIDI path, so — like the USB-MIDI transport relocation it finishes —
**the result must be flash-tested host + peripheral + DIN before merge.** Only the
final state is meaningfully testable; intermediate phases are build-green only.

## Goal

Remove the last `RZA1/` includes from `src/deluge/io/midi/midi_engine.cpp` so it
leaves `scripts/platform_boundary_allowlist.txt`. After this, the only remaining
allowlisted app files are `deluge.cpp` (boot/input — its own effort) and
`audio_engine.cpp` (the SSI/DMA audio-callback seam), plus the deferred
`hid/encoder*.cpp` (→ `feat/encoder-overhaul`).

This is the *tail* of the USB-MIDI transport relocation (see
`usb_midi_transport_relocation.md`): the in-code comments in `midi_engine.cpp`
already flag several reads as living "until phases 6–7", i.e. transport state the
app still pokes directly. This plan finishes that, plus relocates the DIN/serial
read path and the USB host/peripheral mode queries to the boundary.

## Coupling inventory (what still crosses the boundary)

Current `RZA1/` includes in `midi_engine.cpp` and why each is there:

| Include | Used for |
|---|---|
| `RZA1/cpu_specific.h` | `TIMING_CAPTURE_ITEM_MIDI` (board constant — the DIN-MIDI byte-timestamp capture slot) |
| `RZA1/uart/sio_char.h` | DIN/serial MIDI I/O (see W1) |
| `RZA1/usb/r_usb_basic/r_usb_basic_if.h` | `g_usb_usbmode`/`USB_HOST`, `usb_cstd_usb_task()`, `USB_NUM_USBIP` |
| `RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h` | (verify — likely transitive only now) |
| `RZA1/usb/r_usb_hmidi/src/inc/r_usb_hmidi.h` | (verify — likely transitive only now) |

(`RZA1/system/iodefine.h` was already confirmed dead and removed.)

### W1 — DIN/serial MIDI → `<libdeluge/midi_io.h>`

DIN **write/flush/space** already route through the boundary (`deluge_midi_write`,
`deluge_midi_flush`, `deluge_midi_write_space` → `bsp/rza1/midi_io.c` → UART). The
**read** path does not. Sites in `midi_engine.cpp`:

- `midi_engine.cpp:284` — `uartGetCharWithTiming(TIMING_CAPTURE_ITEM_MIDI, &byte)`.
  **The subtle one.** This returns the next serial byte *and* a pointer to the
  hardware timestamp of its arrival, used to time incoming MIDI clock with low
  jitter. The existing `deluge_midi_read(port, dst, max)` has no timestamp. Needs a
  timed-read boundary op — e.g. `bool deluge_midi_din_read_timed(uint8_t* byte,
  uint32_t* arrival_ticks)` (returns false when nothing available), or a variant of
  `deluge_midi_read` that also yields per-byte timing. Whatever the shape, the
  timestamp must stay in board/foundation time units the app already understands.
- `midi_engine.cpp:263` — `uartFlushIfNotSending(UART_ITEM_MIDI)` → already have
  `deluge_midi_flush(DELUGE_MIDI_DIN)`; just repoint.
- `midi_engine.cpp:97` — `uartGetTxBufferFullnessByItem(UART_ITEM_MIDI)` (the DIN
  half of `anythingInOutputBuffer()`). Add a boundary "bytes pending TX" query or
  derive it (TX buffer size − `deluge_midi_write_space`).

### W2 — USB host/peripheral mode → boundary

- `midi_engine.cpp:92,527` — `g_usb_usbmode == USB_HOST` (host vs peripheral role).
- `midi_engine.cpp:528` — `g_usb_peri_connected` (peripheral mode: is a host attached).

`midi_io.h` already has `DelugeMidiPortKind {DIN, USB_DEVICE, USB_HOST}` and
`deluge_midi_port_connected(port)`. Decide whether to (a) add explicit role queries
(`bool deluge_midi_usb_is_host(void)`, `bool deluge_midi_usb_peripheral_connected(void)`)
or (b) express the same facts through the existing port-kind/connected API. (a) is a
smaller, more literal change; (b) is cleaner but reshapes how the app enumerates
devices. **Lean (a)** for a faithful, testable first pass.

### W3 — USB stack pump → BSP

- `midi_engine.cpp:505` — `usb_cstd_usb_task()` pumps the Renesas USB host stack,
  immediately followed by `bsp_usb_midi_service()`. The app shouldn't pump the HAL
  stack. Fold `usb_cstd_usb_task()` into `bsp_usb_midi_service()` (or
  `deluge_midi_service()`), then drop the app call. **Open question:** the `usbLock`
  re-entrancy guard around it is app-side concurrency state; decide whether it moves
  down with the pump or stays as an app gate on `deluge_midi_service()`.

### W4 — transport-struct reach-in (the phases 6–7 residue)

The receive-decode loop reads BSP-owned transport state directly:
`connectedUSBMIDIDevices[ip][d].transport->{numBytesReceived, receiveData,
currentlyWaitingToReceive}` (around `midi_engine.cpp:535–560`), plus the externs
`stopSendingAfterDeviceNum[]`, `usbDeviceNumBeingSentToNow[]`, `timeLastBRDY[]`
(declared `midi_engine.cpp:53–60`). `transport` is the BSP's `BspUsbMidiDevice`.

The pull-model end state: the app drains received USB-MIDI bytes via
`deluge_midi_read(usbPort, …)` and decodes them with the `deluge_midi` protocol lib
(`decode_event`), never dereferencing `->transport`. `timeLastBRDY` (bulk-IN
timestamp for clock-in) needs the same timed-read treatment as W1, or a dedicated
"last USB-MIDI RX timestamp" query. The send-path guards
(`stopSendingAfterDeviceNum`/`usbDeviceNumBeingSentToNow`) belong inside the BSP
send state machine; confirm they aren't still needed app-side after W3.

### W5 — board constants

- `TIMING_CAPTURE_ITEM_MIDI` → move to `board_layout.hpp` (or fold into the W1 timed
  read so the slot id never crosses the boundary — preferable).
- `USB_NUM_USBIP` → already mirrored as `DELUGE_USB_NUM_CONTROLLERS` in `midi_io.h`;
  repoint `midi_engine.cpp`'s remaining uses to it.

## Suggested phase order (each build-green)

1. **W5 + W2 (low-risk, literal):** repoint `USB_NUM_USBIP` → boundary constant; add
   the two USB-role queries and use them. No control-flow change.
2. **W1 DIN read:** design + add the timed-read boundary op, implement in
   `bsp/rza1/midi_io.c` over `uartGetCharWithTiming`, repoint the serial read +
   flush + TX-pending query. DIN-testable in isolation (MIDI-DIN clock in/out).
3. **W3 pump relocation:** move `usb_cstd_usb_task()` into the BSP service; settle the
   `usbLock` question.
4. **W4 receive pull-through:** replace `->transport->…` reads with
   `deluge_midi_read(usbPort,…)` + `deluge_midi` decode; resolve `timeLastBRDY` and
   the send-path externs. This is the structural crux.
5. Drop the three USB `RZA1/...` includes + `cpu_specific.h`; remove
   `midi_engine.cpp` from the allowlist. Lint → 4.

## Hardware test matrix (before merge)

- **USB host:** Deluge as host with a class-compliant USB-MIDI controller — notes,
  CCs, **clock in** (jitter/timing — exercises W1/W4 timestamps), SysEx in/out,
  hot-plug/unplug, multiple cables.
- **USB peripheral:** Deluge as a USB-MIDI device into a computer — same, plus the
  `g_usb_peri_connected` connect/disconnect path (W2).
- **DIN:** TRS MIDI in/out — notes, **clock in timing**, SysEx, running status
  (exercises the `numSerialMidiInput` accumulator around the timed read).
- **Concurrency:** heavy MIDI traffic during SD activity (`currentlyAccessingCard`
  gate at `checkIncomingUsbMidi`) and during audio load — confirm no `usbLock`
  regressions after W3.

## Notes / gotchas

- `bsp/rza1/midi_io.c` already includes `RZA1/cpu_specific.h` + `RZA1/uart/sio_char.h`
  (downward, fine) — the W1 read impl lands there next to the existing write/flush.
- The `deluge_midi` protocol lib (`src/midi/`) already has `decode_event`; W4 should
  reuse it rather than the inline decode still in `midi_engine.cpp`.
- Keep the timestamp semantics identical (same tick source/units) — clock-in timing
  is audible if it drifts. This is the single most test-sensitive part.
