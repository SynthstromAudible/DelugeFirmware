# Novation Launchpad MK3 — USB host support

**Status:** Confirmed working on hardware (2026-06-09) — port 2 carries pad MIDI  
**Related:** [launchpad-vector-extension.md](./launchpad-vector-extension.md) (Vector-style UI extension plan), [usb-multiport-midi.md](./usb-multiport-midi.md)

---

## Observed behaviour on current firmware

With multi-port MIDI support (`67a0a5782`), a connected Launchpad MK3 already appears in **MIDI Devices** as two entries:

- `Launchpad X port 1` (or similar product name)
- `Launchpad X port 2`

This comes from **descriptor parsing**, not from enumerating two separate USB MIDI interfaces.

`hostedDeviceConfigured()` walks the full configuration descriptor, finds `bNumEmbMIDIJack >= 2` in class-specific endpoint descriptors, and creates two routable cables on the **same** `ConnectedUSBMIDIDevice` / USB pipe pair:

| Menu entry | `portNumber` on send | USB cable nibble |
|------------|---------------------|------------------|
| port 1 | 0 | 0 |
| port 2 | 1 | 1 |

---

## Launchpad USB model (hardware)

Novation documents **two MIDI interfaces** on one USB attachment:

| Interface | Typical name | Purpose |
|-----------|--------------|---------|
| 1st | LPX DAW In/Out | Session / DAW mode |
| 2nd | LPX MIDI In/Out | Note / Custom / Programmer mode, LED control |

Each interface has its own bulk in/out endpoint pair. On macOS/Windows these show as separate MIDI ports (`MI_00`, `MI_01`), not as cable 0/1 on one port.

Reference: [Launchpad X Programmer's Reference Manual](https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/Launchpad%20X%20-%20Programmers%20Reference%20Manual.pdf)

---

## Confirmed: port 1 / port 2 mapping

Hardware test (2026-06-09): **MIDI from Launchpad works fine** when routed to **port 2**.

The multi-port virtual-cable model is sufficient — no second USB interface enumeration required.

| Port | Role (expected) | Status |
|------|-----------------|--------|
| port 1 | DAW / session interface | Present; use only for DAW-style integration |
| port 2 | Pad MIDI / Programmer / LEDs | **Works** — route clips and learn here |

### Remaining polish (optional)

1. **Rename** menu entries for Launchpad VID/PID: `… DAW` / `… MIDI` instead of `port 1` / `port 2`
2. **Connect hook** on port 2: auto-send Programmer mode SysEx  
   `F0 00 20 29 02 0C 0E 01 F7` (so pads light without manual mode switch)
3. User docs: always route to **port 2** (or **MIDI** once renamed)

---

## VID/PID table (Novation `0x1235`)

| Model | PID range | Alt PID |
|-------|-----------|---------|
| Launchpad X | `0x0103`–`0x0112` | `0x1103` |
| Launchpad Mini MK3 | `0x0113`–`0x0122` | `0x1113` |
| Launchpad Pro MK3 | `0x0123`–`0x0132` | `0x1123` |

---

## Relationship to multi-port MIDI work

| | MRCC 880 / OXI | Launchpad MK3 (current) |
|--|----------------|-------------------------|
| Mechanism | Virtual cables, 1 USB pipe | Same — 2 menu entries, 1 USB pipe |
| Created by | `bNumEmbMIDIJack` in descriptor | Same |
| Conflict | None | None |

Multi-port work is **already doing the right thing for the menu**. The unknown is whether cable 1 on the first enumerated pipe reaches the Launchpad's pad MIDI path.

---

## Recommended workflow (current firmware — no custom code)

Use a **Launchpad Custom mode** (Novation Components) or **Programmer mode** with a fixed note/CC layout, then **manually map** on the Deluge via existing MIDI learn and clip routing.

1. Connect Launchpad → route to **port 2** in MIDI Devices
2. Build a Custom mode in [Novation Components](https://components.novationmusic.com/) — each pad sends a distinct Note On (or CC)
3. On Deluge session grid: MIDI learn clip launch/arm to those notes
4. Optional LED feedback: send Note On back to port 2 from a MIDI clip or CV/Gate track (velocity = Launchpad palette colour)

This gives a personal 8×8 (or 8×8 × N units) layout without grid-mirror firmware. Two Launchpads = 16 columns of mappable pads if each has a non-overlapping note range in Custom mode.

Non-goals for now: native Deluge grid mirroring, DAW session mode, auto Programmer SysEx on connect.

---

## Fallback plan (not needed)

Second USB interface enumeration was considered if port 2 failed on hardware. **Ruled out** — virtual cable port 2 works.

---

## Programmer mode essentials

SysEx header: `F0 00 20 29 02 0C`

| Function | SysEx |
|----------|-------|
| Programmer mode | `… 0E 01 F7` |
| Live mode (restore) | `… 0E 00 F7` |

LED feedback: Note On ch 1 = static colour; velocity = palette index.

---

## Test plan

| Test | Pass criteria |
|------|---------------|
| Launchpad attach | Two entries in MIDI Devices |
| Route to port 2, press pads | Note On received |
| Send Note On to port 2 | Pad lights (Programmer mode) |
| Route to port 1 only | DAW/session traffic (if any), not pad grid |
| MRCC 880 regression | Multi-port unchanged |

---

## Implementation checklist (revised)

- [x] Document observed port 1 / port 2 behaviour
- [x] Hardware test: port 2 carries pad MIDI
- [ ] Launchpad connect hook (Programmer SysEx) on port 2 only — optional UX
- [ ] Optional: rename port 1/2 → DAW/MIDI for Launchpad VID/PID
- [x] Second-interface USB enumeration — not required

---

## Key files

| File | Role |
|------|------|
| `src/deluge/io/midi/midi_device_manager.cpp` | Port count from descriptor, `port N` naming |
| `src/deluge/io/midi/cable_types/usb_common.cpp` | `portNumber` → USB cable nibble on send |
| `src/deluge/io/midi/root_complex/usb_hosted.cpp` | Per-cable receive routing |
