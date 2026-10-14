# Param-Automation Delete/Paste Edits — Design

**Status:** approved design, pre-plan.
**Predecessors:** M1 complete (M1a/b/c/d) — the value-write Edit family. Migration plan `docs/dev/project_model_migration_plan.md`; north-star `docs/dev/project_model_north_star.md`. This is the **param-mutation coda**: the two non-value param mutations M1c deliberately excluded, reified as Edits before the structural M2 funnel.

## Goal

Reify the remaining user param-automation mutations — **automation-delete** and **automation-paste** — as `deluge::edits::Edit` subclasses, funnelling their call sites through `deluge::edits::apply`, behaviour-preservingly and golden-gated. This completes the param-mutation Edit family (the write/command arm of the north-star for the param tier).

## Non-Goals

- **Copy** (`AutoParam::copy`) — a read-only clipboard capture (no project-state change), so NOT an Edit; left as-is.
- Structural (note/clip) mutations — that is M2.
- Deleting any interior log — unlike M1c's `SetParamValue`, these sites have no shared interior logger; each already builds its own `Action` inline, which the Edit takes over. Nothing to shrink.
- The engine kernels (`AutoParam::deleteAutomation` / `paste`) are unchanged — the Edits funnel the existing call sequences.

## Key Facts (verified)

- **Delete** — every site does `Action* a = getNewAction(ActionType::AUTOMATION_DELETE, NOT_ALLOWED)` then `autoParam->deleteAutomation(a, ms)`. `deleteAutomation` records undo via `a->recordParamChangeIfNotAlreadySnapshotted(ms, /*stealData=*/true)` (the before-nodes are stolen into the Consequence, leaving the lane empty). So passing a live `action` IS the undo mechanism.
- **Paste** — the caller (`pasteAutomation`) does `Action* a = getNewAction(ActionType::AUTOMATION_PASTE)`; `if (a) a->recordParamChangeIfNotAlreadySnapshotted(ms, /*stealData=*/false)`; `autoParam->paste(startPos, endPos, scaleFactor, ms, copied, isPatchCable)`. The Consequence is snapshotted (copy) before the mutation.
- **Copy** — `autoParam->copy(startPos, endPos, copied, isPatchCable, ...)` captures nodes into the clipboard; no project mutation.

## Key Decisions (resolved in brainstorming)

1. **Two separate Edit types**, not a unified verb-enum type — distinct domain verbs with distinct payloads (delete carries nothing; paste carries region + clipboard + scale). Matches the north-star's "few meaningful commands" and keeps each `apply` focused.
2. **Include the tempo-delete site** (`song.cpp:2971`, `clearTempoAutomation`) — same `getNewAction(AUTOMATION_DELETE)`+`deleteAutomation` pattern; funnel it identically.
3. **Copy is not reified** — read-only.
4. **Funnel-phase fields** (`engineStack_`, and for paste `copied_`/`isPatchCable_`) are engine/transient details carried for the funnel, documented removable at the M3/M4 authority flip, exactly like `SetParamValue::engineStack_`.

## The Edits

```cpp
namespace deluge::edits {

/// Clear a param's automation (the reification of the AUTOMATION_DELETE call sites). apply opens the
/// AUTOMATION_DELETE Action and delegates to AutoParam::deleteAutomation, which snapshots the stolen
/// before-nodes into that Action as the undo Consequence.
class DeleteParamAutomation final : public Edit {
public:
	DeleteParamAutomation(std::optional<project::ParamAddress> address, ModelStackWithAutoParam* engineStack);
	Error apply(Context& ctx) override;
private:
	std::optional<project::ParamAddress> address_;
	ModelStackWithAutoParam* engineStack_;   // funnel-phase borrow
};

/// Paste clipboard automation into a param region (the reification of pasteAutomation's mutation).
/// apply opens the AUTOMATION_PASTE Action, pre-snapshots the before-state, then delegates to
/// AutoParam::paste. `copied`/`isPatchCable` are funnel-phase engine details.
class PasteParamAutomation final : public Edit {
public:
	PasteParamAutomation(std::optional<project::ParamAddress> address, int32_t startPos, int32_t endPos,
	                     float scaleFactor, CopiedParamAutomation* copied, bool isPatchCable,
	                     ModelStackWithAutoParam* engineStack);
	Error apply(Context& ctx) override;
private:
	std::optional<project::ParamAddress> address_;
	int32_t startPos_, endPos_;
	float scaleFactor_;
	CopiedParamAutomation* copied_;          // funnel-phase clipboard borrow
	bool isPatchCable_;
	ModelStackWithAutoParam* engineStack_;   // funnel-phase borrow
};

} // namespace deluge::edits
```

`DeleteParamAutomation::apply`:
```cpp
Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);
engineStack_->autoParam->deleteAutomation(action, engineStack_);
return Error::NONE;
```

`PasteParamAutomation::apply`:
```cpp
Action* action = actionLogger.getNewAction(ActionType::AUTOMATION_PASTE);
if (action) {
	action->recordParamChangeIfNotAlreadySnapshotted(engineStack_, /*stealData=*/false);
}
engineStack_->autoParam->paste(startPos_, endPos_, scaleFactor_, engineStack_, copied_, isPatchCable_);
return Error::NONE;
```

## Call-Site Transformation (the proven M1c recipe)

Each site: `Context ctx{*currentSong, currentSong->project}` + construct the Edit from `addressFor(*ms)` + the site's args + `apply(ctx, edit)`, replacing the inline `getNewAction`+mutation. Anchors indicative (re-confirmed at plan time):

**Delete (5):**
- `gui/menu_item/automation/automation.cpp:43-45`
- `gui/views/view.cpp:1359` (`modEncoderButtonAction_deleteAutomation` — the `getNewAction` at ~:1357 folds into the Edit)
- `gui/views/automation_view.cpp:1590`, `:2558`
- `model/song/song.cpp:2971` (`clearTempoAutomation`)

**Paste (1):**
- `gui/views/automation/editor_layout/mod_controllable.cpp:884-897` — the `getNewAction`/pre-snapshot/`paste` triple folds into the Edit; the surrounding popup + post-paste refresh stay in the caller.

## Slice Plan

- **Task 1 — `DeleteParamAutomation`**: new `delete_param_automation.{h,cpp}`; unit case in `edit_test`; funnel the 5 delete sites; golden + firmware gate; commit.
- **Task 2 — `PasteParamAutomation`**: new `paste_param_automation.{h,cpp}`; unit case in `edit_test`; funnel the paste site; golden + firmware gate; commit.

## Testing / Verification

- **Unit (`edit_test`)**: a delete case (automate a param → `DeleteParamAutomation` → assert nodes cleared + an `AUTOMATION_DELETE` Action holds the Consequence) and a paste case (seed a `CopiedParamAutomation` → `PasteParamAutomation` → assert nodes written). Reuse the song-less standalone stack; mind the headless ActionLogger crash-safety (no `deleteAllLogs` with consequences present; the `UNPATCHED_TEMPO` no-Action trick where a clean-slate matters).
- **Golden**: `songs_run.py` OK=15, `param_record` OK=3, `edit_undo_redo` OK (all unchanged — serialization/undo untouched), against the corpus.
- **Firmware**: `./dbt build Debug` clean.
- **Milestone**: targeted ARM + on-device smoke (delete an automation lane + undo; copy/paste an automation region + undo) ride the next flash.

## Risks

| Risk | Mitigation |
|---|---|
| Delete's `stealData=true` undo semantics altered | Edit calls `deleteAutomation(action, ms)` verbatim; the steal happens inside it unchanged. |
| Paste's pre-snapshot ordering (`recordParamChangeIfNotAlreadySnapshotted` before `paste`) altered | Edit reproduces caller's exact order; the `if (action)` guard preserved. |
| `isPatchCable` computed differently in the Edit vs caller | Compute `isPatchCable` at the CALL SITE (as today) and pass it into the Edit — do not recompute inside `apply`. |
| Tempo delete has special `shouldRecord`/`recordTempoChange` semantics | `deleteAutomation` path is identical for tempo; the Edit changes only *who calls* `getNewAction`+`deleteAutomation`, not the engine kernel. Golden `songs` covers tempo round-trip. |
| Headless `edit_test` ActionLogger crash (consequence-free audio pump) | Same guards as M1c/M1d: no `deleteAllLogs` with consequences; the delete case opens exactly one Action and is not followed by a freeing test. |
