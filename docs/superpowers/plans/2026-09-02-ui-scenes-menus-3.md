# UI Scenes E3 — Closing the Menu Tree — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the eleven menu items still standing on `nodes::kPending` and pay off the debts E2 recorded, so `validate_tree` reports zero placeholders with the allowance off and the legacy menu classes become deletable.

**Architecture:** The eleven placeholders need four legacy UIs — `sampleBrowser`, `sampleMarkerEditor`, `audioRecorder` and `loadMidiDeviceDefinitionUI` — and all four sit on machinery the scenes layer cannot reach: `Browser`'s file listing lives in a `gui/` class, and it runs asynchronously on the storage-owner fiber. So the plan's spine is an extraction (the file catalogue moves into `storage/`, where the layer rules already allow it) and an async seam (a scene drives a listing and renders the shell's existing `Working` overlay while it is in flight). The scenes and menu items land on top of those. The rest of the plan is E2's recorded debts.

**Tech Stack:** C++20, CppSpec (host suite `build/tests`, sim suite `build-sim`), CMake ≥ 3.27, the `ui`/`gfx`/`shell`/`scenes` libraries from plans A–E2, the script harness from plan G.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md`. Read its amendments section first — it records every E1 and E2 ruling this plan inherits, including the ones that overturned earlier rulings.

## A note on this plan's code blocks

E2's plan carried illustrative code, and **every one of its thirteen tasks found errors in it** — eight to twelve per task: directories that did not exist, a `Style` field naming the wrong face, five spec helpers that were never written, a node kind that violated the spec's own rules, a legacy call that segfaults through a null `display`. That plan's most valuable output was often the correction.

So this plan deliberately carries **interfaces and contracts rather than bodies**. Where behaviour must match legacy, it names the legacy symbol and file and says "match it" instead of guessing at the code. Treat any code below as a shape to verify, never as text to transcribe, and report what you find wrong — that reporting is part of the deliverable.

## Global Constraints

- snake_case under `src/scenes/`; the scenes library never includes from `hid/` or `gui/` (except `gui/l10n/`) and never calls frame primitives (`scripts/check_layer_rules.py`, rows `scenes-includes-gui`, `scenes-no-frame-primitives`, `scenes-no-root-ui`, `scenes-no-display`).
- `src/bsp/rza1` is retired — never edit it. Never commit `src/bsp/rust/Cargo.lock`; `git checkout --` it before committing (same for `src/deluge/memory/heaps.cpp`, `tools/deluge_run.py`).
- All three checkers exit 0, and `scripts/check_citations.py` (warning-only) gains no new hits.
- `docs/superpowers/` is gitignored: spec edits need `git add -f`.
- Compare against legacy on the same fixture, **driving each side independently from its own sentinel**. E2 shipped three comparisons that read stale state and would have passed with the port wrong.
- Mutation-prove any mapping where items look alike; run the mutation, do not reason about it.
- Assert node sets by identity, never a count with slack.
- Anything time-driven needs quiet-tick coverage. E2 shipped a cursor blink that computed the right phase and never repainted, and an auto-repeat whose unit spec was structurally blind because a `RepaintAll` armed both surface gates after every press.
- Any row or layout constant must be checked at **both** panel geometries.
- Cite legacy by **symbol and line** (`browser.cpp:91 beginListing`). Line-only citations rot: E2 corrected over 500, ~50 of them broken by its own two-line edit to a legacy file.
- `git status --porcelain tests/fixtures` clean after a run; guard anything that writes to the card (`sim/scenes_spec/support/host_flash.h` and `NoSdRoot` in `context_menu_scene_spec.cpp` are the patterns).
- `./dbt build Debug` compiles all C++ and stops at a known pre-existing `ld.lld: … Invalid record` from a Rust/LLVM skew. That is the expected end state, not a failure.

---

## File Structure

**New, and in this order of dependence:**
- `src/deluge/storage/file_catalogue.{h,cpp}` — the listing, sorting, binary search and culling machinery extracted from `Browser`, with no UI dependencies. The keystone: everything else in the browser half needs it, and legacy `Browser` becomes a caller.
- `src/scenes/browser/listing.{h,cpp}` — the async seam: a scene asks for a listing, the request runs on the storage owner, the scene renders `Working` until it completes.
- `src/scenes/browser/browser_scene.{h,cpp}` — one scene, parameterised by descriptor (sample browse, MIDI-device-definition load).
- `src/scenes/marker/marker_scene.{h,cpp}` and `src/scenes/recorder/recorder_scene.{h,cpp}`.
- `src/ui/oled/waveform.{h,cpp}` and/or `src/ui/grid/waveform.{h,cpp}` — whatever the marker editor needs that would otherwise be frame primitives in scene code. Three such widgets were added this way in E2; do not relax the rule instead.
- `src/scenes/menu/nodes/sample.{h,cpp}` — the file-selector, start/end-point and record-to-sample items.
- `src/scenes/menu/nodes/midi_device.{h,cpp}` — `midiDeviceMenu`'s five children and the MPE direction/zone selectors.

**Modified:** `src/scenes/menu/node.h` (`CustomItem`'s backward arm), `menu/context.{h,cpp}` (session vs arranger), `menu/menu_scene.{h,cpp}`, `menu/validate.{h,cpp}` (placeholder accounting to zero), `menu/nodes/{roots,sound_osc,settings}.h`, `src/scenes/registry.{h,cpp}`, and `src/deluge/gui/ui/browser/browser.{h,cpp}` (delegating to the extracted catalogue).

---

### Task 1: Extract the file catalogue into `storage/`

The keystone, and the highest-risk task in the plan: it moves live, memory-sensitive code out of a shipping class. `Browser` owns the file listing (`browser.h:67-92`: `readFileItemsForFolder`, `sortFileItems`, `searchFileItems`, `getNewFileItem`, `emptyFileItems`, `deleteSomeFileItems`, `cullSomeFileItems`, `numFileItemsDeletedAtStart/AtEnd`, `firstFileItemRemaining`, `lastFileItemRemaining`, `currentDir`, `fileItems`). None of it draws anything; all of it is unreachable from `src/scenes/`, which may include `storage/` but not `gui/`.

Read `browser.h`'s own comments before moving anything: two of these members carry scar tissue from bugs where a cached `char const*` aliased into a `std::string` inside a vector element that was later moved or erased. Preserve the ownership decisions those comments describe, and keep the comments with the code.

**Files:**
- Create: `src/deluge/storage/file_catalogue.h`, `file_catalogue.cpp`, `tests/spec/storage/file_catalogue_spec.cpp`
- Modify: `src/deluge/gui/ui/browser/browser.{h,cpp}` (delegate), `src/deluge/CMakeLists.txt`, `cmake/deluge_specs.cmake` or the host suite's CMake as needed

**Interfaces:**
- Produces: a `FileCatalogue` owning the sorted `FileItem` vector, the culling window and the current directory, exposing listing, sort, search, cull and the "entries that bracket the culled window" state. Name the operations after the legacy members so a reader can map them one-to-one.
- Consumes: `storage/file_item.h`, `storage/storage_manager.h`, `storage/fs_status.h`.

- [ ] **Step 1: Characterise before moving.** Write `tests/spec/storage/file_catalogue_spec.cpp` against the *legacy* `Browser` statics as they stand — listing a folder from the committed card fixture (`tests/fixtures/card/`), the sort order (`strcmpspecial` via `searchFileItems`, with `shouldInterpretNoteNames`/`octaveStartsFromA` set both ways), an exact hit and a miss returning the insertion point, and a cull that erases entries either side and leaves `firstFileItemRemaining`/`lastFileItemRemaining` naming the survivors. This is the safety net for the move; it must pass **before** you touch anything.
- [ ] **Step 2: Run it — PASS against legacy.** Record the numbers (entry count, order, indices) in your report; they are the invariant.
- [ ] **Step 3: Move the code** into `storage::FileCatalogue`, and make `Browser`'s members thin delegates so no `gui/` call site changes. Keep the static-lifetime shape legacy relies on, or say precisely why it is safe to change.
- [ ] **Step 4: Re-run the same spec, unchanged, now against the catalogue.** Identical results, byte for byte where the assertions are textual. Then run the host and sim suites and `./dbt build Debug`.
- [ ] **Step 5: Commit** — `refactor(storage): extract the browser's file catalogue so the scenes layer can reach it`

---

### Task 2: The async listing seam

`Browser::beginListing` (`browser.cpp:91`) dispatches onto the storage owner (`storage::Owner::run`, `owner.h:43`), shows a loading animation, and completes on the fiber — `runPendingListing` (`browser.cpp:108`) switches on a `ListingAction` and calls back. The shell is immediate-mode and tick-driven, so a scene cannot block; it must render something while a listing is in flight and repaint when it lands.

The shell already owns a `Working` scene (`SceneKind::Working`) and the notice machinery E1 built; use them rather than inventing a spinner.

**Files:**
- Create: `src/scenes/browser/listing.{h,cpp}`, `sim/scenes_spec/listing_spec.cpp`
- Modify: `src/scenes/CMakeLists.txt`

**Interfaces:**
- Produces: a request/poll seam — a scene asks for a listing (open, into folder, up a level, by path, reload, mirroring `ListingAction`), asks whether one is in flight, and takes the result once. Model the mailbox on E2's `MenuState::pending_scene`/`take_action()` and heed what that taught: `take_action()` is a one-slot mailbox with a silent overwrite, documented as such after review. Decide deliberately whether this seam needs stronger delivery than that, and write the contract down either way.
- Consumes: `storage::Owner::run` (`owner.h:43`), `storage::FileCatalogue` (Task 1), the shell's `Working` scene.

- [ ] **Step 1: Failing spec** — a listing requested against the card fixture reports in-flight, then completes with the catalogue populated; a second request while one is in flight does the documented thing (and the spec says which); a failed listing surfaces an error rather than hanging. On the host, `Owner::run` is inline (`owner.h:55-59` — `on_owner()` is always false on host), so say plainly in the report what your specs therefore do **not** cover about real fiber concurrency.
- [ ] **Step 2: Run to see it fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — sim and host suites green.
- [ ] **Step 5: Commit** — `feat(scenes): async listing seam over the storage owner`

---

### Task 3: `BrowserScene`

One scene, parameterised by descriptor, replacing what the menu tree reaches: sample browsing (`sampleBrowser`) and the MIDI-device-definition load. Legacy is `browser.cpp` plus `sample_browser.cpp` and `load_midi_device_definition_ui.cpp`; the file list, folder navigation, the typed search over `text_keyboard`, and `Browser::renderOLED`'s layout are all there.

The load/save family for songs, presets, patterns and kit rows is **out of scope** — it is reached from root UIs, not from menu items, and belongs to a later plan.

**Files:**
- Create: `src/scenes/browser/browser_scene.{h,cpp}`, `sim/scenes_spec/browser_scene_spec.cpp`, goldens under `tests/golden/ui/scenes/browser_*`
- Modify: `src/scenes/registry.{h,cpp}` (next free `SceneKind`), `src/scenes/CMakeLists.txt`

**Interfaces:**
- Produces: a descriptor table (what the browser is for, which extensions, where it starts, what accepting an entry does) and the scene. `RenameScene` (`src/scenes/rename/`) is the closest shipped model for a qwerty-driven scene, and `ContextMenuScene`'s descriptor table is the closest model for parameterisation. Follow both.
- Consumes: Tasks 1–2, `ui::grid::text_keyboard`, `ui::oled::text_edit`, `ui::oled::list`.

- [ ] **Step 1: Failing spec** — open on the card fixture and list a folder; descend and ascend; type to search and land on the matching entry; accept an entry and observe the descriptor's action; the in-flight state renders `Working`. Compare the rendered list against legacy's own `renderOLED` on the same fixture, the way E2's scene specs compare against legacy renderers.
- [ ] **Step 2: Run to see it fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, record goldens at both geometries, render each with `python3 scripts/pbm2png.py` and look at it.**
- [ ] **Step 5: Commit** — `feat(scenes): BrowserScene over the extracted catalogue`

---

### Task 4: `SampleMarkerScene`

`sample_marker_editor.cpp` (1407 lines) with `gui/waveform/` (1092) behind it. It draws a waveform on the OLED and on the pad grid, and moves start/end/loop markers. Anything it needs that would be a frame primitive in scene code becomes a toolkit widget in `src/ui/` with its own spec and goldens — E2 added three that way (`text_edit`, `option_window`, `routing`); do not relax `scenes-no-frame-primitives`.

**Files:**
- Create: `src/scenes/marker/marker_scene.{h,cpp}`, the waveform widget(s) under `src/ui/`, their specs, `sim/scenes_spec/marker_scene_spec.cpp`, goldens at both geometries
- Modify: `src/scenes/registry.{h,cpp}`, the relevant CMake

- [ ] **Step 1: Failing spec** — the waveform renders for a fixture sample; a marker moves with the encoder and writes through to the model, compared against legacy's `writeValue` on the same fixture; the grid half paints. `MarkerType` and the marker-specific arms come from `sample_marker_editor.h:62`.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run; goldens recorded and inspected.**
- [ ] **Step 5: Commit** — `feat(scenes): SampleMarkerScene and the waveform widgets`

---

### Task 5: `AudioRecorderScene`

`audio_recorder.cpp` (285 lines) — the smallest of the four. Legacy's menu item drives it unusually: `osc/audio_recorder.h:50-57` opens the UI and then calls `audioRecorder.process()` from the item, so read that interaction before designing the scene's tick.

**Files:**
- Create: `src/scenes/recorder/recorder_scene.{h,cpp}`, `sim/scenes_spec/recorder_scene_spec.cpp`, goldens
- Modify: `src/scenes/registry.{h,cpp}`, CMake

- [ ] **Step 1: Failing spec** — arm, record, stop, and the resulting sample reaching the model; the states legacy shows on screen. Anything that writes to the card is guarded.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run; goldens inspected.**
- [ ] **Step 5: Commit** — `feat(scenes): AudioRecorderScene`

---

### Task 6: The eight sample menu items

`FILE_BROWSER` ×2 (`file_selector.cpp:44`), `START_POINT`/`END_POINT` ×2 each (`sample/loop_point.cpp`, and `sample/start.h`/`end.h` which only set `markerType`), `RECORD_TO_SAMPLE` ×2 (`osc/audio_recorder.h`), plus `audio_clip_waveform` (`audio_clip/sample_marker_editor.h:26`). These are `CustomItem`s that open the scenes from Tasks 3–5, on the `MenuState::pending_scene` mechanism E2 built.

**Files:**
- Create: `src/scenes/menu/nodes/sample.{h,cpp}`
- Modify: `src/scenes/menu/nodes/sound_osc.h` (`:195`, `:244`, `:246`, `:303`, `:307`), `nodes/roots.h:427`
- Test: `sim/scenes_spec/tables_*_spec.cpp` per this plan's comparison rules

- [ ] **Step 1: Failing spec** — each item opens the right scene with the right descriptor, mutation-proved by swapping two and watching a test fail. Relevance predicates (`osc##i##_file_selector`, `osc##i##_is_sample`) must keep matching legacy's `isRelevant`.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run.**
- [ ] **Step 5: Commit** — `feat(scenes): the sample file, marker and record-to-sample items`

---

### Task 7: `midi_device_definition_linked` and `specific_output_selector`

The last two placeholders. `linked.h:42` opens `loadMidiDeviceDefinitionUI` — Task 3's second descriptor. `specific_output_selector` (`roots.h:426`, `relevance::audio_source_is_specific`) is an output picker, not a browser; find its legacy class and match it.

**Files:** Modify `src/scenes/menu/nodes/roots.h:332,426`; create whatever item files they need.

- [ ] **Step 1: Failing spec** per this plan's rules.  - [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run — `validate_tree` now reports zero `PlaceholderCustom` with `allow_placeholder_custom` off. Assert that.**
- [ ] **Step 5: Commit** — `feat(scenes): the last two placeholder items; the tree has none left`

---

### Task 8: `midiDeviceMenu`'s subtree

E2 deferred this on size and recorded it in `nodes/dynamic.h`. `menus.cpp:1444-1453` gives the five children: `mpe::directionSelectorMenu`, `defaultVelocityToLevelMenu`, `device_send_clock_menu`, `device_receive_clock_menu`, `device_is_relative_menu`. The MPE arm is `gui/menu_item/mpe/` (291 lines): `DirectionSelector` has instance state, a runtime `getTitle()`, and chains onward to `zoneSelectorMenu` → `zone_num_member_channels`.

Four of the five need a binding family scoped to `MenuContext::midi_cable` — per-cable state, unlike every binding shipped so far. That is the task's real work.

**Files:** Create `src/scenes/menu/nodes/midi_device.{h,cpp}`; modify `nodes/dynamic.{h,cpp}` (give `midi_devices` its `select_chain`), `menu/bindings.{h,cpp}` if the per-cable family belongs there.

- [ ] **Step 1: Failing spec** — each child reads and writes the same per-cable state as its legacy item, on a fixture with two cables so a wrong cable is caught; the chain from `midi_devices` lands on the page; `DirectionSelector`'s runtime title and its onward chain match.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run; the dump JSON moves (new reachable nodes) — regenerate and say what.**
- [ ] **Step 5: Commit** — `feat(scenes): the per-device MIDI page and the MPE zone selectors`

---

### Task 9: Session vs arranger

E2's `song_midi_learn` and `configure_song_macros` stay visible in the arranger where legacy hides them, because `context_for` takes one `bool session_or_arranger_root` that deliberately conflates them. E2's review costed this at about half a day and found it is really a **three-way** conflation: `sound_editor.cpp:1762-1765` selects the song root for `sessionView`, `arrangerView` *and* `automationView`, and legacy hides these two items from all but the first.

**Files:** Modify `src/scenes/menu/context.{h,cpp}` (`:163` and the `MenuContext` field), `menu/relevance.{h,cpp}`, `nodes/roots.h`, and the ~62 positional-bool call sites in `sim/scenes_spec/`.

- [ ] **Step 1: Failing spec** — the two items are relevant in a session context and not in an arranger or automation one, compared against legacy's own `isRelevant` on all three.
- [ ] **Step 2: Run to see it fail.**
- [ ] **Step 3: Implement.** Audit the 9 existing `is_song_view` reads — `context.cpp:168,325` and `bindings.cpp:265` must keep meaning "either". The call-site updates are mechanical but adjacent bools make them easy to get silently wrong; say how you checked.
- [ ] **Step 4: Run — the dump JSON moves (the two nodes gain `available_when`). Regenerate.**
- [ ] **Step 5: Commit** — `fix(scenes): hide the song-only items outside the session view`

---

### Task 10: The remaining E2 debts

Each is small; together they are a task. From E2's ledger: `CustomItem` has no backward arm, so `ascend()`'s blanket `Kind::Custom` exclusion (`menu_scene.cpp:1165-1167`) stands in for one — E2's review traced the chain-source customs and found no live second variant, but nothing tests it. `perform_editor` mirrors two `PerformanceView` bools and the wiring plan owes sync calls, including `releaseViewOnExit` (`performance_view.cpp:1327-1332`), the ordinary exit path. Mod-matrix pad blinking is unported. The row-scroll path (`ui/oled/list.cpp:156`) is untested for a *selected* over-long row. Seven pre-existing self-duplicate goldens are owed cleanup.

**Files:** `src/scenes/menu/node.h`, `menu/menu_scene.{h,cpp}`, `menu/nodes/action.h`, `sim/scenes_spec/`, `tests/golden/ui/shell/`.

- [ ] **Step 1: Failing specs** — a backward arm that fires on ascend and is *not* the forward arm; a selected over-long row that scrolls; whatever pins the mirror's completeness.
- [ ] **Step 2: Run to see them fail.**  - [ ] **Step 3: Implement; delete or rename the seven duplicate goldens.**  - [ ] **Step 4: Run.**
- [ ] **Step 5: Commit** — `fix(scenes): CustomItem backward arm, row scrolling, and E2's recorded debts`

---

### Task 11: Acceptance scripts and accounting

Level-2 scripted runs for what this plan shipped — the browser, the marker editor, the recorder, the per-device MIDI page — on the pattern in `sim/scenes_spec/menu_scripts_spec.cpp`. E2's equivalent task found two real bugs that eleven tasks of unit specs had missed; expect this one to do the same, and treat a script that finds nothing as a reason to look harder at the script.

**Files:** `tests/ui-scripts/shell/*.txt`, goldens under `tests/golden/ui/shell/`, `sim/scenes_spec/menu_scripts_spec.cpp`, `src/scenes/menu/validate.h`.

- [ ] **Step 1:** Write the scripts, record once, **render every frame and look at it**. A golden that repeats its predecessor records nothing — delete or rename it.
- [ ] **Step 2:** Determinism — run twice, `got/` byte-identical via `python3 scripts/ui_snapshot_diff.py`.
- [ ] **Step 3:** Accounting — `kPendingCustomNodes` is now **0**; assert it, and remove `ValidateOptions::allow_placeholder_custom` if nothing else needs it.
- [ ] **Step 4:** Regenerate the dump; `menu_dump_check.sh` clean.
- [ ] **Step 5: Commit** — `test(scenes): level-2 scripts for the browser, marker and recorder; zero placeholders`

---

### Task 12: Rules, README, spec amendments, and the E4 handoff

**Files:** `scripts/check_layer_rules.py` + baseline, `src/scenes/README.md`, the design spec, `docs/superpowers/specs/2026-08-26-ui-migration-design.md`.

- [ ] **Step 1:** Any new ratchet rows this plan's surface needs. **Probe each alternative of each row separately** — E2 recorded a row as "probe-verified" when the probe had matched a different alternative and exercised nothing about the identifier list it was meant to guard. Report the observed exit status per alternative.
- [ ] **Step 2:** README true to what shipped, including which system a contributor adds a menu item to during the coexistence.
- [ ] **Step 3:** Spec amendments for this plan's rulings, and the citation sweep (`scripts/check_citations.py` clean, symbol+line convention applied to what you touched).
- [ ] **Step 4:** **Update the migration spec's deletion schedule.** Its "after phase 4" row lists `gui/menu_item/**`, `gui/ui/menus.*` and `gui/ui/sound_editor.*`; E2 corrected the precondition to "`validate_tree` reports zero `PlaceholderCustom` with the allowance off". That precondition is now met, so state what remains before the deletion can actually happen — spec F's wiring, and nothing else from this plan.
- [ ] **Step 5: Commit** (`git add -f` for the specs) — `test(scenes): layer rules, README, spec amendments, E4 handoff`

---

## Self-review notes

- **Spec coverage.** Spec E §7 step 3 promises "BrowserScene family — nine UIs → one". This plan ports the two the *menu tree* reaches and leaves the load/save family for songs, presets, patterns and kit rows to a later plan, because those are reached from root UIs rather than menu items and the menu tree's legacy-vs-port oracle cannot exercise them. Task 12 records that split against §7 so the next author is not surprised by it. §4.4 validation reaches zero placeholders in Task 11.
- **Placeholders:** none in this plan, and none left in the tree when it lands — that is the point of it.
- **Type consistency:** `FileCatalogue` (Task 1) is consumed by Tasks 2–3; the listing seam (Task 2) by Task 3; `BrowserScene`'s descriptor table (Task 3) by Tasks 6–7; `SampleMarkerScene`/`AudioRecorderScene` (Tasks 4–5) by Task 6; `CustomItem`'s backward arm (Task 10) is independent of all of them.
- **Known risks:** (a) Task 1 moves memory-sensitive code whose own comments record two prior use-after-free bugs — the characterisation spec in Step 1 exists precisely because "it still compiles" is not evidence here. (b) `Owner::run` is inline on the host (`owner.h:55-59`), so no spec in this plan exercises real fiber concurrency; Task 2 must say so rather than implying coverage. (c) Tasks 8 and 9 both move the committed dump JSON, and they must not land in a way that leaves the CI diff stale between them.
