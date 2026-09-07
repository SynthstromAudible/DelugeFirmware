/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "definitions_cxx.hpp"
#include "gui/ui/shell_guest_surfaces.h"
#include "gui/ui/ui.h"
#include "shell/profiles/deluge.h"
#include "shell/scene.h"
#include "shell/shell.h"

#include <cstdint>

namespace deluge::gui::ui {

/// @brief The legacy `UI` that owns a whole `Shell` and renders it -- spec F §2.1's *shell as
///        guest*.
///
/// `ShellHostUI` is registered and pushed like any other legacy UI (`openUI(&shellHostUI)`), and
/// inside it a `Shell` runs its own scene stack over a `GuestSurfaceSet`. Legacy's render hooks are
/// served out of the shell's surfaces; legacy's input hooks feed the shell's `InputLowering`; and
/// `timerCallback` is the shell's tick -- see `@par The tick` below, which is also where the LEDs
/// come from.
///
/// @par Input is fed, not answered
///
/// **Nothing the input hooks do is synchronous.** `Shell::on_board_event` and
/// `Shell::on_encoder_edges` push into `InputLowering`'s queues; routing to a scene happens in
/// `Shell::pump_input`, which only `Shell::tick` calls (`shell/shell.cpp:561-571`). So a hook can
/// never report what a scene did with a press, and every return value below is decided statically
/// from what legacy does with it -- see each hook's note. The tick that drains the queue is
/// `timerCallback`, and every one of those hooks arms it through `armTickSoon` -- see
/// `@par The tick`.
///
/// **camelCase, deliberately** (plan F1's global constraint 3): everything here is a legacy `UI`
/// override or something a legacy call site names, and a façade over legacy spells things the way
/// legacy does. The `Shell` it drives is `snake_case` on the other side of that seam.
///
/// @par The virtuals this class does NOT override, and why
///
/// The failure mode this list exists for is a guest that silently inherits a `UI` default where it
/// should have forwarded: nothing breaks until a scene needs that hook, and by then it reads as a
/// scene bug. `::UI` declares 32 virtuals (`gui/ui/ui.h:94-173`, `grep -n virtual`); sixteen are
/// overridden below and the other sixteen are listed here with the reason.
///
// clang-format off
/// | not overridden | line | why |
/// |---|---|---|
/// | `~UI` | `ui.h:94` | Defaulted. This class owns its members by value; the implicit destructor is correct. |
/// | `modEncoderAction` | `ui.h:104` | **The three gold-knob hooks stay with `view`, and F1 task 6 decided that deliberately against its own brief -- see `@par The gold knobs` below.** The base forwards to `view` (`ui.cpp:35-37`), legacy's own mod-knob path, which is where `SoundEditor::modEncoderAction` ends up **only from its innermost `else`** (`gui/ui/sound_editor.cpp:1642`) -- so this is *not* parity, and what it drops is in `@par What leaving the mod hooks on the base costs`. Task 8's, with `ModKnobs`. |
/// | `modButtonAction` | `ui.h:105` | Base forwards to `view` (`ui.cpp:39-41`); `SoundEditor` does not override it at all, so this row -- alone of the three -- really is legacy's behaviour unchanged. Legacy never routes a mod button to `buttonAction` either -- `Buttons::buttonAction` intercepts the eight of them and `goto dealtWith`s (`hid/buttons.cpp:99-107`) -- so **the shell never sees `ButtonId::Mod0`..`Mod7` under the guest at all.** Recorded as a gap, not a decision, and it has a second-order cost: see `@par Shift`. |
/// | `modEncoderButtonAction` | `ui.h:106` | Base forwards to `view` (`ui.cpp:43-45`), which is where `SoundEditor` lands only from its own `else` (`gui/ui/sound_editor.cpp:1647-1654`), losing its `automationView` redirect -- see the same `@par`. **F1 task 8 settled the ordering shell spec §15.5 asks about, and settled it the other way: `view` keeps the push and the shell never sees it.** With `ModKnobs` installed and its target seeded, feeding the press as well would run the same gesture twice -- `ModKnobs::mod_encoder_button_action` is the port of the `view` function this row forwards to -- so `buttonAction` below drops `ButtonId::ModEnc0`/`ModEnc1` instead. What that costs is one line: `View::modEncoderButtonAction_changeModControllable`'s value refresh is guarded on `getCurrentUI() == &soundEditor` (`gui/views/view.cpp:1390-1392`), false under the guest, so the ported menu's displayed value goes stale after a gold-knob push. Note the dispatch is *gated on this class's `buttonAction`*: `Buttons` only reaches it when the current UI returned `NOT_DEALT_WITH` (`hid/buttons.cpp:134-141`, the arms at `:226-231`), which is one more reason `buttonAction` below returns that value. |
/// | `graphicsRoutine` | `ui.h:108` | **Deliberately kept.** The base runs the *root* view's per-frame animation whenever `canSeeViewUnderneath()` (`ui.cpp:47-51`), and `SoundEditor` does not override it either -- so the clip view under an open menu keeps its playhead moving. Overriding it would stop that. |
/// | `toClipMinder` | `ui.h:122` | Null, as `SoundEditor` leaves it. A guest is not a clip minder; `currentUIIsClipMinderScreen()` must keep answering about the view underneath. |
/// | `toTimelineView` | `ui.h:127` | Null, as `SoundEditor` leaves it, for the same reason. |
/// | `scrollFinished` | `ui.h:129` | `SoundEditor` overrides it to clear `UI_MODE_HORIZONTAL_SCROLL` and re-render the root (`sound_editor.cpp:1062-1067`); both halves are about legacy UI modes and legacy rendering, neither of which the shell owns. **Recorded as an uncovered gap**, not a decision to forward: a scroll begun under an open guest ends without the guest hearing about it. |
/// | `pcReceivedForMidiLearn` | `ui.h:130` | **Task 10** (the MIDI event kind and `MidiLearn`'s producers). |
/// | `noteOnReceivedForMidiLearn` | `ui.h:132` | **Task 10**. |
/// | `getUI` | `ui.h:162` | Returns `this`, which is right: the guest is not a chooser that opens a different UI. |
/// | `getUIContextType` | `ui.h:167` | Defaults to `getUIType()`, as `SoundEditor` leaves it. |
/// | `getUIModControllableContext` | `ui.h:170` | `NONE`, as `SoundEditor` leaves it. |
/// | `canDisplayFavourites` | `ui.h:171` | False, as `SoundEditor` leaves it. |
/// | `isFavouritesVisible` | `ui.h:172` | False, as `SoundEditor` leaves it. |
/// | `isBanksVisible` | `ui.h:173` | False, as `SoundEditor` leaves it. |
// clang-format on
///
/// Two of those rows are gaps rather than settled answers and are named as gaps: `scrollFinished`
/// above, and `displayOrLanguageChanged`, which **is** overridden below but only to ask the shell
/// for a repaint -- see its own note for what that does and does not buy.
///
/// @warning `ui.h:94-160`, the range plan F1's task 5 brief cites for "every `UI` virtual", stops
///          thirteen lines short of the class's last one. Seven virtuals live at `:162-173`, and
///          one of them, `getUIType()` (`:164`), is **pure** -- a guest written to that range alone
///          would not compile. The real span is `:94-173`.
///
/// @par The tick: `timerCallback` is the only thing that runs the shell
///
/// **Nothing inside the guest routes, renders, or lights an LED except `Shell::tick`.** The input
/// hooks below only push into `InputLowering`'s queues; routing happens in `Shell::pump_input`,
/// which only `tick()` calls (`shell/shell.cpp:561-571`), and the LEDs are composed and sent by
/// `Shell::derive_leds`/`Shell::flush` at step 9 of that same function (`shell/shell.cpp:512-514`).
/// So a guest whose timer is not armed is a guest that is frozen, however much legacy is feeding
/// it -- which is why the arm is re-established in every place legacy can leave it unarmed, not
/// only in `opened()`.
///
/// `TimerName::UI_SPECIFIC` is the timer, dispatched to `getCurrentUI()` by
/// `UITimerManager::routine` (`gui/ui_timer_manager.cpp:142-148`). Legacy clears it out from under
/// its owner at four places -- `changeUIAtLevel` (`gui/ui/ui.cpp:92`), `changeRootUI` (`:112-114`,
/// the only conditional one: it clears only while `currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW`),
/// `closeUI` (`:197`) and `openUI` (`:233`) -- all of them *before* the `opened()`/`focusRegained()`
/// that follows, so arming from those two hooks is safe and is the only thing that makes the guest
/// survive a UI opening and closing over it. The guest arms unconditionally, so the one conditional
/// clear costs it nothing; it is named because a reader counting clears would otherwise miscount.
///
/// @par The unit mismatch: an absolute deadline against a duration
///
/// `Shell::next_wake()` is an **absolute** millisecond deadline (`shell/shell.h:353`), recomputed at
/// the bottom of every tick (`shell/shell.cpp:515`). `UITimerManager::setTimer` takes a **duration**
/// and converts it to audio samples with `ms * 44` (`gui/ui_timer_manager.cpp:253-255`). The
/// conversion is therefore `in = (int32_t)(deadline - now)`, in the wrap-safe difference form
/// `Shell::compute_next_wake` and `RepaintGate` both use (`shell/shell.cpp:1017-1025`,
/// `ui/core/repaint_gate.cpp:7`) -- a plain `<` across the ~49.7-day `uint32_t` wrap picks the wrong
/// one of two straddling deadlines -- and then clamped to `kMinRearmMs .. kMaxRearmMs`.
///
///  - **A deadline already past** (`in <= 0`) arms `kMinRearmMs`, not zero. It is not an error: the
///    shell asks for `now_ms` routinely (a router repaint armed at a zero-millisecond deadline,
///    `shell/shell.cpp:399-413`), and there is nothing to catch up on -- the tick that just ran was
///    the one it was waiting for.
///  - **Arming earlier than asked is always safe; arming later is not.** The next tick recomputes
///    `next_wake()` from scratch, so an early wake costs one tick and corrects itself. That is why
///    the clamp is one-sided in its consequences and why `kMaxRearmMs` may exist at all.
///
/// @par Why the floor is 1 ms and not 0 -- the busy loop this shell has shipped before
///
/// `Scene::next_timer()` may legally return **0**, meaning "next tick"
/// (`shell/scene.h:452-504`), and `AudioClipScene::next_timer()` returns it for the whole of
/// playback (`scenes/audio_clip/audio_clip_scene.cpp:1187-1193`). `Shell::next_wake()`'s own
/// `@warning` records that a host cannot tell that apart from "wake once, immediately", and that the
/// shell "only ticks when its host asks it to" -- **this class is that host**, so the answer is here.
///
/// `setTimer(UI_SPECIFIC, 0)` would put `triggerTime` at exactly `AudioEngine::audioSampleTimer`
/// (`gui/ui_timer_manager.cpp:257-266`) and `timeNextEvent` with it. **`kMinRearmMs` (1 ms = 44
/// samples) is the smallest duration `setTimer`'s `ms * 44` can express that leaves `triggerTime`
/// strictly in the future**, and that is the whole of the argument: with zero, `triggerTime` is
/// already behind the audio sample timer by the time `UITimerManager::routine` is next called at all
/// -- audio advances at 44.1 kHz, far faster than the main loop the routine is called from -- so
/// `timeNextEvent` is past, the early return at `gui/ui_timer_manager.cpp:54-57` does not fire, and
/// the timer's own test at `:64-65` is true. The whole 29-timer sweep and the shell's whole tick
/// would then run on **every** pass of the main loop, for as long as the guest kept re-arming at
/// zero. With 44 samples they run at most once a millisecond.
///
/// @note Precisely at the instant of arming, `timeNextEvent - audioSampleTimer` is **0**, and
///       `routine`'s guard is `>= 0`, so the early return *is* taken and the timer does not fire
///       that instant -- it becomes due one audio sample (~22.7 us) later. An earlier draft of this
///       paragraph said a zero arm "defeats the early return"; it does not, and the distinction is
///       recorded because the conclusion rests on the sample timer advancing between two calls of
///       `routine`, not on the guard being wrong.
///
/// @warning **1 ms is not legacy's animation cadence, and a future tenant will want the difference.**
///          Legacy's per-frame hook is `graphicsRoutine`, whose one external driver is
///          `TimerName::GRAPHICS_ROUTINE`, re-armed at **15 ms** (`gui/ui_timer_manager.cpp:213-218`;
///          `grep -rn "graphicsRoutine()" src/deluge` gives exactly one call site outside the class
///          hierarchy). So a scene answering 0 gets ~1000 ticks/s here where legacy's equivalent runs
///          at ~66. 1 ms is nonetheless the right floor **for this plan's tenant**: the first one is
///          `MenuScene`, which has no zero-timer arm at all, while a floor of 15 ms would put 15 ms
///          of latency on the repaint after *every* button press -- and legacy has none there, since
///          `doAnyPendingUIRendering` runs from the main loop rather than from a timer
///          (`gui/ui/ui.cpp:375-396`). The condition that ends this answer is a tenant whose
///          `next_timer()` returns 0, i.e. `AudioClipScene`; what it needs is the third answer
///          `Shell::next_wake()`'s warning already asks for -- a distinct "every frame" cadence --
///          not a different floor here.
///
/// @par The LEDs: the ownership rule, in both directions
///
/// **There is no `LedSink`, and this class implements no LED sink.** Spec F §2.1 says "the guest's
/// `derive_leds` writes through an `indicator_leds` `LedSink`"; `grep -rn LedSink src/` has no hits,
/// `derive_leds` is a private member of `Shell` (`shell/shell.h:482`) rather than of the guest, and
/// the writing is done by `Shell::flush`, not by `derive_leds` (`shell/shell.cpp:966-992`). The seam
/// that actually exists is `SurfaceSet::set_led`/`set_indicator`, implemented for the guest by F1
/// task 4 (`gui/ui/shell_guest_surfaces.cpp:175`, `:189`). What this task adds is the *tick* that
/// makes any of it run.
///
///  - **An LED a scene declares is the guest's.** `Shell::flush`'s diff loop only ever calls
///    `set_led` for an id with `desired_declared` set (`shell/leds.cpp:75-80`), which
///    `Shell::derive_leds` sets only from the composed `LedFrame` (`shell/shell.cpp:838-857`). The
///    guest then takes the LED away from legacy's blinker for as long as it holds it:
///    `GuestSurfaceSet::set_led` calls `indicator_leds::setLedState` with the default
///    `allowContinuedBlinking = false`, whose first act is `stopLedBlinking(led)`
///    (`hid/led/indicator_leds.cpp:42-52`, `:141-149`) -- so legacy's `LED_BLINK` timeout stops
///    writing it (`updateBlinkingLedStates`, `:116-139`, which only touches `active` blinkers).
///  - **An LED no scene declares is legacy's, and the guest never touches it.** Not a policy this
///    class implements but one it must not break: an undeclared id fails `led_send_needed`'s
///    `desired_declared` test above, so no tick can write it however many times it runs.
///  - **The handover back is coherent for LEDs and stale for indicators.** `setLedState` writes
///    legacy's `ledStates[]` shadow (`hid/led/indicator_leds.cpp:49`), so a legacy write after the
///    guest closes is a write onto a true shadow. `GuestSurfaceSet::set_indicator` cannot do the
///    same -- it calls `deluge_control_set_indicator` directly, because `KnobIndicator` is already
///    the four segment levels and there is no scalar to hand `actuallySetKnobIndicatorLevel` -- so
///    `knobIndicatorLevels[]` goes stale, and that function's early-out then suppresses a later
///    legacy write of the level the shadow still holds (`hid/led/indicator_leds.cpp:187-191`).
///    **This went live with F1 task 8, which is what task 7 said would make it live.** The one
///    declarer is `services::ModKnobs` (`grep -rn "frame.indicator(" src/scenes` --
///    `scenes/services/mod_knobs.cpp:687`, `:696`, `:703`), `Shell::flush` calls `set_indicator`
///    only for an indicator declared this tick (`shell/shell.cpp:996-1008`), and the guest now has a
///    `ModKnobs` in its `Services`. The fix task 7 named is in: `exitUI`'s close path calls
///    `indicator_leds::clearKnobIndicatorLevels()` (`hid/led/indicator_leds.h:88`) *before*
///    `close()`, which puts shadow and hardware back in step at level 0 in time for the
///    `focusRegained` that `closeUI` runs on the same call -- every root view's reaches
///    `view.setActiveModControllableTimelineCounter(...)` and so `setKnobIndicatorLevels()`. See
///    that function for the two residual cases.
///
/// @par The gold knobs: why `modEncoderAction` is NOT wired, against the brief
///
/// Spec F §2.1 and F1's task-6 brief both list `modEncoderAction` among the hooks that "forward
/// into the shell's `InputLowering`". Doing that would corrupt the value, arithmetically:
///
///  - The shell's only encoder entry points take **raw A-pin edges**
///    (`Shell::on_encoder_edges`, `shell/shell.h:294`; `DELUGE_EVENT_ENCODER`'s `value`,
///    `shell/input_lowering.h:133-138`), and legacy has already turned a gold knob's edges into an
///    accelerated magnitude before it calls: `interpretEncoders` hands over
///    `offset * encoder.calcNextKnobSpeed(offset)` (`hid/encoder_input.cpp:192-194`), where
///    `calcNextKnobSpeed` returns `clamp(speed, 1.0, 3.0)` (`hid/encoders.cpp:76`).
///  - **The lowering does not re-accelerate it; `ModKnobs` does.** The `EncoderKind::Accelerating`
///    branch is `delta = edges` and its own comment says so -- "the raw edge count is the magnitude;
///    acceleration is applied by whoever consumes it, through `calc_next_knob_speed`"
///    (`shell/input_lowering.cpp:187-193`). The one consumer is F1 task 8's service
///    (`scenes/services/mod_knobs.cpp:274`), which multiplies by
///    `InputLowering::calc_next_knob_speed` (`shell/input_lowering.h:366`). So the second
///    multiplication is real but lands one layer further down than a reading of the lowering alone
///    would suggest.
///  - **The factor is ~18x, not 9x, because the Accelerating branch does not divide by
///    `edges_per_detent`.** `feedDetents` below multiplies by it on the way in (2 on the Deluge) and
///    only the `Detented` branch divides it back out (`shell/input_lowering.cpp:178-186`). So a
///    forwarded gold turn of `e` raw edges reaches `ModKnobs` as `2 * e * s_legacy` and leaves it as
///    `2 * e * s_legacy * s_shell`, up to **18x** the raw edge count where legacy delivers at most
///    3x -- and 2x even with both speeds at their 1.0 floor.
///  - It would also acquire a debounce legacy does not have, and this one *is* in the lowering.
///    `InputLowering::action_gold_turn` (`shell/input_lowering.cpp:191`) swallows the first turn
///    after a 500 ms pause; legacy's counterpart is dead code -- the field the wiggle test reads is
///    only ever written in a branch guarded by `offset != 0` that also requires `offset == 0`
///    (`hid/encoder_input.cpp:187-202`), which the lowering's own note at
///    `shell/input_lowering.cpp:206-213` already records.
///
/// So the mapping is not a mapping, and what the gold knobs need is F1 task 8's `ModKnobs` service
/// plus a shell entry point that accepts an already-lowered turn.
///
/// @par What leaving the mod hooks on the base costs, which is NOT nothing
///
/// **`SoundEditor` does override two of the three, and calling this parity would be wrong.**
/// `SoundEditor::modEncoderAction` (`gui/ui/sound_editor.cpp:1621-1645`) reaches
/// `UI::modEncoderAction` only from its innermost `else` (`:1604`); above it are two arms this guest
/// has no counterpart for:
///
///  - the `automationView` redirect (`:1584-1586`), and
///  - **`UI_MODE_MIDI_LEARN` -> `getCurrentMenuItem()->learnKnob(...)`** (`:1589-1601`) -- so under
///    the toggle, **MIDI-learning a menu parameter to a gold knob silently stops working.**
///
/// `modEncoderButtonAction` has the same shape, minus the learn arm (`:1609-1616`).
/// `modButtonAction` really is untouched by `SoundEditor`, so that one row *is* parity.
///
/// **Still not closable, and F1 task 8 -- which was named as the task that would close it -- found
/// out why.** Task 6 recorded that the port's answer was "the routed-to-`MenuScene` path", after
/// `ModKnobs`'s own note (`scenes/services/mod_knobs.h`: *"what it would have cost is
/// `SoundEditor::modEncoderAction`'s MIDI-learn arm ... whose port is `MenuScene`"*), and that it
/// needed the service installed **and** the gold turn routed. The service is now installed. The
/// other half does not help, because **there is no destination**: legacy's arm calls
/// `getCurrentMenuItem()->learnKnob(...)` (`gui/ui/sound_editor.cpp:1627-1639`), and
/// `MenuItem::learnKnob` (`gui/menu_item/menu_item.h:181`, implemented by
/// `MenuItemWithCCLearning::learnKnob`, `gui/menu_item/menu_item_with_cc_learning.cpp:43-57`) has
/// **no port at all** -- `grep -rn "learn_knob" src/scenes` is empty. `MenuScene` has the *other*
/// three learn entry points (`learn_note_on`, `learn_cc`, `learn_program_change`,
/// `scenes/menu/menu_scene.h`), which are `SoundEditor`'s three MIDI-message members; the knob arm
/// is a fourth and was never ported. Adding it is a `CustomItem` hook plus a port of
/// `soundEditor.currentModControllable`, i.e. scenes-layer work, not wiring. Recorded as a
/// divergence with the real condition on it, and the claim in `mod_knobs.h` corrected.
///
/// @par Shift: two state machines, and what actually keeps them in step
///
/// §2.1's rule is that legacy `Buttons::` state is *read* into `UiContext`, not duplicated.
/// **There is no seam to read it through.** `UiContext` is built by `Shell::make_context`, which is
/// private and fills `.shift`/`.sticky` from the lowering's own fields
/// (`shell/shell.cpp:549-559`), and `InputLowering` has no shift setter -- only
/// `clear_shift_sticky()` (`shell/input_lowering.h:214`) and `set_shift_is_sticky()` (`:221`). So the
/// guest's only lever is *which events it feeds*, and the two machines stay in step only because
/// `buttonAction` below feeds every button legacy hands it, `padAction` feeds every pad, and
/// `InputLowering::toggle_shift` is a line-for-line port of `Buttons::commandToggleShift`
/// (`shell/input_lowering.cpp:306-326` against `hid/buttons.cpp:237-278`).
///
/// Three ways they still diverge, all measured rather than predicted, none fixable from this file:
///
///  1. **The sticky-shift feature flag.** Legacy reads
///     `runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky)` at release time
///     (`hid/buttons.cpp:250`); the shell reads `LoweringConfig::shift_is_sticky`, which this class
///     constructs as its default **false** (`shell/input_lowering.h:29-31`) and nothing sets. With
///     the setting on, legacy latches shift on a tap and this shell does not -- and `ctx.shift` is
///     the whole gate on the menu's shortcut-pad jump (`scenes/menu/menu_scene.cpp:1392`), so under
///     the toggle a sticky-shift user loses shortcut pads. **A release blocker for the toggle**,
///     and it belongs with the boot wiring: `LoweringConfig` is construction-time
///     (`shell/shell.h:27-29`) and this object is a static-init global that cannot read flash.
///     Same class of gap, same home, as `ShellConfig::screensaver_idle_ms`'s own warning
///     (`shell/shell.h:30-43`).
///  2. ~~`hold_time_ms` is a constant here and a setting there.~~ **Closed by F1 task 8.** The tap
///     threshold is `FlashStorage::holdTime` (audio samples) in legacy and
///     `LoweringConfig::hold_time_ms` in the shell; `feed()` now converts and seats it through
///     `InputLowering::set_hold_time_ms` on **every** pad and button, which is where legacy's own
///     live read happens (`hid/buttons.cpp:264`). Not once per open: `nodes::set_default_hold_time`
///     (`scenes/menu/nodes/settings.cpp`) is the ported settings item and writes the flash global
///     from *inside* the guest, so a per-open seed would lag it by a whole session. Task 6 deferred
///     it because
///     no `LoweringConfig` mutator existed for it; task 8 added the one, on the same argument that
///     justified `set_shift_is_sticky` -- these two fields are settings, and the other two
///     (`short_press_ms`, `edges_per_detent`) are a constant and a board fact.
///  3. **Mod buttons.** Legacy clears `considerShiftReleaseForSticky` on *every* button press
///     including the eight mod buttons (`hid/buttons.cpp:88-95`, above the mod-button interception
///     at `:99-107`); the shell never sees them (the `modButtonAction` row above), so
///     shift-hold + mod-button + shift-tap latches shift in the shell and not in legacy.
class ShellHostUI final : public ::UI {
public:
	/// @brief Build the guest over \p board and \p registry.
	/// @param registry The application's scene table. `Shell` copies the pointers out of it into its
	///                 own table (`shell/shell.cpp:174-176`), so a temporary is fine.
	/// @param board    Board capability descriptor; the real Deluge unless a spec says otherwise.
	/// @param profile  The matching board profile. The pair is a parameter rather than a constant
	///                 for one reason: `canOpen()`'s refusal has to be drivable, and the only board
	///                 that triggers it is one whose panel is a shape `OLED::main` cannot hold --
	///                 the 64-row geometry every drawing spec in this stack is also checked at.
	explicit ShellHostUI(const deluge::ui::shell::SceneRegistry& registry,
	                     const DelugeBoard& board = deluge::ui::shell::kDelugeBoard,
	                     const deluge::ui::shell::BoardProfile& profile = deluge::ui::shell::deluge_profile());

	/// @name The scene the guest opens on
	/// @{
	/// @brief Set what `opened()` pushes.
	///
	/// Task 9's fourteen entry points each know which node the caller wanted; this is how they say
	/// so. Defaults to a `SceneId` whose kind is `SceneKind::Null`, which no registry can resolve --
	/// so a caller that forgets to set one gets a refused `opened()` (see below) rather than an
	/// empty guest.
	/// @param initial The scene to push.
	void setInitialScene(deluge::ui::shell::SceneId initial) { initial_ = initial; }
	/// @brief What `opened()` will push.
	deluge::ui::shell::SceneId getInitialScene() const { return initial_; }
	/// @}

	/// @brief The shell this guest runs. Tasks 6-8 reach the input lowering, the services and the
	///        tick through here.
	deluge::ui::shell::Shell& shell() { return shell_; }
	/// @brief The surfaces the shell renders through.
	GuestSurfaceSet& surfaces() { return surfaces_; }

	/// @brief Can this guest be opened at all?
	///
	/// False when the board profile's `SurfaceRole::Main` surface is a shape legacy's single
	/// `OLED::main` cannot hold, in which case `blit()` would drop every frame silently
	/// (`GuestSurfaceSet::canReachMainPanel`). A configuration error, answered before anything is
	/// pushed rather than discovered as a blank screen.
	bool canOpen() const { return surfaces_.canReachMainPanel(); }

	// ---- the legacy hooks, in `gui/ui/ui.h` order --------------------------------------------

	/// @name The tick's two clamps
	/// @{
	/// @brief What `UITimerManager::setTimer` multiplies a millisecond duration by
	///        (`gui/ui_timer_manager.cpp:253-255`).
	static constexpr int32_t kTimerSamplesPerMs = 44;
	/// @brief The shortest re-arm the guest will ask for. See `@par Why the floor is 1 ms`.
	static constexpr int32_t kMinRearmMs = 1;
	/// @brief The longest, and it is arithmetic rather than policy: `setTimer` computes `ms * 44`
	///        into an `int32_t`, so anything above this is signed overflow. A deadline further out
	///        than ~13.6 hours is woken early instead, which is always safe (see `@par The unit
	///        mismatch`).
	static constexpr int32_t kMaxRearmMs = 2147483647 / kTimerSamplesPerMs;
	/// @}

	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void selectEncoderAction(int8_t offset) override;
	ActionResult timerCallback() override;
	bool opened() override;
	void focusRegained() override;
	void displayOrLanguageChanged() override;
	bool canSeeViewUnderneath() override;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	ActionResult exitUI() override;
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	UIType getUIType() override { return UIType::SHELL_HOST; }

private:
	/// @brief Copy the shell's grid into legacy's pad image, for the columns \p x0 to \p x1 and the
	///        rows \p whichRows selects, and report the top scene's opacity.
	///
	/// The one body behind `renderMainPads` and `renderSidebar`, which differ only in their column
	/// span and in which of legacy's two send paths picks the result up.
	/// @param whichRows Bit `y` selects row `y`; legacy's own convention
	///                  (`gfx/grid/pad_grid.h:36`, "matches what the existing renderMainPads(whichRows)
	///                  already consumes").
	/// @param image     Legacy's pad image, or null for the opacity-query form.
	/// @param x0        First grid/image column to copy.
	/// @param x1        One past the last.
	/// @return The top scene's opacity -- see `opaque()`.
	bool serveGrid(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth], int32_t x0, int32_t x1);

	/// @brief Is the shell's top scene opaque over the pads, in legacy's sense?
	///
	/// Legacy's `renderMainPads`/`renderSidebar` return "did I use these rows up": false means
	/// *transparent*, "obey the UI under me" (`gui/ui/ui.h:140-142`). Spec F §2.2 states the dual
	/// of this mapping for the phase-5 adapter -- `is_grid_overlay()` is `!renderMainPads()` -- so
	/// this is that same equivalence read in the guest direction.
	///
	/// `MenuScene::is_grid_overlay()` is true (`scenes/menu/menu_scene.h:456-457`), so the first
	/// tenant answers **false** here, which is exactly what `SoundEditor` answers: it does not
	/// override either hook, so it takes `UI`'s `return false`.
	bool opaque();

	/// @brief The shell's clock, in milliseconds, for every hook that has to stamp one.
	///
	/// `getSystemTime()` is seconds since scheduler start (`OSLikeStuff/scheduler_api.h:74`,
	/// `OSLikeStuff/task_scheduler/task_scheduler_c_api.cpp:204-206`), which is the unit
	/// `LoweringConfig`'s thresholds are already expressed in -- unlike
	/// `AudioEngine::audioSampleTimer`, legacy's other clock, which counts audio samples and would
	/// need a lossy `/ 44` on every call. Plan F1 forbids a wall
	/// clock inside `src/shell/**` and `src/ui/**` (`shell-no-clock`); supplying one is exactly
	/// what a host is for.
	///
	/// @return Milliseconds since start, wrapping every ~49.7 days. The shell's own arithmetic is
	///         unsigned differences throughout, so the wrap costs one interval, not correctness.
	/// @note Not `static`, so F1 task 7's tick can be given a different clock in one place if it
	///       needs one.
	uint32_t nowMs() const;

	/// @brief Feed one already-detented encoder turn to the shell as A-pin edges.
	///
	/// Legacy's four function encoders reach a `UI` in **detents** -- `interpretEncoders` calls
	/// `DetentedEncoder::take()` and hands the result over (`hid/encoder_input.cpp:112`,
	/// `hid/encoders.cpp:28-38`) -- while the shell's entry point takes **edges** and divides by
	/// `LoweringConfig::edges_per_detent` itself (`shell/input_lowering.cpp:178-186`). So this
	/// multiplies back up. Feeding the detent count straight through would halve every turn.
	///
	/// @warning **The `int8_t` ceiling, and why this saturates instead of wrapping.**
	///          `Shell::on_encoder_edges` takes an `int8_t` (`shell/shell.h:294`), so the product
	///          must fit in -128..127; the lowering's own warning records what happens when it does
	///          not (`shell/input_lowering.h:407-430`). **The guest can exceed it.** Legacy's
	///          `detentDelta` is an `int32_t` accumulated in `DetentedEncoder::pos` across however
	///          many `applyEdges` calls happen before a `take()`, and `interpretEncoders` skips the
	///          `take()` entirely while the SD routine is yielding (`hid/encoder_input.cpp:96-106`,
	///          the `continue`s at `:100` and `:105`) and while a stem export is running (`:66-68`,
	///          after the `applyEdges` calls at `:58-63`). Each skipped pass can add up to 63
	///          detents (`deluge_encoder_take_edges` reports an `int8_t`, halved at
	///          `hid/encoders.cpp:31`), so a knob turned through a multi-second song load delivers
	///          the whole accumulation in one call. So the clamp is `+-(127 / edges_per_detent)`,
	///          i.e. +-63 detents at the Deluge's two edges per detent, applied **before** the
	///          multiply. That is legacy's own choice for two of its four encoders -- tempo and
	///          select get `clamp(detentDelta, -128, 127)` (`hid/encoder_input.cpp:117`) -- and a
	///          divergence from it for the other two, which legacy hands the full `int32_t` "for
	///          natural acceleration" (`:124`, `:140`). Saturating loses the excess of an
	///          impossible turn; wrapping would reverse an ordinary one.
	/// @param role    Which encoder the turn belongs to. Resolved to a board index through
	///                `BoardProfile::encoder_by_index` rather than hard-coded, because that table
	///                is what the lowering resolves back through
	///                (`shell/input_lowering.cpp:166-170`) and a profile may order it differently.
	///                A role no index carries is dropped.
	/// @param detents The signed detent count legacy handed over.
	void feedDetents(deluge::ui::EncoderRole role, int32_t detents);

	/// @brief Feed one decoded board event, stamped with `nowMs()`.
	/// @param ev The event.
	void feed(const DelugeInputEvent& ev);

	/// @brief Re-arm `TimerName::UI_SPECIFIC` from the shell's own deadline.
	///
	/// Called from `timerCallback` and nowhere else, because `Shell::next_wake()` is only
	/// authoritative immediately after a `tick()` recomputed it (`shell/shell.cpp:515`).
	/// See `@par The unit mismatch` on the class for the conversion and both clamps.
	///
	/// A `std::nullopt` deadline means "nothing is armed; sleep until the next input" -- so nothing
	/// is armed here either, and the timer stays inactive (`UITimerManager::routine` cleared it
	/// before calling, `gui/ui_timer_manager.cpp:66`). **With one exception**: while the lowering
	/// holds the SD-routine lock its deferred queue is waiting on a fact no scene can express as a
	/// deadline, so the guest keeps polling. That is what makes the release time-driven rather than
	/// input-driven -- F1 task 6 recorded the divergence and named this function as its home.
	/// @param nowMs The clock the tick just ran on.
	void rearmTimer(uint32_t nowMs);

	/// @brief Ask for a tick at the earliest moment legacy can express.
	///
	/// The counterpart to `rearmTimer` for every path that changes the shell's state from *outside*
	/// a tick: `Shell::next_wake()` is stale there (it is only recomputed at the bottom of `tick()`,
	/// and `Shell::boot()`/`Shell::init()` arm no deadline at all), so there is nothing to convert
	/// and the only correct answer is "soon". Arming earlier than necessary costs one tick; not
	/// arming at all freezes the guest until something else happens to.
	void armTickSoon();

	/// The scene `opened()` pushes; `SceneKind::Null` until a caller sets one.
	deluge::ui::shell::SceneId initial_{};
	/// @brief The config `shell_` was built with, kept so `feedDetents` can read
	///        `edges_per_detent` back.
	///
	/// `InputLowering` exposes `profile()` (`shell/input_lowering.h:278`) and `shift_is_sticky()`
	/// (`:223`) but not the edge ratio, and a literal `2` here would silently go stale the day
	/// F1 task 8 seats a real config. Declared before `shell_` so it is alive when the constructor
	/// hands it over.
	deluge::ui::shell::ShellConfig config_{};
	GuestSurfaceSet surfaces_;
	deluge::ui::shell::Shell shell_;
};

} // namespace deluge::gui::ui

/// @brief The one guest, named the way legacy names its UIs (`extern SoundEditor soundEditor;`).
///
/// Constructed over `deluge::scenes::scene_registry()`. Task 9's entry points push it with
/// `openUI(&shellHostUI)` when `RuntimeFeatureSettingType::ShellMenu` is on.
extern deluge::gui::ui::ShellHostUI shellHostUI;
