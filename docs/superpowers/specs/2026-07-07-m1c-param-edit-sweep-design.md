# M1c — Param-Value Edit Sweep + Interior-Log Retirement — Design

**Status:** approved design, pre-plan.
**Predecessor:** M1a/M1b (the `deluge::edits::Edit` substrate + `SetParamValue` pilot), commits `afe1293c6..58753ad30`. Plan: `docs/superpowers/plans/2026-07-07-m1-edit-substrate-pilot.md`.
**Context:** M1c of `docs/dev/project_model_migration_plan.md`; north-star `docs/dev/project_model_north_star.md`.

## Goal

Reify **every genuine user param-value gesture** as a `deluge::edits::SetParamValue` Edit, then **delete the interior undo log** (`auto_param.cpp:318`, inside `AutoParam::setCurrentValueInResponseToUserInput`) once no non-Edit path produces it. Behaviour-preserving throughout; each slice golden-gated exactly as the pilot was.

This completes the migration norm "new/edited persistent-state mutations are Edits" for the param-value tier, and removes the funnel-phase duplication (Edit pre-log **and** interior log, reconciled by dedupe) that the pilot deliberately left in place.

## Non-Goals (own Edits / own work later)

- **Automation-delete** (`AutoParam::deleteAutomation` + direct `getNewAction(ActionType::AUTOMATION_DELETE)` sites): a different mutation shape → its own Edit, after M1c.
- **Copy/paste automation** (`autoParam->paste`/`copy`): its own Edit, later.
- **The recorded-automation stream itself** (the recording branch of `setCurrentValueInResponseToUserInput`, and the RT reconcile path generally): stays on the engine path; formalised at M4 (snapshot boundary + event feed).
- Engine/file-load param writes (`setCurrentValueWithNoReversionOrRecording`, `setCurrentValueBasicForSetup`): not user edits, never in scope.

## Key Decisions (resolved in brainstorming)

1. **Scope = all user param-VALUE writes** across Groups A–F (below), scalar and region. Delete/paste excluded.
2. **RT/input sites are included, not excluded.** MIDI-CC / pitch-bend on learned params and MPE step-editing are funnelled through the same `SetParamValue`. The north-star's line ("recorded automation is not an Edit") sits **downstream** of the call site, inside `setCurrentValueInResponseToUserInput`'s recording branch; the pilot's guard `!(isPlaying && recordingArmed)` already reifies only the **discrete** edit and leaves the recorded stream untouched. So including these sites is behaviour-preserving and honours the principle by construction.
3. **The interior log is retired in M1c.** Because every scalar producer of it becomes an Edit carrying the idempotent pre-log, `auto_param.cpp:318` can be deleted at the end (S6) without dropping any undo snapshot. (At M1c the M4 `emitModelChanged` hook is still a no-op, so no event-rate concern arises yet; that is an M4 design problem, and excluding these sites now would not solve it — only postpone it.)
4. **One clean domain Edit, `SetParamValue`.** The Edit vocabulary is the durable API surface, so it stays a small set of meaningful verbs. `SetParamValue`'s public shape is `{ ParamAddress target; int32_t value; std::optional<Region{pos,length}> region; }`. Engine-only hints (`mayDeleteNodesInLinearRun`) are **not** promoted into that constructor — they ride as a funnel-phase internal, documented removable at the M3/M4 authority flip, exactly like `engineStack_`. No per-call-site Edit subtypes; existing per-drum loops (affect-entire kit) stay loops and emit one Edit per write, coalescing into one Action as they do today.
5. **The one outlier stays off-Edit.** `note_row.cpp:4503` (live-MPE scalar, carries `livePos`/`doMPEMode`, which `setValuePossiblyForRegion` cannot express) is the recorded-stream case. It does not become an Edit. The plan must first *prove* it cannot reach the interior log (it runs only in recording context → the recording branch, which skips line 318); that proof is a **precondition** of the S6 deletion. If the proof fails, it is handled reconcile-side (or the interior log's retirement is re-scoped), never by inventing a new Edit type.

## Call-Site Inventory (from the M1c scoping sweep)

Anchors are indicative (pre-M1c line numbers); the plan re-confirms each.

**Group A — SoundEditor menus (scalar, via `writeCurrentValue`):**
- `gui/menu_item/unpatched_param.cpp:73` (single sound) and `:66` (affect-entire kit loop, per-SoundDrum).
- `gui/menu_item/patched_param/integer.cpp:42`, `:63` (single sound, two branches) and `:57` (affect-entire kit loop).
- `gui/menu_item/patch_cable_strength.cpp:242` (patch-cable amount).

**Group B — Automation view pad / held-pad region edits (region):**
- `gui/views/automation/editor_layout/mod_controllable.cpp:803` (unselected-pad + knob), `:1017`/`:1025` (neighbour-node freeze), `:1036`/`:1037` (single-pad value, called twice), `:1344`/`:1346` (multi-pad interpolated fill, called twice). Note the deliberate double-calls and neighbour-node re-writes — behaviour to preserve exactly.

**Group C — Performance view (region):**
- `gui/views/performance_view.cpp:1428` (`setParameterValue`, `setValuePossiblyForRegion(newValue, ms, modPos, modLength)`). Its own bookkeeping at `:1305` (`logPerformanceViewPress` → `closeAction(...)`, `PerformanceViewPress` consequence) is left intact.

**Group D — MIDI-CC / follow / pitch-bend on learned params (region):**
- `midi_follow.cpp:1075`; `mod_controllable_audio.cpp:955`, `:1045`, `:1115`; `melodic_instrument.cpp:661` (adopts `view.modPos/modLength` only if the held pad matches, else scalar; passes `mayDelete=false`).

**Group E — MPE step-editing held-pad edits (region):**
- `instrument_clip_view.cpp:7923` (`reportNoteOffForMPEEditing`, per-dimension `setValueForRegion`), `note_row.cpp:4493` (`recordPolyphonicExpressionEvent`, step-editing branch, `setValueForRegion`).
- **Outlier, NOT in scope:** `note_row.cpp:4503` (live-play branch, `setCurrentValueInResponseToUserInput` with `livePos`/`doMPEMode`). `note_row.cpp:4451` (`clearMPEUpUntilNextNote`) is a delete/zero-write shape → delete-Edit later.

**Group F — Tempo (scalar):**
- `song.cpp:2900` (`Song::setBPM`, `setCurrentValueInResponseToUserInput(intTempo, ms, shouldLog, -1)`). Tempo early-returns from `recordUnautomatedParamChange` (`shouldRecordUnautomatedParamChange` false for `UNPATCHED_TEMPO`; it has its own `recordTempoChange`), so the pre-log is a natural no-op here — behaviour-preserving. `song.cpp:2912` (`clearTempoAutomation`) is delete-shape → out of scope.

**The interior log** (`ActionLogger::recordUnautomatedParamChange`, `action_logger.cpp:241`) has exactly two callers today: `auto_param.cpp:318` (to retire) and `set_param_value.cpp` (the pilot pre-log). After the sweep, the only remaining scalar producers must all be Edits.

## Architecture / The Recipe

Each in-scope site is transformed the way `View::modEncoderAction_existentParam` was:

```
// before
ms->autoParam->setValuePossiblyForRegion(value, ms, pos, length /*, mayDelete*/);

// after
deluge::edits::Context ctx{*currentSong, currentSong->project};
deluge::edits::SetParamValue edit{deluge::edits::addressFor(*ms), value, pos, length /*, mayDelete*/, ms};
deluge::edits::apply(ctx, edit);
```

- `addressFor(*ms)` supplies the durable id-address (nullopt for never-saved owners → engine-resolved, exactly as today).
- The Edit owns its undo consequence via the pre-log; the interior log dedupes until S6 deletes it.
- Loops (affect-entire kit) wrap the per-iteration write, not the loop — N Edits coalesce into one Action, one undo, as today.
- `SetParamValue` gains an internal `mayDeleteNodesInLinearRun` (default true; Group D/E sites pass false) — a funnel-phase field, not part of the domain constructor's intent.

## Slice Plan

Each slice: funnel one surface, extend `edit_test` with that surface's case, run the golden + firmware gate, commit. Behaviour-preserving per slice.

| Slice | Sites | Shape | Notes |
|---|---|---|---|
| **S1** | Group A menus (incl. affect-entire kit loops) | scalar | Uniform `writeCurrentValue`; single stack-build pattern; establishes the menu recipe + kit-loop coalescence test. |
| **S2** | Group C perf + Group D MIDI-in/follow/pitch-bend | region | Shared `setValuePossiblyForRegion(newValue, ms, modPos, modLength)` shape; introduces the `mayDelete=false` funnel field; perf's own `closeAction` bookkeeping untouched. |
| **S3** | Group B automation-view pad/region edits | region | Highest-quirk: double-calls and neighbour-node re-writes must be reproduced verbatim. |
| **S4** | Group E MPE step-editing held-pad edits | region | `setValueForRegion` sites; per-dimension loops. |
| **S5** | Group F tempo | scalar | Confirms the tempo `shouldRecord` no-op path stays a no-op through the Edit. |
| **S6** | **Retire the interior log** | — | Precondition: prove no non-Edit scalar producer of `recordUnautomatedParamChange` remains (including the `note_row.cpp:4503` outlier). Then delete `auto_param.cpp:318`; the pilot's pre-log becomes the sole producer (its dedupe still guards coalescing/multi-call). |

Ordering rationale: uniform scalars first (S1), then the highest-reuse region shape (S2), then the quirk-heavy (S3) and specialised (S4/S5) sites, then retirement (S6) once the producer set is closed.

## Testing / Verification

- **Per slice:** extend `tests/projection/edit_test.cpp` with a representative case for the surface (mutation applies, correct Action type, coalescence/dedupe where relevant); run the full x86 golden net with the corpus — `songs_run.py` (OK=15) and `param_record` (OK=3) via `DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`; `./dbt build Debug` clean.
- **S6 specifically:** a test asserting that a scalar edit **with `auto_param.cpp:318` gone** still yields exactly one `ConsequenceParamChange` (i.e. the Edit pre-log fully replaces the interior log), plus a check that a bare MIDI-in-style scalar edit still records its undo snapshot.
- **Milestone boundary:** targeted ARM/qemu golden on the kernel-relevant fixtures, then on-device smoke (a knob turn, a menu edit, a MIDI-CC move, an automation-view pad edit, undo of each) on the next flash.

## Risks

| Risk | Mitigation |
|---|---|
| A funnelled site changes behaviour in the recording branch (e.g. `mayDelete` flipped) | Preserve `mayDelete` per-site as the funnel field; golden `param_record` net is the guard (exercises record-during-playback). |
| The `note_row.cpp:4503` outlier can in fact reach the interior log | S6 is gated on an explicit proof; if it fails, retirement is re-scoped, not forced. |
| Automation-view double-call / neighbour-node quirks silently altered | S3 reproduces call structure verbatim; golden nets + an automation-view-shaped `edit_test` case. |
| `addressFor` nullopt path differs from the site's current resolution | `addressFor` is behaviour-identical to `doc()`'s classification (established in M1); nullopt → engine-resolved, same as today. |
| Coalescence across a kit affect-entire loop regresses to N undos | S1 test pins N-writes → one Action (today's coalescence via `getNewAction(ALLOWED)`). |
