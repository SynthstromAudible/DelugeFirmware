# M1d — Edit Undo/Redo Scripting Hook (loaded song) — Design

**Status:** approved design, pre-plan.
**Predecessors:** M1a/b pilot (`afe1293c6..58753ad30`), M1c value-write sweep (`a8bd6794e..ff3717f9d`). Migration plan `docs/dev/project_model_migration_plan.md` (M1d).

## Goal

A self-checking sim harness that drives the **Edit path** (`deluge::edits::SetParamValue` → `deluge::edits::apply`) over a **real loaded song**, proving:

1. the edit resolves through a genuine DOM address (`addressFor` returns a real `ParamAddress`, and `resolveParam(addr) == doc()` — i.e. the addressed path is live, not the engine fallback), and
2. the **real** undo/redo path (`ActionLogger::revert`) restores state exactly.

This supplies the **loaded-song, DOM-resolved-address coverage the unit `edit_test` cannot reach** — `edit_test` uses a song-less standalone param, so `addressFor` always returns `nullopt` and the engine-fallback path is taken. M1d completes M1.

## Non-Goals

- The save/load round-trip gate already exists (`songs_run.py` settled-fixed-point hashes) — not re-done here.
- The engine live-recording path is already covered by `param_record_diff` (engine `setCurrentValueInResponseToUserInput` + `livePos` + `RecordingMode::ARRANGEMENT`, undo via raw `swapState`). M1d is the **discrete Edit** path with **real ActionLogger undo**, a distinct concern.
- No firmware behaviour change: M1d is pure test code.
- Drum / audio-clip owner kinds (`OwnerId::drum()` / `OwnerId::clip()`) — follow-up; M1d covers `song()` + `instrument()`.

## Key Decisions (resolved in brainstorming)

1. **Self-checking via the REAL undo path.** Script a `SetParamValue` edit → `actionLogger.revert(BEFORE)` (undo) → assert state == pre-edit → `actionLogger.revert(AFTER)` (redo) → assert == post-edit. Exercises the full `Edit → pre-log → Action → ConsequenceParamChange → revert` chain. No committed baseline — a pass/fail ctest.
2. **Targets: song-global + first instrument clip.** Every song exercises `OwnerId::song()` (`UNPATCHED_VOLUME`); a song with a synth/kit `InstrumentClip` also exercises `OwnerId::instrument()`, skipped gracefully otherwise.
3. **New corpus-driven harness**, mirroring `param_record_diff`'s structure (not folded into it — different path, different concern). SKIPs (exit 77) when `$DELUGE_SONG_CORPUS` is unset, like `songs` / `param_record`.
4. **Assertion scope: `currentValue` + `doc()->nodes`, NOT `doc()->current_value`.** The Edit's *forward* apply syncs the store scalar, but `revert` → `swapState` restores the engine value + nodes, not the store scalar — store-scalar undo is M3+ model-authority work. Asserting it on undo would assert behaviour that does not yet exist.

## Why the real undo path is feasible headless (the enabling insight)

The unit `edit_test` couldn't drive real undo because (a) its empty song made `getNewAction`'s `ActionClipState` a 0-byte alloc → null → no Action, and (b) freeing consequences pumps the uninitialised audio engine. Over a **loaded** song both problems vanish:

- The song has **real clips**, so `getNewAction`'s clip-snapshot alloc is non-zero → the Action opens (with `recording = OFF`, and headless `getCurrentUI() == getRootUI() == nullptr` so the UI guard passes).
- `revert` only **moves** an Action between the BEFORE/AFTER queues — it does **not** free consequences. As long as the script is edit→undo→redo with **no `deleteAllLogs`** (and no second edit whose `getNewAction` would `deleteLog(AFTER)`), nothing is freed → no audio pump → no crash.

## Harness Shape

`tests/golden/edit_undo_redo.cpp` — `edit_undo_redo <song.XML>`, exit `0` = all checks pass, `1` = load error, `2` = usage/IO, `3` = a check failed. Reuses `param_record_diff`'s native scaffolding: `load_song` (deserialize → `currentSong`), a `snapshotOf(msap)` helper capturing `{currentValue, copy of doc()->nodes}`, and an `equal(a, b)` comparator.

Per target param, `scriptEditUndoRedo(label, msap)`:
```
playbackHandler.recording = RecordingMode::OFF;         // discrete edit → getNewAction opens an Action
actionLogger.deleteAllLogs() is NOT called after edits  // avoid consequence-free audio pump

auto addr = deluge::edits::addressFor(*msap);
CHECK addr.has_value()                                   // real address, not engine fallback
CHECK song->project.resolveParam(*addr) == msap->doc()   // addressed path is live (== param_record_diff's S1 proof)

Snapshot pre = snapshotOf(msap);
int32_t newValue = msap->autoParam->getCurrentValue() + kDelta;   // guaranteed != current

deluge::edits::Context ctx{*currentSong, currentSong->project};
deluge::edits::SetParamValue edit{addr, newValue, 0, 0, msap};
deluge::edits::apply(ctx, edit);
Snapshot post = snapshotOf(msap);
CHECK post.currentValue == newValue                       // forward mutation landed

Action* a = actionLogger.firstAction[BEFORE];
CHECK a && a->type == ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE   // the Edit's pre-log opened it

actionLogger.revert(BEFORE, false, false);                // UNDO
CHECK equal(snapshotOf(msap), pre)                        // cv + nodes restored

actionLogger.revert(AFTER, false, false);                 // REDO
CHECK equal(snapshotOf(msap), post)                       // cv + nodes re-applied
```

**Target selection:**
- Song-global: `song->setupModelStackWithSongAsTimelineCounter(mem)->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME)` (exactly `param_record_diff`). Always run.
- Instrument clip: find the first session `InstrumentClip` whose output is `SYNTH`/`KIT`, build a clip-level unpatched-param `ModelStackWithAutoParam` for a stable param id (e.g. its master volume / an unpatched param the clip owns), such that `addressFor` classifies it `OwnerId::instrument(output serialId)`. If none found, print `# no-instrument-clip` and skip this sub-test (still exit 0). *(Exact stack-build API resolved at plan time against `Clip`/`ModelStack` helpers.)*

**Crash-safe sequencing:** each target runs on a **fresh `Song` load** (load → song-global test; reload the same XML → instrument-clip test). Each fresh load starts on a clean ActionLogger (its edit→undo→redo leaves one Action in the redo queue, never freed — it leaks harmlessly at process exit). This avoids one target's `getNewAction`/`deleteAllLogs` freeing a prior target's Action (which would pump the headless audio engine). Reloading mirrors `param_record_diff`'s `dumpAfterSaveReload`.

## Runner + ctest

`tests/golden/edit_undo_redo_run.py --binary <exe> [--wrapper ...]`: iterate the corpus, run the binary per song, treat exit 0 as OK, exit 77 as SKIP, nonzero as FAIL; print `OK=N REGRESS=M`. Mirrors `param_record_run.py` minus the baseline diff (self-checking). Registered in `sim/CMakeLists.txt` as `add_executable(edit_undo_redo ...)` + an `edit_undo_redo` ctest with `SKIP_RETURN_CODE 77`.

## Testing / Verification

- New `edit_undo_redo` ctest green over the corpus (`export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`), x86 and targeted ARM/qemu.
- Regression net unchanged: `edit_test`, `songs_run.py` OK=15, `param_record` OK=3, `./dbt build Debug` clean.
- No firmware behaviour change (test-only); the firmware build gate confirms no accidental app-code edits.

## Risks

| Risk | Mitigation |
|---|---|
| Two targets in one process free a prior Action → audio pump crash | Run each target on a fresh `Song` load (reload between targets), so each starts on a clean ActionLogger; never call `deleteAllLogs` with consequences present. |
| `getNewAction` returns null over a loaded song (UI guard / recording mode) | `recording = OFF`; headless `getCurrentUI() == getRootUI() == nullptr` passes the guard; real clips make the clip-snapshot alloc non-zero. Harness CHECKs the Action opened (exit 3 if not). |
| Instrument-clip stack build differs per song / not present | Skip gracefully when no synth/kit InstrumentClip; the song-global target always runs, so every corpus song exercises at least `OwnerId::song()` + full undo. |
| `revert` needs display/globals headless | `param_record_diff` already reverts-adjacent headless; `edit_test` created `display`. Harness creates `display` up front (as `edit_test`/`clip_clone_roundtrip` do). |
| Store-scalar not restored on undo mistaken for a bug | Assertion compares cv + nodes only; store-scalar-on-undo is explicitly out of scope (M3+). |
