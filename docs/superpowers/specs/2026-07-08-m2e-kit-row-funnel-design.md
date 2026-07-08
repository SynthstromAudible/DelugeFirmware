# M2e Kit-row structure — faithful funnel (no Consequence) — design

**Goal:** Reify the kit-row add/remove structural mutations as `deluge::edits::Edit` Commands, addressed by a new
`KitRowAddress`, **without adding any undo Consequence**. Behaviour-preserving: the ops stay non-undoable exactly as
today (the `deleteAllLogs()` calls remain). This is the clip→kit continuation of the M2 funnel.

## Strategic constraint (why no Consequence)

The north-star retires the Action/Consequence system in favour of a command pattern + undo tree
([[project-model-north-star]]). M1–M2d only ever *reused* the Consequences that already existed (a behaviour-
preserving bridge — zero net investment in the doomed subsystem). Kit-row add/remove is **not undoable today** (every
gesture calls `actionLogger.deleteAllLogs()`; there is no `ConsequenceNoteRowExistence`). Creating one would be net-new
surface area on the exact subsystem we intend to delete — against the grain of the goal. So M2e reifies the mutations
as Commands (funnel) and **defers undo to the future command/undo-tree substrate** (an M3+ question). Kit-row ops go
from "not a command, not undoable" → "a command, not-yet-undoable": strictly forward, nothing wasted.

## Findings (from exploration)

- Kit-row **add**: `InstrumentClipView::createDrumForAuditionedNoteRow(DrumType)` (instrument_clip_view.cpp:957). Line
  973 `deleteAllLogs()` (no undo). SOUND drums defer into `loadInstrumentPresetUI` **asynchronously** (line 1019) —
  out of scope. The **synchronous** path is MIDI/GATE (lines 1023–1041): `StorageManager::createNewDrum` +
  `kit->addDrum` + `noteRow->setDrum` + `kit->beenEdited()`.
- Kit-row **remove**: `InstrumentClipView::deleteDrum(SoundDrum*)` (instrument_clip_view.cpp:5567) —
  `kit->removeDrum`, reassigns the row to an unassigned drum / conditionally `deleteNoteRow`, retires automation stores
  (`backed_params.removeOwner`), then destructs + deallocs the drum. A second remove gesture (the delete-row pad,
  600–656) is out of scope for slice 1.
- Addressing already exists: `Drum::serialId()/sessionId()` (drum.h:92-101) and `Kit`(Output)`::serialId()/sessionId()`
  (output.h:111-116). No new id scaffolding needed.

## Address: `KitRowAddress`

`src/deluge-project/kit_row_address.h`:
```cpp
struct KitRowAddress {
  uint32_t kit_id{0};     ///< Kit(Output)::serialId() when saved, else sessionId().
  bool kit_is_session{false};
  uint32_t drum_id{0};    ///< Drum::serialId() when saved, else sessionId(). 0 = unassigned (a to-be-created row).
  bool drum_is_session{false};
  auto operator<=>(const KitRowAddress&) const = default;
};
```
`kitRowAddressFor(const Kit&, const Drum&)` → serial-else-session for both, else nullopt (mirrors clipAddressFor).
`RemoveKitRow` resolves an existing row; `AddKitRow`'s drum is new so its address carries `drum_id=0` (the id is
minted by the engine on create — a funnel-phase gap M3 closes).

## Edits

Both are funnel-phase engine-borrow Commands with **no Consequence** and no undo.

### `AddKitRow` — `src/deluge/model/edits/add_kit_row.{h,cpp}`

Reifies the synchronous (MIDI/GATE) branch. Ctor `(Kit* kit, NoteRow* noteRow, DrumType drumType, ModelStackWithNoteRow* engineStack)`.
`apply`:
```cpp
Drum* drum = StorageManager::createNewDrum(drumType_);
if (drum == nullptr) return Error::INSUFFICIENT_RAM;
ParamManager paramManager;
kit_->addDrum(drum);
noteRow_->setDrum(drum, kit_, engineStack_, nullptr, &paramManager);
kit_->beenEdited();
drum_ = drum;  // exposed via drum() for the view's drawDrumName
return Error::NONE;
```
`Drum* drum() const` accessor (the view needs the new drum for `drawDrumName`). The view keeps the mode logic,
note-row acquisition, the SOUND async path, `deleteAllLogs`, and rendering.

### `RemoveKitRow` — `src/deluge/model/edits/remove_kit_row.{h,cpp}`

Reifies `deleteDrum` faithfully (whole body, since there is no undo the dealloc/reassignment/automation-retirement all
stay verbatim). Ctor `(std::optional<KitRowAddress> address, SoundDrum* drum, ModelStackWithTimelineCounter* engineStack)`.
`apply` reproduces deleteDrum: `kit->removeDrum` → row reassignment / conditional `deleteNoteRow` → `param_backup`
+ `backed_params.removeOwner` → destruct + dealloc → `mustUpdateReverbParamsBeforeNextRender`. The view keeps
`deleteAllLogs`.

## Funnel sites

- `instrument_clip_view.cpp:1023–1041` (non-SOUND add branch) → `AddKitRow` + `drawDrumName(addEdit.drum())`.
- `instrument_clip_view.cpp:5567–5611` (`deleteDrum` body) → `RemoveKitRow`.

## Testing — apply-then-assert-existence (no undo)

The undo harness doesn't fit (nothing to undo). New `edit_undo_redo` targets do apply + assert:
- `kitAddTarget`: on a kit clip, resolve a note-row, `AddKitRow{kit, noteRow, DrumType::GATE, ms}`; assert
  `kit->getDrumIndex(edit.drum()) >= 0` and `noteRow->drum == edit.drum()`. (GATE = no preset/sample, synchronous.)
- `kitRemoveTarget`: on a kit clip with ≥2 SOUND drums, capture drum count, `RemoveKitRow{addr, drum, ms}`; assert the
  kit's drum count decreased. (No revert — non-undoable.)
These mutate the loaded song destructively; fine for one-shot (song discarded after). Skip songs without a kit clip.

## Gates

edit_undo_redo OK (+ kit targets), edit_test OK, golden songs OK=15, param_record OK=3, firmware clean. Catalog kit/drum
row → **landed (funnel; non-undoable by design — undo deferred to the command/undo-tree substrate)**.

## Out of scope (later slices / M3)

Async SOUND-drum add; the drum-replace sub-mode; the delete-row-pad gesture; **any undo** (arrives with the command/
undo-tree substrate, not a Consequence); M3 addressing gap for newly-created drums.
