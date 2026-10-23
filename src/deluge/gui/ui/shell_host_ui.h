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
/// `Shell::pump_input`, which only `Shell::tick` calls (`shell/shell.cpp:582-592`). So a hook can
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
/// scene bug. `::UI` declares 32 virtuals (`gui/ui/ui.h:94-173`, `grep -n virtual`); twenty-one are
/// overridden below and the other eleven are listed here with the reason. Two of the overridden,
/// `pcReceivedForMidiLearn` and `noteOnReceivedForMidiLearn`, carry nothing yet -- see `@par MIDI
/// learn` below before assuming that path is live.
///
// clang-format off
/// | not overridden | line | why |
/// |---|---|---|
/// | `~UI` | `ui.h:94` | Defaulted. This class owns its members by value; the implicit destructor is correct. |
/// | `graphicsRoutine` | `ui.h:108` | **Deliberately kept.** The base runs the *root* view's per-frame animation whenever `canSeeViewUnderneath()` (`ui.cpp:47-51`), and `SoundEditor` does not override it either -- so the clip view under an open menu keeps its playhead moving. Overriding it would stop that. |
/// | `toClipMinder` | `ui.h:122` | Null, as `SoundEditor` leaves it. A guest is not a clip minder; `currentUIIsClipMinderScreen()` must keep answering about the view underneath. |
/// | `toTimelineView` | `ui.h:127` | Null, as `SoundEditor` leaves it, for the same reason. |
/// | `scrollFinished` | `ui.h:129` | `SoundEditor` overrides it to clear `UI_MODE_HORIZONTAL_SCROLL` and re-render the root (`sound_editor.cpp:1062-1067`); both halves are about legacy UI modes and legacy rendering, neither of which the shell owns. **Recorded as an uncovered gap**, not a decision to forward: a scroll begun under an open guest ends without the guest hearing about it. |
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
/// which only `tick()` calls (`shell/shell.cpp:582-592`), and the LEDs are composed and sent by
/// `Shell::derive_leds`/`Shell::flush` at step 9 of that same function (`shell/shell.cpp:533-535`).
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
/// `Shell::next_wake()` is an **absolute** millisecond deadline (`shell/shell.h:379`), recomputed at
/// the bottom of every tick (`shell/shell.cpp:536`). `UITimerManager::setTimer` takes a **duration**
/// and converts it to audio samples with `ms * 44` (`gui/ui_timer_manager.cpp:253-255`). The
/// conversion is therefore `in = (int32_t)(deadline - now)`, in the wrap-safe difference form
/// `Shell::compute_next_wake` and `RepaintGate` both use (`shell/shell.cpp:1056-1064`,
/// `ui/core/repaint_gate.cpp:7`) -- a plain `<` across the ~49.7-day `uint32_t` wrap picks the wrong
/// one of two straddling deadlines -- and then clamped to `kMinRearmMs .. kMaxRearmMs`.
///
///  - **A deadline already past** (`in <= 0`) arms `kMinRearmMs`, not zero. It is not an error: the
///    shell asks for `now_ms` routinely (a router repaint armed at a zero-millisecond deadline,
///    `shell/shell.cpp:420-434`), and there is nothing to catch up on -- the tick that just ran was
///    the one it was waiting for.
///  - **Arming earlier than asked is always safe; arming later is not.** The next tick recomputes
///    `next_wake()` from scratch, so an early wake costs one tick and corrects itself. That is why
///    the clamp is one-sided in its consequences and why `kMaxRearmMs` may exist at all.
///
/// @par Why the floor is 1 ms and not 0 -- the busy loop this shell has shipped before
///
/// `Scene::next_timer()` may legally return **0**, meaning "next tick"
/// (`shell/scene.h:543-595`), and `AudioClipScene::next_timer()` returns it for the whole of
/// playback (`scenes/audio_clip/audio_clip_scene.cpp:1197-1203`). `Shell::next_wake()`'s own
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
/// `derive_leds` is a private member of `Shell` (`shell/shell.h:508`) rather than of the guest, and
/// the writing is done by `Shell::flush`, not by `derive_leds` (`shell/shell.cpp:1005-1031`). The seam
/// that actually exists is `SurfaceSet::set_led`/`set_indicator`, implemented for the guest by F1
/// task 4 (`gui/ui/shell_guest_surfaces.cpp:175`, `:189`). What this task adds is the *tick* that
/// makes any of it run.
///
///  - **An LED a scene declares is the guest's.** `Shell::flush`'s diff loop only ever calls
///    `set_led` for an id with `desired_declared` set (`shell/leds.cpp:75-80`), which
///    `Shell::derive_leds` sets only from the composed `LedFrame` (`shell/shell.cpp:877-896`). The
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
///    `scenes/services/mod_knobs.cpp:691`, `:700`, `:707`), `Shell::flush` calls `set_indicator`
///    only for an indicator declared this tick (`shell/shell.cpp:1035-1047`), and the guest now has a
///    `ModKnobs` in its `Services`. The fix task 7 named is in: `closeGuest()`, the body BOTH of
///    the guest's close paths run (`exitUI`'s long-press Back and `timerCallback`'s short one, F1
///    task 11), calls `indicator_leds::clearKnobIndicatorLevels()` (`hid/led/indicator_leds.h:88`)
///    *before* `close()`, which puts shadow and hardware back in step at level 0 in time for the
///    `focusRegained` that `closeUI` runs on the same call -- every root view's reaches
///    `view.setActiveModControllableTimelineCounter(...)` and so `setKnobIndicatorLevels()`. See
///    that function for the two residual cases.
///
/// @par The gold knobs are the scenes' `ModKnobs`
///
/// The guest's gold knobs go to the shell's router, which hands `Encoder{GoldLower/GoldUpper}`,
/// `EncoderButton{ModEnc0/1}` and `Button{Mod0..7}` to the knobs' service; that service and legacy's
/// `View` keep one focus (`View::knobFocus()`), so what the knobs point at is the same either side
/// of an open guest.
///
///  - **A turn arrives lowered.** `interpretEncoders` has applied legacy's wiggle check and
///    `calcNextKnobSpeed` before it calls `modEncoderAction` (`hid/encoder_input.cpp`), so the turn
///    goes to `Shell::route_lowered_turn`, not into the lowering: the lowering's `action_gold_turn`
///    would swallow the first turn after a pause, which legacy's own (dead) wiggle check never does,
///    and `ModKnobs` would accelerate it a second time.
///  - **A push is fed once.** `Buttons::buttonAction` delivers MOD_ENCODER_0/1 twice: to
///    `buttonAction`, which feeds it, and to `modEncoderButtonAction` in its tail, which here does
///    nothing.
///  - **A mod button is fed as its matrix button.** `Buttons` hands the eight to `modButtonAction`
///    and never to `buttonAction` (`hid/buttons.cpp:99-107`).
///  - **The knob popup outlives the guest.** `ModKnobs`' param popup is a passive overlay; when only
///    such overlays are left the guest closes, and its close hook shows the popup through legacy's
///    display notification for the lifetime it had left, as legacy's own popup outlives the menu.
///
/// **What it costs.** `SoundEditor::modEncoderAction`'s two arms above its forward to `view`
/// (`gui/ui/sound_editor.cpp`) have no counterpart, so under the guest:
///
///  - **In MIDI-learn mode a gold turn edits the knob's param instead of learning it.** Legacy's
///    `UI_MODE_MIDI_LEARN` arm calls `getCurrentMenuItem()->learnKnob(...)` and forwards nothing;
///    `MenuItem::learnKnob` has no port.
///  - **Over automation view a gold turn edits through `ModKnobs`**, where legacy hands it to
///    `automationView.modEncoderAction`.
///
/// @par MIDI learn: two of the four messages are forwarded, and NONE of them is reachable yet
///
/// F1 task 10. `SoundEditor` learns a menu item from four MIDI message types and the guest reaches
/// them by four different mechanisms, only two of which exist:
///
// clang-format off
/// | message | `SoundEditor` | how legacy dispatches | the guest |
/// |---|---|---|---|
/// | program change | `pcReceivedForMidiLearn` (`sound_editor.cpp:1605-1611`) | `getCurrentUI()->pcReceivedForMidiLearn`, `view` as the `else` (`playback/playback_handler.cpp:2897-2901`) | **overridden below**, forwarding to `MenuScene::learn_program_change` through `shellGuestMenuLearnProgramChange` |
/// | note on | `noteOnReceivedForMidiLearn` (`:1595-1597`) | the same shape (`playback_handler.cpp:2968-2971`) | **overridden below**, forwarding to `MenuScene::learn_note_on`, then feeding the shell's own chain |
/// | CC | `midiCCReceived` (`:1600-1608`) | **no `UI` virtual.** A hardcoded `getCurrentUI() == &soundEditor` at `playback_handler.cpp:3164` | **lost.** See below |
/// | pitch bend | `pitchBendReceived` (`:1611-1619`) | the same hardcoded test at `playback_handler.cpp:3091` | **lost**, and doubly: `MenuItem::learnKnob` has no port either -- see the mod-knob `@par` above |
// clang-format on
///
/// The first two are closable because `UI` declares a virtual for each (`ui.h:130`, `:132`) and a
/// guest can override it. The last two are not: `gui/ui/ui.h` declares **exactly two**
/// `*ForMidiLearn` hooks, and closing CC or pitch bend means adding a third and fourth and
/// rewriting two legacy dispatch sites -- a legacy-behaviour change plan F1 does not make. Recorded
/// at `playback_handler.cpp:3164` and `:3091`, at `MidiLearn::cc_received`, and in migration §10.2
/// item 8.
///
/// @warning **All four are unreachable under the guest today, for a reason none of the above
///          names, and it is worth knowing before anyone counts this as working.** Every one of
///          these dispatch arms is gated on `currentUIMode == UI_MODE_MIDI_LEARN`
///          (`playback_handler.cpp:2897`, `:2963`, `:3168`, and `SoundEditor`'s own inner tests),
///          and **exactly two lines in the tree assign it -- but there are THREE ways in, and the
///          count of assignments is not the count of entries**:
///          `grep -rn "currentUIMode = UI_MODE_MIDI_LEARN" src/deluge` returns
///          `View::startMIDILearn` (`gui/views/view.cpp:493`) and `SoundEditor::buttonAction`'s
///          `LEARN` arm (`gui/ui/sound_editor.cpp:520`), and nothing else. The first of those has
///          two callers, not one: `View::buttonAction`'s own `LEARN` arm (`view.cpp:192`) and
///          `SessionView::enterMidiLearnMode` (`gui/views/session_view.cpp:3072-3076`), which the
///          song context menu's MIDI-learn entry reaches
///          (`MidiLearnMode::setupAndCheckAvailability`, `gui/context_menu/midi_learn_mode.cpp:51`).
///          **The conclusion still holds, but as of F2 task 4 one of the three reasons has changed
///          and it is now the interesting one.** `View::buttonAction` is the *root view's* fallback
///          and the guest is the current UI; `SoundEditor` is not open at all. The third,
///          `SessionView::enterMidiLearnMode` reached from the song menu's MIDI-learn entry, **does
///          now run under the guest**: its ported form is `MenuScene::take_action`'s
///          `song_midi_learn` (`scenes/menu/action.h:18`), and `dispatchShellActions`' arm for it
///          calls `midiLearnMode.setupAndCheckAvailability()` exactly as
///          `song::MidiLearn::selectButtonPress` does (`gui/ui/shell_action_dispatch.cpp`, the
///          `MenuAction::OpenSongMidiLearn` arm). So `currentUIMode == UI_MODE_MIDI_LEARN` **is**
///          settable from under the guest now.
///
///          It still does not reach these two overrides, for the reason the same arm's second
///          statement supplies: it ends in `openUI(&midiLearnMode)`, so while the mode is set the
///          **context menu** is `getCurrentUI()`, not the guest -- and every one of these dispatch
///          arms asks `getCurrentUI()` (`playback_handler.cpp:2898`, `:2968`). `MidiLearnMode`
///          overrides neither hook (`gui/context_menu/midi_learn_mode.h:23-38`), so `UI`'s own
///          `return false` (`gui/ui/ui.h:130`, `:132-134`) runs and `view` takes the `else`. Closing
///          the context menu clears the mode again (`MidiLearnMode::acceptCurrentOption` and its
///          `BACK` arm both call `sessionView.exitMidiLearnMode()`,
///          `gui/context_menu/midi_learn_mode.cpp:56`, `:68`; `View::endMIDILearn` clears it at
///          `gui/views/view.cpp:511-513`). **So the reachability of these two overrides is unchanged;
///          what changed is that "nothing under the guest sets the mode" is no longer the reason.**
///          Meanwhile the port's own learn mode (`services::MidiLearn::begin`, reached from its
///          `LEARN` arm) sets a **different bit** -- `MidiLearn::active_`, which `PlaybackHandler`
///          cannot see -- and is itself gated on `SceneStack::input_top().is_root()`, which
///          `MenuScene` is not.
///
///          So this is transport built ahead of its producer, deliberately and in that order: the
///          two overrides are the port of `SoundEditor`'s two hooks and are correct, and they start
///          carrying messages the moment something under the guest sets legacy's mode. **Two
///          separate open items would each do it, and they are not the same item:** `MenuScene`
///          porting `SoundEditor::buttonAction`'s `LEARN` arm
///          (`sound_editor.cpp:505-537`, gated on `getCurrentMenuItem()->allowsLearnMode()`), the
///          arm `services::MidiLearn`'s own class doc already names as `MenuScene`'s to port and
///          not the service's. Until then, asserting "MIDI learn works under the toggle" would be a
///          claim broader than its mechanism.
///
/// @par Shift: two state machines, and what actually keeps them in step
///
/// §2.1's rule is that legacy `Buttons::` state is *read* into `UiContext`, not duplicated.
/// **There is no seam to read it through.** `UiContext` is built by `Shell::make_context`, which is
/// private and fills `.shift`/`.sticky` from the lowering's own fields
/// (`shell/shell.cpp:570-580`), and `InputLowering` has no shift setter -- only
/// `clear_shift_sticky()` (`shell/input_lowering.h:214`) and `set_shift_is_sticky()` (`:221`). So the
/// guest's only lever is *which events it feeds*, and the two machines stay in step only because
/// `buttonAction` below feeds every button legacy hands it, `padAction` feeds every pad, and
/// `InputLowering::toggle_shift` is a line-for-line port of `Buttons::commandToggleShift`
/// (`shell/input_lowering.cpp:306-326` against `hid/buttons.cpp:237-278`).
///
/// Each divergence this once had is closed:
///
///  - **The sticky-shift feature flag.** `opened()` seeds `LoweringConfig::shift_is_sticky` from
///    `RuntimeFeatureSettingType::ShiftIsSticky` through `InputLowering::set_shift_is_sticky`, and
///    `ShiftIsSticky::writeCurrentValue` mirrors a live change.
///  - **`hold_time_ms`.** `feed()` seats `FlashStorage::holdTime` (audio samples) through
///    `InputLowering::set_hold_time_ms` on every pad and button, which is where legacy's own live read
///    happens (`hid/buttons.cpp:264`): `nodes::set_default_hold_time` writes the flash global from
///    inside the guest, so a per-open seed would lag it by a whole session.
///  - **Mod buttons.** Legacy clears `considerShiftReleaseForSticky` on every button press including
///    the eight mod buttons (`hid/buttons.cpp:88-95`); `modButtonAction` feeds them as matrix buttons,
///    so `InputLowering::on_button` clears its own on the same presses.
class ShellHostUI final : public ::UI {
public:
	/// @brief Build the guest over \p board and \p registry.
	/// @param registry The application's scene table. `Shell` copies the pointers out of it into its
	///                 own table (`shell/shell.cpp:176-178`), so a temporary is fine.
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

	/// @brief What the guest runs as it closes, before it empties its stack, given its clock: the hand-off of the knob
	///        popup still showing to legacy's display (installed by `wireShellGuestServices()`, which may name the
	///        scenes). The other passive overlays go with the stack.
	using CloseHook = void (*)(uint32_t now_ms);
	/// @brief Install @p hook, or none.
	static void setCloseHook(CloseHook hook) { closeHook_ = hook; }
	/// @brief The hook installed; none when none is.
	static CloseHook closeHook() { return closeHook_; }

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
	ActionResult buttonAction(deluge::hid::Button b, bool on) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset) override;
	void selectEncoderAction(int8_t offset) override;
	/// @brief Route legacy's lowered gold turn to the knobs' service; @see `@par The gold knobs`.
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	/// @brief Nothing: `buttonAction` already fed the press (`Buttons` delivers it twice).
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;
	/// @brief Feed mod button @p whichButton as its matrix button.
	void modButtonAction(uint8_t whichButton, bool on) override;
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
	bool pcReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t program) override;
	bool noteOnReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t note, int32_t velocity) override;
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
	/// `MenuScene::is_grid_overlay()` is true (`scenes/menu/menu_scene.h:505-506`), so the first
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
	///          `Shell::on_encoder_edges` takes an `int8_t` (`shell/shell.h:295`), so the product
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
	/// authoritative immediately after a `tick()` recomputed it (`shell/shell.cpp:536`).
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

	/// @brief Hand the knob indicator rings back to legacy and `close()` this UI.
	///
	/// The body both of the guest's close paths share: `exitUI()`'s long-press Back, and
	/// `timerCallback()`'s "the shell's stack emptied during the tick", which is what a *short* Back
	/// at the ported menu's root produces (`MenuScene`'s `Action::Kind::Remove`,
	/// `scenes/menu/menu_scene.cpp:2057-2071`). Neither the ring hand-back nor the close may be
	/// skipped on either path; see the definition for why the order between them is fixed.
	///
	/// Arms no timer: `close()` is `closeUI(this)`, which unsets `TimerName::UI_SPECIFIC` itself
	/// (`gui/ui/ui.cpp:197`). Callers must not re-arm after it.
	void closeGuest();

	/// @brief Does the scene that would receive this pad claim it? Spec §3.1's predicate.
	///
	/// `input_top()`, not `top()`: a passive overlay (popup, console, working animation) is
	/// `input_transparent()` and is not the scene whose grid this is (`shell/scene.h:445`). Asking
	/// `top()` would let such an overlay's default `All` swallow a pad the menu beneath it would
	/// have forwarded.
	bool claimsGridPad(int32_t x, int32_t y, int32_t velocity);

	/// The scene `opened()` pushes; `SceneKind::Null` until a caller sets one.
	deluge::ui::shell::SceneId initial_{};
	/// The hook `closeGuest()` runs; see `CloseHook`.
	static inline CloseHook closeHook_ = nullptr;
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
