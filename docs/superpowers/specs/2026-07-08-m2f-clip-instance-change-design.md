# M2f Slice 1 — EditClipInstance (arrangement clip-instance change) — design

**Goal:** Reify the self-recording `ClipInstance::change` mutation behind the arrangement resize/swap/drag-within-row
gestures as `deluge::edits::EditClipInstance` — a mutation-only Command that reuses the existing
`ConsequenceClipInstanceChange` (recipe-fitting, no new Consequence). Behaviour-preserving.

**Context:** M2f (sample/audio + arrangement) opening slice, funnel phase. Prior owners: clip (M2d), kit/drum (M2e).

## Findings (from exploration)

- `ClipInstance::change(Action* action, Output* output, int32_t newPos, int32_t newLength, Clip* newClip)`
  (clip_instance.h:33 / .cpp:39) is a **self-recording** mutation: given an Action it allocs + records a
  `ConsequenceClipInstanceChange(output, this, newPos, newLength, newClip)` (undoable) then writes `pos/length/clip`.
  Exactly the shape of `Song::setClipLength` / `attemptNoteAdd`.
- It backs 4 user-gesture sites in `arranger_view.cpp`, all `change(action, OUTPUT, POS, LEN, CLIP)`:
  - resize shorten (1598) + lengthen (1626) — `adjustClipInstanceLength`.
  - swap clip via select encoder (2552) — `selectEncoderAction`.
  - drag-within-row (2129) — `putDraggedClipInstanceInNewPosition` (the same-output/order-unchanged branch).
- Instances live in `Output::clipInstances` (`OrderedPosVector<ClipInstance>`, keyed by `pos`).

## Addressing decision — engine borrows, no durable id (user-steered)

`ClipInstance` has **no stable id** — it is keyed by mutable `pos` (every `change()` moves it), and both existing
consequences re-find instances by `pos`. A durable `ClipInstanceAddress` would require minting/serializing a new id —
DOM/serialization work in kate's S5 domain, and the north-star's *M3* "ids + resolve-at-use" concern. The funnel does
not need it: `EditClipInstance` rides the engine `ClipInstance*`/`Output*` borrow, exactly like `SetClipLength` rides
the `Clip*`. So **Slice 1 carries engine borrows and no `Address`.** A **clip-instance stable id is flagged as an M3
prerequisite** for this owner, to be designed with kate alongside resolve-at-use (likely a generation/handle scheme,
since pos is not durable). See [[project-model-north-star]].

## Component: `EditClipInstance`

`src/deluge/model/edits/edit_clip_instance.{h,cpp}`

```cpp
class EditClipInstance final : public Edit {
public:
  EditClipInstance(Output* output, ClipInstance* clipInstance, int32_t newPos, int32_t newLength, Clip* newClip,
                   Action* action)
      : output_(output), clipInstance_(clipInstance), newPos_(newPos), newLength_(newLength), newClip_(newClip),
        action_(action) {}
  Error apply(Context& ctx) override;
private:
  Output* output_;
  ClipInstance* clipInstance_; ///< Funnel-phase engine borrow (no durable address yet — M3 prereq).
  int32_t newPos_;
  int32_t newLength_;
  Clip* newClip_;
  Action* action_;
};
```
`apply`: `clipInstance_->change(action_, output_, newPos_, newLength_, newClip_); return Error::NONE;`

Mutation-only (action passed in, like `SetClipLength`). `change()` self-records the `ConsequenceClipInstanceChange`.

## Funnel (4 sites in arranger_view.cpp)

Each `clipInstance->change(action, OUTPUT, POS, LEN, CLIP);` →
```cpp
deluge::edits::Context ctx{*currentSong, currentSong->project};
deluge::edits::EditClipInstance edit{OUTPUT, clipInstance, POS, LEN, CLIP, action};
deluge::edits::apply(ctx, edit);
```
The site keeps its `getNewAction`, the `arrangement.rowEdited(...)` engine notifications, and rendering.

## Testing — `edit_undo_redo` arrangement target

Songs with an arrangement carry `Output::clipInstances`. `arrangementTarget`:
1. Iterate the song's outputs; find one with ≥1 clip instance. Skip (print marker) if none.
2. Pick an instance; capture `length`. Open a `CLIP_INSTANCE_EDIT` Action.
3. `EditClipInstance{output, inst, inst->pos, newLength, inst->clip, action}` with `newLength != length` (keep pos +
   clip fixed so the pos-based consequence re-find is unambiguous).
4. Assert `inst->length == newLength` → `revert(BEFORE)` restores `length` → `revert(AFTER)` re-applies.

Robust invariant: **length change + undo restoration** (pos/clip held fixed). `setCurrentClip` not needed (the
consequence re-finds via `output->clipInstances`, not `getCurrentClip`).

## Gates

edit_undo_redo OK (+ arrangement target), edit_test OK, golden songs OK=15 (arrangement serializes), param_record OK=3,
firmware clean. Catalog `CLIP_INSTANCE_EDIT` → landed (change slice); note existence/time/clear deferred + the
clip-instance-id M3 prereq.

## Out of scope

Clip-instance existence (create/delete), arrangement time-expand/contract, arrangement-clear, cross-output drag
(delete+create), the non-undoable audio-clip sample-ref/transpose descriptors, and any durable clip-instance id (M3).
