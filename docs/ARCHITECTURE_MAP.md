# Architecture Map — Harmonic Composition Workflow

> **Status:** Phase 1 discovery. Findings only — no implementation proposed here.
> **Scope:** The subsystems relevant to three related features that together form a
> harmonic-composition workflow: (1) Fold Active Notes, (2) Chord Palette,
> (3) Custom Chord Packs. These are treated as one workflow, not three isolated asks.
>
> All `file:line` references are against the `community` branch as cloned locally.

---

## 0. Executive summary

| Subsystem | State today | Reuse potential |
|---|---|---|
| Chord data + voicings | 33 chord types × 4 voicings, interval-based, unit-tested | **High** — complete data layer already exists |
| Chord palette UI | A `KeyboardLayoutChordLibrary` already browses all 33 chords, selects root, previews on pad press | **High** — the browse/select UI exists; only *placement into the clip* is missing |
| Note→clip write path | `getOrCreateNoteRowForYNote()` + `recordNoteOn()` already used by MIDI + step recording | **High** — "place chord" = call it once per voicing note |
| Piano-roll rendering | Already **sparse-row-aware** for synth clips; `NoteRow::hasNoNotes()` exists | **High** — fold works *with* the grain of the design |
| SD-card loading | Lazy, pull-based; DX7 `.syx` browser is a near-exact template | **Medium** — clean template, mostly boilerplate to mirror |
| Display (OLED vs 7-seg) | Runtime polymorphism via `display->`; pad grid is display-agnostic | **High** for pad-centric features, **lower** for list/text features |

**Headline:** Two of the three features are substantially pre-built. The work is largely
*connecting existing subsystems* rather than inventing new ones.

---

## 1. Chord System

### Where chord definitions live
- `src/deluge/gui/ui/keyboard/chords.h` (lines 26–164) and `chords.cpp` (lines 129–311).
- `constexpr int8_t kUniqueChords = 33;` (`chords.h:28`)
- `constexpr int8_t kUniqueVoicings = 4;` (`chords.h:27`)
- `constexpr int8_t kMaxChordKeyboardSize = 7;` (`chords.h:26`) — max notes per voicing.

### How chords are represented
- **Intervals, not absolute notes and not scale degrees.** A chord is a set of semitone
  offsets from a root.
  - `struct Voicing` (`chords.h:79–82`): array of 7 `int8_t` offsets; unused slots = `NONE` (`INT8_MAX`).
  - `struct Chord` (`chords.h:85–89`): name, a `NoteSet intervalSet`, and 4 `Voicing`s.
  - Interval constants (`chords.h:46–76`): `ROOT=0, MIN3=3, MAJ3=4, P5=7, MIN7=10, MAJ7=11`, …, up to `MAJ14`.
- This answers the brief's question directly: **chords are interval objects**, and a
  reusable chord object (`Chord` + `Voicing` + `ChordList`) already exists.

### The chord manager
- `class ChordList` (`chords.h:139–161`) holds all 33 chords, plus:
  - `voicingOffset[kUniqueChords]` — which of the 4 voicings is currently selected per chord.
  - `chordRowOffset` (`chords.h:157`) — pagination state for browsing.
  - `getChordVoicing(int8_t chordNo)` (`chords.cpp:62–94`) — returns the selected voicing.

### Chord memory (capture/recall slots)
- `ChordMemColumn` (`gui/ui/keyboard/column_controls/chord_mem.{h,cpp}`):
  - `chordMem[8][kMaxNotesChordMem]` with `kMaxNotesChordMem = 10` (`chord_mem.h:25`) —
    8 slots, each up to 10 **absolute MIDI pitches** (note: pitches, not intervals).
  - `handlePad()` (`chord_mem.cpp:45–69`): press = activate slot, release = capture current
    notes (or clear with shift).
  - `writeToFile()` / `readFromFile()` (`chord_mem.cpp:72–141`) — already serializes.
- Mirrored at song level: `Song::chordMem[kDisplayHeight][MAX_NOTES_CHORD_MEM]`
  (`song.h:435–436`), driven by `SongChordMemColumn`.

### How scales/chord relationships are represented
- Chord keyboard derives chord *quality* from the active scale mode at a given degree:
  `KeyboardLayoutChord::evaluatePadsColumn()` (`chord_keyboard.cpp:85–120`) computes
  `root = getRootNote() + state.noteOffset + steps`, looks up quality via the scale,
  selects a `Chord`, takes a `Voicing`, and emits notes.

### Ownership
- Static chord table lives in SDRAM (`chords.cpp:55+`). The data is global/read-only;
  per-clip state (selected voicing, scroll offset) lives in the layout/clip state, not the table.

---

## 2. Piano Roll System

### Where note rows are rendered
- `InstrumentClipView` — `gui/views/instrument_clip_view.{h,cpp}`.
- **The render loop is centralized** in `performActualRender()` (`instrument_clip_view.cpp:7086`),
  reached via `renderMainPads()` (`:7065`). Core loop at lines 7095–7133:
  iterate `yDisplay` from 0..`kDisplayHeight`, map each to a `NoteRow`, render or paint black.
- Colour state is rebuilt in `recalculateColours()` (`:4182`), which loops the same range.

### How the screen-row → pitch/NoteRow mapping works
- **Kit clips:** linear. `getNoteRowOnScreen()` (`instrument_clip.cpp:967`) uses
  `i = yDisplay + yScroll` indexed straight into the `noteRows` vector.
- **Synth/melodic clips:** sparse. `getYNoteFromYDisplay()` (`instrument_clip.cpp:3701`)
  converts `yDisplay + yScroll` → MIDI note (scale-aware), then
  `getNoteRowForYNote()` (`:998`) **binary-searches** the `noteRows` vector and returns
  `nullptr` if that pitch has no row.

### Is there already a row-visibility / filtered-row abstraction?
- **Partial.** Synth clips are *already sparse* — not all 128 pitches have a `NoteRow`;
  missing pitches render black. There is **no existing "hide empty rows" / fold concept**,
  but the renderer already tolerates a non-contiguous row set.
- "Does this row have notes?" already exists: `NoteRow::hasNoNotes()` (`note_row.cpp:3178`),
  used in 11 places. Enumerating rows-with-notes is a simple loop over `noteRows`.

### Can rows be filtered without touching note data?
- **Yes.** Filtering is a *display* concern in `performActualRender` / `recalculateColours`;
  the underlying `noteRows` / `NoteVector notes` are untouched. This is the cleanest
  property for Fold Mode (see `FOLD_MODE_FEASIBILITY.md`).

### Scroll state
- `InstrumentClip::yScroll` (`instrument_clip.h:107`), persisted to song XML (`instrument_clip.cpp:2300`).
- Bounds enforced in `scrollVertical_limit()` (`instrument_clip_view.cpp:4284`),
  currently keyed to `clip->getNumNoteRows()`.

### The note→clip write path (shared by all note entry)
- `InstrumentClip::getOrCreateNoteRowForYNote()` (`instrument_clip.h:167`)
- `InstrumentClip::recordNoteOn()` (`instrument_clip.h:95`, impl `instrument_clip.cpp:4390`)
- Live example from keyboard input (`keyboard_screen.cpp:342–353`) — the exact two-call pattern
  any "place chord" feature would reuse, once per voicing note.

---

## 3. UI Architecture

### The UI stack
- Base class `UI` (`gui/ui/ui.h:92–177`): virtual `padAction()`, `buttonAction()`,
  `renderMainPads()`, `renderSidebar()`, `renderOLED()`.
- Navigation hierarchy in `gui/ui/ui.cpp:80–240`: `uiNavigationHierarchy[]` + `numUIsOpen`.
  - `getCurrentUI()` (`:134`), `getRootUI()` (`:143`), `changeRootUI()` (`:101`),
    `openUI()` (`:227`, push), `closeUI()` (`:175`, pop).
- `currentUIMode` (`ui.h:30`) is a separate **bitmask** of transient modes (zoom, scroll,
  midi-learn, button-held states) — orthogonal to the UI stack.

### Major views
| View | Base | File | Role |
|---|---|---|---|
| `InstrumentClipView` | `ClipView` + `InstrumentClipMinder` | `gui/views/instrument_clip_view.h:71` | Piano roll editor |
| `AudioClipView` | `ClipView` + `ClipMinder` | `gui/views/audio_clip_view.h:27` | Waveform editor |
| `ArrangerView` | `TimelineView` | `gui/views/arranger_view.h:37` | Song arranger |
| `KeyboardScreen` | `RootUI` + `InstrumentClipMinder` | `gui/ui/keyboard/keyboard_screen.h:35` | Keyboard input UI |
| `Browser` (+ subclasses) | `QwertyUI` | `gui/ui/browser/browser.h:64` | File browsing |
| `SoundEditor` | `UI` | `gui/ui/sound_editor.h` | Param menu system |
| `ContextMenu` | `UI` | `gui/context_menu/context_menu.h:27` | Lightweight option menus |

### Clip view vs keyboard view
- **Sibling root UIs over the same clip** (both are `InstrumentClipMinder`s), not parent/child.
- Toggled by the hardware `KEYBOARD` button via `changeRootUI()`:
  clip→keyboard at `instrument_clip_view.cpp:315–322`; keyboard→clip at
  `keyboard_screen.cpp:485–492`.

### Keyboard layouts (relevant to Chord Palette)
- A first-class layout system registered in `keyboard_screen.cpp:67–76`:
  Isomorphic, InKey, Piano, **Chord**, **ChordLibrary**, Drums, Norns.
- Enum `KeyboardLayoutType` (`definitions_cxx.hpp:1035–1043`) — **`KeyboardLayoutTypeChordLibrary` already exists.**
- Base `KeyboardLayout` (`layout.h:42–130`): `evaluatePads()`, `handleVerticalEncoder()`,
  `handleHorizontalEncoder()`, `precalculate()`, `renderPads()`, `renderSidebarPads()`.
- Current layout stored on the clip: `clip->keyboardState.currentLayout`.
- Switched with the **select encoder** via `selectLayout()` (`keyboard_screen.cpp:649–717`).
- `KeyboardLayoutChordLibrary` (`gui/ui/keyboard/layout/chord_library.h:31–59`): paginated
  vertical list of all 33 chords, horizontal root selection, plays chord on pad press.

### Idiomatic extension points (cheapest → most expensive)
1. **New keyboard layout** — register in `layout_list`, implement the base methods. *Medium.*
2. **Column control** — sidebar widget (`column_controls/control_column.h:32`). *Low–medium.*
3. **ContextMenu** — stacked option picker. *Low.*
4. **New root UI** — full `RootUI` subclass + button plumbing. *High.*

### Button/shortcut routing
- Dispatched to `getCurrentUI()->buttonAction(Button, on, inCardRoutine)`.
- SHIFT combos checked inline via `Buttons::isShiftButtonPressed()`
  (e.g. `keyboard_screen.cpp:431–434`). New shortcuts add a branch in the relevant
  `buttonAction()`; held-button states get a `currentUIMode` flag.

---

## 4. File System (SD card)

### FatFS wrapper
- `src/fatfs/fatfs.hpp`. `FatFS::Directory` (lines 127–156): `open()`, `read()`,
  `read_and_get_filepointer()`, `rewind()`. `FatFS::File` (lines 51–123): `open/read/write/lseek`.

### Directory enumeration pattern
- `Browser::readFileItemsForFolder()` (`gui/ui/browser/browser.cpp:266–396`): the canonical
  scan loop — read entry, skip dot-files, check `AM_DIR`, filter by extension, build `FileItem`.
- Entry path: `arrivedInNewFolder()` (`:591`) → `readFileItemsFromFolderAndMemory()` (`:520`)
  → `readFileItemsForFolder()` (`:266`).

### Folder constants
- `getInstrumentFolder(OutputType)` (`util/functions.cpp:1365–1378`) returns
  `"SYNTHS"/"KITS"/"MIDI"/"SONGS"`. **No global registry** — each browser names its own folder.

### Closest template for a user-data library: the DX7 `.syx` loader
- `DxSyxBrowser` (`gui/ui/browser/dx_browser.cpp:27–63`): `allowedFileExtensions = {"SYX", NULL}`
  (`:33`), `currentDir.set("DX7")` (`:52`), `arrivedInNewFolder(...)` (`:55`).
- `DX7Cartridge::load()` (`storage/DX7Cartridge.h:87`) — validates header + checksum (binary).
- This is the single-top-level-folder, extension-filtered, lazy pattern a `CHORD_PACKS/`
  loader would mirror — except chord packs would be **XML**, using the deserializer below.

### XML read/write
- `XMLDeserializer` / `XMLSerializer` (`storage/storage_manager.h:212–262` / `155–183`).
- Read-tag loop pattern (canonical example `Kit::readFromFile`, `kit.cpp:263–330`):
  `while (*(tagName = reader.readNextTagOrAttributeName())) { if (!strcmp(tagName,"x")) … }`,
  with `reader.match()/exitTag()/readTagOrAttributeValueInt()`.
- `StorageManager::loadInstrumentFromFile()` (`storage_manager.cpp:326–420`) shows the
  open-file → `readFromFile()` → in-memory object flow.

### Loading strategy
- **Lazy / pull-based.** `FileItem` (`storage/file_item.h:24–42`) stores metadata only;
  contents parse on selection. No boot-time global index. A chord-pack browser would scan
  `CHORD_PACKS/` on open and parse a pack on selection.

---

## 5. Display Architecture (OLED vs 7-segment) — **critical for the numeric-display hardware**

### The abstraction
- Abstract base `deluge::hid::Display` (`hid/display/display.h:43–111`), `enum DisplayType { OLED, SEVENSEG }` (`:41`).
- Concrete: `OLED : public Display` (`hid/display/oled.h:44`), `SevenSegment : public Display`
  (`hid/display/seven_segment.h:32`).
- Global `extern deluge::hid::Display* display;` (`display.h:115`); detect flag
  `have_oled_screen` (`display.h:120`); runtime swap `swapDisplayType()` (`display.cpp:86–101`).

### Branching model
- **Pure runtime polymorphism** — no `#if HAVE_OLED` gating of behavior. Code calls `display->…`
  and branches with `display->haveOLED()` / `display->have7SEG()` (`display.h:106–107`).
- Representative split: `sample_marker_editor.cpp:337–342` — `if (display->haveOLED()) renderUIsForOled(); else displayText();`.
- The split is **mid/high-level** (a render method picks a path), not scattered per-statement.

### 7-segment constraints
- `kNumericDisplayLength = 4` (`definitions_cxx.hpp:471`). Layer-based compositor
  (`numeric_layer/…`): `setText()`, `setTextAsNumber()`, `setTextAsSlot()`,
  `setScrollingText()` (256-byte scroll buffer), `displayPopup()` (auto-truncates to 4 chars).
- Menu lines available: **3 on OLED** (`oled.h:162`) vs **1 on 7-seg** (`seven_segment.h:68`).

### Pad grid is display-agnostic
- `extern RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]` (`hid/led/pad_leds.h:35–39`)
  — fixed dimensions, no display branching. `renderMainPads()` signature
  (`ui.h:105–107`) takes no display-type parameter. **Anything that lives in the pad grid is
  identical on both hardware families.**

### Dual-display cost verdict
- **Pad-centric feature (Fold Mode):** ~10% extra work — pads identical; only the text
  *feedback* needs a one-line `haveOLED()` branch (OLED label vs a 4-char tag like `FOLD`).
- **List/text feature (Chord Palette):** higher — the chord *name/detail* display diverges
  (OLED multi-line vs 4-char scrolling). The pad-grid selection layer is still shared.

---

## 6. Cross-cutting risks & extension points

### Risks
- **Workflow spans two root UIs.** Browsing/selecting a chord is natural in **keyboard view**
  (where `ChordLibrary` lives), but placement + fold-while-placing is a **clip view / piano-roll**
  experience. Reconciling "browse chord → move piano-roll cursor → place → repeat" across the
  keyboard/clip boundary is the single biggest open design question. (Detailed in `CHORD_PALETTE_FEASIBILITY.md`.)
- **Kit vs synth row models differ** (linear vs sparse). Fold logic must handle both, or be
  scoped to melodic clips first.
- **Voicing/position simultaneity.** Placing a chord must write all voicing notes at the *same*
  quantized position (`instrument_clip.cpp:4409–4435` handles quantization).
- **Upstream acceptance.** Goal is eventual upstream contribution — favor reusing existing
  abstractions (layouts, column controls, deserializer) over new subsystems, and keep each
  feature independently shippable.

### Extension points identified
- Fold: `performActualRender()`, `recalculateColours()`, `scrollVertical_limit()` + a
  screen-row→Nth-non-empty-row helper; a `foldMode` clip flag beside `inScaleMode`.
- Chord palette: extend `KeyboardLayoutChordLibrary`, or a clip-view placement path reusing
  `getOrCreateNoteRowForYNote()` + `recordNoteOn()`.
- Chord packs: a `CHORD_PACKS/` browser modeled on `DxSyxBrowser` + a `ChordPack::readFromFile()`
  modeled on `Kit::readFromFile()`.

---

*Phase 2 (`FOLD_MODE_FEASIBILITY.md`) and Phase 3 (`CHORD_PALETTE_FEASIBILITY.md`) build on this map.*
