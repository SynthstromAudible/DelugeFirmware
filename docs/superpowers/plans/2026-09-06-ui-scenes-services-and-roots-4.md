# UI Scenes E4 — Services, Overlays and the First Two Root Scenes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the three model-aware shell services (`ModKnobs`, `GlobalControls`, `MidiLearn`), the three
missing overlay scenes (`ParamPopupScene`, `CountInScene`, `ScreensaverScene`), the shared timeline state
every clip view needs, and the first two root scenes (`SlicerScene`, `AudioClipScene`) — all as ports of
legacy behaviour, none of them new.

**Architecture:** The three services implement `shell::Service` but live in `src/scenes/`, because
`src/shell/**` may not include `model/**` (`scripts/check_layer_rules.py`'s `shell-includes-outside` row)
and all three touch the model. They are the port of `View` (`gui/views/view.cpp`, 2,908 lines), which is the
god object holding all three concerns plus several that belong to views E5/E6 will port. The router already
routes for them: `mod_knobs` → top scene → `global_controls` (`shell/router.h:55-65`), which is exactly
legacy's `Buttons::buttonAction` shape — a pre-UI section, `getCurrentUI()->buttonAction`, then a tail
(`hid/buttons.cpp:133-232`). The two root scenes are the first scenes that own the grid as a base with a
sidebar, and the first that need scroll/zoom, which is why the shared timeline state is a task of its own.

**Tech Stack:** C++23, `deluge_scenes` (`src/scenes/`) over `deluge_ui_shell` (`src/shell/`) and the toolkit
(`src/ui/`, `src/gfx/`); specs in `sim/scenes_spec/` (links `deluge_app`, so it can drive the legacy classes)
and `tests/spec/` (host, cannot); level-2 script goldens under `tests/ui-scripts/` + `tests/golden/ui/`.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md` (§5 splitting the god-views, §6
overlays, §7 row 4, §8 testing), with `docs/superpowers/specs/2026-08-26-ui-shell-design.md` §5 (the service
table) and `docs/superpowers/specs/2026-08-26-ui-migration-design.md` (§10 handoff, §10.4 carry list).

**Branch:** `ui/scenes-4` off `ui/scenes-3`. Branch in place — no worktrees.

---

## Global Constraints

Every task's requirements implicitly include this section.

1. **No new UI, scenes or UX.** Kate, 2026-09-06: *"we shouldn't be adding any scenes/ui/ux that didn't exist
   prior."* Everything here is a port of something legacy already does. If a task's legacy source has no
   counterpart for a behaviour you are about to write, that behaviour does not ship — raise it instead. A
   comment reading "spec design, not a port" or "legacy has none" is a defect to report, not a decision you
   may make. This rule outranks any spec sentence, including the ones in the specs this plan argues from:
   the boot splash was removed on `ui/scenes-3` (`f6549e223`) precisely because a spec section asked for it.
2. `snake_case` functions and variables, `PascalCase` types, `k`-prefixed constants, namespace
   `deluge::scenes` (and nested `deluge::scenes::<area>`). camelCase only in a façade over legacy.
3. `src/scenes/**` may include `scenes/**`, `shell/**`, `ui/**`, `gfx/**`, `notify/**`, `libdeluge/**`, the
   standard library, `model/**`, `modulation/**`, `playback/**`, `processing/**`, `storage/**`,
   `gui/l10n/**`, `definitions_cxx.hpp`. **Never `gui/` otherwise, never `hid/`.** `model/model_stack.h` is
   narrower still: only `menu/context.cpp` and `menu/bindings.cpp` may include it, and
   `menu::model_stack_for()` is the single entry point everything else uses.
4. The scenes library never calls frame primitives directly (`scenes-no-frame-primitives`) — it composes
   widgets from `src/ui/`. If a widget you need does not exist, **add the widget**; do not relax the rule.
5. No `Action::Kind::LockInput` from any scene (`shell/scene.h`'s warning on `Action`).
6. No heap allocation in library code, and no wall clock: every deadline is `Scene::next_timer()` /
   `Service::next_timer()` against the `now_ms` the shell passes in (`shell-no-clock`, `ui-no-clock`).
7. Never edit `src/bsp/rza1` (retired). Never commit `src/bsp/rust/Cargo.lock` (it is modified by the build;
   leave it unstaged).
8. Cite legacy by **symbol AND line** — `View::modEncoderAction` (`view.cpp:818-842`), never a bare line
   number. `scripts/check_citations.py` is warning-only and cannot see a citation that points at the wrong
   symbol; a reviewer can.
9. **Assert, then clear. Never defensively clear.** A spec that resets state it did not prove was set is a
   spec that cannot fail.
10. All four checkers exit 0: `check_layer_rules.py`, `check_platform_boundary.py`, `check_gfx_layer.py`,
    `check_citations.py`. The layer-rule baseline **may only shrink**.

### Verification gates (every task)

- CI gate: `cmake --build build-sim --target deluge_scenes_spec -j8 && ctest --test-dir build-sim`
  (33/33 at the start of this plan)
- Host: `ctest --test-dir build/tests -C Debug` (98/98 at the start of this plan)
- The four checkers, and `bash scripts/menu_dump_check.sh` if any menu table changed
- `./dbt build Debug` — **stops at a known pre-existing `ld.lld: … Invalid record`** from a Rust/LLVM skew
  that is not this work. Reaching that link error is a pass; a compile error before it is yours.

### Why this plan carries interfaces and citations, not bodies

Deliberate, and it is the third plan in a row to do it. E2's plan carried illustrative code and **every one
of its thirteen tasks found an error in it**; E3's plan carried interfaces and contracts instead and the
implementers did better work faster. A signature, an enum, a file path and an exact legacy citation are
things this plan can get right; a body it writes from outside the implementer's context is a thing the
implementer must first debug and then discard. So: exact names, exact types, exact citations, exact
verification — and the body is the implementer's to write from the legacy source named in the step.

The corollary binds the implementer: **the legacy citation in a step is the requirement, not the step's
prose.** Where the two disagree, read the code and report the plan's error.

### The oracle discipline (this plan's central safety claim)

Spec E §7's acceptance is "the level-2 scripts produce identical snapshots for the legacy UI through
`LegacySceneAdapter` and for the new scene". **`LegacySceneAdapter` does not exist** — it is spec F's
`RunOptions::second_registry` (`tests/spec/shell/support/script_runner.h:68-73`), and nothing links
`deluge_scenes` into an application yet. So acceptance for E4 is the same oracle E3 proved out, and the same
disciplines, which were all earned by a failure:

- **Each side driven from its own sentinel.** A comparison whose reference is computed from the port's own
  state proves nothing. E3's browser spec did exactly that for three plans (`mirror(s.file_index(), …)`) and
  the final review caught it; see migration spec §10.6.
- **Mutation-prove every mapping.** Perturb the port and watch the spec fail; perturb *legacy* and watch it
  fail too. Name both mutations in the commit message. A mutation that passes is the finding: E3's marker
  spec had one that did not fail, because four markers sharing a column made two increments equal.
- **Assert node/pixel/LED sets by identity**, not counts with slack.
- **Look at every golden.** A golden byte-identical to its predecessor records nothing.
- Quiet-tick coverage for anything time-driven, and **both panel geometries** (48 and 64 rows) for anything
  that draws.
- Where a legacy entry point genuinely cannot be driven (an unbounded blocking loop, a null root UI), say so
  **at the spec site** and count it as uncovered in the task's report. An honest small number beats an
  inflated one.

---

## File Structure

**New — services (implement `shell::Service`, live in scenes because they are model-aware):**

| File | Responsibility |
|---|---|
| `src/scenes/services/mod_knobs.{h,cpp}` | The `ModTarget`, gold-knob turns and acceleration, mod-button mode, knob indicators, mod LEDs; pushes `ParamPopupScene` |
| `src/scenes/services/global_controls.{h,cpp}` | Transport buttons, record combos, tempo encoder and button, tap tempo, sync scaling, back long-press, undo/redo; Play/Record/Tap/SyncScaling/Triplets LEDs |
| `src/scenes/services/midi_learn.{h,cpp}` | Learn mode, the pressed thing, the flash phase; scenes report `learnable_at(pad)` |

**New — overlay scenes:**

| File | Responsibility |
|---|---|
| `src/scenes/overlay/param_popup_scene.{h,cpp}` | The transient gold-knob value popup (`SceneKind::FirstApp + 6`) |
| `src/scenes/overlay/count_in_scene.{h,cpp}` | The record count-in digit (`SceneKind::FirstApp + 7`) |
| `src/scenes/overlay/screensaver_scene.{h,cpp}` | The idle animation (`SceneKind::Screensaver`) |
| `src/ui/oled/rainfall.{h,cpp}` | The rain animation, moved out of `hid/display/` — pure pixels |
| `src/ui/oled/starfield.{h,cpp}` | The starfield animation, moved out of `hid/display/` |

**New — shared clip machinery and root scenes:**

| File | Responsibility |
|---|---|
| `src/scenes/timeline/timeline.{h,cpp}` | `TimelineState`: scroll, zoom, tick squares, the scroll/zoom gestures every clip view shares |
| `src/scenes/slicer/slicer_scene.{h,cpp}` | The slicer (`SceneKind::FirstApp + 8`) |
| `src/scenes/audio_clip/audio_clip_scene.{h,cpp}` | The audio clip view (`SceneKind::FirstApp + 9`) |

**Modified:** `src/scenes/registry.{h,cpp}` (four new kinds and their accessors), `src/scenes/CMakeLists.txt`,
`src/scenes/README.md`, `scripts/check_layer_rules.py` (new ratchet rows), the three design specs
(amendment sections).

**Legacy touched:** none by default. Any extraction out of `gui/` or `hid/` follows E3's §15.2 pattern —
move the file, leave the legacy callers pointing at the moved symbol, and prove behaviour preservation.

---

## Task 1: `ParamPopupScene`

**Files:**
- Create: `src/scenes/overlay/param_popup_scene.{h,cpp}`
- Modify: `src/scenes/registry.{h,cpp}`, `src/scenes/CMakeLists.txt`
- Test: `sim/scenes_spec/param_popup_spec.cpp` (new), registered in `sim/scenes_spec/CMakeLists.txt`

**Interfaces:**
- Produces: `namespace deluge::scenes::overlay { constexpr shell::SceneKind kParamPopupSceneKind = static_cast<shell::SceneKind>(static_cast<uint8_t>(shell::SceneKind::FirstApp) + 6); class ParamPopupScene; }`
  with `void show(params::Kind kind, int32_t param_id, int32_t knob_pos, PatchSource source1, PatchSource source2, uint32_t now_ms);`
  and `std::optional<uint32_t> next_timer() const` for its own expiry.
- Consumes: `ui::oled::popup` widgets, `menu::bindings`' value formatting where it already spells the same
  question (check before duplicating — `bindings::format_value` or equivalent).

**Legacy:** `View::displayModEncoderValuePopup` (`gui/views/view.cpp:1084-1289`),
`View::flushPendingModEncoderValuePopup` (`view.cpp:1290-1301`), `View::calculateKnobPosForDisplay`
(`view.cpp:1302-1325`).

- [ ] **Step 1: Read the legacy popup end to end and write down its arms.** 200 lines with several
      branches: patch-cable vs plain param, the source names, the "pending" path that defers the popup when
      it cannot be shown yet, and the 7SEG/OLED split (7SEG is gone — see spec A; drop those arms and say so
      in the port's doc). Produce a table in the task report: arm → condition → what it draws.

- [ ] **Step 2: Write the failing spec first, driving legacy.** In `sim/scenes_spec/param_popup_spec.cpp`,
      build the song fixture (`sim/scenes_spec/support/song_fixture.h`), set up a param, call the **legacy**
      `view.displayModEncoderValuePopup(...)` and capture what it put on the panel; then drive the port's
      `ParamPopupScene::show(...)` with the same arguments from its own state and compare the full 128×48
      panel. Expect FAIL — the scene does not exist.

- [ ] **Step 3: Implement the scene.** `render_oled` draws through `ui::oled::popup`; `oled_transparent(Main)`
      returns true (spec E §6: every overlay reports it so the scene beneath shows around it);
      `is_grid_overlay()` false and `dims_grid()` false — it composites nothing on the grid, and a popup must
      not halve the brightness of a grid it never touches (`shell.cpp`'s `dims_grid` note). `handle` returns
      unhandled: the popup routes nothing.

- [ ] **Step 4: Timer and expiry.** The popup's lifetime is a deadline, not a countdown — `next_timer()`
      returns the remaining milliseconds and `timer_repaints()` names `{oled=true, grid=false}`. Do **not**
      arm the grid: the OLED flush blits unconditionally and the grid's diffs, so an unneeded OLED arm is a
      permanent blit (shell spec §15.1). Add a quiet-tick example: no input, the popup expires on its own,
      and the scene removes itself with `SceneStack::remove(kind)` — **not** `Action::Kind::Pop`, which takes
      everything above it down too (spec D §13).

- [ ] **Step 5: Both geometries.** Run the render comparison at 48 and 64 rows via the two profiles, as
      every drawing spec on this stack does.

- [ ] **Step 6: Mutation-prove.** Perturb the port's knob-position-to-display conversion by one and confirm
      the comparison fails; perturb legacy's `calculateKnobPosForDisplay` and confirm it fails too. Record
      both in the commit message.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 2: `ModKnobs` — the target, the turns, and acceleration

**Files:**
- Create: `src/scenes/services/mod_knobs.{h,cpp}`
- Test: `sim/scenes_spec/mod_knobs_spec.cpp`

**Interfaces:**
- Produces: `namespace deluge::scenes::services { class ModKnobs final : public shell::Service; }` with
  `void set_target_with_timeline(TimelineCounter* counter, bool send_midi_feedback);`
  `void set_target_without_timeline(ModControllable* controllable, ParamManager* params);`
  `int32_t mode() const;` (the port of `getModKnobMode`), and a constructor taking the
  `overlay::ParamPopupScene&` it shows values through plus the `shell::SceneStack&` it pushes on.
- Consumes: Task 1's `ParamPopupScene::show`.

**Legacy:** `View::modEncoderAction` (`view.cpp:818-842`), `View::modEncoderAction_nonExistentParam`
(`:843-852`), `View::modEncoderAction_existentParam` (`:853-948`),
`View::calculateKnobPosForModEncoderTurn` (`:949-970`), `View::getModelStackWithParam` (`:971-1011`),
`View::getParameterNameFromModEncoder` (`:1012-1057`), `View::potentiallyMakeItHarderToTurnKnob`
(`:1058-1083`), `View::setActiveModControllableTimelineCounter` (`:1867-1899`),
`View::setActiveModControllableWithoutTimelineCounter` (`:1900-1922`), `View::setModRegion` (`:1923-1950`),
`View::pretendModKnobsUntouchedForAWhile` (`:1951-1955`), `View::notifyParamAutomationOccurred`
(`:1691-1712`), `View::sendMidiFollowFeedback` (`:1713-1731`), `View::instrumentBeenEdited` (`:1326-1331`).

- [ ] **Step 1: Establish the `ModTarget` boundary before writing any of it.** The three "set active mod
      controllable" entry points plus `setModRegion` are the state; everything else reads it. Write the
      state struct first and list, in the task report, every legacy global it replaces
      (`activeModControllableModelStack` and friends) with a citation each.

- [ ] **Step 2: Gold-knob acceleration — this is a carry item, not new work.** `calcNextKnobSpeed` was
      recorded as owed by E1 and carried through E2 and E3 (`src/scenes/README.md`'s carry list). Port it
      here with `potentiallyMakeItHarderToTurnKnob`, and note in the doc that this closes that carry.

- [ ] **Step 3: Write the failing spec, both sides from their own sentinel.** Drive the legacy
      `view.modEncoderAction(0, +1)` on a fixture and read `ParamManager`'s resulting value; drive the port's
      `ModKnobs::handle(UiEvent{Encoder{GoldUpper}, +1}, ctx)` on a model reset to the same starting point
      and compare the **model's** value, not the port's own report of it. **A write comparison where legacy
      runs first and the port stores nothing silently passes** — that was E2's biggest catch, proven by
      deleting a hook and watching the suite stay green. Each side gets its own sentinel value.

- [ ] **Step 4: Implement the turn path**, including the non-existent-param arm (legacy has a distinct one)
      and the popup show. Route through the shell's `Service::handle`; the router already sends gold-knob
      turns and `ButtonId::Mod0..Mod7` here first (`router.h:55-65`).

- [ ] **Step 5: The knob *push* is not a `mod_knobs` event.** `ButtonId::ModEnc0`/`ModEnc1` reach the scene
      first, exactly as legacy routes them from the tail after the UI's own `buttonAction`
      (`hid/buttons.cpp:225-231`). Add a spec that asserts the ordering: a scene that handles `ModEnc0` sees
      it and the service does not.

- [ ] **Step 6: Mutation-prove the value path** — perturb the port's `calculateKnobPosForModEncoderTurn`
      and confirm failure; perturb legacy's and confirm failure. Then delete the port's model write entirely
      and confirm the suite goes red (the E2 blindness check).

- [ ] **Step 7: Run the gates and commit.**

---

## Task 3: `ModKnobs` — buttons, mode, indicators and LEDs

**Files:**
- Modify: `src/scenes/services/mod_knobs.{h,cpp}`
- Test: `sim/scenes_spec/mod_knobs_spec.cpp` (extend)

**Interfaces:**
- Produces: `void leds(shell::LedFrame& frame) override;` and the mode accessor Task 2 declared.

**Legacy:** `View::modButtonAction` (`view.cpp:1497-1547`), `View::setModLedStates` (`:1548-1679`),
`View::getModKnobMode` (`:1680-1690`), `View::modEncoderButtonAction` (`:1332-1350`),
`View::modEncoderButtonAction_deleteAutomation` (`:1351-1362`),
`View::modEncoderButtonAction_changeModControllable` (`:1363-1378`), `View::setKnobIndicatorLevels`
(`:1379-1402`), `View::setKnobIndicatorLevel` (`:1403-1472`),
`View::convertPatchCableKnobPosToIndicatorLevel` (`:1473-1496`).

- [ ] **Step 1: Enumerate the eight mod buttons and what each mode lights.** `setModLedStates` is 130 lines
      of the trickiest LED logic on the device. Write the mode → LED table into the task report first, with
      a citation per row, and have the spec assert that table by identity — **every declared LED id and its
      state**, not "at least one LED is on".

- [ ] **Step 2: Write the failing LED spec.** Compose the port's `leds(LedFrame&)` and compare the frame's
      `entries()` against what legacy's `setModLedStates` wrote, driven from its own sentinel state. The
      `KnobIndicator` levels compare through `LedFrame::indicator_or_null`.

- [ ] **Step 3: Implement the button and mode half.**

- [ ] **Step 4: Implement the indicator half**, including the patch-cable conversion, which has its own
      curve (`convertPatchCableKnobPosToIndicatorLevel`).

- [ ] **Step 5: The indicator is a 4-LED column, not a ring.** The type is `ui::KnobIndicator`; the rename
      from `KnobRing` was made deliberately on `ui/toolkit-fixes` (`df1e82724`). Do not reintroduce the old
      spelling in comments.

- [ ] **Step 6: Mutation-prove one LED row and one indicator level**, both sides.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 4: `GlobalControls`

**Files:**
- Create: `src/scenes/services/global_controls.{h,cpp}`
- Test: `sim/scenes_spec/global_controls_spec.cpp`

**Interfaces:**
- Produces: `namespace deluge::scenes::services { class GlobalControls final : public shell::Service; }` —
  `handle`, `leds`, `tick`, `next_timer` (the play-flash cadence).

**Legacy, and the shape to preserve:** legacy asks these questions in three places, and the router's chain
is the same three places in the same order — `mod_knobs` → top scene → `global_controls`
(`shell/router.h:55-56`):

| legacy | port |
|---|---|
| `Buttons::buttonAction`'s pre-UI section (`hid/buttons.cpp:100-132`: shift, cross-screen, select-enc bookkeeping) | already `InputLowering`'s, built in plan D — **verify, do not re-port** |
| `getCurrentUI()->buttonAction` (`buttons.cpp:133`) | the scene, routed second |
| the tail: `PLAY` (`buttons.cpp:144-168`), `SHIFT` (`:169-172`), `RECORD` (`:173-204`), `TEMPO_ENC` (`:205-222`), `MOD_ENCODER_0/1` (`:223-231`) | this service, routed last — except the two mod-encoder buttons, which Task 2 step 5 already established reach the scene |
| `View::buttonAction`'s global arms: `TAP_TEMPO` (`view.cpp:146-184`), `LEARN` (`:185-198`), `PLAY`/`RECORD` under learn (`:199-230`), `SAVE` (`:231-275`), `LOAD` (`:276-333`), `SYNC_SCALING` (`:334-403`), `BACK` (`:404-424`), undo (`:425-441`), redo (`:442-448`), `SELECT_ENC`+shift (`:449-471`) | this service |
| `View::setLedStates` (`:514-517`), `View::setTimeBaseScaleLedState` (`:502-513`), `View::flashPlayRoutine` (`:2754-2765`), `flashPlayEnable`/`flashPlayDisable` (`:2766+`) | this service's `leds`/`tick`/`next_timer` |

- [ ] **Step 1: Split the list.** Some `View::buttonAction` arms belong to MIDI learn (Task 5) — the
      `TAP_TEMPO` arm's learn branch, the `PLAY`/`RECORD`-under-learn arms, the `LEARN` button itself. Decide
      the boundary and write it into the report: the service that *owns the mode* owns the arm, and the other
      asks it. Do not duplicate the logic in both.

- [ ] **Step 2: `SAVE` and `LOAD` are the load/save family's openers.** Those UIs are not ported (migration
      §10.4 puts them on phase 6). Port the arm's *decision* and leave the open itself as a named, cited gap
      the way E3's action seam does — a `GlobalAction` outbox entry, or a documented no-op with a `@warning`.
      **Do not invent a browser.**

- [ ] **Step 3: Write the failing spec.** Drive legacy's `Buttons::buttonAction(PLAY, true, false)` on a
      fixture and read `playbackHandler`'s resulting state; drive the port's `handle` from its own sentinel
      and compare the model. Cover the record combo (`recordButtonPressUsedUp`, `buttons.cpp:149-168`) — it is
      a two-button interaction and a single-press spec cannot see it.

- [ ] **Step 4: Implement.**

- [ ] **Step 5: The play flash is a deadline, not a poll.** `flashPlayRoutine` is legacy's timer; here it is
      `next_timer()` plus `leds()`. Add a **quiet-tick** example: no input at all, and the Play LED changes
      state on schedule.

- [ ] **Step 6: Mutation-prove** the transport write and one LED, both sides.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 5: `MidiLearn`

**Files:**
- Create: `src/scenes/services/midi_learn.{h,cpp}`
- Test: `sim/scenes_spec/midi_learn_spec.cpp`

**Interfaces:**
- Produces: `class MidiLearn final : public shell::Service;` with `bool active() const;`,
  `void begin(); void end();`, `void thing_pressed(LearnTarget target, bool on);`, and the flash phase the
  scenes read when they paint a learnable pad.
- Consumes: the scene-side report. `MenuScene` already has `learn_*` hooks from E1/E2 — **find them and use
  them**; do not add a second mechanism (`src/scenes/menu/menu_scene.h`).

**Legacy:** `View::startMIDILearn` (`view.cpp:472-481`), `View::endMIDILearn` (`:482-501`),
`View::sectionMidiLearnPadPressed` (`:518-531`), `View::clipStatusMidiLearnPadPressed` (`:532-545`),
`View::noteRowMuteMidiLearnPadPressed` (`:546-559`), `View::drumMidiLearnPadPressed` (`:560-578`),
`View::instrumentMidiLearnPadPressed` (`:579-596`), `View::endMidiLearnPressSession` (`:597-622`),
`View::noteOnReceivedForMidiLearn` (`:623-744`), `View::ccReceivedForMIDILearn` (`:767-795`),
`View::midiLearnFlash` (`:796-817`), `View::clearMelodicInstrumentMonoExpressionIfPossible` (`:745-766`).

- [ ] **Step 1: The five `*MidiLearnPadPressed` overloads are one question asked five times.** Each takes a
      different model object. Spec D's service table says "scenes report `learnable_at(pad) -> LearnTarget`",
      so the port inverts it: the *scene* names what is under the pad, the service holds the mode. Write the
      `LearnTarget` variant and prove all five legacy call sites map onto it — one row per overload, with a
      citation, in the report.

- [ ] **Step 2: Write the failing spec.** Drive legacy through `view.startMIDILearn()` +
      `view.sectionMidiLearnPadPressed(true, 2)` + `view.noteOnReceivedForMidiLearn(...)` and read the
      learned command out of the model; drive the port from its own sentinel and compare the model.

- [ ] **Step 3: Implement mode, target and the learn commit.**

- [ ] **Step 4: The flash is time-driven.** `midiLearnFlash` runs on legacy's `MIDI_LEARN_FLASH` timer; here
      it is `next_timer()` and a phase the scenes read. **Quiet-tick coverage required**, and the flash must
      not arm the OLED (`timer_repaints`) — this is a grid concern only.

- [ ] **Step 5: `endMidiLearnPressSession`'s delete-on-release.** Holding learn and pressing a learned thing
      *clears* it (`view.cpp:146-184`'s `deleteMidiCommandOnRelease` and `:597-622`). Port it; spec it with
      the model asserted before and after.

- [ ] **Step 6: Mutation-prove** the learn commit and the flash phase.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 6: `CountInScene`

**Files:**
- Create: `src/scenes/overlay/count_in_scene.{h,cpp}`
- Modify: `src/scenes/registry.{h,cpp}`
- Test: `sim/scenes_spec/count_in_spec.cpp`

**Interfaces:**
- Produces: `constexpr shell::SceneKind kCountInSceneKind = FirstApp + 7;` and
  `class CountInScene final : public shell::Scene;`.

**Legacy:** the count-in popup lives in `deluge_notify`'s drain today —
`notify_drain.cpp:92-113` formats `playbackHandler.count_in_visual()` into
`display->displayPopup(buffer, 0, true, 255, 2)` and cancels it at zero, also clearing
`UI_MODE_RECORD_COUNT_IN` with a **masked** compare (the comment there explains why an exact compare is
wrong: `UI_MODE_AUDITIONING` is a flag ORed on top). The count itself moves in
`PlaybackHandler` (`playback_handler.cpp:441-446`).

- [ ] **Step 1: Read `notify_drain.cpp:92-113` and reproduce its four facts exactly**: the digit is
      right-aligned, blink speed is 2, the popup is cancelled at zero, and the mode clear is masked. Write
      them into the scene's doc with citations.

- [ ] **Step 2: Write the failing spec** comparing the port's panel against the legacy popup for counts
      4, 3, 2, 1 and the clear at 0. Legacy drives itself from `playbackHandler`; the port reads the same
      source but through its own call.

- [ ] **Step 3: Implement.** `oled_transparent(Main)` true, `dims_grid()` false, blink through the toolkit's
      `Blink`, `timer_repaints` = `{oled=true, grid=false}`.

- [ ] **Step 4: Quiet-tick the blink** — four ticks with no input must produce the two distinct phases, and
      a golden that repeats its predecessor records nothing.

- [ ] **Step 5: Mutation-prove** the blink phase and the right-alignment.

- [ ] **Step 6: Run the gates and commit.**

---

## Task 7: `ScreensaverScene`, and moving the two animations out of `hid/`

**Files:**
- Create: `src/scenes/overlay/screensaver_scene.{h,cpp}`, `src/ui/oled/rainfall.{h,cpp}`,
  `src/ui/oled/starfield.{h,cpp}`
- Modify: `src/deluge/hid/display/screensaver.{h,cpp}` (point at the moved classes), `src/scenes/registry.{h,cpp}`
- Test: `sim/scenes_spec/screensaver_scene_spec.cpp`, plus a golden for each animation mode

**Legacy:** `hid/display/rainfall.{h,cpp}` (287 + 299 lines), `hid/display/starfield.{h,cpp}` (69 + 126),
`hid/display/screensaver.{h,cpp}` (220 + 108). The shell's **policy** half is already built and shipped —
`shell::Screensaver` (`src/shell/screensaver.h`) ports `Screensaver::noteActivity` and the idle deadline, and
its class doc says in so many words that the animation is this task's. Read that doc first; it also documents
the latch this task removes: **a refused push disables the service for good**, because `SceneKind::Screensaver`
has named no registered scene until now.

- [ ] **Step 1: Move, do not rewrite.** `Rainfall` is a self-contained animation over a canvas, and the logo
      table is transcribed verbatim from the bootloader's `logoPixels` (`rainfall.cpp:32-42`). Move both
      classes into `src/ui/oled/` and adapt the drawing surface to the toolkit's; the *arithmetic* must not
      change. This follows E3 §15.2's extraction pattern: move the file, leave legacy's callers pointing at
      the moved symbol.

- [ ] **Step 2: Prove the move preserved behaviour before building anything on it.** Same seed, same frame
      count, same pixels — compare the moved class's output against the legacy class's, frame by frame, for
      at least one full logo interval. This is the whole safety argument for the move; a golden of the new
      one alone proves nothing.

- [ ] **Step 3: Write the failing scene spec.** The scene reads its seed from `SceneId::arg`, which the shell
      service already passes (`shell/screensaver.h`'s `seed` parameter).

- [ ] **Step 4: Implement the scene.** Both modes (blank, starfield, rainfall) come from the same setting
      legacy reads; find it and read the same one. `oled_transparent` is **false** — the screensaver replaces
      the panel, and legacy renders into its own canvas rather than `main` precisely so waking is "stop
      overriding" (`hid/display/screensaver.h:30-34`).

- [ ] **Step 5: Remove the service's latch note if it no longer applies** — with a real scene registered, the
      "refused push latches the service off" path stops being the documented default. Update
      `shell/screensaver.h`'s doc; do not change its behaviour.

- [ ] **Step 6: Mutation-prove** one animation constant (drop count, speed) against the legacy comparison.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 8: `TimelineState` — the scroll/zoom every clip view shares

**Files:**
- Create: `src/scenes/timeline/timeline.{h,cpp}`
- Test: `sim/scenes_spec/timeline_spec.cpp`

**Interfaces:**
- Produces: `namespace deluge::scenes::timeline { struct TimelineState; }` carrying x-scroll, x-zoom and the
  tick-square state, plus the gesture helpers the two root scenes call.

**Legacy:** `TimelineView` (`gui/views/timeline_view.{h,cpp}`, 82 + 513 lines),
`ClipNavigationTimelineView` (`clip_navigation_timeline_view.{h,cpp}`, 33 + 51),
`ClipView` (`clip_view.{h,cpp}`, 43 + 389). Spec E §5 names this port `timeline.{h,cpp}` (shared) —
"`TimelineState` + helpers".

- [ ] **Step 1: Draw the line between state and view.** These three classes are a base-class chain that mixes
      state with rendering and with `RootUI` duties. Only the state and the pure helpers move here; anything
      that draws belongs to the scene, and anything that is `RootUI` plumbing belongs to spec F. Write the
      three-column table (symbol → destination → why) into the report before writing code.

- [ ] **Step 2: Write the failing spec against legacy's own arithmetic.** Scroll and zoom are integer maths
      with wrap and clamp behaviour; drive `ClipView`'s own methods on a fixture and compare the port's
      answers, each side from its own starting state.

- [ ] **Step 3: Implement the state and the gestures.**

- [ ] **Step 4: The zoom transition is legacy's `PadLEDs` animation and the port does not carry it.** E3
      established this for the marker editor and recorded the ceiling it imposes on comparison
      (`sim/scenes_spec/marker_scene_spec.cpp`'s zoom example — clearing the UI mode does not buy a second
      detent, because the second `zoom()` blocks on the transition state the first left behind). Read that
      note before writing the zoom spec, and record the same ceiling here if it applies.

- [ ] **Step 5: Mutation-prove** one clamp and one wrap, both sides.

- [ ] **Step 6: Run the gates and commit.**

---

## Task 9: `SlicerScene` — state, render and encoders

**Files:**
- Create: `src/scenes/slicer/slicer_scene.{h,cpp}`
- Modify: `src/scenes/registry.{h,cpp}`
- Test: `sim/scenes_spec/slicer_scene_spec.cpp`

**Interfaces:**
- Produces: `constexpr shell::SceneKind kSlicerSceneKind = FirstApp + 8;` and `class SlicerScene final : public shell::Scene;`.

**Legacy:** `Slicer::focusRegained` (`gui/ui/slicer.cpp:59-73`), `Slicer::renderOLED` (`:74-99`),
`Slicer::redraw` (`:100-102`), `Slicer::renderMainPads` (`:103-155`), `Slicer::graphicsRoutine` (`:156-204`),
`Slicer::horizontalEncoderAction` (`:205-232`), `Slicer::verticalEncoderAction` (`:233-247`),
`Slicer::selectEncoderAction` (`:248-275`).

- [ ] **Step 1: Write down the slicer's two modes and its state.** `slicer.h:29-33` carries the preview
      target; the OLED page shows the slice count and mode. One table, cited.

- [ ] **Step 2: Write the failing render spec, comparing the full panel and the pad grid** against legacy's
      `renderOLED` and `renderMainPads`, each side driven from its own sentinel — legacy gets its own
      `focusRegained` and its own encoder turns, not the port's numbers.

- [ ] **Step 3: Implement render.** The waveform on pads goes through `ui::grid::waveform`
      (`src/ui/grid/waveform.h`) — it already exists and is rendering-only, with the scan staying in
      `model/sample`. **`scenes-no-legacy-waveform` forbids naming `waveformRenderer` or
      `waveformBasicNavigator`** (E3 task 12's ratchet row); that split is the point.

- [ ] **Step 4: Implement the three encoder arms.**

- [ ] **Step 5: `graphicsRoutine` is a per-tick redraw, not a timer.** Decide which it is here — if it needs
      a deadline, `next_timer()` plus a `timer_repaints()` mask naming **only** the surfaces it draws.

- [ ] **Step 6: Both geometries, and mutation-prove** one pad column and one OLED field.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 10: `SlicerScene` — pads, preview and the commit

**Files:**
- Modify: `src/scenes/slicer/slicer_scene.{h,cpp}`
- Test: `sim/scenes_spec/slicer_scene_spec.cpp` (extend)

**Legacy:** `Slicer::buttonAction` (`slicer.cpp:276-399`), `Slicer::stopAnyPreviewing` (`:400-408`),
`Slicer::preview` (`:409-434`), `Slicer::runPreviewOp` (`:435-445`), `Slicer::previewForTarget` (`:446-483`),
`Slicer::padAction` (`:484-569`), `Slicer::runDoSliceOp` (`:570-574`), `Slicer::commitSlice` (`:575-595`),
`Slicer::doSlice` (`:596-751`).

- [ ] **Step 1: The preview path touches audio and the card.** `runPreviewOp`/`runDoSliceOp` are deferred
      operations. Read E3's `RecorderScene` first (`src/scenes/recorder/recorder_scene.{h,cpp}`) — it is the
      closest shape on this stack, and it carries the SD-routine guard the final review found missing
      (`AudioEngine::sdRoutineBusy()`): **a scene must not free or commit through the card from the shell
      tick while the SD routine is active.** Decide, in the report, whether `doSlice` has the same hazard,
      with the evidence either way.

- [ ] **Step 2: Write the failing spec for the pad grid's slice selection**, comparing against legacy's
      `padAction` from its own sentinel.

- [ ] **Step 3: Implement pads and buttons.**

- [ ] **Step 4: Implement preview and commit**, with whatever guard step 1 concluded is needed, and a spec
      that fails without it.

- [ ] **Step 5: `spec::QuietAudio` exists for this.** The audio engine's routines segfault in a spec process
      on the input ring; `sim/scenes_spec/support/card_access.h` is the disarm E3 built. Use it rather than
      inventing a second mechanism, and say at the spec site what it disarms.

- [ ] **Step 6: Mutation-prove** the slice count arithmetic, both sides.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 11: `AudioClipScene` — render and navigation

**Files:**
- Create: `src/scenes/audio_clip/audio_clip_scene.{h,cpp}`
- Modify: `src/scenes/registry.{h,cpp}`
- Test: `sim/scenes_spec/audio_clip_scene_spec.cpp`

**Interfaces:**
- Produces: `constexpr shell::SceneKind kAudioClipSceneKind = FirstApp + 9;` and
  `class AudioClipScene final : public shell::Scene;`.
- Consumes: Task 8's `TimelineState`.

**Legacy:** `AudioClipView::opened` (`gui/views/audio_clip_view.cpp:68-77`), `focusRegained` (`:78-90`),
`renderOLED` (`:91-94`), `renderMainPads` (`:95-222`), `timerCallback` (`:223-230`), `renderSidebar`
(`:231-261`), `graphicsRoutine` (`:262-311`), `needsRenderingDependingOnSubMode` (`:312-323`),
`setupScroll` (`:850-856`), `getMaxLength` (`:857-863`), `getMaxZoom` (`:864-870`).

- [ ] **Step 1: The sidebar is a real thing with its own widget.** `ui/grid/sidebar.h` already exists. Use
      it; check its status-pad colour rules against legacy's before assuming they match.

- [ ] **Step 2: Write the failing render spec** — full pad grid including the sidebar, and the OLED page,
      each side from its own sentinel, at both geometries.

- [ ] **Step 3: Implement render**, with the waveform through `ui::grid::waveform` and the position through
      `ui::oled::sample_position` (both already built).

- [ ] **Step 4: `timerCallback` and `graphicsRoutine` are the playhead.** That is time-driven: `next_timer()`
      plus a `timer_repaints()` mask naming only what moves. Quiet-tick coverage, and check the mask against
      what the scene actually draws — arming a surface you do not paint is a permanent blit on the OLED and
      free on the grid (shell spec §15.1, §15.3).

- [ ] **Step 5: Mutation-prove** one waveform column and the playhead position, both sides.

- [ ] **Step 6: Run the gates and commit.**

---

## Task 12: `AudioClipScene` — edits, and the first consumer of a `MenuAction`

**Files:**
- Modify: `src/scenes/audio_clip/audio_clip_scene.{h,cpp}`
- Test: `sim/scenes_spec/audio_clip_scene_spec.cpp` (extend)

**Legacy:** `AudioClipView::buttonAction` (`audio_clip_view.cpp:324-471`), `padAction` (`:472-591`),
`changeUnderlyingSampleLength` (`:592-648`), `changeUnderlyingSampleStart` (`:649-702`), `playbackEnded`
(`:703-706`), `clipNeedsReRendering` (`:707-719`), `sampleNeedsReRendering` (`:720-725`),
`selectEncoderAction` (`:726-739`), `setClipLengthEqualToSampleLength` (`:740-752`), `adjustLoopLength`
(`:753-786`), `horizontalEncoderAction` (`:787-797`), `editClipLengthWithoutTimestretching` (`:798-836`),
`verticalEncoderAction` (`:837-849`).

- [ ] **Step 1: `setClipLengthEqualToSampleLength` is one of the five parked `MenuAction`s.**
      `src/scenes/menu/action.h`'s table names `audioClipView.setClipLengthEqualToSampleLength()` as what
      `MenuAction::set_clip_length` means, and says plainly that **nothing consumes the outbox** and that the
      consumer is the wiring plan's. Implement the *method* here so the future dispatcher has something real
      to call, and add a spec that drives it directly. Do **not** build the dispatcher — that is spec F's, and
      building half of it here would leave a second seam nobody owns.

- [ ] **Step 2: Write the failing edit spec.** Length and start edits write to the model; compare the
      **model** after driving each side from its own sentinel, and delete the port's write once to prove the
      spec can fail.

- [ ] **Step 3: Implement the button and pad arms.**

- [ ] **Step 4: Implement the length/start edits**, including `editClipLengthWithoutTimestretching`, which is
      a distinct arm from `adjustLoopLength`.

- [ ] **Step 5: `clipNeedsReRendering`/`sampleNeedsReRendering` are notifications, not calls.** They map onto
      `Scene::on_model_changed(const notify::DirtySet&)`. Use it; do not add a bespoke callback.

- [ ] **Step 6: Mutation-prove** the length edit and the start edit, both sides.

- [ ] **Step 7: Run the gates and commit.**

---

## Task 13: Level-2 scripts and pixel goldens

**Files:**
- Create: `tests/ui-scripts/shell/param_popup.txt`, `count_in.txt`, `screensaver.txt`, `slicer.txt`,
  `audio_clip.txt` — every script on this stack lives in that one directory, whatever library it drives —
  and one golden directory per script under `tests/golden/ui/shell/<name>/`
- Modify: `sim/scenes_spec/menu_scripts_spec.cpp` (or a sibling in the same target), which is the existing
  level-2 pattern: it builds a fixture, then calls
  `uitest::expect_script_goldens(self, "<name>", scenes::scene_registry(), Services{}, opts)`.
  **Note the `Services{}` argument** — every script on this stack has run with three null services until
  now, and this plan's Tasks 2–5 give it real ones. A script that presses PLAY only means something once
  they are passed in; pass them, and say in the report which scripts changed behaviour as a result.

- [ ] **Step 1: One script per scene, each a real sequence**: boot into the scene, one navigation, one edit
      gesture, one popup — spec E §7's acceptance shape, minus the `LegacySceneAdapter` half that does not
      exist yet.

- [ ] **Step 2: Record the goldens and then LOOK AT EVERY ONE.** `python3 scripts/pbm2png.py` renders them.
      A golden byte-identical to its predecessor records nothing — either the step did nothing or the
      snapshot is in the wrong place. E1 shipped seven self-duplicate goldens and E3 had to carry them.

- [ ] **Step 3: Both geometries** where the script's scene draws differently at 48 and 64 rows. Per-kind
      unit goldens (as opposed to script goldens) go flat in `tests/golden/ui/scenes/<name>_48.pbm` /
      `_64.pbm`, which is where E1–E3's scene goldens live.

- [ ] **Step 4: Commit, with the golden count and what each one shows in the message.**

---

## Task 14: Ratchet rows, README and spec amendments

**Files:**
- Modify: `scripts/check_layer_rules.py`, `src/scenes/README.md`, the three design specs

- [ ] **Step 1: Add the ratchet rows this plan's boundaries need**, each with a zero baseline, each
      **probed separately**: make the violation the row forbids, confirm the checker exits 1 and names the
      right file and rule, then revert. A row recorded as "probe-verified" whose probe tested a different
      alternative is worse than no row — that happened in E2. Candidates: the services must not name
      `getCurrentUI`, `soundEditor` or any `gui/` view global; the new scenes must not name
      `waveformRenderer`/`waveformBasicNavigator` (extend the existing `scenes-no-legacy-waveform` globs
      rather than adding a parallel row).

- [ ] **Step 2: Confirm the baseline only shrank.** If any count moved, stop and report it rather than
      re-freezing.

- [ ] **Step 3: README.** Document the three services and where they live *and why they are not in
      `src/shell/`* (the `shell-includes-outside` row — this is the architectural fact a contributor will
      otherwise get wrong); the four new scene kinds; what the screensaver move did; and update the carry
      list — gold-knob acceleration and the mod-matrix blink hook are closed or moved by this plan.

- [ ] **Step 4: Spec amendments.** Scenes spec: a new "Amendments closing E4" section recording every ruling
      this plan made and every divergence it found. Shell spec: the service table's three rows now name real
      classes in another library — record that. Migration spec: update §10.2's blocker list (this plan does
      not remove a blocker, but it does supply `set_clip_length`'s callee) and §10.4's carry list.

- [ ] **Step 5: Record what the oracle covers**, in the shape §10.6 established: entry points driven from
      their own sentinel, comparison sites, and an honest uncovered list. That number is what the legacy
      deletion gets scheduled against.

- [ ] **Step 6: Run every gate one final time and commit.**

---

## Spec coverage (self-review)

| Spec requirement | Task |
|---|---|
| Shell spec §5 `GlobalControls` row | 4 |
| Shell spec §5 `ModKnobs` row (incl. `ParamPopupScene`, acceleration, indicators) | 1, 2, 3 |
| Shell spec §5 `MidiLearn` row | 5 |
| Shell spec §5 `Screensaver` row — the scene half | 7 |
| Scenes spec §6 overlays: `CountInScene`, `ParamPopupScene`, `ScreensaverScene` | 6, 1, 7 |
| Scenes spec §5 `timeline.{h,cpp}` (shared) | 8 |
| Scenes spec §5 `AudioClipScene` "split by concern" | 11, 12 |
| Scenes spec §7 row 4: `SlicerScene`, `AudioClipScene` | 9, 10, 11, 12 |
| Scenes spec §8 testing (per-kind goldens, both geometries, level-2 scripts) | every task; 13 |
| Migration spec §10.4 carry: gold-knob acceleration | 2 |

**Not in this plan, deliberately:** `KeyboardScene` (5,252 lines — E5) and `PerformanceScene` (2,249 — E5 or
E6); the load/save browser family (§10.4, phase 6); spec F's wiring, all six blockers of §10.2. Also **not**
resolved here: the four inventions the 2026-09-06 audit found still in the tree (`Shell::dim_grid`'s overlay
dim, `GestureEvent::Hold`, the widened `SecondPad`) and the two legacy bug-fixes awaiting a ruling. They are
recorded and are Kate's call; a task here must not quietly remove or extend any of them.
