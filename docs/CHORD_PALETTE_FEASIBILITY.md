# Feasibility — Chord Palette

> **Status:** Phase 3 analysis. Findings + options only — no implementation.
> **Depends on:** `ARCHITECTURE_MAP.md` (§1 Chord System, §3 UI, §5 Display).
> **Not:** AI composition, auto-generation, or a progression generator. This is purely a
> *faster way to enter harmony* — the musician stays fully in control.

---

## 1. Goal

Treat a chord as a compositional object:

> **Select chord → place chord → continue.** (e.g. Am · place · F · place · C · place · G · place.)

Preserve all existing note-level editing afterward.

## 2. Existing infrastructure to reuse (substantial)

| Capability | Where | Reuse |
|---|---|---|
| 33 chord types × 4 voicings, interval-based, unit-tested | `chords.{h,cpp}` | The whole data layer — no new chord model needed |
| `ChordList` (voicing selection, pagination) | `chords.h:139–161` | Browse/select state |
| **A browse-and-preview chord UI already exists** | `KeyboardLayoutChordLibrary` (`layout/chord_library.h:31–59`) | Vertical list of all 33 chords, horizontal root select, plays on pad press |
| 8-slot chord memory, serialized | `chord_mem.{h,cpp}`, `song.h:435–436` | Optional "favourite/captured chords" |
| Note→clip write path | `getOrCreateNoteRowForYNote()` (`instrument_clip.h:167`) + `recordNoteOn()` (`:95`, impl `:4390`) | "Place chord" = call once per voicing note at the cursor position |
| Quantization | `instrument_clip.cpp:4409–4435` | Snap placed chords to grid |

**The single missing piece** is *placement*: the existing ChordLibrary layout only **auditions**
the chord live (`keyboard_screen.cpp:299–302`); it does not write notes into the clip at a chosen
position.

## 3. The core design tension (the real question)

The desired loop — *browse chord → move the piano-roll cursor → place → repeat, with Fold keeping
the voiced rows visible* — straddles **two sibling root UIs**:

- **Browsing/selecting** a chord is native to **keyboard view** (`KeyboardScreen`), where the
  layout system and `ChordLibrary` already live.
- **Placing at a position** and **seeing it fold** is native to **clip view / piano roll**
  (`InstrumentClipView`).

These are siblings toggled by the `KEYBOARD` button (`instrument_clip_view.cpp:315–322`), over the
same clip. So the design choice is fundamentally: **where does "select" live, and where does
"place" land?** This is the decision to settle before any implementation.

## 4. UI placement options (ranked)

### Option A — Extend `KeyboardLayoutChordLibrary` (lowest cost)
- **Fit:** high for *browse/select*; the UI already exists and is discoverable via the select
  encoder layout cycle (`keyboard_screen.cpp:649–717`).
- **Cost:** low — add a "place into clip" action to the existing layout, reusing the write path.
- **Catch:** placement happens in keyboard view, where there is **no piano-roll cursor and no Fold
  view**. "Move cursor → place → repeat" doesn't map cleanly; you'd place at the playhead or a
  keyboard-defined position, not a spatial piano-roll step. Fold cannot reinforce it here.
- **Best when:** the desired interaction is "audition + commit at playhead," not "spatial placement."

### Option B — Chord placement *in clip view* (best fit for the stated workflow)
- **Fit:** high for the *whole* loop — the cursor, the steps, and Fold all live here.
- **Shape:** a chord-entry sub-mode of `InstrumentClipView` — select the active chord (a small
  selector: a `ContextMenu`, a sidebar **column control**, or a held-button + encoder), then tap a
  step to drop all voicing notes there via `getOrCreateNoteRowForYNote()` + `recordNoteOn()`.
  Fold Mode then collapses the view to exactly the voiced rows.
- **Cost:** medium — needs a lightweight chord selector surfaced inside clip view and a
  tap-step-to-place handler; reuses the chord data + write path wholesale.
- **Catch:** introduces a new interaction mode in the busiest view; must not collide with existing
  pad semantics (use a `currentUIMode` held-state, mirroring scale-button handling).

### Option C — Sidebar column control (`control_column.h:32`)
- **Fit:** medium. A "chord palette column" could hold/select the active chord while you place in
  the main grid. Narrow (sidebar width) — better as the *selector* for Option B than as the whole feature.

### Option D — New root UI (`ChordPaletteUI : RootUI`)
- **Fit:** low. Over-engineered; duplicates clip/keyboard plumbing, fights the existing two-root
  model, and needs new button bindings. Not recommended.

### Recommendation
**Option B is the truest fit for the stated workflow**, with **Option A as a fast first proof**:
extend `ChordLibrary` to commit a chord (validates the chord-data → write-path bridge cheaply),
then build the clip-view placement sub-mode that pairs with Fold for the real composer loop.
Use **Option C** as the in-clip selector if a sidebar control reads better than a menu.

## 5. Data model options

- **No new chord model needed.** Reuse `Chord`/`Voicing`/`ChordList`. "Active chord" = a
  `(chordNo, root, voicingIndex)` selection; placement expands it through `getChordVoicing()`
  into voicing offsets, adds the root, and writes each resulting pitch.
- Voicing choice: `ChordList` already tracks `voicingOffset` per chord (4 voicings) — expose
  cycling (encoder) rather than hardcoding `voicings[0]` as ChordLibrary does today
  (`chord_library.cpp:101`).
- Custom chord packs (separate feature) would extend the *source* of chords, not this placement
  logic — see `ARCHITECTURE_MAP.md` §4 and the DX7-browser template.

## 6. Placement workflow options

1. **Tap-to-place (spatial):** select active chord → tap a piano-roll step → all voicing notes
   land at that quantized position. Best for "select · place · select · place." (Option B.)
2. **Playhead-commit:** in keyboard/ChordLibrary, audition then commit at the live position.
   Cheapest; weaker spatial control. (Option A.)
3. **Capture-then-paste (clipboard):** capture currently-pressed/selected chord into a slot
   (`chord_mem` already does this), then paste onto steps — pairs well with multi-slot recall.

All three reuse the same write path; they differ only in *where the position comes from*.

## 7. Hardware interaction model (OLED + 7-segment)

- The **pad-grid** parts (chord grid in ChordLibrary, tap-to-place steps) are display-agnostic and
  identical on both units.
- The **chord name/voicing readout** is the part that diverges: OLED can show
  name + notes + voicing on multiple lines; the **7-segment shows ~4 chars** (e.g. `AM7`, `F69`)
  via `setText`/scrolling (`seven_segment.h`, 4-char limit at `definitions_cxx.hpp:471`).
  This is the higher-cost display category, but it's confined to the label, not the interaction.
- For the user's 7-segment unit specifically: ensure chord identities read clearly in 4 chars
  (the existing chord short-names like `M7`, `-7`, `69`, `SUS4` already fit this budget).

## 8. Complexity estimate

- **Option A (commit-at-playhead, proof):** small — wire the existing layout's chord to the write
  path. Validates the bridge.
- **Option B (clip-view placement sub-mode):** medium — a selector surface + a tap-step handler +
  `currentUIMode` state + display readout. No new chord model, no new file format.
- **Pairing with Fold:** free once both exist — Fold simply collapses to the placed voicing rows.

## 9. Risks

- **Cross-view coherence** (the §3 tension) — settle "select where / place where" first.
- **Simultaneity** — all voicing notes must share one quantized position (`:4409–4435`).
- **Mode collisions** in clip view — gate behind a held-button/`currentUIMode` like scale mode.
- **7-segment legibility** of chord names — verify the 4-char short-names disambiguate enough.
- **Scope creep toward "generation"** — keep it strictly select-and-place; no progression logic.

## 10. Verdict

**Highly feasible — most of it already exists.** The chord data, voicings, a browse/preview UI, an
8-slot capture store, and the note-write path are all present; the only genuinely new work is
*placement* and the *UI seam* between selecting a chord and dropping it into the piano roll.
Recommended path: **prove the bridge via Option A**, then build the **clip-view placement sub-mode
(Option B)** that pairs with Fold for the real composer loop. Custom chord packs slot in later as a
new *source* of chords without touching this placement logic.

*No code is proposed here; implementation planning follows once placement model and UI seam are chosen.*
