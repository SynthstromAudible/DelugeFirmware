# M2d PasteNoteRow — design

**Goal:** Reify the per-row paste mutation behind the `NOTES_PASTE` / `PATTERN_PASTE` gestures, closing the last
M2d clip-structure row and thereby M2d.

**Context:** The write/command arm of the project-model north-star (`deluge::edits::Edit`), funnel phase. This is the
final op of M2d (clip-structure). Prior M2d ops: `ShiftClip`, `ClearClip`/`ClearNoteRow`, `MultiplyClip`, `SetSwing`,
`SetClipLength`, `SetTempo`; `CLIP_DELETE`/`NOTEROW_ROTATE`/`NOTE_TAIL_EXTEND` are dead ActionTypes.

## The gesture

`InstrumentClipView::pasteNotes` (instrument_clip_view.cpp:1376–1526, 151 lines) pastes the view's copy buffer
(`firstCopiedNoteRow`, a `CopiedNoteRow` linked list) into the current clip. Anatomy:

- **Setup/guards** — region + `scaleFactor`; the `noScaling` branch calls `lengthenClip`/`shortenClip`
  (**already funnelled → `SetClipLength`**) + `zoomToMax`. View-coupled (`getPosFromSquare`, zoom).
- **Action** — `PATTERN_PASTE` (from-file) or `NOTES_PASTE`.
- **Region clear** — `clip->clearArea(...)` when overwriting.
- **Kit loop** (1437–1467) and **melodic loop** (1470–1500) — iterate the copy buffer, resolve a target row
  (`yScroll` / `getYNoteFromYDisplay` / `getOrCreateNoteRowForYNote` / `getCurrentKit` — all view state), optional
  per-row `clearArea`, then the repeated mutation `thisNoteRow->paste(modelStack, copiedRow, scaleFactor, endPos, action)`.
- **getOut** — `recalculateColours`, render, popups, `closeAction`. All view/UI.

Structurally identical to `NudgeNotes`: a view-owned loop over copied items, each item's engine mutation reifiable,
sharing one Action.

## Decision — per-row `PasteNoteRow` Edit (Approach A)

Reify **only** the repeated mutation `noteRow->paste(...)` at the two loop sites, mirroring the proven per-item
pattern (`NudgeNote`, `AddNote`). Everything else — the loop, copy-buffer iteration, target-row resolution, the shared
Action, `clearArea`, and all display — stays in the view because it is intrinsically view/copy-buffer state.

Rejected: a monolithic `PastePattern` Edit (would reach back into view methods `getYNoteFromYDisplay` /
`getOrCreateNoteRowForYNote` / `getCurrentKit` — awkward, high-risk). Region `clearArea` stays direct (a distinct
"clear" mutation; can get its own edit later if wanted).

## Component: `PasteNoteRow`

`src/deluge/model/edits/paste_note_row.{h,cpp}`

```cpp
class PasteNoteRow final : public Edit {
public:
  PasteNoteRow(std::optional<project::NoteRowAddress> address, CopiedNoteRow* copiedRow, float scaleFactor,
               int32_t screenEndPos, Action* action, ModelStackWithNoteRow* engineStack);
  Error apply(Context& ctx) override;
private:
  std::optional<project::NoteRowAddress> address_;
  CopiedNoteRow* copiedRow_;          ///< Funnel-phase copy-buffer borrow.
  float scaleFactor_;
  int32_t screenEndPos_;
  Action* action_;
  ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
};
```

`apply`: `bool ok = engineStack_->getNoteRow()->paste(engineStack_, copiedRow_, scaleFactor_, screenEndPos_, action_);
return ok ? Error::NONE : Error::INSUFFICIENT_RAM;`

- `CopiedNoteRow` is a model type (`model/note/copied_note_row.h`), forward-declared in the header; `.cpp` includes
  `note_row.h`.
- `NoteRow::paste` self-records its undo via `recordNoteArrayChangeIfNotAlreadySnapshotted` into `action_`, and inserts
  via `attemptNoteAdd` (overlap-safe). In the current impl it always returns `true`; the `Error` return is kept for
  faithfulness (the real RAM failure in the melodic path is `getOrCreateNoteRowForYNote` → null, which stays in the view).
- `action_` is passed in (the view owns the shared PATTERN_PASTE/NOTES_PASTE Action) — same pattern as `SetClipLength`.

## Funnel (2 sites in `pasteNotes`)

Kit (@1462) and melodic (@1495), each replacing:
```cpp
bool success = thisNoteRow->paste(modelStackWithNoteRow, thisCopiedNoteRow, scaleFactor, endPos, action);
if (!success) { goto ramError; }
```
with:
```cpp
deluge::edits::Context pasteCtx{*currentSong, currentSong->project};
deluge::edits::PasteNoteRow pasteEdit{deluge::edits::noteRowAddressFor(*modelStackWithNoteRow), thisCopiedNoteRow,
                                      scaleFactor, endPos, action, modelStackWithNoteRow};
if (deluge::edits::apply(pasteCtx, pasteEdit) != Error::NONE) { goto ramError; }
```
The `goto ramError` flow is preserved. The melodic path's own `getOrCreateNoteRowForYNote` null-check + `goto ramError`
(pre-paste) is untouched.

## Testing — `edit_undo_redo` `pasteTarget`

paste needs a copy buffer, which is view state; the harness builds a minimal `CopiedNoteRow` directly:

1. Find an addressable instrument clip with an **empty** target note-row (or clear one) so `attemptNoteAdd` has free
   space; capture its note count (0).
2. Construct a `CopiedNoteRow` with one synthetic `Note` (pos 0, length 1, some velocity). Set `numNotes=1`,
   `notes` → the Note, `yNote`/`yDisplay` from the target. Guard against its destructor freeing borrowed note memory
   (null out `notes`, or manage lifetime).
3. Open a `NOTES_PASTE` Action; drive `PasteNoteRow` with `scaleFactor=1`, `screenEndPos = loopLength`. The pasted
   `newPos = xScroll + 0`; assert it is `< maxPos` (skip if the loaded song's xScroll ≥ loopLength).
4. Assert the target row gained a note → `revert(BEFORE)` removes it → `revert(AFTER)` re-adds it.
5. `song->setCurrentClip(clip)` for the note-array revert's `getCurrentClip()` resolution (established coverage).

Robust invariant: **note-count change + undo restoration** (not exact positions, which depend on `xScroll`).

## Gates

edit_undo_redo OK=40 (+ pasteTarget), edit_test OK, golden songs OK=15, param_record OK=3, firmware clean. Catalog row
`NOTES_PASTE`/`PATTERN_PASTE` → landed. On-device paste-undo smoke deferred to the M2 hardware gate.

## Out of scope

Region `clearArea` reification (distinct clear mutation); the whole-function view orchestration; M2h Action view-state.
