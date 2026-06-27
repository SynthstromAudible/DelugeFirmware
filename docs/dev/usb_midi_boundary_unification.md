# USB-MIDI transport → the `<libdeluge/midi_io.h>` boundary (the last Tier-2 coupling)

**Status: implemented** on `feat/libdeluge-usb-midi-boundary` (2026-06-09) —
build + extended boundary lint (**0**) + host tests green. **Not yet
flash-tested**: it is the live, real-time USB-MIDI path, so — exactly like the
original USB-MIDI transport relocation it completes — **it must be flash-tested
as USB host *and* peripheral before merge** (matrix below). See the Outcome
section at the end for how the implementation resolved each design decision and
the intentional behavioral deviations to verify on hardware.

When this lands, the extended platform-boundary lint (which now also forbids
`drivers/` and `bsp/`) goes to **0**: `src/deluge` includes only `<libdeluge/...>`,
and **the application compiles against any BSP** — which is precisely what unblocks
the host-sim and `deluge-embassy` targets.

## Why this one is special: design it for Embassy, not for the RZ/A1L

The three remaining offenders (`midi_engine.cpp`, `midi_device_manager.cpp`,
`cable_types/usb_common.cpp`) all include `bsp/rza1/usb_midi.h`. The temptation is
to just rename the `bsp_usb_midi_*` calls to `deluge_midi_usb_*` and move the
declarations into the boundary. **That would be a mistake** — it would bake the
RZ/A1L USB driver's shape into the contract a Rust BSP has to implement.

Look at what the app currently calls (all in `midi_engine.cpp`'s receive path):

- `bsp_usb_midi_host_send_idle(ip)`
- `bsp_usb_midi_setup_host_receive_transfer(ip, d)`
- `bsp_usb_midi_rearm_peripheral_receive(ip)`

These are **Renesas USB-stack pumping** — manually arming the next bulk transfer,
checking whether the host controller is mid-send. **An `embassy-usb` BSP has no
equivalent and never could**: in Embassy you `await` a `MidiClass` read/write and
the executor drives the transfers; there is no "rearm the receive transfer" call to
expose. So these must not appear at the boundary at all — they belong *inside* the
BSP, driven by `deluge_midi_service()`/read/write. The boundary the app sees has to
be the small, transport-agnostic one that both an RZ/A1L driver and an Embassy
`MidiClass` can satisfy.

That reframes the whole task: not "relocate the BSP API" but **"reduce the app to
the transport-agnostic boundary that already exists for DIN, and hide everything
USB-specific in the BSP."**

## Current coupling inventory

### A. The `transport` struct reach-in (the core problem)

`ConnectedUSBMIDIDevice` (app, `midi_device_manager.h:55`) is *almost* logical —
`cable[4]`, `maxPortConnected` — but carries `struct BspUsbMidiDevice* transport`,
a pointer into BSP-owned state. The app dereferences it:

| Field | Sites | What it really is |
|---|---|---|
| `transport->canHaveMIDISent` | midi_engine:224, usb_common:48, midi_device_manager:286–287/341 | **App policy** — set from the device *name* ("don't send to LUMI/Foot Controller"), read by the send path. Not transport state at all. |
| `transport->connected` | midi_device_manager:288/326/342/358 | **App logical state** — "is there a live send sink" (explicitly "was cable[0] != NULL"). |
| `transport->sq` | midi_device_manager:285 | **BSP transport state** — reset to 0 on device setup. The only genuinely-transport field the app touches. |
| `transport` (the pointer) | midi_device_manager:84 (`= bsp_usb_midi_device(ip,d)`) | The coupling itself. |

### B. The `bsp_usb_midi_*` calls

| Call | Site(s) | Disposition (see Target) |
|---|---|---|
| `bsp_usb_midi_init()` | mdm:87 | → BSP-internal (board/midi init) |
| `bsp_usb_midi_device(ip,d)` | mdm:84 | **eliminate** (no transport pointer) |
| `bsp_usb_midi_buffer_message(transport, word)` | mdm:667 | → `deluge_midi_write(port, eventbytes)` |
| `bsp_usb_midi_send_buffer_space(transport)` | mdm:671 | → `deluge_midi_write_space(port)` |
| `bsp_usb_midi_anything_in_output_buffer()` | me:84 | → boundary query / derive from write-pending |
| `bsp_usb_midi_flush_output()` | me:249 | → `deluge_midi_flush(port)` / fold into service |
| `bsp_usb_midi_receive_pending(ip,d)` | me:522 | → implicit in `deluge_midi_read` returning 0 |
| `bsp_usb_midi_drain_received(ip,d,buf,n)` | me:527 | → `deluge_midi_read(port, buf, n)` |
| `bsp_usb_midi_last_rx_ticks(ip)` | me:531 | → an RX-timestamp query (see D-timestamp) |
| `bsp_usb_midi_host_send_idle(ip)` | me:587 | **hide in BSP** (host-stack pumping) |
| `bsp_usb_midi_setup_host_receive_transfer(ip,d)` | me:589 | **hide in BSP** |
| `bsp_usb_midi_rearm_peripheral_receive(ip)` | me:578 | **hide in BSP** |

## Target architecture

**Unify USB-MIDI onto the same port + byte-stream boundary as DIN.** It already
exists in `midi_io.h`: `deluge_midi_read/write(DelugeMidiPort, bytes)`,
`deluge_midi_write_space`, `deluge_midi_flush`, `deluge_midi_port_connected`,
`deluge_midi_service`, and `deluge_midi_port_count`/`_kind` (the dynamic-port model
USB enumeration needs). The bytes are USB-MIDI 1.0 **4-byte event packets** — which
the app already packs/decodes with the `deluge_midi` protocol lib
(`pack_event`/`decode_event`, `src/midi/`). The cable number lives in the packet
header, so one **port per USB-MIDI device** carries all its cables; the app demuxes
cables from the decoded events (it already does).

So:

- **`ConnectedUSBMIDIDevice` becomes purely logical**: `{cable[4], maxPortConnected,
  canHaveMIDISent, connected, DelugeMidiPort port}`. The `transport` pointer and the
  `BspUsbMidiDevice` struct **leave the app entirely** (the struct stays BSP-internal
  in `usb_midi.h`, no longer referenced from `src/deluge`).
- **Send**: app packs `MIDIMessage` → event word (`pack_event`) →
  `deluge_midi_write(device.port, bytes)`; back-pressure via
  `deluge_midi_write_space`. Gated by the app's own `canHaveMIDISent` (now a logical
  field).
- **Receive**: app `deluge_midi_read(device.port, buf, n)` → `decode_event` →
  route to `cable[event.cable]`. No `receive_pending`/`rearm`/`setup_transfer` — the
  BSP arms its own transfers; the app just reads what's there.
- **Service**: `deluge_midi_service()` (already the app's pull point) drives all USB
  transport internally — host enumeration polling, transfer (re)arming, send
  draining. The RZ/A1L host-stack pumping moves *here*, hidden. An Embassy BSP
  implements `deluge_midi_service` as a no-op (its executor already runs the USB
  task) or a yield.

### The transport fields, resolved

- **`canHaveMIDISent`** → a plain `bool` field on the (logical) `ConnectedUSBMIDIDevice`.
  Pure app policy; never crosses the boundary.
- **`connected`** → a plain `bool` field on the logical struct (it already *means*
  "cable[0] != NULL / live sink"). The BSP's own notion of "device enumerated" stays
  behind `deluge_midi_port_connected(port)`; keep the two distinct.
- **`sq`** → BSP-internal. Reset by the BSP when a port is (re)opened; the app stops
  touching it. If a reset trigger is needed, fold it into the
  enumeration/port-open path the BSP already runs in `deluge_midi_service`.

## Design decisions to settle

- **D-port-model (the crux): one `DelugeMidiPort` per USB-MIDI device.** Map
  `(ip, d)` → a stable port id; DIN is its own port. The app keeps its
  `connectedUSBMIDIDevices[ip][d]` data model app-side and stores the port id per
  device. Rejected: per-*cable* ports (cables are a sub-address inside the event
  packet — finer than the transport unit and not how Embassy's `MidiClass` is shaped).
- **D-byte-stream vs message API.** Use the **byte stream of event packets**
  (`deluge_midi_read/write`), not a `send_event_word`/`recv_event_word` message API.
  Byte streams unify USB with DIN, keep pack/decode in the app's `deluge_midi` lib
  (testable, shared), and are exactly what `embassy-usb`'s `MidiClass` exposes.
  Endianness of the 4-byte word is owned by `deluge_midi` (already little-endian via
  `UsbEventPacket::word()/fromWord()`).
- **D-timestamp.** `bsp_usb_midi_last_rx_ticks(ip)` timestamps incoming MIDI clock.
  Mirror the DIN solution (`deluge_midi_din_read_timed`): either a
  `deluge_midi_read_timed(port, …)` variant or a per-port "last RX tick" query.
  Keep the tick source/units identical (clock-in timing is audible).
- **D-host-vs-peripheral.** Already covered by `deluge_midi_usb_is_host()` /
  `deluge_midi_usb_peripheral_connected()`. The app keeps branching device-count on
  these; the *transfer mechanics* of each mode are hidden in the BSP.
- **D-service contract.** Pin down `deluge_midi_service()` as "pump all MIDI
  transport, non-blocking" so RZ/A1L can do its host-stack polling and Embassy can
  no-op. This is the contract that makes both BSPs satisfy one boundary.

## Phases (each build-green; the whole only flash-testable host + peripheral)

1. **Move the transport fields off the struct.** Add `canHaveMIDISent` + `connected`
   as logical fields on `ConnectedUSBMIDIDevice`; repoint the ~7 `transport->{…}`
   sites. `sq` reset moves into the BSP. Build-green, behaviour identical (still uses
   the BSP API for I/O). This alone removes the struct *reach-in*.
2. **Add the USB port mapping.** Assign a `DelugeMidiPort` per device; wire
   `deluge_midi_port_kind`/`_count` for USB. No behaviour change.
3. **Send path → boundary.** `bufferMessage`/`sendBufferSpace`/`flush`/
   `anything_in_output_buffer` → `deluge_midi_write`/`_write_space`/`_flush` on the
   port. Drop `bsp_usb_midi_buffer_message`/`_send_buffer_space`/`_flush_output`/
   `_anything_in_output_buffer` from the app.
4. **Receive path → boundary.** `receive_pending`/`drain_received`/`last_rx_ticks` →
   `deluge_midi_read`(+timed); **delete** the `host_send_idle`/
   `setup_host_receive_transfer`/`rearm_peripheral_receive` calls — fold that logic
   into the BSP's `deluge_midi_service`.
5. **Sever the pointer.** Remove `ConnectedUSBMIDIDevice::transport` and
   `bsp_usb_midi_device()`; the app no longer names `BspUsbMidiDevice`. Drop the
   `bsp/rza1/usb_midi.h` include from all three files → **lint 0 (extended)**.
6. **Tidy.** `bsp_usb_midi_init()` → behind a board/midi init boundary call;
   confirm `usb_midi.h` is referenced only within `src/bsp/rza1`.

## Hardware test matrix (before merge)

- **USB host:** class-compliant controllers — notes/CC, **clock-in timing** (D-timestamp),
  SysEx in/out, multi-cable, hot-plug/unplug, the "don't send to LUMI/Foot Controller"
  policy (`canHaveMIDISent`).
- **USB peripheral:** into a computer — same, plus connect/disconnect
  (`peripheral_connected`) and the `rearm` path now hidden in the BSP.
- **Both modes back-to-back** (the host/peripheral branch).
- **Under load:** heavy MIDI during audio + SD activity — no dropped/duplicated
  events, no added clock jitter.

## Risk notes

- Live real-time USB path; only the finished result is testable. Flash-test host +
  peripheral before merge, on real hardware.
- Keep the `deluge_midi` pack/decode **bit-identical** — it's the shared protocol and
  any change is felt as wrong notes.
- The win is structural: after this, `BspUsbMidiDevice` and all USB-stack pumping are
  invisible to the app, and the MIDI boundary is one an `embassy-usb` `MidiClass`
  can implement directly. That is the long-term, Embassy-aligned shape — worth the
  extra phase-1 step of moving the fields rather than rename-relocating the BSP API.

## Outcome (implementation, 2026-06-09)

How the design decisions resolved:

- **D-port-model:** port 0 = DIN; ports 1..MAX_NUM_USB_MIDI_DEVICES = USB device
  slots on the single controller. The mapping is owned by the BSP and queried via
  a new boundary call `deluge_midi_usb_port(controller, device)`; the app stores
  the id in `ConnectedUSBMIDIDevice::port` at `MIDIDeviceManager::init()`.
  `deluge_midi_port_count()/_kind()` are implemented.
- **D-timestamp:** a read variant, `deluge_midi_read_timed(port, dst, max,
  *arrival_ticks)` — one timestamp per drained batch, read from the same
  `timeLastBRDY[ip]` at the same point as before, so MIDI-clock-in timing is
  bit-identical. `deluge_midi_read()` forwards to it with a NULL timestamp.
- **`connected` did *not* become an app field** (plan §phase-1 refined): the app
  only ever *wrote* it — the only reader is the BSP send loop. So the BSP now
  maintains it itself, from the HAL enumeration paths, via four new notifications
  (`bsp_usb_midi_device_attached/_detached`, `bsp_usb_midi_peripheral_configured/
  _detached`) called in `r_usb_hmidi_driver.c` (`hmidi_configured`/`hmidi_detach`)
  and `r_usb_pdriver.c` (`usb_peri_configured`/`usb_peri_detach`) right before the
  existing app callbacks. These also absorb the app-side `sq = 0`, `setup()`'s
  transfer-counter zeroing, and the `anyUSBSendingStillHappening[ip] = 0` resets
  (an extern-global coupling the plan's inventory missed).
  `deluge_midi_port_connected()` now works for USB ports.
- **`canHaveMIDISent`** became the planned app-policy field on
  `ConnectedUSBMIDIDevice` (the pre-existing foot-controller/LUMI double-assignment
  was preserved bit-for-bit).
- **Send:** `bufferMessage` writes the event word's 4 little-endian bytes via
  `deluge_midi_write(port, …)`; `sendBufferSpace` = `deluge_midi_write_space / 4 * 3`
  (boundary speaks transport bytes, callers keep serial-MIDI bytes — values
  identical). `anything_in_output_buffer` is gone: `anythingInOutputBuffer()` loops
  `deluge_midi_write_pending()` over all ports; USB `write_pending` reports the send
  ring's backlog (bytes accepted, not yet handed to hardware), which matches the old
  global flag's semantics.
- **Receive:** drain + re-arm live inside the BSP read. Gating is unchanged
  (usbLock skip, host `send_idle` guard, connected check), **but the re-arm now
  happens *before* the app dispatches the drained batch, not after** — the only
  intentional ordering change; the drained bytes are already copied out, so the
  DMA target is free. Verify under load on hardware.
- **D-service / init:** `deluge_midi_service()` contract pinned as "pump all MIDI
  transport, non-blocking"; new `deluge_midi_init()` replaces the app calling
  `bsp_usb_midi_init()` (phase 6).
- `usb_midi.h` is now referenced only from `src/bsp/rza1` and the two HAL
  enumeration files (by prototype); the per-device drain/re-arm/ring helpers are
  static. `ConnectedUSBMIDIDevice` lost its `transport` pointer; nothing in
  `src/deluge` names `BspUsbMidiDevice`. Extended lint: **0**.
