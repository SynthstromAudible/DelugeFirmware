# UI migration and coexistence

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec F of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md` §12). Depends on specs B–E and G for the shapes
it adapts between.

Naming: `snake_case` functions/variables; `PascalCase` types.

## 1. Why

97k lines of UI cannot be replaced in one step, and the project is a living fork with
community PRs landing continuously. The migration must keep the firmware shippable after
every PR, keep in-flight contributor branches mergeable, and give a mechanical, visible
measure of progress. This spec fixes the two coexistence topologies, the shared-buffer
rules, the façade/codemod/ratchet mechanics, the branch strategy, and the deletion
schedule.

Divergence facts (2026-08-26): the `main`↔`next` merge-base is 2026-08-10; since then
`main` changed 21 UI files in 16 commits (mostly l10n strings, `instrument_clip_view.cpp`,
`browser.cpp`). The often-quoted "367 commits behind" is almost entirely non-UI; UI
reconciliation is cheap.

## 2. Two topologies, in sequence

### 2.1 Phase 4 — shell as guest

`ShellHostUI : UI` is one legacy `UI` registered like any other and pushed with
`openUI(&shell_host_ui)`. Inside it a full `Shell` runs its own scene stack.

| Legacy hook | Guest behaviour |
|---|---|
| `padAction`, `buttonAction`, `*EncoderAction`, ~~`modEncoderAction`~~ | forwarded into the shell's `InputLowering` — **but not `modEncoderAction`, and legacy `Buttons::` state cannot be read into `UiContext` at all** (F1 task 6; see the corrections below) |
| `renderOLED(Canvas&)` | the shell's `Main` surface ~~aliases~~ **serves** that canvas's storage (F1 task 4; see §3) |
| `renderMainPads(rows, image, mask)` / `renderSidebar(...)` | the shell's grid surface ~~aliases~~ **serves** `image` row by row; returns the top scene's opacity (`!is_grid_overlay()`) |
| `timerCallback()` | the shell's `tick`; `UI_SPECIFIC` re-armed to `next_wake_` — **right, and incomplete: `next_wake_` is an absolute deadline and `setTimer` takes a duration, and the guest must also arm from four hooks legacy disarms it from** (F1 task 7; see the corrections below) |
| `opened()` / `focusRegained()` | ~~`Shell::push(initial)`~~ → `Shell::init(SceneId)` / `on_resume` on ~~the shell's top~~ → `SceneStack::input_top()` (F1 task 5; see the corrections below) |
| `canSeeViewUnderneath()` | ~~top scene's `oled_transparent(Main)`~~ → **`!top().occludes_grid()`** (F1 task 5 — copying the struck-through form ships a regression; see the corrections below) |
| `exitUI()` | ~~`Shell::pop()`~~ → `SceneStack::pop()`; ~~returns `NOT_DEALT_WITH` … so `closeUI` pops the guest~~ → the guest calls `close()` itself when the pop empties the stack and returns `ACTIONED_AND_CAUSED_CHANGE` (F1 tasks 5 and 6; see the corrections below) |

LEDs: ~~the guest's `derive_leds` writes through an `indicator_leds` `LedSink`~~ → **`Shell::flush`
writes through `SurfaceSet::set_led`, which `GuestSurfaceSet` implements over
`indicator_leds::setLedState`** (F1 tasks 4 and 7; there is no `LedSink` and `derive_leds` is the
shell's, not the guest's — see the corrections below); the guest
owns an LED only while one of its scenes declares it, so legacy and guest never fight.
Popups raised by the model while the guest is up still go through `display->` until
phase 5; the guest's `Notices` service is idle in phase 4.

First tenant: `MenuScene`, entered from every place `soundEditor` is entered today
(`openUI(&soundEditor)` → `openUI(&shell_host_ui)` + `push(Menu{node})`).

**Corrections (F1 task 5, 2026-09-07): four rows of the table above are wrong, and two of them would
have shipped a visible regression.** `ShellHostUI` is now written; each correction is at the code, in
the hook it belongs to.

- **`canSeeViewUnderneath()` is not `oled_transparent(Main)`** — and §2.2's `LegacySceneAdapter` row,
  which states the same equivalence in the other direction, is wrong for the same reason. Legacy keeps
  OLED transparency in a *data member*, `UI::oledShowsUIUnderneath` (`gui/ui/ui.h:158`), set false by
  `UI::UI` (`gui/ui/ui.cpp:31-33`) and read by `doAnyPendingOLEDRendering` to decide how far down the
  stack to start drawing (`gui/ui/ui.cpp:353-356`). `canSeeViewUnderneath()` gates something else
  entirely: whether the **root view** is still live underneath — `UI::graphicsRoutine` runs the root's
  per-frame animation only when it is true (`gui/ui/ui.cpp:47-51`), and `Session::armingChanged` re-arms
  the play flash through it (`playback/mode/session.cpp:1295`). `Slicer` holds the two at opposite
  values on one object — `oledShowsUIUnderneath = true` at `gui/ui/slicer.h:35`,
  `canSeeViewUnderneath() { return false; }` at `:38` — which is proof in the shipped tree that they are
  independent questions. The guest maps it to `!top().occludes_grid()` (`shell/scene.h:386-387`).
  **The regression the wrong mapping would have shipped:** `MenuScene::oled_transparent` is false
  (`scenes/menu/menu_scene.h:464`) where `SoundEditor::canSeeViewUnderneath` is true
  (`gui/ui/sound_editor.h:121`), so the clip view under an open menu would have stopped animating.
- **`exitUI()`'s return value pops nothing.** The row says it returns `NOT_DEALT_WITH` on an empty
  shell stack "so `closeUI` pops the guest". Neither of its two readers does that, and every legacy
  implementation closes *itself* — `RenameUI::exitUI` (`gui/ui/rename/rename_ui.cpp:88-91`),
  `SampleMarkerEditor::exitUI` (`gui/ui/sample_marker_editor.cpp:680-683`), `Browser::exitUI`
  (`gui/ui/browser/browser.h:133-136`), and `SoundEditor::exitCompletely`'s `close()` at
  `gui/ui/sound_editor.cpp:761`. The guest therefore calls `close()` when the pop empties its stack; a
  guest that only returned the value would trap the user inside itself.

  **The two readers, measured** (`grep -rn "exitUI" src/ | grep -v exitUIMode` — 26 lines; an earlier
  version of this bullet said "exactly one caller", which was wrong, and a correction whose authority is
  its measurement must not carry an unmeasured claim):

  1. `gui/ui_timer_manager.cpp:150-153` (`TimerName::BACK_MENU_EXIT`) stores the result and tests it
     against `REMIND_ME_OUTSIDE_CARD_ROUTINE` alone, to re-arm the timer. No `closeUI` in that path.
  2. `gui/ui/keyboard/layout/column_controls.cpp:89` tests it against `ACTIONED_AND_CAUSED_CHANGE` and
     on a match calls `keyboardScreen.killColumnSwitchKey(LEFT_COL)` — the keyboard sidebar's menu-exit
     pad, behind `RuntimeFeatureSettingType::EnableKeyboardViewSidebarMenuExit` (`:86-87`).

  **Two divergences follow, recorded rather than fixed, because both are inert in phase 4 and neither
  is:**

  - ~~The guest returns `NOT_DEALT_WITH` where every legacy self-closing `exitUI` returns
    `ACTIONED_AND_CAUSED_CHANGE`.~~ **Closed by F1 task 6: the guest now returns
    `ACTIONED_AND_CAUSED_CHANGE` on the close path** (`SoundEditor::exitUI` is `exitCompletely()`,
    `gui/ui/sound_editor.h:130`, which returns that value at `gui/ui/sound_editor.cpp:771`, and
    `gui/ui/ui.h:152-153` says the hook "returns whether a UI exited"). Task 5's stated trigger for
    the change was itself wrong and is corrected here: **routing pads into the shell does not make
    reader 2 reachable.** `MatrixDriver::padAction` calls `getCurrentUI()->padAction` and nothing else
    (`hid/matrix/matrix_driver.cpp:79`), and `ColumnControlsKeyboard::evaluatePads` has **one route
    in, not one caller** — six call sites, five of them subclasses chaining up
    (`layout/piano.cpp:39`, `layout/in_key.cpp:37`, `layout/isomorphic.cpp:40`,
    `layout/chord_keyboard.cpp:59`, `layout/chord_library.cpp:57`), with only
    `gui/ui/keyboard/keyboard_screen.cpp:205` entering from outside the hierarchy — so the only route
    to it while the guest is current is
    the guest forwarding the pad on the way `SoundEditor::padAction` does when the keyboard screen is
    the root (`gui/ui/sound_editor.cpp:1457-1460`) — which the guest does not do. The value changed on
    the legacy-fidelity argument, not on a reachability one.
  - The guest can never return `REMIND_ME_OUTSIDE_CARD_ROUTINE`, which `exitCompletely` does when the
    settings menu is leaving while `isSDRoutineActive()` (`gui/ui/sound_editor.cpp:733-747`, the return
    at `:698`). That is the tail of a larger gap: leaving the settings menu is also what writes
    `FlashStorage`, the MIDI devices file, the runtime-feature settings and the MIDI-follow defaults
    (`:703-709`), and nothing on the guest's exit path does any of it. The guest cannot know it is in a
    settings root, so this belongs with F1 task 9, which maps the fourteen callers to `SceneId`s and is
    the first place that does.
- **`opened()` is `Shell::init(SceneId)`, not `Shell::push(initial)`.** There is no `Shell::push`;
  `init` is `stack_.push(initial)` with the result dropped (`shell/shell.cpp:226-228`), which is why the
  guest checks the depth afterwards rather than trusting the call.
- **"aliases" is "serves", in both render rows**, following F1 task 4's §3 correction: the guest copies
  the shell's Main surface into the canvas legacy hands `renderOLED`, and copies the shell's grid rows
  into `image`. §3's amendment has the evidence.

**Corrections (F1 task 6, 2026-09-07): the input row is wrong in two independent ways.**
`ShellHostUI`'s input hooks are now written; both corrections are at the code.

- **`modEncoderAction` must NOT be forwarded into `InputLowering`, and neither may the other two mod
  hooks.** The shell's encoder entry points take **raw A-pin edges** (`Shell::on_encoder_edges`,
  `shell/shell.h:294`), while legacy has already turned a gold knob's edges into an accelerated
  magnitude before it calls: `interpretEncoders` passes `offset * encoder.calcNextKnobSpeed(offset)`
  (`hid/encoder_input.cpp:192-194`) where `calcNextKnobSpeed` returns `clamp(speed, 1.0, 3.0)`
  (`hid/encoders.cpp:76`). **The lowering does not re-accelerate it — `ModKnobs` does.** The
  `EncoderKind::Accelerating` branch is `delta = edges`, and its own comment says acceleration "is
  applied by whoever consumes it, through `calc_next_knob_speed`"
  (`shell/input_lowering.cpp:187-193`); the one consumer is `scenes/services/mod_knobs.cpp:274`. So
  the second multiplication is real but lands a layer lower than the lowering. **And the factor is
  ~18x, not 9x**: the Accelerating branch does not divide by `edges_per_detent`, only the `Detented`
  one does (`shell/input_lowering.cpp:178-186`), so `feedDetents`' ×2 survives intact and `e` raw
  edges arrive as `2 · e · s_legacy · s_shell` where legacy delivers at most `3 · e`. A forwarded
  turn additionally picks up the shell's first-turn-after-a-pause debounce
  (`shell/input_lowering.cpp:191`), which legacy's own copy cannot reach
  (`hid/encoder_input.cpp:187-202`; the lowering records the same dead branch at
  `shell/input_lowering.cpp:206-213`). The gold knobs need F1 task 8's `ModKnobs` **plus** a shell
  entry point that accepts an already-lowered turn.

  **Leaving them on the base is not `SoundEditor` parity, and the difference is user-visible.**
  `SoundEditor` overrides two of the three: `modEncoderAction` (`gui/ui/sound_editor.cpp:1621-1645`)
  reaches `UI::modEncoderAction` only from its innermost `else` (`:1604`), above which sit the
  `automationView` redirect (`:1584-1586`) and the `UI_MODE_MIDI_LEARN` →
  `getCurrentMenuItem()->learnKnob(...)` arm (`:1589-1601`); `modEncoderButtonAction` has the same
  shape minus the learn arm (`:1609-1616`). Only `modButtonAction` is genuinely untouched by
  `SoundEditor`. So **under the toggle, MIDI-learning a menu parameter to a gold knob silently stops
  working.** Not closable in task 6: both arms ask about `currentUIMode` and
  `getCurrentMenuItem()` — `SoundEditor`'s own navigation record, stale while the guest is what is
  open. Task 6 recorded the port's answer as the routed-to-`MenuScene` path `ModKnobs` names as the
  thing its service ordering exists to protect (`scenes/services/mod_knobs.h:55-77`), needing the
  service installed *and* the gold turn routed — "both halves of task 8". **Task 8 installed the
  service and found that the other half would not have helped: there is no destination.** Legacy's
  arm calls `getCurrentMenuItem()->learnKnob(...)`, and `MenuItem::learnKnob`
  (`gui/menu_item/menu_item.h:181`, implemented by `MenuItemWithCCLearning::learnKnob`,
  `gui/menu_item/menu_item_with_cc_learning.cpp:43-57`) **has no port at all** —
  `grep -rn "learn_knob" src/scenes` is empty. `MenuScene` carries `learn_note_on`, `learn_cc` and
  `learn_program_change` (`scenes/menu/menu_scene.h`), which are `SoundEditor`'s three MIDI-*message*
  members (`sound_editor.cpp:1588-1608`); the knob arm is a fourth and was never transcribed.
  Closing it is a new `CustomItem` hook plus a port of `soundEditor.currentModControllable`, i.e.
  scenes-layer work, and it *also* still needs the shell entry point for an already-lowered turn.
  Recorded as a divergence with the real condition on it, not claimed as parity.
- **"legacy `Buttons::` state is *read* into `UiContext`, not duplicated" is not implementable.**
  `UiContext` is built by `Shell::make_context`, which is private and fills `.shift`/`.sticky` from
  `InputLowering`'s own fields (`shell/shell.cpp:567-577`); `InputLowering` has no shift setter, only
  `clear_shift_sticky()` (`shell/input_lowering.h:214`) and `set_shift_is_sticky()` (`:221`). There
  are therefore two shift state machines, and the guest's only lever is which events it feeds. They
  stay in step because the guest feeds every button and every pad legacy hands it and
  `InputLowering::toggle_shift` is a line-for-line port of `Buttons::commandToggleShift`
  (`shell/input_lowering.cpp:306-326` against `hid/buttons.cpp:237-278`) — except in three measured
  places, of which the first is **a release blocker for the toggle**:

  1. ~~The sticky-shift feature flag.~~ **Fixed in task 6, and an earlier version of this bullet
     deferred it to task 8 for a reason that was false.** `InputLowering::set_shift_is_sticky` exists
     (`shell/input_lowering.cpp:334-341`) precisely because "the settings menu toggles it live"
     (`shell/input_lowering.h:29-30`), and `Shell::lowering()` is public and non-const
     (`shell/shell.h:383`) — so nothing about `LoweringConfig` being construction-time applies to
     *this* field. `ShellHostUI::opened()` now seeds it from
     `runtimeFeatureSettings.get(ShiftIsSticky)`, and `ShiftIsSticky::writeCurrentValue`
     (`gui/menu_item/runtime_feature/shift_is_sticky.cpp:23-49`) mirrors it live, alongside the
     `Buttons::clearShiftSticky()` it already had to pair with. Left unfixed this was a real
     regression: `ctx.shift` is the whole gate on the menu's shortcut-pad jump
     (`scenes/menu/menu_scene.cpp:1406`), so a sticky-shift user would have lost shortcut pads under
     the toggle.
  2. ~~**`hold_time_ms` is the one that genuinely is construction-time.**~~ **Closed by F1 task 8.**
     The tap threshold is `FlashStorage::holdTime` in legacy — audio samples, re-read *live* at
     shift-release time (`hid/buttons.cpp:264`) — and `LoweringConfig::hold_time_ms` in the shell.
     Task 6 deferred it because `set_shift_is_sticky` was the only `LoweringConfig` mutator
     `InputLowering` exposed; task 8 added `set_hold_time_ms` beside it, on the argument that these
     two fields are settings while the other two (`short_press_ms`, `edges_per_detent`) are a
     constant and a board fact. `ShellHostUI::feed()` converts and seats it on **every** pad and
     button, which is where legacy's own read happens — not once per open, because
     `nodes::set_default_hold_time` (`scenes/menu/nodes/settings.cpp`) is the ported settings item
     and writes the flash global from *inside* the guest.
  3. Legacy clears `considerShiftReleaseForSticky` on *every* button press including the eight mod
     buttons (`hid/buttons.cpp:88-95`), but `Buttons::buttonAction` intercepts those and never calls
     the current UI (`:99-107`), so the shell never sees `ButtonId::Mod0`..`Mod7` at all.

**Two more facts about the input row that the table cannot carry** (F1 task 6):

- **The hooks feed; they do not answer.** Routing happens in `Shell::pump_input`, which only
  `Shell::tick` calls (`shell/shell.cpp:579-589`), so no hook can report what a scene did with a
  press and every return value is decided statically. `buttonAction` returns `NOT_DEALT_WITH`, which
  is what keeps `Buttons::buttonAction`'s tail running: `commandToggleShift` (`hid/buttons.cpp:169-171`),
  the transport, and the mod-encoder-button dispatch at `:226-231`, which is itself gated on that
  value (`:134-141`). Note also that the long-press-Back timer is **not** in that tail — it is armed at
  `:114-121`, above the `getCurrentUI()->buttonAction(...)` call at `:134` and independent of what it
  returns, correcting a claim `ShellHostUI` used to carry.
- **The `int8_t` edge ceiling is real and the guest saturates at it.** `Shell::on_encoder_edges` takes
  an `int8_t`, so at the Deluge's two edges per detent a turn of more than 63 detents wraps negative
  (`shell/input_lowering.h:407-430`). The guest *can* be handed more: legacy's `detentDelta`
  accumulates in `DetentedEncoder::pos` across every pass `interpretEncoders` skips during the SD
  routine (`hid/encoder_input.cpp:96-106`) or a stem export (`:66-68`). So the guest clamps to
  ±(127 / `edges_per_detent`) **before** multiplying — legacy's own choice for tempo and select
  (`hid/encoder_input.cpp:117`) and a divergence from it for horizontal and vertical, which legacy
  hands the full `int32_t` (`:124`, `:140`).

The rows that held up unchanged: `renderMainPads`/`renderSidebar` really do return the top scene's
opacity, and §2.2's dual for it (`is_grid_overlay()` is `!renderMainPads()`) is the mapping the guest
implements; `focusRegained` really is `on_resume`, with the one refinement that it goes to
`SceneStack::input_top()` rather than `top()`, because that is what `Scene::on_resume`'s own contract
says (`shell/scene.h:359-374`) and what the stack itself does (`shell/scene_stack.h:38-46`);
`getGreyoutColsAndRows` really is the top scene's `greyout_mask()`, with `doGreyout == false` spelled
as an all-zero mask (`scenes/menu/menu_scene.cpp:1472-1504`).

**One phase-4 topology defect the table does not mention and cannot be read off it: the greyout is
applied twice.** Legacy greys once, in the send path — `prepareColour` (`hid/led/pad_leds.cpp:283-298`,
the tint at `:293-296`) reads the masks `reassessGreyout` took from `getUIGreyoutColsAndRows`
(`:734-735`) and never mutates `PadLEDs::image`. The shell has no send stage to hook, so it greys its
own grid in place (`Shell::render_grid`, `shell/shell.cpp:782-788`, over the whole stack's OR at
`:328-334`); those already-greyed colours then reach the pads through `GuestSurfaceSet::send_columns`,
which writes `image` and calls `sortLedsForCol` — back through `prepareColour`, which greys them again
for whatever the guest just reported. A greyed pad is darker under the guest than under `SoundEditor`.
Answering `false` here is not the fix: `gui/ui/ui.cpp:73-83` would then take the mask from the UI
*underneath*. The fix is a shell seam separating compose from tint, or the guest reporting an empty
mask while the shell owns the tint end to end; it is recorded at `ShellHostUI::getGreyoutColsAndRows`
and belongs with the tick (F1 task 7) or the acceptance round. Phase 5 dissolves it, since
`prepareColour` goes with the rest of the legacy send path.

**Corrections (F1 task 7, 2026-09-07): the timer row is right and incomplete, and the LED sentence
names a type that does not exist.** `ShellHostUI::timerCallback` is now written; each correction is
at the code.

- **`next_wake_` is an absolute deadline; `setTimer` takes a duration.** The row does not say which,
  and the two are different units. `Shell::next_wake()` returns `std::optional<uint32_t>`, an
  absolute millisecond clock recomputed at the bottom of every tick (`shell/shell.h:378`,
  `shell/shell.cpp:533`); `UITimerManager::setTimer(which, ms)` is a duration and converts it with
  `ms * 44` into audio samples (`gui/ui_timer_manager.cpp:253-255`). The guest converts with the
  wrap-safe difference `(int32_t)(deadline - now)` — the form `compute_next_wake` and `RepaintGate`
  both use, because a plain `<` across the ~49.7-day `uint32_t` wrap picks the later of two
  straddling deadlines — and clamps it to 1 ms … `INT32_MAX / 44`. The upper clamp is arithmetic, not
  policy: above it `ms * 44` is signed overflow. Arming *earlier* than asked is always safe, because
  the next tick recomputes `next_wake()` from scratch.
- **A deadline of `now_ms` must not arm zero.** `Scene::next_timer()` may answer 0, meaning "next
  tick" (`shell/scene.h`), and `AudioClipScene::next_timer()` answers it for the whole of playback
  (`scenes/audio_clip/audio_clip_scene.cpp:1193-1199`), so `next_wake()` reports `now_ms` on every
  tick for as long as that scene is stacked. **The floor of 1 ms is the smallest duration `setTimer`
  can express that leaves `triggerTime` strictly in the future** (`ms * 44`, so 44 samples), and that
  is the whole argument: `setTimer(UI_SPECIFIC, 0)` puts `triggerTime` at exactly
  `AudioEngine::audioSampleTimer` (`gui/ui_timer_manager.cpp:257-266`), which the audio engine leaves
  behind before `UITimerManager::routine` is next called at all, so the whole 29-timer sweep and the
  shell's whole tick would run on every pass of the main loop, for ever. (Precisely at the instant of
  arming the difference is 0 and `routine`'s `>= 0` guard *does* return early, `:54-57`; the argument
  rests on the sample timer advancing between two calls, not on that guard failing. An earlier draft
  of this bullet said a zero arm "defeats" it, which is false at exactly the boundary it is about.)
- **Legacy's per-frame cadence is 15 ms, not the main loop's rate**, and three docs in this stack
  said otherwise (`shell/scene.h`'s `next_timer`, `shell/shell.h`'s `next_wake`,
  `scenes/audio_clip/audio_clip_scene.h`'s `next_timer`, all corrected in place).
  `grep -rn "graphicsRoutine()" src/deluge` gives exactly one call site outside the `UI` hierarchy:
  `TimerName::GRAPHICS_ROUTINE`, re-armed at 15 ms (`gui/ui_timer_manager.cpp:213-218`). The guest
  keeps the 1 ms floor anyway, because the first tenant (`MenuScene`) has no zero-timer arm while a
  15 ms floor would put 15 ms of latency on the repaint after *every* press — which legacy does not
  have, since `doAnyPendingUIRendering` runs from the main loop (`gui/ui/ui.cpp:375-396`).
- **The row omits that legacy disarms `UI_SPECIFIC` from under its owner**, at
  `changeUIAtLevel` (`gui/ui/ui.cpp:92`), `changeRootUI` (`:112-114` — the one conditional clear, on
  `currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW`), `closeUI` (`:197`) and `openUI` (`:233`) —
  each of them immediately before the `opened()`/`focusRegained()` that follows. Since
  nothing inside the guest runs except `Shell::tick`, a guest that armed only in `opened()` would
  freeze the moment any UI opened over it and closed again. The guest therefore arms from `opened()`
  (success path only), `focusRegained()`, `displayOrLanguageChanged()`, `exitUI()`'s survive path and
  all five input hooks — and deliberately **not** from `exitUI()`'s close path, where `closeUI` is
  about to unset it and where an arm would deliver this guest's callback to the UI beneath.
- **There is no `LedSink`.** `grep -rn LedSink src/` has no hits. `derive_leds` is a private member
  of `Shell` (`shell/shell.h:507`), not of the guest, and it does not write anything: it composes the
  frame and fills the `desired_*` shadows (`shell/shell.cpp:867-886`), and `Shell::flush` is what
  calls `SurfaceSet::set_led` (`shell/shell.cpp:995-1021`). The `indicator_leds` half of the sentence
  is right and was already built by F1 task 4 (`GuestSurfaceSet::set_led`). What task 7 adds is the
  *tick*, without which none of it ever runs.
- **The ownership rule holds, and it is one-sided in the guest's favour.** An id no scene declared
  fails `led_send_needed`'s `desired_declared` test (`shell/leds.cpp:75-80`), so no number of ticks
  can write it; an id a scene does declare is taken away from legacy's blinker, because
  `setLedState`'s default `allowContinuedBlinking = false` calls `stopLedBlinking`
  (`hid/led/indicator_leds.cpp:42-52`, `:141-149`). The handover back is coherent for LEDs
  (`setLedState` keeps `ledStates[]` true) and **not** for knob indicators, whose
  `knobIndicatorLevels[]` shadow `GuestSurfaceSet::set_indicator` cannot update — but that is not
  live in phase 4 as built: the only `frame.indicator(...)` declarer in the tree is `ModKnobs`
  (`scenes/services/mod_knobs.cpp:687`), a service, and the guest is constructed with `Services{}`.
  F1 task 8 is what makes it live, and `indicator_leds::clearKnobIndicatorLevels()` on the close path
  is the fix that belongs with it.
- **The double greyout above is NOT the tick's, and is re-homed.** Task 5 recorded it as belonging
  "with the tick (F1 task 7) or the acceptance round". Having written the tick: the tick is what
  makes it *observable* for the first time — before it, nothing sent a pad at all — but it neither
  causes nor can fix it. Both fixes are changes to `src/shell` (a seam separating compose from tint,
  or a `ShellConfig` flag saying the host owns the tint), i.e. shared plan-D code four tasks in this
  plan depend on, and the defect is a *brightness*, whose oracle is the level-3 A/B picture. It
  therefore belongs to **F1 task 11**, the acceptance round, and not to the tick.

### 2.2 Phase 5 — shell as host

The scheduler task is `Shell::tick`. `deluge.cpp`'s `dispatchInputEvent`,
`doAnyPendingUIRendering`, `uiTimerManager.routine` and the UI half of
`readButtonsAndPads` are removed. Every remaining legacy `UI` runs through
**`LegacySceneAdapter : Scene`** — one class, one instance per legacy UI
(`SceneId::Legacy{UIType}`):

| Scene hook | Adapter behaviour |
|---|---|
| `handle(UiEvent)` | `padAction` / `buttonAction(b, on, /*inCardRoutine=*/false)` / `*EncoderAction`; a `REMIND_ME_OUTSIDE_CARD_ROUTINE` return re-queues the event (the shell already deferred while `sd_busy()`, so this is rare) |
| `render(Frames&)` | `renderOLED(canvas)`; `renderMainPads/renderSidebar(rows, image, mask)` into the shell's surfaces (rows from the dirty mask) |
| `leds(LedFrame&)` | read-only bridge of `indicator_leds`' shadow state |
| `on_enter` / `on_resume` / `on_exit` | `opened()` / `focusRegained()` / nothing |
| `is_grid_overlay()` | `!renderMainPads()` (today's opacity query) |
| `oled_transparent(Main)` | ~~`canSeeViewUnderneath()`~~ → `oledShowsUIUnderneath` (F1 task 5; see §2.1's corrections) |
| `occludes_grid()` | `!canSeeViewUnderneath()` (F1 task 5; the row above used to claim this one's source) |
| `greyout_mask()` | `getGreyoutColsAndRows()` |
| `tick(dt)` | drives the adapter's `Timer` for `UI_SPECIFIC` → `timerCallback()` |

`openUI/closeUI/changeRootUI/changeUISideways/getCurrentUI/getRootUI` become shims over
the stack ops (`getRootUI()` returns the root adapter's `UI*` or `nullptr` for a new
scene), so unmigrated UIs that navigate keep working. `uiTimerManager` survives as a shim
inside the adapter until the last legacy UI goes. `View view`'s services are replaced by
the shell's in this phase; legacy UIs call the shell's `ModKnobs`/`GlobalControls` through
`view.` façade methods.

## 3. Shared framebuffers

Formats are byte-identical, so aliasing is free and copy-less:

- Phase 4: the guest's `Main` surface *is* `OLED::main`'s storage (`bytes()`); the grid
  surface's rows alias `PadLEDs::image` via `PadGrid::row(y)`; the guest calls
  `OLED::markChanged()` and `sendOutMainPadColours()` instead of its own flush.
- Phase 5: `OLED::main` and `PadLEDs::image` become façade names for the shell's surfaces;
  `sendMainImage`, `sortLedsForCol`, `copyBackgroundAroundForeground` and the three static
  canvases are deleted; the shell flushes.

`PadLEDs::occupancyMask` (used by legacy sidebar rendering) is kept as an adapter-private
buffer until phase 7.

**Correction (F1 task 4, 2026-09-07): the phase-4 bullet above is wrong, and the guest copies.**
The formats *are* byte-identical — that much held up, and `shell_guest_surfaces.cpp` `static_assert`s
it — but neither aliasing claim survives contact with the code:

- **The grid cannot alias at all.** `gfx::grid::PadGrid` stores `cells_[kMaxGridRows][kMaxGridCols]`
  inline, a 32-column row stride, while `PadLEDs::image` is
  `[kDisplayHeight][kDisplayWidth + kSideBarWidth]`, an 18-column stride. `pad_grid.h:38-41` had
  already said so in as many words: `row(y)` is the seam for **copying** a single row to or from
  that legacy buffer, "never reinterpret the whole `cells_` array as one contiguous legacy image".
  There is no version of `PadGrid::row(y)` that points into `PadLEDs::image`; the storage is a
  member, not a span.
- **The OLED could alias, but must not.** `OLED::main` is an `OwnedCanvas` over its own 768-byte
  store, and `OLED::clearMainImage()` zeroes it from seventeen legacy call sites — including
  `doAnyPendingOLEDRendering` (`gui/ui/ui.cpp:351-371`), which clears it immediately before asking
  each open UI to re-render. The shell is retained-mode: `Shell::flush` blits only a surface whose
  `blit_pending` a repaint raised. An aliased buffer that legacy zeroed between two shell renders
  would go blank with nothing to tell the shell to paint it again.

So `SurfaceSet::build`'s own `storage_[]` and `grid_storage` stay as they are — **no shell seam for
borrowed storage was added** — and the copy is measured rather than waved through: **768 B per OLED
blit** and **48 B per changed column pair**, 432 B for a full-grid repaint (exactly
`sizeof(PadLEDs::image)`). All three are `static_assert`ed at the code as
`GuestSurfaceSet::kOledCopyBytesPerBlit`, `kGridCopyBytesPerColumnPair` and
`kGridCopyBytesPerFullFlush`.

**Stated as per-tick costs, not per-second rates — corrected in the task's review round, and the
correction is the interesting part.** The first version of this paragraph quoted "≤ 21.6 kB/s at the
fastest grid gate, 20 ms". There is no such ceiling, and two shipped scenes already exceed it:
`RecorderScene` polls at `kRecorderPollMs = 10` (`src/scenes/recorder/recorder_scene.h:180`) and does
not override `timer_repaints()`, so it takes `Scene`'s `{.oled = true, .grid = true}` default
(`src/shell/scene.h:629`) — a 10 ms gate on **both** surfaces; and `AudioClipScene::next_timer()`
returns `0u` for as long as the playhead is live
(`src/scenes/audio_clip/audio_clip_scene.cpp:1199`) with `{.grid = true}`
(`audio_clip_scene.h:477`), so the grid gate arms every tick and no millisecond figure bounds it at
all. Sweeping the tree (14 `next_timer()` overrides against 6 `timer_repaints()` overrides) there is
no cadence ceiling to quote for either surface.

The bounds that are actually true:

- **Grid: the per-pair diff, not a cadence.** `Shell::flush` compares each pair against `last_sent`
  before calling `send_columns` at all (`src/shell/shell.cpp:970-986`), so a tick in which no pad
  changed colour costs **zero bytes**, however fast the ticks come. 432 B is the price of a full
  repaint, not of a tick.
- **OLED: per tick that rendered, because there is no diff.** `flush` blits a surface
  unconditionally once it has rendered (`src/shell/shell.cpp:453-460` is the ruling that says so, and
  says why the OLED half may not over-arm the way the grid half may). 768 B per re-rendered tick,
  bounded only by the host's tick rate; the 10 ms `RecorderScene` gate gives 76.8 kB/s as a **floor**
  for a busy panel, not a ceiling.

For scale, legacy already memcpys up to that whole OLED image per frame in
`copyBackgroundAroundForeground` whenever a console or popup is up, and `sortLedsForCol` already
copies those same 16 colours per pair it sends.

Phase 5's bullet is unaffected: it deletes the legacy buffers rather than aliasing them.

## 4. Façades, codemods, removal dates

- Every façade carries `// FACADE (remove after UI phase N): forwards to …` and a row in
  `docs/dev/ui_migration_facades.md` with its removal phase and the ratchet counter that
  gates it.
- Mechanical rewrites ship as scripts under `scripts/codemods/` (libclang-based where
  semantics matter, regex where safe), each idempotent and documented with before/after:
  `display->displayPopup(...)` → `notify::notice(...)`; `uiNeedsRendering(x, rows)` →
  `notify::raise(...)`; `Buttons::isButtonPressed(B)` → `ctx.held.has(ButtonId::B)`;
  `OLED::main.drawX(...)` → gfx names; `indicator_leds::setLedState(L, on)` → `leds.set(...)`
  in `leds()`. Contributors rerun the codemod on their branch instead of hand-resolving.

## 5. Ratchet

`scripts/check_platform_boundary.py` generalises to `scripts/check_layer_rules.py` with the
same shrink-only discipline, the same pre-commit hook and CI step:

- Rules are rows `(from_glob, forbidden_include_prefix | forbidden_symbol_regex, spec)`.
- The committed baseline `scripts/layer_rules_baseline.txt` holds **counts per (file, rule)**;
  `--update` rewrites it; a PR that increases any count fails with the rule's spec link.
- Rows from day one: `src/gfx/** → anything outside gfx` (already); `src/ui/** →
  model|libdeluge|gui|hid|l10n`; `src/notify → *`; `src/scenes → hid|gui/ui/ui.h`;
  `model|playback|processing|storage|io|modulation → gui|hid` (the 652 of spec C);
  `src/scenes → currentSong` (counted for the later model work); `LegacySceneAdapter`
  instances; `FACADE` includes; `uiTimerManager.` uses.

## 6. Branch and PR strategy

- Everything lands on `next` as small PRs — one extraction, one scene, one codemod, one
  table each. No long-lived `ui-rewrite` branch.
- `next` merges `main` at each `main` release tag; UI conflicts are resolved by rerunning
  the codemods.
- `docs/dev/ui_contributing.md` (one page) tells contributors where UI code goes now: a new
  feature goes in the scene if one exists; a legacy UI is frozen except bug fixes once its
  scene has shipped; menus are `Node` literals. The PR template links it and the ratchet's
  failure message links the relevant spec.

## 7. Deletion schedule

| After phase | Delete |
|---|---|
| 2 (gfx) | nothing — façades only |
| 3 (toolkit) | nothing |
| 4 (guest + `MenuScene`) | `gui/menu_item/**`, `gui/ui/menus.*`, `gui/ui/sound_editor.*`, `gui/menu_item/generate/**` — **but not on this row's own authority; see §10** |
| 5 (host) | `gui/ui/ui.cpp` internals, `hid/buttons.cpp` routing, `hid/matrix/`, `hid/encoder_input.cpp`, `View view`, `OLED::` compositing and static canvases, `indicator_leds` command API, `hid/led/pad_leds.cpp` flush/animations, `currentUIMode` |
| 6 (per scene) | that scene's legacy UI, its adapter instance, its façade rows |
| 7 | `LegacySceneAdapter`, `ShellHostUI`, `uiTimerManager` shim, all façades, `hid/display/`, `hid/led/`, `gui/` |

## 8. Risks and budgets

- **Behaviour drift**: the parity scripts (spec G §6) gate every scene; an intentional
  behaviour change is a separate PR that re-records goldens with the diff reviewed.
- **Flash/RAM**: link-map diff in CI (`./dbt sizediff`) with budgets — shell + toolkit must
  not exceed the code they replace at phase 5; ~40 static scene instances with POD state
  must fit the SDRAM `.bss` vacated by `menus.cpp`'s 411 objects. Measured at the end of
  phases 3, 4 and 5.
- **On-device validation**: a per-phase checklist (boot, menu, popup, song load, record,
  each root scene, screensaver, MIDI learn) on real hardware before the phase's last PR —
  goldens cover pixels, not SPI/PIC timing.
- **Two flush paths in phase 4**: the guest flushes through legacy calls; a scene that
  requests a repaint while a legacy popup is up must not clobber it — the guest checks
  `OLED::isPopupPresent()` and defers, mirroring what legacy UIs do.
- **Community**: frozen-legacy rule + codemods + ratchet messages keep in-flight PRs
  mergeable; `ui_contributing.md` is the contract.

## 9. Open questions

1. Whether `ShellHostUI` (phase 4) is worth its adapter code given phase 5 follows
   directly — it is, if `MenuScene` ships before the host inversion is ready; if the
   schedule collapses them, skip 2.1 and go straight to 2.2 with `MenuScene` as the first
   native scene.
2. `getRootUI()` returning `nullptr` for a new root scene: the 69 external callers are
   being removed by spec C's burn-down; any that remain at phase 5 need the null check they
   should already have.

## 10. Amendment: what the phase-4 deletion actually waits on (2026-09-02, closing spec E3)

§7's phase-4 row has always been the schedule's most-quoted line and its least precise. It names
what goes; it never named the precondition. Two corrections, in order.

### 10.1 The precondition E2 stated, and the fact that it is now met

The E2 plan corrected the precondition to: **`validate_tree` reports zero
`Issue::Kind::PlaceholderCustom` over `kAllRoots` with the allowance off.** (That correction was
written into the E2 plan's step list but never landed in this spec — it is being written down here,
one plan late, which is itself the argument for putting preconditions in the spec rather than in a
plan step.)

**As of E3 task 7, that precondition is met.** `kPendingCustomNodes` is 0, no node table binds
`&kPending`, the `PlaceholderCustom` arm is unconditional, and
`ValidateOptions::allow_placeholder_custom` — the option that could have hidden a regression here —
is deleted along with its eleven call sites. The ported menu tree is complete: 477 nodes, 55 custom
nodes behind 55 distinct real items, zero placeholders.

### 10.2 What still blocks the deletion — and it is not the menu tree

**Nothing from the menu tree remains.** As far as spec E knows, the only blocker is **spec F's own
wiring**, which is this document. Concretely, and each of these is a thing that does not exist today
rather than a thing that is half-done:

1. ~~**Nothing links `deluge_scenes`.**~~ **Closed by F1 task 1.** `libdeluge_scenes.a` is in the
   device closure (`src/bsp/rust/build.rs`, both the CMake target list and the archive deps array,
   on both the device and host paths) and `deluge_scenes` is in `deluge_host`'s
   `target_link_libraries` (`sim/CMakeLists.txt:446`) — which is the *actual* host-sim closure;
   `build.rs`'s own `run_host_app` path targets a `build-embassy-hostapp` tree that no CMake flag
   in this repo configures, so following it would have left the port out of the binary §10.8's A/B
   runs on. `deluge_scenes` `PUBLIC`-links `deluge_ui_shell`, so this is what put `src/shell` in
   the firmware image too, and `$<COMPILE_ONLY:deluge_app>` (`src/scenes/CMakeLists.txt:144-152`)
   is why both closures must keep linking `deluge_app` directly.

   **What it opened, and what now holds it shut.** Before this, an `#include "scenes/..."` from a
   legacy translation unit compiled and then failed to resolve — the build was the enforcement.
   `check_layer_rules.py`'s `legacy-includes-port` and `legacy-names-port` rows (F1 task 12) are
   the replacement: `src/deluge/**` is held to zero, less the eight files in `LEGACY_PORT_SEAMS`
   that are the argued seams, and the second row exists because this tree names a symbol through a
   bare `extern` precisely when the include is forbidden.
2. ~~**No entry point.**~~ **Closed for eight of the fourteen, by F1 tasks 5-7 and 9, behind a
   runtime toggle that is Off by default.** `ShellHostUI` (`src/deluge/gui/ui/shell_host_ui.{h,cpp}`)
   is §2.1's guest: a `UI` subclass pushed with `openUI`, forwarding buttons, pads and encoders into
   `Shell`, rendering through `GuestSurfaceSet` into legacy's own `OLED::main` and `PadLEDs::image`,
   re-arming `TimerName::UI_SPECIFIC` from `Shell::next_wake()`, and declaring LEDs through
   `SurfaceSet::set_led`. `openShellMenu` (`src/deluge/gui/ui/shell_menu_entry.cpp`) is the one
   door, declared in `src/deluge/util/shell_menu_entry.h` — in `util/` deliberately, because that
   directory is inside `check_layer_rules.py`'s `NON_UI` glob, so the move is judged by the very
   rule it is about rather than hidden from it.

   **Six of the fourteen are still open, and each is a node with no port**, listed with its reason
   in `shell_menu_entry.cpp`'s own fourteen-entry-point table. With the toggle on the firmware has
   *both* menus, reachable by different gestures; with it off — the default — every gated site runs
   the exact legacy statements it ran before, which F1 task 9 verified line by line at all eight
   sites.

   **MIDI is a separate half and is only partly closed** — see item 8. Two of the four learn
   messages have a transport and all four are inert for a third reason recorded there.

   **One user-visible divergence the level-3 A/B found, with two causes** (§10.8): with the ported
   menu open, legacy leaves the clip view showing with the sidebar **greyed**
   (`SoundEditor::getGreyoutColsAndRows`, `gui/ui/sound_editor.cpp:252-294`) and the guest **blacks
   the grid**. Fixing either cause alone still leaves it wrong: (a) `GuestSurfaceSet::send_columns`
   writes the menu's all-black grid into `PadLEDs::image`, and (b) legacy's greyout walk never runs
   at all, because `ShellHostUI` never re-requests grid rendering, so `reassessGreyout` and
   `prepareColour` are never reached. Proved by probe rather than by inspection: disabling (a)
   alone brought the sidebar back **ungreyed** (`pad 16 0 0 255 6`, boot's colour, not the OFF
   leg's `3 160 7`).
3. **No `MenuAction` dispatcher.** Five ported items request root-UI behaviour
   (`openUI(&performanceView)`, `setClipLengthEqualToSampleLength()`, the MIDI-learn overlay, the two
   song verbs) through `MenuScene::take_action()`, which nothing consumes. That mailbox is a one-slot
   silent overwrite, so the dispatcher must poll every tick.
4. **No `take_choice()` dispatcher.** `BrowserScene`'s deferring descriptor parks a chosen file and
   closes; performing `SampleBrowser::claimCurrentFile` and legacy's `maybeGetParentMenu` re-rooting
   is the wiring layer's.
5. ~~**Three boot calls.**~~ **Closed by F1 task 8**, and two of the three turned out not to be boot
   calls at all — the account is `src/scenes/registry.h`'s boot-wiring record.
   `menu::set_shortcuts_version()` is flash byte 60, whose value **does** live in a global the
   settings loader populates (`SoundEditor::shortcutsVersion`, written at
   `storage/flash_storage.cpp:568`) — what this item meant is that `src/scenes` may not *name* that
   `gui/` member. It is pushed from `SoundEditor::setShortcutsVersion`, the sole writer, which all
   four change paths go through (boot, both factory-reset menus, and the legacy settings item).
   `scenes::assigned_node_ids()` is called from `deluge_boot`, and was already reached through
   `scene_registry()` at static init. `ShellConfig::screensaver_idle_ms` is
   `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes * 60000`
   (`hid/display/screensaver.cpp:81-83`, `:93-95`), pushed from `Screensaver::settingsChanged()` —
   which **now has a port**, `shell::Screensaver::set_idle_ms` plus
   `Shell::set_screensaver_idle_ms`.

   **What remains open — three things, and they are NOT all the same shape.** An earlier version of
   this paragraph said "the legacy→port direction is closed for all three; the port→legacy direction
   is closed for none". That is true of 5.1 and 5.2 and **false of 5.3**, which is the item the
   generalisation was tidiest about: `Screensaver::settingsChanged()` and
   `SoundEditor::setShortcutsVersion` really do push into the port, so 5.1 and 5.2 are one-way gaps
   needing only the reverse writer — but **`AudioClipScene::set_flash_cursor()` is called by no application
   path at all** — the only two calls in the tree are in `sim/scenes_spec/audio_clip_scene_spec.cpp`
   — so 5.3 has no legacy→port push either and needs a reader *and* a writer.
   This branch's own records already said so (`src/scenes/README.md`'s boot-wiring section calls it
   "the one that is genuinely unwired"; `src/deluge/deluge.cpp`'s boot comment says "unwired in
   either direction"), and the spec is the one place that made the third fit the first two. Each
   item states its own condition below.

   1. **The ported screensaver setters** (`scenes/menu/nodes/settings.cpp`) write the same globals
      and cannot call `hid/`, so a change made from the ported screensaver menu notifies neither
      screensaver until reboot.
   2. ~~**The ported shortcut-layout setter never reaches flash byte 60.**~~ — **CLOSED, F2 task 6.**
      The gap was real: `gui/ui/sound_editor.cpp:124-137` listed four writers of the *setting* and
      named the fourth, `nodes::set_shortcuts_version`, as one that did **not** reach
      `setShortcutsVersion` — so a layout chosen from the ported settings menu held for the session
      and was gone at the next power-on, and under the toggle it is the one the user actually
      presses. It now reaches it: the hook files `MenuAction::SetShortcutsVersion`
      (`scenes/menu/action.h`) beside its own mirror write, and `dispatchShellActions`
      (`gui/ui/shell_action_dispatch.cpp`) performs `soundEditor.setShortcutsVersion(...)`. Both
      writes are kept — the mirror is what the item displays, the request is what persists — and the
      round trip does not loop, because `setShortcutsVersion`'s own mirror call reaches the free
      `menu::set_shortcuts_version` rather than the item hook. Pinned by
      `sim/scenes_spec/menu_actions_spec.cpp`'s `menu_action_shortcuts_version`, six examples
      covering both directions. Item 5.1 above is **not** the same shape and was wrongly grouped
      with this one in `src/scenes/README.md`: the screensaver setters' *settings* persist, it is
      only the `Screensaver::settingsChanged()` apply they drop.
   3. **The play-cursor flash (byte 36) is unwired in BOTH directions** — the one item here that is
      not a one-way gap. `AudioClipScene::set_flash_cursor()` is called by **no application path**
      — its only two callers in the tree are in `sim/scenes_spec/audio_clip_scene_spec.cpp` — so
      unlike 5.1 and 5.2 there is no legacy→port push to be the closed half.
      `FlashStorage::readSettings` writes `PadLEDs::flashCursor` straight
      (`storage/flash_storage.cpp:440`) — `hid/`, which `src/scenes` may never include — so the
      scene keeps its own copy defaulting to the factory value (`flash_storage.cpp:287`), and a
      user who turns the play cursor off still sees it in the ported audio clip view. This item
      was E4 task 11's and was never on this list; it belongs here because this list is what a
      contributor works from and item 5 otherwise reads as closed. Recorded at
      `src/deluge/deluge.cpp`'s boot-wiring comment and in `src/scenes/README.md`'s boot-wiring
      section.

   **What each one needs, stated separately because they differ.** 5.1 and 5.2 are **one-way**: the
   port already learns of a legacy change and cannot report its own, so each needs only the reverse
   writer — which for both means the `MenuAction` dispatcher of **item 3**, since a ported node
   cannot name `hid/` or `SoundEditor` directly. 5.3 is **two-way and needs no dispatcher**: a
   reader at boot (byte 36 into `AudioClipScene::set_flash_cursor`, beside legacy's own writer, on
   the shape `SoundEditor::setShortcutsVersion` demonstrates) and, once a ported item can change it,
   a writer back. Today only the reader is owed, because no ported node edits it yet.
6. **`perform_editor`'s four sync calls** into `menu::performance_editing_mode()`, because a
   `PerformanceView` class member offers no bare-`extern` escape.
7. **No application installs a service** — added by E4 task 13, the first thing in the tree that
   ever tried to hold all three services and a `Shell` at once. `services::ModKnobs`,
   `services::GlobalControls` and `services::MidiLearn` each take the `Shell`'s own `SceneStack&`
   (two of them its `InputLowering&`) and each *uses* it, so a real service cannot be constructed
   before the shell it serves; and `Services` used to be taken **by value into the `Shell`'s
   constructor**, with `service_list_` built once there, so it could not be installed after it
   either. An application that built a `Shell` could only pass `Services{}`, and **no gold-knob
   turn, transport button or MIDI-learn press reached anything outside the scene stack.** Unlike 5,
   this is not a setting that degrades — it is a whole routing tier that is unreachable.

   **The mechanism half is closed.** F1 task 2 added `Shell::set_services` (`src/shell/shell.h`),
   which sets `services_`, re-seats `Router`'s own by-value copy and rebuilds the deduplicated
   `service_list_` — well defined on a re-install, and covered by `tests/spec/shell/services_spec.cpp`
   with all four one-line mutations killed. `sim/scenes_spec/scene_scripts_spec.cpp`'s forwarding
   `LateService` shim is gone: it constructs the three in `after_construct` and calls the setter,
   with its 34 goldens unmoved. Shell spec §15.9 has the account.

   **And the application half is closed too, by F1 task 8.**
   `src/deluge/gui/ui/shell_guest_services.{h,cpp}` owns the three, builds them over
   `shellHostUI`'s own `SceneStack`/`InputLowering` and installs them with `Shell::set_services` —
   the first call that setter has had outside a spec — from `deluge_boot` and from
   `ShellHostUI::opened()`. It is a separate translation unit rather than three members of
   `ShellHostUI` because the host spec suite compiles `gui/ui/shell_host_ui.cpp` directly and never
   builds the `deluge_scenes` closure, so naming those three types there would take that suite off
   the board; the seam is one no-argument function it stubs.

   **Installing is not sufficient, which task 8 found by reading what the services then declare.**
   `ModKnobs` keeps its own `ModTarget` where legacy keeps `View::activeModControllableModelStack`,
   and an unseeded one reports `mode() == -1`: its `leds()` declares all eight mod LEDs **off**
   (`scenes/services/mod_knobs.cpp:725-735`) and `declare_indicators` declares level **0** on both
   gold knobs (`:683-690`), every tick the guest runs. So the wiring re-seeds `currentSong`,
   `View`'s active mod controllable and mod region and both `ViewState` blocks, at boot and on every
   open. Seeding it in turn made a latent double-dispatch live — legacy delivers MOD_ENCODER_0/1
   both to `buttonAction` and to the tail's `modEncoderButtonAction`, whose base forwards to `view`,
   the function `ModKnobs::mod_encoder_button_action` ports — so the guest drops those two ids from
   its feed and leaves the gesture on legacy's own code. Recorded in code at
   `src/scenes/registry.h`'s boot-wiring record and `gui/ui/shell_host_ui.h`.
8. **~~No producer for three of `MidiLearn`'s four entry points.~~ Partly closed by F1 task 10, and
   the item's own arithmetic was wrong.** It said `note_on_received`, `cc_received` **and
   `thing_pressed`** had zero callers in `src/`. `thing_pressed` had three all along — the service's
   own `handle()` calls it for the TAP, PLAY and RECORD arms
   (`scenes/services/midi_learn.cpp:513`, `:521`, `:529`), which is the same fact the item's last
   sentence states about the button arms. Only `note_on_received` and `cc_received` were
   producerless, and `cc_received` re-enters `note_on_received` internally (`:445`).

   **What task 10 built.** `UiEvent::Kind::MidiNoteOn` and `Kind::MidiCc` (`src/shell/scene.h`),
   carrying a forward-declared `::MIDICable*` plus channel and two data bytes;
   `Router::run_chain` needed no reordering to carry them (the other two services decline any
   non-button shape, so a MIDI event already went scene → `global_controls` → `midi_learn`, which
   is legacy's own note-on order) but did gain a `bool` return, because the producer is a legacy
   `UI` virtual whose contract is that bool. `Shell::route_midi` is the synchronous entry;
   `ShellHostUI::noteOnReceivedForMidiLearn` is the producer.

   **What is still owed, and it is three separate things:**

   * **`cc_received` has a transport and no producer.** `PlaybackHandler::midiCCReceived` reaches
     learn through a hardcoded `getCurrentUI() == &soundEditor` at
     `playback/playback_handler.cpp:3164`, *outside* the `UI_MODE_MIDI_LEARN` gate, and `UI` declares
     no CC learn virtual to override — `gui/ui/ui.h` has exactly two `*ForMidiLearn` hooks (`:130`
     pc, `:132` noteOn). Same for pitch bend at `:3091`. Closing either means adding a `UI` virtual
     and rewriting a legacy dispatcher, which is a legacy-behaviour change plan F1 does not make.
     Recorded at both dispatcher sites, at `MidiLearn::cc_received` and at `ShellHostUI`'s
     `@par MIDI learn`.
   * **Nothing calls `thing_pressed` from a pad.** The scenes that would answer `learnable_at(pad)`
     (`SessionScene`, `NoteClipScene`, `ArrangerScene`) are E5's and E6's, unchanged.
   * **The whole path is inert, for a reason this item never named.** Every learn dispatcher is
     gated on `currentUIMode == UI_MODE_MIDI_LEARN`. **Two assignments, three entries** — and the
     distinction matters, because the two owed items are not the same item.
     `grep -rn "currentUIMode = UI_MODE_MIDI_LEARN" src/deluge` returns exactly two lines,
     `View::startMIDILearn` (`gui/views/view.cpp:493`) and `SoundEditor::buttonAction`'s LEARN arm
     (`gui/ui/sound_editor.cpp:520`); the first has **two** callers, `View::buttonAction`'s LEARN arm
     (`gui/views/view.cpp:192`) and `SessionView::enterMidiLearnMode`
     (`gui/views/session_view.cpp:3072-3076`), which the song context menu's MIDI-learn entry reaches
     (`MidiLearnMode::setupAndCheckAvailability`, `gui/context_menu/midi_learn_mode.cpp:51`). None of
     the three runs while `ShellHostUI` is the current UI. The port's own learn mode sets
     `MidiLearn::active_`, a different bit that `PlaybackHandler` cannot see, and is itself gated on
     `input_top().is_root()` — false for `MenuScene`. **So two separate open items would each make
     these transports live, and neither is E5/E6's:**

     1. `MenuScene` porting `SoundEditor::buttonAction`'s LEARN arm
        (`gui/ui/sound_editor.cpp:505-537`, gated on `getCurrentMenuItem()->allowsLearnMode()`) —
        which `services::MidiLearn`'s own class doc already assigns to `MenuScene` and explicitly not
        to the service; and
     2. **item 3 of this list**, the missing `MenuAction` dispatcher: the third entry's ported form
        is `MenuScene::take_action`'s `song_midi_learn` (`scenes/menu/action.h:18`), whose legacy
        body is `midiLearnMode.setupAndCheckAvailability()` — i.e. the context-menu route above.
        Nothing polls that mailbox.

     Until one of the two lands, both transports are correct and carry nothing.

   Task 10 also closed the **guest half** this item did not mention: `ShellHostUI` now overrides
   `pcReceivedForMidiLearn` and `noteOnReceivedForMidiLearn` and forwards to
   `MenuScene::learn_program_change` / `learn_note_on`, which E2 task 8 wrote and specced and which
   nothing had routed into. Program change routes no `UiEvent` at all, deliberately:
   `View::pcReceivedForMIDILearn` is an empty body (`gui/views/view.h:63`), so there is nothing for
   `services::MidiLearn` to port on that message.
9. **Installing the services re-records part of the level-2 baseline** — added by E4's final review,
   and it belongs with 7 rather than after it: doing the wiring in 7 is what makes this fall due.
   All 24 `expect_script_goldens` calls in `sim/scenes_spec/menu_scripts_spec.cpp` pass
   `Services{}`, and twelve of those scripts drive **19 gold-knob turns** between them. What decides
   whether a given step's golden moves is the focused **page**, not the router's chain, and the
   split is measured:
   * A turn the page's own widget claims never reaches `Router::run_chain` at all —
     `Router::route_post_render` drops it on `ctx().claims` first — so installing the services
     changes nothing. `menu_int`'s `after_gold_turn` is that case, and
     `sim/scenes_spec/scene_scripts_spec.cpp`'s `menu_gold_turn` script records the same node with
     the real three services installed to prove it.
   * A turn the page does not claim is dropped today and will, with a real `services::ModKnobs`,
     write the mod param and push a `ParamPopupScene` over the menu — a second `scene` line in
     `.state` and a value strip in the `.pbm`. `menu_decimal`'s first `GoldUpper` turn is the worked
     example.

   Each of the 19 has to be classified and looked at as it is re-recorded; that is a step in the
   wiring work, not a separate plan. E4 deliberately did not do it, because the same commit range
   also reordered the router's chain (§15.4/§15.5 of the shell spec) and a golden diff with two
   causes cannot be reviewed for either. Recorded in code at `src/scenes/README.md`'s carry table,
   `tests/ui-scripts/shell/menu_int.txt` and `menu_scripts_spec.cpp`'s own header.

   **F1 task 11 carried this out and refused it, and the two bullets above are both wrong.** It
   moved `RealServices` into `sim/scenes_spec/support/real_services.h`, installed the real three
   over all twelve gold-turn blocks, and recorded into a scratch tree.

   * **No `ParamPopupScene` appeared anywhere.** Across all 68 moved golden files the diff carries
     not one added or removed `scene` line. The second bullet's predicted popup does not happen.
   * **A turn the page claims does not "change nothing" — it changes the model.** `menu_int`'s own
     C++ assertion (`+2` detents on `nodes::saturation` reads back 4) fails, reading **0**, and its
     `after_gold_turn` panel moves with it. `menu_submenu_horizontal`'s equivalent goes 32 → 28.
   * The uniform part is uniform: **57 `.state` files gain exactly `led Mod1 on` and the two
     `indicator` lines**. But **11 `.pbm` files move pixels** — `menu_chain` (3), `menu_int` (1),
     `menu_patch_cables` (1), `menu_range` (4), `menu_submenu_horizontal` (2).

   **Why, and it is not the router.** `InputLowering::action_gold_turn` runs on the gold path
   unconditionally, gated on no service — the Continuous arm of
   `InputLowering::apply_encoder_edges` (`src/shell/input_lowering.cpp:191`), which
   `on_encoder_edges` forwards to (`:158-163`) — and swallows a turn
   unless `(now_ms - last_turn_ms_[role]) < short_press_ms` (`:214`; 500 ms,
   `src/shell/input_lowering.h:26`). `ModKnobs::pretend_knobs_untouched()`
   (`src/scenes/services/mod_knobs.cpp:193-197`) writes that same array from the other side via
   `InputLowering::pretend_knob_untouched` (`input_lowering.cpp:451-461`,
   `last_turn_ms_ = now_ms - 1000`), and `RealServices::installer()` reaches it twice at
   construction — `set_target_with_timeline` (`mod_knobs.cpp:120`) and `set_mod_region` (`:165`) —
   **but only when a `mod_target` is seeded**. At `menu_int.txt`'s `+2` at t=300: unseeded,
   `300 - 0 = 300 < 500` → acted → 4; seeded, `300 - (0-1000) = 1300 ≥ 500` → swallowed → 0.
   `menu_gold_turn` reads 4 on the same node because its `+1` at t=100 arms the debounce first.

   **The seam to know about before installing the next service**: a service mutated shared
   `InputLowering` state at construction, and the effect surfaced in a debounce that no routed event
   passes through. Re-measured after the review (correcting task 11's first pass, which misread an
   interleaved "1" for the unseeded case): unseeded raises no assertion failure at all, and with all
   twelve unseeded the re-record is **57 `.state` files, +2 lines each
   (`indicator lower/upper 0 0 0 0`), zero pixels**.

   **The install has landed, UNSEEDED** (F1 task 11 fix round 2, after the re-review reversed the
   first refusal by performing the recording itself). The twelve gold-turn blocks install the real
   three through `sim/scenes_spec/support/real_services.h` with `svc.song` set and `svc.mod_target`
   left null; the other twelve keep `Services{}` because they drive no gold turn. Measured cost:
   **57 `.state` files, 114 insertions, 0 deletions, 0 `.pbm`, 0 value assertions**, every inserted
   line one of `indicator lower 0 0 0 0` / `indicator upper 0 0 0 0` — `ModKnobs::leds` declaring
   both rings dark for want of a target. `led Mod1 on` does not appear; that comes from seeding.

   The argument for doing it: `Services{}` is three nulls, which is **further** from the application
   than an unseeded `ModKnobs`, so this was never a choice between a right fixture and a wrong one —
   it is very wrong versus one seeded step from right, and 114 lines of a single repeated pair is
   not the ambiguous two-cause diff E4 task 13's rule exists to prevent. That rule still binds; it
   does not reach this case.

   **Caution, and it is the part that keeps the goldens honest: unseeded is exactly the
   configuration in which the swallowed-turn defect cannot bite.** With no `mod_target` nothing
   backdates the knob clock, so every gold turn in those 57 frames acts as it did under
   `Services{}`. They record the services being present; they settle nothing about the gesture, and
   the caution is written at the goldens themselves (`menu_scripts_spec.cpp`'s `@warning`,
   `tests/ui-scripts/shell/menu_int.txt`).

   **Still owed, as a named edit rather than a research task:** seed `svc.mod_target`, arm the
   debounce in each script the way `tests/ui-scripts/shell/menu_gold_turn.txt` already does (one
   leading detent inside `short_press_ms`), and re-record. Measured cost of that step: `led Mod1 on`
   on 57 frames plus **11 `.pbm` files moving pixels** — `menu_chain` (3), `menu_int` (1),
   `menu_patch_cables` (1), `menu_range` (4), `menu_submenu_horizontal` (2) — and the two value
   assertions above, which is precisely the diff the debounce arming exists to prevent.

### 10.3 Two consequences of the deletion that are not preconditions but must be planned for

* **Deleting `gui/ui/sound_editor.*` and `gui/ui/menus.*` deletes the oracle.** Sixteen files under
  `sim/scenes_spec/` drive the *legacy* menu objects side by side with the port — that comparison is
  this stack's central safety claim, and it is the only reason the port is believed faithful. The
  deletion PR must decide what replaces it: goldens alone are weaker, so the honest options are to
  delete the legacy tree *after* a device-validated release rather than at the moment the port
  compiles, or to keep `menus.cpp`'s object definitions alive behind a spec-only target for one more
  phase. This is a schedule question, not a technical blocker, and it is the one §7's row hides.
* **`soundEditor` has ~30 namers outside `gui/menu_item/`.** Views, browsers, the keyboard, MIDI
  follow, `flash_storage`, `action_logger` and `source.cpp` all reference it. Most are
  `openUI(&soundEditor)` or a `getCurrentMenuItem()` read and become shims or scene pushes, but they
  are the deletion's real edit surface and none of them is a menu item. `gui/ui/menus.*` also holds
  the `MenuItem` globals several of those files reach directly.

### 10.4 What spec E leaves that is *not* a phase-4 blocker

Recorded so a reader of §7 does not mistake E4's scope for a precondition.

> **Correction, 2026-09-07, closing E4.** This section was written expecting E4 to be the load/save
> browser family. **It was not.** E4 (`2026-09-06-ui-scenes-services-and-roots-4.md`) built the
> three model-aware shell services, the three missing overlay scenes, the shared `TimelineState` and
> the first two application scenes (`SlicerScene`, `AudioClipScene`). The reordering was structural
> rather than opportunistic: `GlobalControls`' and `MidiLearn`'s `View`-arm groups are gated on
> `stack_.input_top().is_root()`, so until a root scene existed those arms were unreachable and
> untestable, and `AudioClipScene` is what made them live. Everything the paragraph below says about
> the load/save family still holds — it is **E5's or E6's** rather than E4's. E4's own coverage
> number is §10.7.

The family in question is `gui/ui/load/load_song_ui`, `load_instrument_preset_ui`, `load_pattern_ui` and all
of `gui/ui/save/` (songs, presets, patterns, kit rows). E3 ported the two browsers the *menu tree*
reaches and stopped there deliberately: the other seven are opened **from root UIs rather than from
menu items**, so the menu tree's legacy-vs-port oracle cannot exercise them, and they carry
`SlotBrowser`'s brand-new-name axis (`mayDefaultToBrandNewNameOnEntry`, `getUnusedSlot`) that
`BrowserScene` has none of. Those files are on §7's **phase 6** row (per-scene), not phase 4.

Also carried, all of them recorded with pointers in `src/scenes/README.md`'s roadmap section (which
the same correction retitles): the `FileCatalogue` claim covering a listing rather than a browsing
session; mod-matrix pad blinking; `model/clip/audio_clip.cpp`'s surviving
`gui/waveform/waveform_renderer.h` include (a `model/` class that renders — `SessionScene`'s to
close); `MenuScene::ascend()`'s silent no-op at depth 0 where legacy's `goUpOneLevel` calls
`exitCompletely()`; a `CustomItem::title` hook for the three pages that want a runtime title; and
three `IntegerWithOff` nodes that draw "0" where legacy draws "OFF".

**E4 adds four rows to that list**, none of them a phase-4 blocker either:

* **`slicing::SliceOps` has no implementation.** Six virtuals with no-op defaults, wired by nobody —
  the whole audio/model/card half of the slicer. `Slicer` reaches that work through four `gui/`
  globals the ratchet forbids (`sampleBrowser`, `soundEditor`, `instrumentClipView`, `PadLEDs`), so
  the port names the calls at a boundary and keeps the state, the guards, the coalescer, the
  dispatch and the gate. Until a wiring layer implements it the slicer selects, deletes, renders and
  snapshots correctly and **makes no sound and no kit**. `SliceOps::play_position` is the third
  consumer of the voice walk no spec can drive, so the fixture that unblocks it — a `Song` with a
  booted `AudioEngine` and one sounding voice — unblocks all three.
* **`ClipView`'s `Y_ENC` shift arm is a silent no-op for a real gesture, and must not survive E5.**
  `AudioClipScene` *claims* the `Y_ENC` (and `X_ENC`-held + CLIP) chord exactly as legacy does
  (`gui/views/clip_view.cpp:249`) and then does nothing: two of the three arguments
  `Clip::shiftHorizontally` takes are decided by `getCurrentUI() == &automationView` (`:203`,
  `:206`) — a question with no answer while no `AutomationScene` exists — and the undo consequence
  `alloc_sdram`s (`:240`). On the device this gesture shifts the clip's contents. **Whoever writes
  `AutomationScene` ports this arm in the same change.** Recorded at the arm itself, in
  `src/scenes/README.md`'s carry table, and here.
* **`AudioClipScene::clip_resynced` is written from a citation and driven by nothing.** A `@warning`
  on its declaration says so.
* **Six doc-comment tables mangled by clang-format's comment reflow** — `menu/nodes/midi_device.h`,
  `menu/nodes/patch_cables.h`, `recorder/recorder_scene.h`, `shell/scene.h`, `shell/scene_stack.h`,
  `shell/shell.h`. Rewriting one as a bulleted list fixes it, and the citations in it have to be
  re-verified line by line while doing so (E4 tasks 9 and 14 each fixed one this way).

### 10.5 Eleven defects in shipping firmware, found by the port and not fixed by it

They belong in a release note, not only in a spec, and are written up in full in
`src/scenes/README.md`'s "Defects this port found in shipping firmware" with a `@warning` at each
site: a **use-after-free** in `AudioRecorder::finishRecording` (freed pointer still advertised to a
reader on the preemptive audio interrupt); `Browser`'s and `LoadInstrumentPresetUI`'s
**default-directory fallbacks are dead** (they test `FOLDER_DOESNT_EXIST` where the read path
returns `FILE_NOT_FOUND`); and **the first gold-knob turn after boot is always swallowed**, which
also means every committed UI-script golden depicts an arm the device never presents first.

E4 task 3 added two more, both in `View`'s mod-knob half and both found by porting it:
**`View::modButtonAction` dereferences `ModControllable::getModKnobMode()` unguarded** at
`gui/views/view.cpp:1534` (a read) and `:1549` (a write, reached on a press in a clip-minder view
where `:1517` is not), both inside the `:1510`/`:1511`/`:1512` guards, where `View::getModKnobMode`
(`:1680-1689`) null-checks the same pointer and the base implementation genuinely returns null
(`model/mod_controllable/mod_controllable.cpp:47-49`); and **`View::setKnobIndicatorLevel` reads an
uninitialised `int32_t knobPos`** (declared `:1410`, consumed `:1468`) on the one of its four paths
that assigns nothing. Neither is annotated in `view.cpp` itself — the same reason the browser row
gives, one order of magnitude worse: this stack cites `view.cpp` by line more than a hundred times.

E4 task 8 added a sixth, found by porting the clip views' scroll and zoom:
**`ClipView::getTickSquare` compares a scroll against the zoom.** The arm that stops following an
auto-extending linear recording reads `if (newXScroll != currentSong->xZoom[NAVIGATION_CLIP])`
(`gui/views/clip_view.cpp:382`), where `newXScroll` came from
`xScrollBeforeFollowingAutoExtendingLinearRecording` (`:379`), which the follow arm filled from
`currentSong->xScroll[NAVIGATION_CLIP]` (`:366`). So the restore is skipped whenever the remembered
scroll happens to equal the current zoom — leaving the view where the follow pushed it rather than
where the user was — and performed in the one case the guard exists to skip. Reached by recording a
clip longer than one screen. Established by reading and by driving legacy's own `getTickSquare`
from both sides (`sim/scenes_spec/timeline_spec.cpp`); the port reproduces it rather than fixing
it, for the reason every row here gives, and it is annotated at
`src/scenes/timeline/timeline.h`'s `tick_square` rather than in `clip_view.cpp`.

E4 tasks 9, 10 and 12 added the last four, three of them in the slicer and one in `ClipView`:

* **`Slicer::renderMainPads` never checks that it was given an image**, although `UI`'s own header
  requires every override to (*"These \*must\* check whether image has been supplied"*,
  `gui/ui/ui.h:141-142`) and `SampleMarkerEditor::renderMainPads` does
  (`gui/ui/sample_marker_editor.cpp:1362-1366`). `closeUI` quizzes every UI down to the one being
  closed with the **no-argument** form (`gui/ui/ui.cpp:184-192`), so `image` is null, and closing
  the slicer takes that path on every exit. REGION mode reaches `WaveformRenderer::renderFullScreen`'s
  memset; MANUAL mode writes `image[yy + 4][xx]` directly. **Memory corruption**, established by
  reading the enclosing braces rather than reproduced — whether address 0 faults on an RZ/A1 depends
  on the boot-time memory map, which is why it has survived.
* **`Slicer` declares 432 bytes of stack it never touches, twice** (`gui/ui/slicer.cpp:107`, `:376`).
  Cosmetic; recorded because a reader comparing the three render arms will otherwise assume the two
  dead arrays matter.
* **`Slicer::selectEncoderAction`'s slice-count wrap only fires on an exact hit** — equality, not a
  range (`gui/ui/slicer.cpp:249-257`) — while `offset` is whole detents, so a fast turn steps over
  both ends and a single -20 turn from the opening 16 lands on **-4**, which the page then displays.
  Reachable with an ordinary flick of the select encoder. The port reproduces it exactly and asserts
  both halves against legacy's own `selectEncoderAction`.
* **`ClipView::lengthenClip` and `shortenClip` dereference `actionLogger.firstAction[BEFORE]`
  without a null check** — `gui/views/clip_view.cpp:98` and `:129`. The nearest null-guarded read of
  that pointer is `lengthenClip`'s `undoing` test **eighteen lines up, in a different `if` whose
  result is not carried down**, and `shortenClip` has no earlier test at all, which is what makes it
  easy to miss rather than an obvious oversight. It survives on the device because an `Action`
  almost always exists by then; the window is a fresh boot (or an `actionLogger.deleteAllLogs()`,
  which `SoundEditor::setup` does on every menu open) followed immediately by a length edit.
  **Established by driving it**: `sim/scenes_spec/audio_clip_scene_spec.cpp` exited 139 rather than
  failing, which is why its `Rig::reseed` seeds one inert `ActionType::MISC` action. The port guards
  it and is deliberately **not** a faithful reproduction — a port cannot faithfully reproduce a
  segfault.

**F1 task 11 added an eleventh, and it is the most reachable of the set: `SoundEditor::setup`
dereferences a null `Clip*`.** `sound_editor.cpp:1876`'s `else if (clip->type == ClipType::AUDIO)`
runs whenever `currentSound` is null — `currentSound = newSound` at `:1856`, while `newSound` is
only ever assigned inside the `else if (clip)` arm at `:1679-1744` — so a `setup()` reached with the
default null `clip` dereferences null. **Measured, not read**: it SIGSEGVs at that line in the
**toggle-off** leg of §10.8's A/B, on an unmodified tree.

**The trigger, corrected.** The gesture that reaches it from the firmware's *default* state is
**SHIFT + SELECT — open Settings** — site 1, `gui/views/view.cpp:477`, reached through
`View::buttonAction` from any root. An earlier version of this record said "plain SELECT in the
session view — open the Song FX menu", which inherited a false premise about where boot lands:
**boot lands in `InstrumentClipView`, not the session view** (`tests/golden/ui/app/boot/boot.state`
shows `led Clip on` and `led Synth on`, and F1 task 11's own script says so). Site 2
(`gui/views/session_view.cpp:569`) is still a real null-clip site and still an everyday gesture, but
it needs a SONG press first, so it is not the one reachable with no navigation. Sites 3
(`gui/views/arranger_view.cpp:346`) and 4 (`gui/views/performance_view.cpp:898`) pass the same null
clip into the same function and so reach the same line; that is inspection, not a measurement.

It is not a crash on the device because address 0 is mapped there, and the real hazard is what
happens *instead*: `ClipType::AUDIO == 1`, so a byte of 1 at `0 + offsetof(Clip, type)` **takes**
the branch and seeds `currentSampleControls` and `currentPriority` with near-null pointers that
later menu code both reads and **writes** through. "Latent rather than fatal" is right about the
read and silent about the write — and on the RZ/A1 the low page is the **exception vector region**,
so a write through a near-null pointer does not present as a menu bug at all. It presents as an
unattributable lockup some time later, which is the worst shape a defect can have for a bug report. **The guest makes it strictly less reachable** — both scripts that
crash under the OFF fixture exit 0 under the ON one, because `openShellMenu` never dereferences the
clip — which is the opposite of a reason not to fix it. Not fixed here: it is a legacy behaviour
change, and this stack's standing rule sends those to Kate.

Four of the eleven are in `gui/views/view.cpp` and `gui/views/clip_view.cpp` and **none of the four
is annotated in the legacy file**: this stack cites those two by line more than 130 times, and
inserting a comment would shift every citation. The eleventh is unannotated in `sound_editor.cpp`
for the same reason and at a larger scale. Every port-side `@warning` is complete, and
`src/scenes/README.md`'s "Defects this port found in shipping firmware" is the full table.

### 10.6 What the oracle actually covers for the three new scenes (2026-09-04, closing E3's final review)

§10.3's first bullet says deleting the legacy tree deletes the oracle. **Read the deletion schedule
against this number, not against the headline "the port matches legacy, proven by specs that run
both".** For the *menu tree* that headline is close to true — sixteen specs drive the legacy
`MenuItem` objects. For the three genuinely new UIs — `BrowserScene`, `MarkerScene`,
`RecorderScene` — it was locally false until E3's final review, and even now the honest count is
smaller than the file count suggests.

**Covered** means: the spec drives the legacy entry point from *its own* inputs, on a model or
catalogue reset to what the port started from, and compares the two answers. A comparison whose
reference is computed from the port's own state proves nothing and is not counted; neither is a
comparison against a transcription of legacy's body held in the spec file. Both existed on this
branch and both are gone (the review's B1 and B2).

**26 legacy entry points, across 27 comparison sites.**

| scene | entry points driven from their own sentinel | comparison sites | spec |
|---|---|---|---|
| Browser | 14 | 6 | `sim/scenes_spec/browser_scene_spec.cpp` |
| Marker | 11 | 18 | `sim/scenes_spec/marker_scene_spec.cpp` |
| Recorder | 1 | 3 | `sim/scenes_spec/recorder_scene_spec.cpp` |

**Browser** (`gui/ui/browser/browser.cpp` unless stated). `beginListing` (`:97`) and
`runPendingListing` (`:126`); `openListingImpl` (`:349`); `arrivedInNewFolder` (`:367`);
`readFileItemsFromFolderAndMemory` (`:280`); `selectEncoderAction` (`:549`), both its plain arm and
its `newFileIndex < 0` wrap; `finishSelectEncoderAction` (`:702`); `clampFileSelectionAndScroll`
(`:1009`); `setEnteredTextFromCurrentFilename` (`:1316`); `predictExtendedText` (`:782`);
`Browser::padAction` (`:1186`) and `QwertyUI::padAction` (`gui/ui/qwerty_ui.cpp:202`);
`goIntoFolder`/`goIntoFolderImpl` (`:1334`/`:1339`); `goUpOneDirectoryLevel`/`Impl` (`:1359`/`:1379`);
`renderOLED` (`:925`), compared over the **full** 128x48 panel including the title rows. Each site
compares `fileIndexSelected`, `scrollPosVertical`, `enteredText`, `enteredTextEditPos`,
`scrollPosHorizontal` and the panel.

**Marker** (`gui/ui/sample_marker_editor.cpp`). `selectEncoderAction` (`:268`); `padAction` (`:339`)
main-column arms — move, select-and-hold, loop-start add, loop-start remove, loop-end remove,
loop-lock toggle; `writeValue` (`:148`); `getColsOnScreen` (`:236`) with its four column helpers;
`horizontalEncoderAction` (`:687`), zoom and scroll; `renderMainPads` (`:1362`) and, through it, the
private `renderColumn` (`:1268`) and `renderMarkerInCol` (`:1058`) over four marker layouts and every
blink phase; `renderOLED` (`:1098`); `reverseRemap` (`:83`); `loopLock`/`loopUnlock`;
`WaveformNav::opened` as `SampleMarkerEditor::opened` calls it (`:126`).

**Recorder** (`gui/ui/audio_recorder.cpp`). `renderOLED` (`:116`) and nothing else.

**Pinned only to literals, with the reason each one is not drivable.** These are what a deletion
loses outright.

* Browser's cull-and-reload arms — `reloadImpl` (`:735`) and `selectEncoderAction`'s four reload
  dispatches (`:637`, `:648`, `:665`, `:676`). They need `numFileItemsDeletedAtStart`/`AtEnd`
  non-zero, i.e. a folder past `FILE_ITEMS_MAX_NUM_ELEMENTS`; no fixture builds one. **This is the
  largest single gap** — it is the part of the browser most likely to be wrong and least likely to
  be noticed.
* `SampleBrowser::opened`'s configuration (`gui/ui/browser/sample_browser.cpp:96-172`) and
  `SampleBrowser::enterKeyPress`'s folder arm (`:333-360`) are **transcribed** into the spec's
  `SpecBrowser`, seven flags and one branch, each cited. `SampleBrowser` is `final` and `opened()`
  dereferences `soundEditor.getCurrentAudioFileHolder()` and drives `actionLogger`,
  `favouritesManager` and `instrumentClipView`, so it cannot run in a spec process. Everything past
  the configuration — the listing, the search, the clamp, the prediction — is `Browser`'s own code,
  executed.
* The whole of `LoadMidiDeviceDefinitionUI`. `opened()` (`:45-47`) tests
  `getRootUI()->toClipMinder()`, and `getRootUI()` indexes an empty UI stack in a spec process. The
  MIDI browser is pinned by its descriptor table and its `start_at` against cited lines only.
* `AudioRecorder`'s entire behaviour: `opened` (`:63`), `process` (`:194`), `buttonAction` (`:280`),
  `slowRoutine` (`:168`), `finishRecording` (`:260`), `updateStatus`, `padAction`,
  `getGreyoutColsAndRows` (`:58`). `process()` is an unbounded blocking loop that pumps
  `AudioEngine::routineWithClusterLoading`, `deluge_display_service` and `readButtonsAndPads`;
  `opened()` starts a real `SampleRecorder` on the audio engine through `setupRecordingToFile` and
  drives `soundEditor`/`PadLEDs`/`indicator_leds`/`instrumentClipView`; `buttonAction`'s release arm
  calls `getRootUI()->buttonAction` on a null root. **Of the three new scenes the recorder is the
  one the oracle barely touches.**
* `SampleMarkerEditor::timerCallback` (`:771`), which renders straight into `PadLEDs::image`. The
  scene's blink is compared against `renderMainPads` instead, with the one place the two legacy
  render paths disagree (phase 2) asserted in both directions.
* A second consecutive zoom detent. A successful `WaveformBasicNavigator::zoom` leaves
  `UI_MODE_HORIZONTAL_ZOOM` set until the `PadLEDs` transition completes, and nothing completes it
  in a spec process, so legacy refuses the next detent where the port takes it. One detent is
  compared; the mode is asserted.

**No legacy counterpart at all** (not a gap, and not counted either way): the browser's favourites
and banks rows and `setFileByFullPath` (`:230`); `createFolder`, `getUnusedSlot`,
`currentFileDeleted`, `goIntoDeleteFileContextMenu`; `SampleBrowser`'s sample preview
(`currentFileChanged`/`folderContentsReady`) and `claimCurrentFile` (`:398`), which the port defers;
`SampleMarkerEditor::renderSidebar`, `verticalEncoderAction`, and `padAction`'s audition-column and
mute-column arms.

**What this means for §7 and §10.3.** The menu tree can be deleted against its own oracle. The three
new scenes cannot be treated the same way: browser reload-under-cull and the audio recorder are
carried by literals and by citations, not by a comparison, and no amount of golden recording
strengthens that — a golden pins the port to itself. Either device-validate those two areas before
the legacy files go, or keep `browser.cpp`, `sample_marker_editor.cpp` and `audio_recorder.cpp`
alive behind a spec-only target for one more phase, which is the same choice §10.3 offers for
`menus.cpp` and should be made once for both.

### 10.7 What the oracle actually covers for E4's nine subjects (2026-09-07, closing E4)

The same question §10.6 asks, for the three services, the three overlay scenes, the shared timeline
state and the two application scenes E4 built. **Read the deletion schedule against this number**,
which is what §10.3's first bullet says it must be read against.

**Covered** means what §10.6 established: the spec drives the legacy entry point from *its own*
inputs, on a model or fixture reset to what the port started from, and compares the two answers. A
comparison whose reference is computed from the port's own state is not counted; neither is a
comparison against a transcription of legacy's body held in the spec file.

**64 legacy entry points, across 121 comparison sites.**

| subject | entry points driven from their own sentinel | comparison sites | spec (`sim/scenes_spec/`) |
|---|---|---|---|
| `ModKnobs` | 11 | 13 | `mod_knobs_spec.cpp` |
| `GlobalControls` | 2 | 7 | `global_controls_spec.cpp` |
| `MidiLearn` | 11 | 15 | `midi_learn_spec.cpp` |
| `ParamPopupScene` | 2 | 3 | `param_popup_spec.cpp` |
| `CountInScene` | 1 | 1 | `count_in_spec.cpp` |
| `ScreensaverScene` | 2 | 2 | `screensaver_scene_spec.cpp` |
| `TimelineState` | 14 | 29 | `timeline_spec.cpp` |
| `SlicerScene` | 7 | 21 | `slicer_scene_spec.cpp` |
| `AudioClipScene` | 14 | 30 | `audio_clip_scene_spec.cpp` |

**How "comparison sites" is counted here, and why it differs from §10.6.** A site is **one example
(`it(...)`) that calls a legacy entry point with spec-chosen inputs and asserts**. Precisely, so it
can be re-derived: walk the spec's lines, tracking the enclosing `it(`/`they(` block; **skip lines
whose first non-space characters are `//`, `///`, `*`, `*/` or `/*`**; and count a block that
contains both a **call** to one of the entry points named below (matched as the identifier followed
by `(`) and an `expect`. A legacy call reached only through a **teardown helper** does not count —
`global_controls_spec.cpp`'s `release_all_legacy_buttons()` calls `Buttons::buttonAction` to release
held buttons so one example's combo cannot leak into the next, which is not the gesture under test;
counting helpers transitively would turn that spec's 7 into 21.

Two places the rule earns its precision, both found by a reviewer re-deriving these numbers:

* **Comment lines must be excluded.** Counting `global_controls_spec.cpp`'s blocks for the bare word
  `buttonAction` *without* the comment skip gives **8**; with it, **7**. The extra block is "the
  root gate: `View`'s arms need a root scene, and the `Buttons` tail never does", which names
  `View::buttonAction` and `Buttons::buttonAction` in its opening comment and **calls neither** — it
  drives the port and asserts against a rule established by reading. 7 is the count.
* **§10.6's own site numbers are a hand count and do not re-derive.** Applying this rule to E3's
  specs gives 6 for the browser and 3 for the recorder — matching §10.6 exactly — and **5** for the
  marker against a recorded **18**. Recorded rather than reconciled: §10.6's entry-point column is
  unaffected, and this plan's own lesson (task 9's unreproducible "8 violations") is that a
  measurement a reader cannot repeat is an argument wearing a measurement's clothes.

The entry-point column is the substantive one either way, and every entry point in it is enumerated
below by name.

**`ModKnobs`** (`gui/views/view.cpp`). `modEncoderAction`; `modButtonAction`;
`displayModEncoderValuePopup`; `calculateKnobPosForModEncoderTurn`;
`convertPatchCableKnobPosToIndicatorLevel`; `setKnobIndicatorLevel`; `isClipContext`, over all five
reachable target shapes; `getModKnobMode`; and legacy's own three target setters —
`setActiveModControllableTimelineCounter`, `setActiveModControllableWithoutTimelineCounter`,
`setModRegion` — which are driven as legacy's side of the sentinel and whose resulting
`activeModControllableModelStack` is compared field by field. The central example runs the two sides
on **two different clips**, each with its own `SoundInstrument` and `ParamManager`, each seeded by
its own explicit write, and reads the resulting `ParamManager` back by identity rather than asking
the port what it did.

**`GlobalControls`** (`hid/buttons.cpp`, `gui/views/view.cpp`). `Buttons::buttonAction`, driven at
15 call sites, and `View::buttonAction` reached through it — a stub **root** UI forwards, because
`UI::buttonAction`'s base returns `NOT_DEALT_WITH` and does not. Two entry points is a small number
for a 24-example spec, and it is the honest one: transport is a **process singleton** (one
`PlaybackHandler`, one `Buttons`, one `AudioEngine::audioSampleTimer`), so no fixture can give
legacy and the port one each. The discipline is applied in the shape available and stated at the
file: each side establishes its own starting state and asserts the reset took; legacy's answer is
captured into a local and the model is then **poisoned** with a value neither transition can produce
(`RecordingMode::ARRANGEMENT`, `false` for a metronome legacy just turned on, `0x5EED` for the BACK
arm's cluster counter); both sides are asserted to have moved.

**`MidiLearn`** (`gui/views/view.cpp`). `startMIDILearn`; `endMIDILearn`; `buttonAction`;
`midiLearnFlash`; `noteOnReceivedForMidiLearn`; `ccReceivedForMIDILearn`; and the five learn-pad
entry points — `sectionMidiLearnPadPressed`, `instrumentMidiLearnPadPressed`,
`clipStatusMidiLearnPadPressed`, `noteRowMuteMidiLearnPadPressed`, `drumMidiLearnPadPressed`. The
`LearnedMIDI` objects are per-fixture where legacy allows and process singletons where it does not
(`midiEngine.globalMIDICommands`); the poison step is what makes the singleton case honest.

**`ParamPopupScene`** (`gui/views/view.cpp`). `displayModEncoderValuePopup` and
`flushPendingModEncoderValuePopup`, compared as **the assembled scene's own pixels** through a real
`deluge::hid::display::OLED` and a real `Shell`, not as the widget in isolation.

**`CountInScene`** (`gui/ui/notify_drain.cpp`'s count-in block, `:92-113`). One entry point: legacy's
own `display->displayPopup(buffer, 0, true, 255, 2)` call at `:100`, with `buffer` formatted from
`playbackHandler.currentVisualCountForCountIn` — the same public sentinel the port reads for itself
through `count_in_visual()`. Neither side is computed from the other's.

> **This one sits on the boundary of the rule above, and is marked so it is not read as precedent.**
> `notify_drain.cpp`'s count-in block never runs: `count_in_spec.cpp`'s `legacy_show_count_in()` is a
> **two-line transcription** of `:98-100` — the `intToString` call and the five `displayPopup`
> arguments — and what it reaches from there is real, unmodified legacy
> (`OLED::displayPopup` → `popupText`, and the panel is compared through the real
> `hid::display::OLED` singleton). So the *drawing* is driven, the *dispatch* is transcribed. It is
> counted because the transcribed part is a call site rather than a body — five literal arguments a
> reader can check against `:100` in one glance, with no branch and no arithmetic — and because
> nothing above `:100` is reachable from a spec (`notify_drain` is driven by the tick tree). A
> future subject that transcribes a *body* to reach legacy is **not** the same thing and does not
> get counted on this precedent; §10.6's exclusion means what it says.

**`ScreensaverScene`** (`hid/display/screensaver.cpp`). `Screensaver::timerEvent` and
`Screensaver::noteActivity`, observed through `isActive()` and `getCanvas()`, with the two 128x48
panels compared **pixel for pixel on every frame** in both animated modes.

> **What that pair can and cannot disagree about.** Since the move, legacy's own `Screensaver`
> includes `ui/oled/rainfall.h` and `ui/oled/starfield.h` (`hid/display/screensaver.h:21-22`) — so
> **both sides draw with the same objects.** The comparison is therefore about *sequencing*: which
> of `scatter()` / `advance()` / `render()` each side runs, in what order, on which tick, and into
> which canvas. It cannot disagree about pixel *content* for a given animation state, because there
> is only one implementation of that left in the tree. That is exactly why the transcription oracles
> exist and why the move needed a second, textual argument — and it is why "pixel for pixel" here
> means "the assembled frame agrees", not "the animation arithmetic was re-derived".

What is *not* counted
here is the move's own proof: `Rainfall` and `Starfield` are compared against
`sim/scenes_spec/support/oracle_{rainfall,starfield}` — a transcription of the pre-move source, and
§10.6's rule excludes transcriptions. It is a strong transcription (produced by a recorded
three-substitution `sed`, reproduced byte-identically by the reviewer against
`git show`, and corroborated by an independent reverse-mapped diff showing only comment-column
realignment), but it is a transcription, and this table counts what the rule counts.

**`TimelineState`** (`gui/views/timeline_view.cpp`, `clip_navigation_timeline_view.cpp`,
`clip_view.cpp`, `audio_clip_view.cpp`). Fourteen functions, driven on **two** concrete views
(`audioClipView` and `instrumentClipView`, 18 view×function pairs): `horizontalEncoderAction`,
`buttonAction`, `zoomToMax`, `scrollRightToEndOfLengthIfNecessary`, `scrollLeftIfTooFarRight`,
`isSquareDefined`, `getSquareFromPos`, `getSquareEndFromPos`, `getPosFromSquare`,
`getLengthExtendAmount`, `getLengthChopAmount`, `displayNumberOfBarsAndBeats`,
`tripletsButtonPressed`, and `ClipView::getTickSquare` through a pointer-to-member (it is
`protected`).

**`SlicerScene`** (`gui/ui/slicer.cpp`). `selectEncoderAction`; `horizontalEncoderAction`;
`verticalEncoderAction`; `buttonAction`; `renderMainPads`; `renderOLED`; `focusRegained`. Several
arms need a slice table with more than one entry and neither side can build one from its own
`padAction` (legacy's adder is behind the undriveable voice walk), so the spec **tells both sides the
same table**, each through its own front door — the port through its own ported adder, legacy
through its own public members — and asserts the two agree before the gesture under test runs.

**`AudioClipScene`** (`gui/views/audio_clip_view.cpp` and its bases). `padAction`; `buttonAction`;
`horizontalEncoderAction`; `verticalEncoderAction`; `selectEncoderAction`; `timerCallback`;
`graphicsRoutine`; `renderMainPads`; `renderSidebar`; `renderOLED`; `getMaxLength`; `getMaxZoom`;
`clipNeedsReRendering`; `setClipLengthEqualToSampleLength`.

**Pinned only to literals or to a counter, with the reason each one is not drivable.** These are
what a deletion loses outright.

* **`AudioRecorder`-shaped: everything behind `audioRecorder` in `GlobalControls`** —
  `beginOutputRecording` (`hid/buttons.cpp:161`, `:182`), `isCurrentlyResampling` (`:195`),
  `endRecordingSoon` (`:196`). Not ported at all, so there is nothing on the port side to compare;
  the *decisions* that reach them are covered through
  `GlobalControls::unported_opens(UnportedOpen)`, a tally **per site**, so an assertion about one
  arm cannot be satisfied by another. `openUI(&saveSongUI)` / `openUI(&loadSongUI)` /
  `context_menu::clearSong` (`view.cpp:262`, `:314`, `:291-294`) have the same shape.
* **`slicing::SliceOps` has no implementation.** Six virtuals with no-op defaults — the whole
  audio/model/card half of the slicer. `Slicer::doSlice` (`gui/ui/slicer.cpp:596-751`) itself is not
  driven: it calls `sampleBrowser.claimAudioFileForInstrument`, reads `soundEditor.currentSource` /
  `currentSound`, calls `instrumentClipView.recalculateColours` and ends in
  `sampleBrowser.exitAndNeverDeleteDrum()` — four `gui/` globals this layer may never name, plus a
  card read. The spec drives a fake and compares everything **around** the commit: the snapshot, the
  gate, the dispatch and the release. This is the largest single gap in E4, and it is the direct
  analogue of E3's `AudioRecorder` gap.
* **The voice walk, which three separate things need.** `Slicer::graphicsRoutine`
  (`slicer.cpp:156-203`), `Slicer::padAction`'s do-slice branch (`:508-534`) and
  `SliceOps::play_position` all need `Sound::voices()` to report a live voice, which needs a running
  `AudioEngine`. `support/song_fixture.h` builds a `Song` by direct construction and never calls
  `AudioEngine::init()`, so the only branch a spec can reach is "no voices, no tick square", which
  asserts nothing. One fixture — a `Song` with a booted `AudioEngine` and one sounding voice —
  unblocks all three.
* **`AudioClipScene::clip_resynced` is written from a citation and driven by nothing**, and says so
  on its declaration.
* **`TimelineState::tick_square`'s auto-scroll arm.** `ClipView::getTickSquare`'s follow arm is
  gated on `getCurrentlyRecordingLinearly()` and `getCurrentUI() == this` — a live recorder and a
  real root UI, neither of which the fixture has. The arms reachable from a non-recording clip are
  compared; the recording ones are not.
* **`ModKnobs`' popup suppression arms** for the performance view and the song browser
  (`view.cpp:886-891`, `:902`) — no `PerformanceScene` exists and the song browser is not among the
  legacy UIs `BrowserScene` carries, so no port-side state can make either fire. **`PopupContext::stuttering`**
  — nothing in the port owns the stutter gesture. **The MIDI-clip CC/expression naming arm**
  (`view.cpp:1139-1163`) — reachable since task 2 widened `show()`, but a bare MIDI fixture's gold
  knobs resolve through `MIDIInstrument::getParamFromModEncoder` to no `autoParam`, so no popup is
  shown at all; covering it needs a fixture that assigns a mod-knob CC.
* **`ModKnobs::notify_param_automation_occurred`'s `DISPLAY_AUTOMATION` consumer** — the timer
  callback is `automationView.displayAutomation()` / `view.displayAutomation()`, both view-side and
  unported. Only the *arming* is compared.
* **`MidiLearn`'s `clearMelodicInstrumentMonoExpressionIfPossible`** (`view.cpp:762-782`),
  **`iterateAndCallSpecificDeviceHook(HOOK_ON_MIDI_LEARN)`** (`:620`) and
  **`FlashStorage::writeSettings()`** (`:485`): the first is unported (it needs
  `model/model_stack.h`, which `scenes-model-stack-outside-context` confines to two files) and is
  counted through `MidiLearn::unported_calls`; the second likewise; the third is skipped on **both**
  sides, because `spec::QuietAudio` holds `AudioEngine::audioRoutineLocked` for the whole process,
  so what is asserted is the `shouldSaveSettingsAfterMidiLearn` decision rather than the write.
* **`GlobalControls`' `THRESHOLD_RECORDING_MODE` popup arm** (`hid/buttons.cpp:190-192`), the
  **`SYNC_SCALING` fill-mode arm's note halves** (`view.cpp:340-354`, which need `soundEditor` and
  `instrumentClipView`), the **dedicated undo/redo buttons** (`view.cpp:424-447`, dead code behind
  `#ifdef undoButtonX`, defined nowhere in the tree), and the **UNDO-vs-REDO choice** at `:414-415`
  (`playbackHandler.slowRoutine()` consumes the pending command inside the same call and a spec
  cannot make `deluge::sync::sd_busy()` true, so both spellings read `NONE` by the time control
  returns — the arm's *body running* is covered, which of the two it chose is not).
* **`CountInScene`'s masked `UI_MODE_RECORD_COUNT_IN` clear** (`notify_drain.cpp:104-111`):
  `currentUIMode` lives in `gui/ui/ui.h`, which this layer may never include, so there is no port to
  compare against at all.
* **Four `ScreensaverScene` behaviours, all of them policy rather than drawing and all of them
  `shell::Screensaver`'s half**: `settingsChanged()` (`hid/display/screensaver.cpp:200-241`), the
  permanent-popup / working-animation / stem-export inhibitors (`:106-118`) and with them the
  1000 ms re-check that exists only to run them, the `ScreensaverMode::OFF` early-outs, and the
  `consumeFrameDirty` blit protocol.
* **`AudioClipScene`'s `UI_MODE_HOLDING_SONG_BUTTON` macro arm of `renderSidebar`** (`:247-249`) and
  **the four `PadLEDs` transition modes** — the port carries no transitions at all, so those early
  returns have no counterpart.
* **`View::setModLedStates` is not driven.** The eight-mode × eight-LED table is asserted by
  identity against a transcription of `view.cpp:1679-1693` read row by row, which is the shape §10.6
  excludes. It is a table rather than a body, and the assertion fails on an undeclared LED id, but
  it is not a comparison against running legacy and is not counted above.

**Anything with no legacy counterpart at all** (not a gap, and not counted either way):
`Slicer::redraw` (`slicer.cpp:100-101`, an empty 7SEG-only hook); **`AudioClipView::renderOLED` at
64 rows** and `CountInScene` at 64 rows, because `OLED_MAIN_HEIGHT_PIXELS` is a compile-time 48 and
there is no 64-row legacy singleton to ask — both are goldens at that geometry, exactly as
`marker_scene_spec`'s and `browser_scene_spec`'s are; and the `Y_ENC` shift arm and
`changeRootUI(&automationView)` arm of `ClipView`, which have no port to compare (§10.4).

**Two of `MidiLearn`'s four entry points had no producer in `src/` at all** — `note_on_received`
and `cc_received`; `thing_pressed`'s "no callers" was a miscount, corrected in §10.2 item 8, and the
four button arms have always been live through `Router`. **F1 task 10 gave both a transport and one
of them a producer.** `note_on_received` is now reached from `ShellHostUI::noteOnReceivedForMidiLearn`
through `Shell::route_midi` and the chain's `midi_learn` step, and `sim/scenes_spec/midi_learn_spec.cpp`
drives it from `PlaybackHandler`'s own dispatcher on the legacy side rather than from `View`'s method,
so the `UI_MODE_MIDI_LEARN` gate and the `channelToZone` mapping are inside the compared path.
`cc_received`'s transport is specced the same way and has no producer, for the reason §10.2 item 8
gives. So its coverage now reads as "the service's behaviour is pinned, and the delivery of one of
its two message entry points is too"; what §10.2 item 8 still owes is the CC producer, a scene that
answers `learnable_at(pad)`, and the LEARN arm that would make legacy's dispatchers fire at all.

**What this means for §7 and §10.3.** Two of E4's nine subjects are carried by literals in the way
`AudioRecorder` is: the slicer's **commit path** (`SliceOps`, `doSlice`) and everything the **voice
walk** feeds. Both are unblocked by the same fixture, which is the cheapest single thing anyone
could build to raise this number. Everything else is comparison-covered on the arms a spec process
can reach, and the two services whose gaps are largest — `GlobalControls`' recorder arms and
`MidiLearn`'s absent producers — are gaps in the **wiring**, not in the port: they close when spec F
supplies the callers, not when someone writes more spec. As in §10.6, the honest answer is either
device-validate the slicer's commit before `slicer.cpp` goes, or keep it alive behind a spec-only
target for one more phase — the same choice §10.3 offers for `menus.cpp`, `browser.cpp`,
`sample_marker_editor.cpp` and `audio_recorder.cpp`, and it should be made once for all of them.

### 10.8 What the oracle covers for F1's subjects, and why the number is small (2026-09-07, closing F1)

The same question §10.6 and §10.7 ask, for what plan F1 built: **the guest and the wiring**. Its
oracle is **different in kind** from every prior plan's, and saying so is most of the answer.

**§10.6 and §10.7 count object-level comparisons.** A spec drives a legacy entry point from its own
inputs, on a fixture reset to what the port started from, and compares the two answers — 64 entry
points across 121 comparison sites for E4's nine subjects. That is a wide net with a shallow mesh:
each site proves one function agrees, and nothing proves the functions are reached.

**F1's is a level-3 A/B: the whole application, twice.** `tests/ui-scripts/app/menu.txt` is booted
into the real `deluge_host` binary against two SD-card fixtures that differ by exactly one file —
`tests/fixtures/card` and `tests/fixtures/card-shell-menu`, whose `SETTINGS/CommunityFeatures.XML`
sets `shellMenu` — and the two output trees are compared against each other by
`scripts/ui_app_ab_check.py` as well as each against its own golden. That is **stronger per
comparison** than any object-level fixture: nothing is stubbed, the toggle is read from the card by
the same code the device runs, and the thing compared is what a user would see. It also **covers far
less surface per comparison**, and the honest number is much smaller than E4's.

**The number: 1 script, 5 frames, 2 legs, 10 committed goldens, 1 of the 14 entry points.**

| what the A/B compares | count |
|---|---|
| scripts | 1 (`tests/ui-scripts/app/menu.txt`) |
| snapshots per leg | 5 — `boot`, `menu-root`, `menu-scrolled`, `gold-turn`, `menu-exit` |
| legs | 2 (toggle off, toggle on) |
| OLED panels compared leg-to-leg | 5, **all 5 byte-identical** |
| `.state` files compared leg-to-leg | 5, of which **1 is identical (`boot`) and 4 differ** |
| legacy `openUI(&soundEditor)` entry points driven end to end | **1 of 14** — site 6, `InstrumentClipMinder::buttonAction` (`model/clip/instrument_clip_minder.cpp:399`) |
| ctest rows | 5 — `ui_app_scripts_menu{,_diff,_shell,_shell_diff,_ab}` |

**The headline result is the OLED equality.** Five panels, byte for byte, across an open, a
navigation, a gold-knob edit and an exit: the ported `MenuScene` draws `SoundEditor`'s pixels in the
real firmware. That is one assertion covering the guest's lifecycle, its OLED composition, its
button/encoder forwarding, its timer re-arm, the boot calls and the service install at once — no
object-level fixture in §10.7 covers a chain that long.

**The four `.state` differences are the findings, not the noise**, and each is explained and cited
in `.superpowers/sdd/2026-09-07-ui-wiring-1/task-11-report.md`. The user-visible one is the sidebar
divergence recorded in §10.2 item 2, with both of its causes.

**What one script cannot cover, stated so the number is not read as more than it is:**

* **Thirteen of the fourteen entry points.** Eight are gated and only one is scripted; six have no
  ported node at all. The other seven gated sites are covered at level 2 by
  `sim/scenes_spec/menu_entry_spec.cpp`, which is an object-level oracle, not this one.
* **The four null-clip sites cannot be scripted at all** — the toggle-**off** leg SIGSEGVs in
  legacy before the comparison happens (§10.5's eleventh defect). The script stays on a
  clip-carrying site precisely so the leg that would crash is the one that has to run.
* **MIDI.** Both learn transports F1 built are inert under the guest for the reason §10.2 item 8
  gives, so no script can drive them; their coverage is `midi_learn_spec.cpp`'s, at level 2.
* **The grid.** Four of the five frames disagree on the pads, so the grid is a *finding* here, not
  coverage. Nothing in this suite pins what the sidebar should look like under the guest, because
  what it should look like is still open.
* **Everything the toggle is off for.** The A/B proves the two legs differ in exactly the ways
  recorded; it does not, and cannot, prove the toggle-off leg is unchanged from before this branch.
  That claim is F1's global constraint 2, and its evidence is a different mechanism: at each of the
  eight gated sites every removed line reappears as a **stripped-identical** line inside the
  toggle-off `else`, checked mechanically across the whole diff and then read by hand at the four
  sites where the restructuring is not a straight indent (F1 task 9). The raw diff of `src/deluge/`
  across the branch removes 27 non-comment lines: 24 are such pairs, and the other three are lines
  a *continuation* was appended to — the toggle's own `subMenuEntries` entry, its `UIType` name-table
  row, and `ShiftIsSticky::writeCurrentValue`'s test, rewritten to hold the value it now also
  mirrors into the guest.

**An honest small number beats an inflated one.** 1 script and 5 frames against E4's 121 comparison
sites is the right comparison to print, and the right conclusion to draw from it is not "F1 is less
tested" — the level-1 and level-2 suites grew from 43 to 50 sim specs and 98 to 101 host specs over
these twelve tasks — but that **the acceptance oracle for a wiring plan is narrow and deep where a
porting plan's is wide and shallow**, and the next plan should add scripts (a second entry point, a
second root, a MIDI path once one is live) rather than assume this one generalises.
