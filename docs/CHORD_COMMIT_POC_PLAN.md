# Plan — Chord Commit Proof-of-Concept (the bridge test)

> **Status:** Phase 4 plan. No code committed. This validates ONE thing:
> *existing chord object + existing clip write path = a placed chord.*
> **Depends on:** `ARCHITECTURE_MAP.md`, `CHORD_PALETTE_FEASIBILITY.md`.
> **Recon source:** three deep-reads of the live `community` branch (write path, ChordLibrary
> state, position/undo/display). All `file:line` refs are from that read.

---

## 0. Answer up front

**Can the existing Chord Library write chords into the clip safely? — Yes.**

The smallest safe proof is **Option 1 (Commit at Playhead)**, triggered from within
`KeyboardLayoutChordLibrary`, reusing the *exact* write loop that `KeyboardScreen` already runs
when recording keyboard input. No new chord model, no new file format, no new UI surface.

**One correction to earlier docs** (important, carried into the map): there are **two** clip
write paths, not one —
- `recordNoteOn()` → writes at the **live playhead**, quantized. (What keyboard recording uses.)
- `NoteRow::attemptNoteAdd(pos, …)` → writes at an **explicit grid position**. (What the
  piano-roll step pad / `editPadAction` uses.)

`recordNoteOn()` has **no caller-supplied position** — only a `forcePos0` boolean. So "tap a step
to place" (the eventual palette dream) needs `attemptNoteAdd`, while "commit at playhead" (this
PoC) needs `recordNoteOn`. Picking the playhead model is what keeps the proof tiny.

---

## 1. Exact source files involved

| File | Role in the PoC |
|---|---|
| `src/deluge/gui/ui/keyboard/layout/chord_library.{h,cpp}` | Holds the selection (chord/root/voicing) and the audition loop to mirror |
| `src/deluge/gui/ui/keyboard/keyboard_screen.{h,cpp}` | Owns `buttonAction`/`selectEncoderAction`, ModelStack setup, and the **existing write loop** (`:342–353`) |
| `src/deluge/gui/ui/keyboard/chords.{h,cpp}` | `Chord`/`Voicing`/`ChordList` + `getChordVoicing()` |
| `src/deluge/gui/ui/keyboard/state_data.h` | `KeyboardStateChordLibrary` (`:51–57`) — where selection lives |
| `src/deluge/model/clip/instrument_clip.{h,cpp}` | `getOrCreateNoteRowForYNote()` (`h:167`/`cpp:1076`), `recordNoteOn()` (`h:95`/`cpp:4390`) |
| `src/deluge/model/action/action_logger.*`, `action.*` | Undo grouping (`getNewAction`) |
| `src/deluge/hid/display/display.h` | `displayPopup()` (`:56`) for dual-display confirmation |

No new files required for the PoC.

## 2. Existing functions/classes to reuse (everything)

- **Selection:** `KeyboardStateChordLibrary` (`state_data.h:51–57`) + `ChordList`
  (`chords.h:139–161`). Reached from the layout via `getState().chordLibrary` (`layout.h:123`).
- **Voicing resolution:** `ChordList::getChordVoicing(chordNo)` (`chords.cpp:62–94`) — **already
  respects the per-chord selected voicing** `voicingOffset[chordNo]`. *(Correction: the earlier
  worry that audition "hardcodes voicings[0]" was wrong — the audition path at
  `chord_library.cpp:45` already reads the selected voicing.)*
- **Notes-to-MIDI:** the audition loop at `chord_library.cpp:48–54` already turns voicing offsets
  into absolute notes: `noteFromCoords(pressed.x) + offset`, where the root base is
  `noteOffset + x`.
- **The write loop already exists:** `KeyboardScreen::updateActiveNotes()` (`keyboard_screen.cpp:200–378`)
  contains, in its record branch (`:342–353`), exactly:
  `getOrCreateNoteRowForYNote(note, modelStack, action) → recordNoteOn(mswNR, velocity)`.
  **The PoC re-runs this loop on demand instead of only while record-armed.**

## 3. Proposed interaction model (Option 1 — Commit at Playhead)

1. User is in keyboard view, ChordLibrary layout, browsing chords.
2. User **holds a chord pad** (the chord auditions as today — notes populate `currentNotesState`).
3. User fires a **commit gesture** (see §10 for the gesture; candidate: SHIFT + select-encoder press).
4. The PoC writes every currently-active note in `currentNotesState` into the current clip at the
   live playhead, quantized, as a single undoable action.
5. A 4-char popup confirms (`CHRD`).

This deliberately reuses the auditioned `NotesState` as the source of truth, so the chord/root/
voicing the user *hears* is exactly what gets written — no recomputation, no drift.

## 4. How chord root is determined

Already established by the layout: the pressed pad column gives the root via
`noteFromCoords(pressed.x) = getState().chordLibrary.noteOffset + x` (`chord_library.h:53`), and
`noteOffset` is seeded from the song key (`getRootNote()`, `layout.h:87`). **The PoC reads the
root only implicitly** — it consumes the already-voiced notes in `currentNotesState`, which were
built from this root. Nothing new to compute.

## 5. How selected voicing is determined

Also already established: `ChordList::getChordVoicing(chordNo)` (`chords.cpp:62–94`) reads
`voicingOffset[chordNo]`, which the user adjusts via SHIFT-less horizontal-encoder-while-pad-held
(`chord_library.cpp:78–88`). Because the PoC writes `currentNotesState`, the **currently selected
voicing is captured for free** — whatever is sounding is what commits.

## 6. How placement position is determined

`recordNoteOn()` reads `modelStack->getLivePos()` (`model_stack.cpp:178`), which resolves to
`Clip::getLivePos()` (`clip.cpp:149`) = `lastProcessedPos + ticksSinceLastSwungTick`.
- **Playing:** the moving playhead position.
- **Stopped:** equals `lastProcessedPos` (the ticks term is 0) — i.e. wherever the playhead last
  rested (commonly 0). See §10 caveat.

`KeyboardScreen` can build the required `ModelStackWithNoteRow` the same way its record branch
already does (`keyboard_screen.cpp:342`), so no new position plumbing.

## 7. How all chord notes land at the SAME quantized position

`recordNoteOn()` quantizes `getLivePos()` deterministically (`instrument_clip.cpp:4409–4435`).
Because all N notes are written **in one tight loop with no playback advance between them**, every
call reads the same `unquantizedPos` and therefore computes the **same `quantizedPos`** → the chord
is vertically aligned. (Belt-and-suspenders alternative if any jitter appears: stop-time commit, or
snapshot one `quantizedPos` and use the `attemptNoteAdd(pos,…)` path with that constant — but the
loop approach is the minimal proof.)

Illustrative call sequence (plan, **not** committed code):

```
Action* action = actionLogger.getNewAction(RECORD, ActionAddition::ALLOWED); // group → 1 undo
if (!action) return;                          // no valid UI context → skip
for (each active note n in currentNotesState) {
    auto* mswNR = clip->getOrCreateNoteRowForYNote(n.note, modelStack, action);
    if (mswNR->getNoteRowAllowNull())
        clip->recordNoteOn(mswNR, n.velocity); // reuses the same open RECORD action internally
}
display->displayPopup("CHRD", 3);
```

## 8. Undo/redo & edit-history impact

- `recordNoteOn()` **obtains its own** `Action` via `actionLogger.getNewAction(RECORD,
  ActionAddition::ALLOWED)` (`instrument_clip.cpp:4523`) and snapshots the note array
  (`recordNoteArrayChangeIfNotAlreadySnapshotted`, `:4525`).
- With `ActionAddition::ALLOWED`, consecutive calls in the same loop **coalesce into one Action**,
  so the whole chord is **a single undo step** — exactly the desired behavior.
- Grab the action once before the loop and pass it to `getOrCreateNoteRowForYNote` so any
  scale-alteration consequence is captured in the *same* undo step.
- **Do not** close the action between notes, and **do not** create per-note actions (that would make
  each note independently undoable and break the chord). `recordNoteOn` ignores any `Action*` you
  might try to pass — it always uses the logger's open one; that's fine and expected.

## 9. OLED vs 7-segment feedback

Single call handles both: `display->displayPopup("CHRD", 3)` (`display.h:56`) — the 7-segment
driver truncates to 4 chars, OLED shows the full string. (`popupTextTemporary` is the non-flashing
variant.) For a richer OLED line later, pass an `l10n` string; the 4-char fallback still applies on
7-seg. **No `haveOLED()` branch is needed for the PoC** — the popup API abstracts it. This keeps
the proof fully working on the numeric-display hardware.

## 10. Risks & edge cases

- **Position when stopped (the main UX caveat, not a blocker):** with playback stopped,
  "playhead" = `lastProcessedPos`, often 0. So a stopped commit may stack chords at the clip start.
  *Acceptable for a bridge proof* (we're proving notes land + undo works, not ergonomics). The
  real palette will use explicit step placement (`attemptNoteAdd`) — out of PoC scope.
- **Gesture wiring:** keyboard *layouts* have **no `buttonAction` hook** and no direct ModelStack/
  playhead access — buttons are handled one level up in `KeyboardScreen`
  (`keyboard_screen.cpp:408–611`). So the commit trigger must be added in `KeyboardScreen`, gated on
  `currentLayout == KeyboardLayoutTypeChordLibrary`. Cleanest free gesture: **SHIFT + select-encoder
  press** in `selectEncoderAction()` (`:721–756`), which this layout does not consume. (SAVE-during-
  audition is a fallback.)
- **Empty selection:** if no chord pad is held, `currentNotesState` is empty → the loop is a no-op;
  guard with a "nothing to commit" popup.
- **Scale alteration:** `getOrCreateNoteRowForYNote` can force a scale change if a note is out of
  the song scale (`instrument_clip.cpp` scale path). Passing the action keeps it undoable; for the
  PoC this is acceptable, but worth a log note since it mutates song state.
- **Clip length / wrap:** committing past clip end follows the same rules as live recording (same
  call), so no new behavior — but verify on a short clip.
- **Reachability of the other options** (see below) — confirms why Option 1 wins.

---

## Option evaluation (per the directive)

| Option | Position source | Reachable from ChordLibrary? | Cost | Verdict |
|---|---|---|---|---|
| **1 — Commit at Playhead** | `getLivePos()` via `recordNoteOn` | **Yes** — same view, reuses existing write loop | **Smallest** | ✅ **Chosen** |
| 2 — Commit at clip-view cursor | `getPosFromSquare(lastSelectedNoteXDisplay)` + `attemptNoteAdd` | **No** — cursor/grid live in `InstrumentClipView`, a *different root UI*; ChordLibrary has no step grid | Medium (cross-view) | Future palette, not this PoC |
| 3 — Capture-to-pending | either, applied later in clip view | Partial — needs a staged member + cross-view handoff | Higher | Closer to final UX; defer |

**Why 2 is not the PoC** (correcting the instinct from earlier notes): the chord browser lives in
**keyboard view**, which has no piano-roll cursor or step grid. Option 2's position source
(`lastSelectedNoteXDisplay`, `getPosFromSquare`) only exists in `InstrumentClipView`. Bridging to it
means building the cross-view seam — that's the *feature*, not the *proof*. Option 3 (capture-to-
pending, following the `CopiedNoteRow`/`CopiedParamAutomation` staging precedent at
`copied_note_row.h:23` / `instrument_clip_view.h:236`) is the natural path toward the final palette,
but it too is more than a bridge test.

The proof only needs: *real chord selection → real clip → real notes → real undo.* Option 1
delivers exactly that with the least new surface area.

---

## Success criteria for the PoC

1. Holding a chord in ChordLibrary + firing the commit gesture writes that chord's notes into the
   current clip.
2. All notes share one quantized position (vertically aligned).
3. A single undo removes the whole chord.
4. Confirmation popup shows correctly on a 7-segment unit.
5. Zero new chord models, file formats, browsers, or root UIs introduced.

If all five hold, the bridge is proven and the Chord Palette feature can be built on a known-good
write path. If (2) or (3) fail, the blocking issue is position/undo coupling in `recordNoteOn`,
and the fallback is the explicit-position `attemptNoteAdd` path documented above.

*No code is implemented here. Implementation of the chosen PoC is the next step, on your go.*
