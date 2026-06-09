# Launchpad as Deluge UI extension (Vector-style)

**Status:** Plan (June 2026) — design only, no implementation yet  
**Related:** [launchpad-mk3-usb.md](./launchpad-mk3-usb.md), [usb-multiport-midi.md](./usb-multiport-midi.md), [matriceal-sequencer-mode.md](./matriceal-sequencer-mode.md)

---

## Executive summary

Replicate the **Five12 Vector ↔ Launchpad** integration model on Deluge: plug in a Launchpad MK3, Deluge **initialises it via SysEx** (no Novation Components custom layouts), and the pads become a **live extension** of whatever the Deluge is showing — session grid, sequencer modes, keyboard, etc.

USB plumbing is **done** (port 2 virtual cable works). This project is **application-layer**: a `LaunchpadExtension` subsystem that mirrors pad colours out and routes pad presses in, reusing existing Deluge render/input hooks.

**Inspiration:** Vector auto-syncs Launchpad X / Mini MK3 / Pro MK3; user never builds a custom layout. Deluge should do the same.

---

## Concept mapping: Vector → Deluge

| Vector | Deluge equivalent | Notes |
|--------|-----------------|-------|
| **Project** | **Song** | One `.XML` song |
| **Part** (×8) | **Output / track** | Synth, MIDI, CV, Kit, Audio channel |
| **Preset** (per Part) | **Clip** | Session clip at track × section |
| **Scene** (×8) | **Section** (up to 24) | Row in grid layout; launches all clips in section |
| **Session page** | **Session grid view** | 16×8 clips; launch / arm / mute |
| **Preset page** | **Session overview** | One column per track, clip slots visible |
| **Keys page** | **Keyboard screen** | Note input for active instrument clip |
| **Step page** | **Step / Pulse / Acid sequencer** | Non-linear `SequencerMode` in clip |
| **RackEdit (Drum)** | **Kit drum rows** / drum keyboard layout | Voices × steps |
| **RackEdit (Mono)** | **Scale keyboard** / note rows | Pitch per pad |
| **Dashboard** | **Deluge settings / performance view** | Lower priority for v1 |

Deluge has **more** surface area than Vector (16-wide grid, audio clips, automation, arranger). v1 should focus on **session grid + sequencer modes** — the highest-value overlap.

---

## Launchpad hardware constraints

| Resource | Launchpad X / Mini MK3 | Deluge |
|----------|----------------------|--------|
| Main grid | 8×8 (64 pads) | 16×8 (128 pads) + 2 sidebar columns |
| Extra buttons | Top row + right column (Programmer mode) | Section column (×8), mode column |
| Colours | 127-entry palette via velocity; **RGB via SysEx** | Full RGB per pad |
| USB on Deluge | **Port 2** = pad MIDI | Port 1 = DAW (ignore for this feature) |

**One Launchpad** = 8×8 window (scrolls with `songGridScrollX/Y`).  
**Two Launchpads** = full 16×8 main grid (cols 0–7 + 8–15). See [launchpad-mk3-usb.md](./launchpad-mk3-usb.md).

---

## Vector Launchpad pages (reference)

From Five12 firmware docs (provisional):

| Page | Trigger (MK3) | Grid use |
|------|---------------|----------|
| **Session** | Session button | Row 1: select Part; Row 2: mute Part; Rows 3–4: voice mutes; Rows 5–6: Scene; Row 7: Dashboard nav |
| **Preset** | Drums / User1 | One Part per column — preset slots |
| **Keys** | Keys / User2 | 4-octave performance keyboard |
| **RackNav / RackEdit** | User / Mixer | Pick Part; edit on grid (mono scale degrees or drum 4×16×2) |
| **Step** | (from Session) | Step composition for active Preset |

Deluge plan mirrors **Session + active-clip edit** first; Preset/Keys pages are later phases.

---

## Architecture overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Deluge UI layer                          │
│  SessionView          InstrumentClipView       KeyboardScreen │
│  gridRenderMainPads   SequencerMode::          renderPads     │
│  gridHandlePads       renderPads / handlePadPress           │
└────────────┬────────────────────┬───────────────────────────┘
             │                    │
             ▼                    ▼
┌─────────────────────────────────────────────────────────────┐
│              LaunchpadExtension (new module)                 │
│  • Page detection (session / clip / sequencer / keyboard)     │
│  • Coordinate map: Deluge (x,y) ↔ LP note / LED index        │
│  • LED sync: RGB image[][] → palette or batched LED SysEx    │
│  • Input demux: LP Note On → gridHandlePads / handlePadPress │
│  • Multi-device: LP[0] cols 0–7, LP[1] cols 8–15              │
└────────────┬────────────────────────────────────────────────┘
             │ port 2 (MIDICableUSBHosted)
             ▼
┌─────────────────────────────────────────────────────────────┐
│              Novation Launchpad MK3                          │
│  Programmer mode (SysEx init on connect)                     │
└─────────────────────────────────────────────────────────────┘
```

### Design principle: reuse existing render/input

Do **not** duplicate grid/sequencer logic. Mirror what Deluge already computes:

1. **Output:** After `PadLEDs::image` is filled (session grid or `SequencerMode::renderPads()`), `LaunchpadExtension::syncLedsFromImage()` diff-and-send to Launchpad(s).
2. **Input:** Before normal MIDI clip routing, if extension active and note matches Programmer map → translate to `(x,y)` → call existing handler (`gridHandlePads`, `sequencerMode->handlePadPress`, etc.).

This matches how Vector drives the Launchpad from its internal UI state.

### Key existing hooks

| Deluge component | File | Reuse for |
|------------------|------|-----------|
| Session grid render | `session_view.cpp` → `gridRenderMainPads()` | Session page LED mirror |
| Session grid input | `session_view.cpp` → `gridHandlePads()` | Session page presses |
| Sequencer render/input | `sequencer_mode.h` → `renderPads()` / `handlePadPress()` | Step/Pulse/Acid pages |
| Clip colours | `gridRenderClipColor()` | Launch / arm / mute / pulse colours |
| Device-specific hooks | `specific_midi_device.cpp` → `iterateAndCallSpecificDeviceHook()` | Connect/disconnect lifecycle |
| Norns precedent | `melodic_instrument.cpp` + `keyboard_layout_norns` | External MIDI → pad highlight pattern |

---

## Launchpad protocol (host-owned)

All messages on **port 2** unless noted.

### Connect sequence

| Step | Message |
|------|---------|
| 1 | Programmer/Live → Programmer: `F0 00 20 29 02 0C 0E 01 F7` |
| 2 | Optional: brightness SysEx (`0C` config commands) |
| 3 | Clear all LEDs (batch SysEx or Note Off sweep) |

### Disconnect / disable

| Step | Message |
|------|---------|
| 1 | Return to Live: `F0 00 20 29 02 0C 0E 00 F7` |

### LED updates (choose per phase)

| Method | Pros | Cons |
|--------|------|------|
| **Note On ch 1**, velocity = palette | Simple; matches Vector-ish behaviour | Deluge RGB → nearest palette entry |
| **LED SysEx** `0C 03h`, type 3 RGB | Accurate colours; up to **81 LEDs/message** | More code; larger packets |
| **Hybrid** | Palette for session; SysEx for pulsing | Best UX |

Recommendation: **SysEx batch** for grid refresh (≤64 entries per 8×8); palette fallback if USB busy.

### Input

Programmer mode: each pad sends fixed **Note On** (see Novation programmer manual). Deluge maintains static `note → (x,y)` table per Launchpad model (X / Mini MK3 / Pro MK3 differ slightly on edge buttons).

---

## Launchpad “pages” (Deluge-driven)

Auto-select page from active UI (like Vector sync):

| `LaunchpadPage` | When active | Deluge handler | LED source |
|-----------------|-------------|----------------|------------|
| `SESSION_GRID` | `SessionView` + grid layout | `gridHandlePads` | `gridRenderMainPads` |
| `SEQUENCER` | Clip view + `InstrumentClip` has active `SequencerMode` | `handlePadPress` | `renderPads` |
| `KEYBOARD` | Clip view + keyboard screen | `KeyboardScreen` pad handlers | `layout->renderPads` |
| `DRUMS` | Clip view + kit + drum layout | Drum layout handlers | Drum `renderPads` |
| `INACTIVE` | Arranger, menus, audio clip, etc. | — | Clear LP or dim |

**Page switch:** on `getCurrentUI()` / view transition (hook alongside existing `iterateAndCallSpecificDeviceHook`).

Optional v2: **manual page** via Launchpad top row (Vector row 7 = dashboard nav analogue).

---

## Deluge page designs

### 1. Session grid page (v1 — highest value)

**Maps to:** Vector Session page (simplified).

| Deluge grid cell | Launchpad cell | Action (launch mode) |
|------------------|----------------|----------------------|
| `(x, y)` clip | `(x mod 8, y)` on LP `[x/8]` | Launch / arm / select (per `gridModeActive`) |
| Section column `x=16` | Top row buttons | `gridStartSection` |
| Mode column `x=17` | Right column (optional) | Green/blue launch vs edit |

**Scroll:** `songGridScrollX/Y` — 8×8 LP shows window; re-sync LEDs on scroll.

**Colours:** mirror `gridRenderClipColor()` — armed, playing, muted, recording, MIDI-learn.

### 2. Sequencer page (v1.5)

**Maps to:** Vector Step / RackEdit.

When user opens clip with **Step**, **Pulse**, or **Acid** sequencer:

- Call existing `SequencerMode::renderPads()` into a scratch `RGB image[8][18]`
- Mirror 8×8 (or 16×8 with two LPs) to Launchpad
- Presses → `handlePadPress(x, y, velocity)` with coordinate offset for scroll

**Bonus:** Control columns (x=16–17) can map to Launchpad edge buttons in a later sub-phase.

### 3. Keyboard page (v2)

**Maps to:** Vector Keys page.

Mirror `KeyboardLayout::renderPads()` for active layout (isomorphic, piano, in-key, etc.). 8×8 may show one octave window — follow keyboard scroll.

### 4. Drum / kit page (v2)

**Maps to:** Vector RackEdit drum (4 voices × 16 steps).

Kit rows with drum keyboard layout — map row/y to LP; may only fit 8 rows at a time.

### 5. Preset overview (v3 — optional)

**Maps to:** Vector Preset page.

One column per track, rows = sections — similar to session grid but emphasises **which clip is active per track** rather than launch matrix. Deluge grid already does this; may be redundant with session page.

---

## Multi-Launchpad (16×8)

| Device | Column range | `midiDeviceNum` / port |
|--------|--------------|------------------------|
| Launchpad A (left) | `x = 0..7` | port 2 of device A |
| Launchpad B (right) | `x = 8..15` | port 2 of device B |

Requirements:

- Unique **device ID** per unit (bootloader)
- `LaunchpadExtension` device table (up to 2–3 units)
- Split `syncLedsFromImage()` and input demux by column range
- Fits within `MAX_NUM_USB_MIDI_DEVICES = 6` and 6 hosted menu entries (4 ports for 2 LPs)

---

## Module structure (proposed files)

```
src/deluge/io/midi/device_specific/
  novation_launchpad_mk3.h          # VID/PID, model enum
  midi_device_launchpad_mk3.h/cpp   # MIDICableUSBHosted subclass; connect/disconnect
  launchpad_extension.h/cpp         # Core mirror engine
  launchpad_programmer_map.h/cpp    # Note ↔ (x,y) per model; LED index table
  launchpad_led.h/cpp               # Palette quantise + SysEx batch builder
```

### `LaunchpadExtension` API (sketch)

```cpp
class LaunchpadExtension {
public:
    static LaunchpadExtension& instance();
    void onViewChanged();                    // re-evaluate page
    void syncLedsFromImage(RGB image[][kDisplayWidth + kSideBarWidth],
                           uint8_t occupancyMask[][...]);
    bool handleIncomingNote(MIDICable& cable, uint8_t note, uint8_t velocity);
    bool isActive() const;
};
```

Call sites:

- `SessionView::renderMainPads` (end) → `syncLedsFromImage`
- `InstrumentClipView` after sequencer `renderPads` → `syncLedsFromImage`
- `MidiEngine` / `MelodicInstrument::offerReceivedNote` early exit if extension consumed
- `MIDIDeviceManager::slowRoutine` → `hookOnConnected` sends Programmer SysEx

### Settings / feature flag

Community feature toggle (like Norns layout):

- `EnableLaunchpadExtension` in `runtime_feature_settings`
- Per-song optional: which port 2 device is “primary” LP (by name in `MIDIDevices.XML`)

---

## Implementation phases

### Phase 0 — Foundation (small, shippable alone)

- [ ] VID/PID table: X, Mini MK3, Pro MK3
- [ ] `MIDIDeviceLaunchpadMK3` on **port 2** only (`… port 2` name match or `portNumber==1`)
- [ ] Connect: Programmer SysEx; disconnect: Live SysEx
- [ ] `launchpad_programmer_map` — 8×8 note table for X (reference manual)
- [ ] Runtime feature flag (off by default)
- [ ] No mirroring yet — confirms init works

**Test:** LP enters Programmer mode on plug-in; Deluge normal MIDI on port 2 still works.

### Phase 1 — Session grid mirror (8×8, one Launchpad)

- [ ] `LaunchpadExtension::syncLedsFromImage` from `gridRenderMainPads`
- [ ] RGB → palette or SysEx batch; diff against `lastSent_` to limit traffic
- [ ] Input: Note → `(x,y)` → `gridHandlePads` when session grid active
- [ ] Respect `gridModeActive` (launch vs edit)
- [ ] Follow `songGridScrollX/Y`
- [ ] Consume input — do not double-trigger MIDI clips

**Test:** Clip colours on LP match Deluge; pad press launches/arms; scroll shifts window.

### Phase 2 — Sequencer mode mirror

- [ ] Detect active `SequencerMode` in clip view
- [ ] Mirror `renderPads` / `handlePadPress` for Step, Pulse, Acid
- [ ] Page auto-switch session ↔ sequencer on view change
- [ ] `stopAllNotes` / playback stop clears LP LEDs

**Test:** Edit step sequencer on LP; colours match Deluge; playback unchanged.

### Phase 3 — Edge buttons (Vector-like extras)

- [ ] Top row → section launch (`gridStartSection`)
- [ ] Right column → launch/edit mode toggle (green/blue analogue)
- [ ] Transport shortcuts (optional): play/stop via specific pads

### Phase 4 — Dual Launchpad (16×8)

- [ ] Register two `MIDIDeviceLaunchpadMK3` instances
- [ ] Column split in sync and input
- [ ] User doc: device IDs, powered hub

### Phase 5 — Keyboard + drums

- [ ] Keyboard screen mirror (scrollable window)
- [ ] Kit/drum layout mirror

### Phase 6 — Polish

- [ ] Rename port 1/2 → DAW/MIDI in menu for Novation VID/PID
- [ ] OLED status: “Launchpad sync” icon
- [ ] Song save: remember LP extension enabled (optional)

---

## Non-goals (v1)

- Ableton-style **DAW mode** (port 1 session ring)
- Novation Components custom layout import/export
- Launchpad control of **arranger view**, **automation view**, **audio clip** waveform
- **MPE** or per-pad pressure routing to Deluge (LP pressure could be v3)
- Replacing Deluge pads when LP disconnected (Deluge always remains fully functional)

---

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| MIDI bandwidth (64–128 LEDs × frequent updates) | Diff frames; SysEx batch; throttle to 30–60 Hz |
| Pulse/blink desync | Reuse Deluge `view.blinkOn` / same tick as `PadLEDs` |
| Input double-routing (LP + MIDI clip) | Extension consumes notes when active; cable filter |
| Pro MK3 vs X button layout differs | Per-model maps; start with X / Mini MK3 |
| User on port 1 by mistake | Docs + optional rename DAW/MIDI |
| RAM | Fixed buffers; no per-pad heap; one `lastSent_[8][8]` per device |

---

## Test matrix

| # | Scenario | Pass |
|---|----------|------|
| 1 | LP connect | Programmer mode; no crash |
| 2 | LP disconnect | Live mode restored |
| 3 | Session grid, 8×8 song | Colours match; launch works |
| 4 | Session grid, 16 tracks | Horizontal scroll; LP shows correct window |
| 5 | Step sequencer in clip | Edit steps on LP |
| 6 | MRCC 880 multi-port | Unaffected |
| 7 | 2× Launchpad | 16×8 simultaneous |
| 8 | Feature off | No SysEx; port 2 manual MIDI works |
| 9 | Hub + LP + other MIDI | All enumerate |

---

## Effort estimate (rough)

| Phase | Size | Notes |
|-------|------|-------|
| 0 | S | ~1–2 days |
| 1 | M | Core value; ~1 week |
| 2 | M | Reuses SequencerMode API; ~1 week |
| 3 | S | Edge button maps |
| 4 | S–M | Second device plumbing |
| 5 | L | Layout-specific maps |
| **1+2 shippable MVP** | **~2–3 weeks** | Vector-like session + step edit |

---

## Related documents

- [launchpad-mk3-usb.md](./launchpad-mk3-usb.md) — USB ports, port 2 confirmed working
- [usb-multiport-midi.md](./usb-multiport-midi.md) — virtual cable multi-port (orthogonal)
- [matriceal-sequencer-mode.md](./matriceal-sequencer-mode.md) — example of grid-mapped sequencer plugin
- [Novation Launchpad X Programmer's Reference](https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/Launchpad%20X%20-%20Programmers%20Reference%20Manual.pdf)
- [Five12 Vector user guide](https://files.five12.com/VectorUserGuideV3.0.pdf) — Launchpad integration reference

---

## Open questions

1. **Default on or off?** Community feature flag default `Off` recommended.
2. **Pro MK3 priority?** Same codebase as X/Mini MK3 but edge button map differs — confirm target hardware for Phase 0.
3. **Audio clips in session grid?** Mirror yes/no colours same as instrument clips (likely yes via `gridRenderClipColor`).
4. **Rows layout session?** v1 grid-layout only, or also support rows layout LP mapping?
5. **Persist LP preference in song?** Optional `launchpadExtension="Launchpad X port 2"` in song XML.

---

## Implementation checklist (tracking)

- [ ] Phase 0: Programmer SysEx + feature flag
- [ ] Phase 1: Session grid 8×8 mirror
- [ ] Phase 2: Sequencer mode mirror
- [ ] Phase 3: Edge buttons
- [ ] Phase 4: Dual Launchpad 16×8
- [ ] Phase 5: Keyboard + drums
- [ ] Phase 6: Polish + docs
