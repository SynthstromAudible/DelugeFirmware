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
#include "gui/ui/shell_clock.h"
#include "gui/ui/shell_guest_surfaces.h"
#include "gui/ui/ui.h"
#include "shell/profiles/deluge.h"
#include "shell/scene.h"
#include "shell/shell.h"
#include "ui/core/input.h"

#include <cstdint>

namespace deluge::gui::ui {

/// @brief The legacy `UI` that owns a whole `Shell` and renders it -- the *shell as guest*.
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
/// `Shell::pump_input`, which only `Shell::tick` calls. So a hook can
/// never report what a scene did with a press, and every return value below is decided statically
/// from what legacy does with it -- see each hook's note. The tick that drains the queue is
/// `timerCallback`, and every one of those hooks arms it through `armTickSoon` -- see
/// `@par The tick`.
///
/// **camelCase, deliberately**: everything here is a legacy `UI`
/// override or something a legacy call site names, and a façade over legacy spells things the way
/// legacy does. The `Shell` it drives is `snake_case` on the other side of that seam.
///
/// @par The virtuals this class does NOT override, and why
///
/// The failure mode this list exists for is a guest that silently inherits a `UI` default where it
/// should have forwarded: nothing breaks until a scene needs that hook, and by then it reads as a
/// scene bug. `::UI` declares 34 virtuals (`gui/ui/ui.h`, `grep -n virtual`); twenty-three are
/// overridden below and the other eleven are listed here with the reason.
///
// clang-format off
/// | not overridden | line | why |
/// |---|---|---|
/// | `~UI` | `ui.h:94` | Defaulted. This class owns its members by value; the implicit destructor is correct. |
/// | `graphicsRoutine` | `ui.h:108` | **Deliberately kept.** The base runs the *root* view's per-frame animation whenever `canSeeViewUnderneath()` (`ui.cpp:76-80`), and `SoundEditor` does not override it either -- so the clip view under an open menu keeps its playhead moving. Overriding it would stop that. |
/// | `toClipMinder` | `ui.h:122` | Null, as `SoundEditor` leaves it. A guest is not a clip minder; `currentUIIsClipMinderScreen()` must keep answering about the view underneath. |
/// | `toTimelineView` | `ui.h:127` | Null, as `SoundEditor` leaves it, for the same reason. |
/// | `getUI` | `ui.h` | Returns `this`, which is right: the guest is not a chooser that opens a different UI. |
/// | `getUIContextType` | `ui.h` | Defaults to `getUIType()`, as `SoundEditor` leaves it. |
/// | `getUIModControllableContext` | `ui.h` | `NONE`, as `SoundEditor` leaves it. |
/// | `canDisplayFavourites` | `ui.h:200` | False, as `SoundEditor` leaves it. |
/// | `isFavouritesVisible` | `ui.h:201` | False, as `SoundEditor` leaves it. |
/// | `isBanksVisible` | `ui.h:202` | False, as `SoundEditor` leaves it. |
// clang-format on
///
/// One answer is a gap rather than settled: `displayOrLanguageChanged`, which **is** overridden below but only to
/// ask the shell for a repaint -- see its own note for what that does and does not buy.
///
/// @warning The `UI` virtuals span the whole of `gui/ui/ui.h`'s class, not just its first block: `getUIType()`,
///          near its end, is **pure** -- a guest written to the first block alone would not compile.
///
/// @par The tick: `timerCallback` is the only thing that runs the shell
///
/// **Nothing inside the guest routes, renders, or lights an LED except `Shell::tick`.** The input
/// hooks below only push into `InputLowering`'s queues; routing happens in `Shell::pump_input`,
/// which only `tick()` calls, and the LEDs are composed and sent by
/// `Shell::derive_leds`/`Shell::flush` at the last step of that same function.
/// So a guest whose timer is not armed is a guest that is frozen, however much legacy is feeding
/// it -- which is why the arm is re-established in every place legacy can leave it unarmed, not
/// only in `opened()`.
///
/// `TimerName::UI_SPECIFIC` is the timer, dispatched to `getCurrentUI()` by
/// `UITimerManager::routine` (`gui/ui_timer_manager.cpp:143-149`). Legacy clears it out from under
/// its owner at four places -- `changeUIAtLevel` (`gui/ui/ui.cpp`), `changeRootUI` (the only conditional one: it clears
/// only while `currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW`), `closeUI` and `openUI` -- all of them *before* the
/// `opened()`/`focusRegained()` that follows, so arming from those two hooks is safe and is the only thing that makes
/// the guest survive a UI opening and closing over it. The guest arms unconditionally, so the one conditional clear
/// costs it nothing; it is named because a reader counting clears would otherwise miscount.
///
/// @par The unit mismatch: an absolute deadline against a duration
///
/// `Shell::next_wake()` is an **absolute** millisecond deadline, recomputed at the bottom of every
/// tick. `UITimerManager::setTimer` takes a **duration**
/// and converts it to audio samples with `ms * 44` (`gui/ui_timer_manager.cpp:252-254`). The
/// conversion is therefore `in = (int32_t)(deadline - now)`, in the wrap-safe difference form
/// `Shell::compute_next_wake` and `RepaintGate` both use -- a plain `<` across the ~49.7-day `uint32_t` wrap picks the
/// wrong one of two straddling deadlines -- and then clamped to `kMinRearmMs .. kMaxRearmMs`.
///
///  - **A deadline already past** (`in <= 0`) arms `kMinRearmMs`, not zero. It is not an error: the
///    shell asks for `now_ms` routinely (a router repaint armed at a zero-millisecond deadline),
///    and there is nothing to catch up on -- the tick that just ran was
///    the one it was waiting for.
///  - **Arming earlier than asked is always safe; arming later is not.** The next tick recomputes
///    `next_wake()` from scratch, so an early wake costs one tick and corrects itself. That is why
///    the clamp is one-sided in its consequences and why `kMaxRearmMs` may exist at all.
///
/// @par Why the floor is 1 ms and not 0 -- a 0 floor is a busy loop
///
/// `Scene::next_timer()` may legally return **0**, meaning "next tick"
/// (`Scene::next_timer`), and `AudioClipScene::next_timer()` returns it for the whole of
/// playback. `Shell::next_wake()`'s own
/// `@warning` records that a host cannot tell that apart from "wake once, immediately", and that the
/// shell "only ticks when its host asks it to" -- **this class is that host**, so the answer is here.
///
/// `setTimer(UI_SPECIFIC, 0)` would put `triggerTime` at exactly `AudioEngine::audioSampleTimer`
/// (`gui/ui_timer_manager.cpp:256-265`) and `timeNextEvent` with it. **`kMinRearmMs` (1 ms = 44
/// samples) is the smallest duration `setTimer`'s `ms * 44` can express that leaves `triggerTime`
/// strictly in the future**, and that is the whole of the argument: with zero, `triggerTime` is
/// already behind the audio sample timer by the time `UITimerManager::routine` is next called at all
/// -- audio advances at 44.1 kHz, far faster than the main loop the routine is called from -- so
/// `timeNextEvent` is past, the early return at `gui/ui_timer_manager.cpp:55-58` does not fire, and
/// the timer's own test at `:64-65` is true. The whole 29-timer sweep and the shell's whole tick
/// would then run on **every** pass of the main loop, for as long as the guest kept re-arming at
/// zero. With 44 samples they run at most once a millisecond.
///
/// @note Precisely at the instant of arming, `timeNextEvent - audioSampleTimer` is **0**, and
///       `routine`'s guard is `>= 0`, so the early return *is* taken and the timer does not fire
///       that instant -- it becomes due one audio sample (~22.7 us) later. A zero arm does not
///       "defeat the early return"; the conclusion rests on the sample timer advancing between two
///       calls of `routine`, not on the guard being wrong.
///
/// @warning **1 ms is not legacy's animation cadence, and a future tenant will want the difference.**
///          Legacy's per-frame hook is `graphicsRoutine`, whose one external driver is
///          `TimerName::GRAPHICS_ROUTINE`, re-armed at **15 ms** (`gui/ui_timer_manager.cpp:212-217`;
///          `grep -rn "graphicsRoutine()" src/deluge` gives exactly one call site outside the class
///          hierarchy). So a scene answering 0 gets ~1000 ticks/s here where legacy's equivalent runs
///          at ~66. 1 ms is nonetheless the right floor **for the scenes that run here**: the first is
///          `MenuScene`, which has no zero-timer arm at all, while a floor of 15 ms would put 15 ms
///          of latency on the repaint after *every* button press -- and legacy has none there, since
///          `doAnyPendingUIRendering` runs from the main loop rather than from a timer
///          (`gui/ui/ui.cpp:405-426`). The condition that ends this answer is a tenant whose
///          `next_timer()` returns 0, i.e. `AudioClipScene`; what it needs is the third answer
///          `Shell::next_wake()`'s warning already asks for -- a distinct "every frame" cadence --
///          not a different floor here.
///
/// @par The LEDs: the ownership rule, in both directions
///
/// **There is no `LedSink`, and this class implements no LED sink.** The guest's `derive_leds` does
/// not write through an `indicator_leds` `LedSink`: `derive_leds` is a private member of `Shell`
/// rather than of the guest, and the writing is done by `Shell::flush`, not by `derive_leds`. The
/// seam that actually exists is `SurfaceSet::set_led`/`set_indicator`, implemented for the guest by
/// `GuestSurfaceSet`. What this class adds is the *tick* that makes any of it run.
///
///  - **An LED a scene declares is the guest's.** `Shell::flush`'s diff loop only ever calls
///    `set_led` for an id with `desired_declared` set (`led_send_needed`), which
///    `Shell::derive_leds` sets only from the composed `LedFrame`. The
///    guest then takes the LED away from legacy's blinker for as long as it holds it:
///    `GuestSurfaceSet::set_led` calls `indicator_leds::setLedState` with the default
///    `allowContinuedBlinking = false`, whose first act is `stopLedBlinking(led)`
///    (`hid/led/indicator_leds.cpp:42-52`, `:141-149`) -- so legacy's `LED_BLINK` timeout stops
///    writing it (`updateBlinkingLedStates`, `:116-139`, which only touches `active` blinkers).
///  - **An LED no scene declares is legacy's.** An undeclared id fails `led_send_needed`'s
///    `desired_declared` test above, so no tick writes it. One the guest wrote and nothing declares
///    any more is handed back once (`SurfaceSet::release_led`): `GuestSurfaceSet::release_led`
///    restores the state legacy had it in when the guest took it. `opened()` starts each session
///    with no LED held (`GuestSurfaceSet::begin_session`).
///  - **The handover back is coherent for LEDs and stale for indicators.** `setLedState` writes
///    legacy's `ledStates[]` shadow (`hid/led/indicator_leds.cpp:49`), so a legacy write after the
///    guest closes is a write onto a true shadow. `GuestSurfaceSet::set_indicator` cannot do the
///    same -- it calls `deluge_control_set_indicator` directly, because `KnobIndicator` is already
///    the four segment levels and there is no scalar to hand `actuallySetKnobIndicatorLevel` -- so
///    `knobIndicatorLevels[]` goes stale, and that function's early-out then suppresses a later
///    legacy write of the level the shadow still holds (`hid/led/indicator_leds.cpp:187-191`).
///    **This is live.** The one declarer is `services::ModKnobs` (`grep -rn "frame.indicator("
///    src/scenes`), `Shell::flush` calls `set_indicator` only for an indicator declared this tick,
///    and the guest has a `ModKnobs` in its `Services`. The fix is in: `closeGuest()`, the body BOTH
///    of the guest's close paths run (`exitUI`'s long-press Back and `timerCallback`'s short one),
///    calls `indicator_leds::clearKnobIndicatorLevels()` (`hid/led/indicator_leds.h:88`)
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
///    `buttonAction`, which feeds it, and to `modEncoderButtonAction` in its tail, which does
///    nothing then.
///  - **In the automation view's editor the knobs are the view's.** A turn and a push go to
///    `automationView` (`forwardAutomationGoldTurn`, `forwardAutomationGoldPush`), which edits its selected
///    param, as `SoundEditor::modEncoderAction` hands them to the view; the push takes `modEncoderButtonAction`'s
///    delivery and is not fed. On the view's overview the knobs' service does what the view would, and keeps the
///    menu's readout following the knob.
///  - **A mod button is fed as its matrix button.** `Buttons` hands the eight to `modButtonAction`
///    and never to `buttonAction` (`hid/buttons.cpp:99-107`).
///  - **The knob popup outlives the guest.** `ModKnobs`' param popup is a passive overlay; when only
///    such overlays are left the guest closes, and its close hook shows the popup through legacy's
///    display notification for the lifetime it had left, as legacy's own popup outlives the menu.
///
/// The scene sees a turn first: in MIDI-learn mode the menu takes it and learns that knob, as
/// `SoundEditor::modEncoderAction` does (`MenuLearn::gold_turn`).
///
/// **What it costs.** On the automation view's overview a gold turn edits through `ModKnobs`, where legacy hands
/// every turn to `automationView.modEncoderAction` -- in learn mode too; both edit the knob's param there.
///
/// @par MIDI learn
///
/// `PlaybackHandler` offers every note-on, program change, and CC or pitch bend outside an MPE zone to the current UI
/// before acting on it (`UI::noteOnReceivedForMidiLearn` and its three siblings). This class overrides all four: each
/// offer becomes a `UiEvent` MIDI kind routed through the guest's chain with `Shell::route_midi`, and the answer
/// goes back to `PlaybackHandler`, which acts on the message only when nothing used it.
///
///  - **The input-top scene first.** The menu learns into its focused item while its own learn mode is on
///    (`MenuLearn::offer`, `SoundEditor`'s answers), and uses nothing otherwise.
///  - **Then `services::MidiLearn`**, the song view's learn, in its own learn mode.
///  - **Then the base `UI` answer**, which is legacy's learn mode reaching `View` -- the song context menu's MIDI-learn
///    entry sets it from under the guest.
///
/// No learn mode is mirrored into `currentUIMode`: each learner keeps its own, and a message is used by whichever is
/// on.
///
/// @par Shift: two state machines, and what actually keeps them in step
///
/// The rule is that legacy `Buttons::` state is *read* into `UiContext`, not duplicated.
/// **There is no seam to read it through.** `UiContext` is built by `Shell::make_context`, which is
/// private and fills `.shift`/`.sticky` from the lowering's own fields, and `InputLowering` has no
/// shift setter -- only `clear_shift_sticky()` and `set_shift_is_sticky()`. So the
/// guest's only lever is *which events it feeds*, and the two machines stay in step only because
/// `buttonAction` below feeds every button legacy hands it, `padAction` feeds every pad, and
/// `InputLowering::toggle_shift` is a line-for-line port of `Buttons::commandToggleShift`
/// (against `hid/buttons.cpp:237-278`).
///
/// The cases that could diverge are closed:
///
///  - **The sticky-shift feature flag.** `opened()` seeds `LoweringConfig::shift_is_sticky` from
///    `RuntimeFeatureSettingType::ShiftIsSticky` through `InputLowering::set_shift_is_sticky`, and
///    `ShiftIsSticky::writeCurrentValue` mirrors a live change.
///  - **`hold_time_ms`.** `feed()` seats `FlashStorage::holdTime` (audio samples) through
///    `InputLowering::set_hold_time_ms` on every pad and button, which is where legacy's own live read
///    happens (`Buttons::commandToggleShift`): `nodes::set_default_hold_time` writes the flash global from
///    inside the guest, so a per-open seed would lag it by a whole session.
///  - **Mod buttons.** Legacy clears `considerShiftReleaseForSticky` on every button press including
///    the eight mod buttons (`hid/buttons.cpp:88-95`); `modButtonAction` feeds them as matrix buttons,
///    so `InputLowering::on_button` clears its own on the same presses.
class ShellHostUI final : public ::UI {
public:
	/// @brief Build the guest over \p board and \p registry.
	/// @param registry The application's scene table. `Shell` copies the pointers out of it into its
	///                 own table, so a temporary is fine.
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
	/// Each menu entry point knows which node the caller wanted; this is how it says so. Defaults to a `SceneId` whose
	/// kind is `SceneKind::Null`, which no registry can resolve -- so a caller that forgets to set one gets a refused
	/// `opened()` (see below) rather than an empty guest.
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

	/// @brief The shell this guest runs; the input lowering, the services and the tick are reached
	///        through here.
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
	///        (`gui/ui_timer_manager.cpp:252-254`).
	static constexpr int32_t kTimerSamplesPerMs = SampleClockMs::kSamplesPerMs;
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
	/// @brief Legacy's horizontal scroll, which the sample browser starts through the host, has ended: leave
	///        `UI_MODE_HORIZONTAL_SCROLL` and take the main columns back (`SampleBrowser::scrollFinished`).
	void scrollFinished() override;
	ActionResult timerCallback() override;
	/// @brief Arm a tick at once if the shell has model facts to drain and this guest is the current UI.
	///
	/// A fact raised between ticks -- by a legacy callback, or through `Shell::route_lowered_turn`
	/// -- reaches the shell's slot of the dirty set however the legacy drain runs, but a guest with no
	/// deadline is asleep until something arms its timer. Called by `drain_notifications` on every
	/// pass of the app loop.
	void wakeForPendingFacts();
	/// @brief Re-read the facts about the root UI that the guest holds (`refreshShellRootFacts`), then the greyout
	///        and pad regions they decide. For whatever changes the root, or the automation view's editor, while the
	///        guest is up.
	void refreshRootFacts();

	/// @brief Close the guest now, its whole stack with it: `SoundEditor::exitCompletely()` for a caller that changes
	///        the root on the same call and so cannot leave the close to the next tick (`forwardMenuViewButton`'s
	///        SESSION). The same hand-back as the guest's own two close paths (`closeGuest`).
	void closeNow() { closeGuest(); }

	/// @brief Mark this visit as one legacy runs with no editor open: the drum creator's browser or take, opened
	///        straight over the clip view. Its close then leaves `currentUIMode` alone, as the legacy browser's and
	///        recorder's own exits do; every other close ends the mode, as `SoundEditor::exitCompletely` does. Call it
	///        after the open succeeded; the next `opened()` forgets it.
	void markVisitWithoutEditor() { visitWithoutEditor_ = true; }
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
	/// @name MIDI learn offers, routed through the guest's chain; see `@par MIDI learn`.
	/// @{
	bool noteOnReceivedForMidiLearn(MIDICable& cable, int32_t channelOrZone, int32_t note, int32_t velocity) override;
	bool pcReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t program) override;
	bool ccReceivedForMidiLearn(MIDICable& cable, int32_t channelOrZone, int32_t cc, int32_t value) override;
	bool pitchBendReceivedForMidiLearn(MIDICable& cable, int32_t channel, uint8_t data1, uint8_t data2) override;
	/// @}
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	UIType getUIType() override { return UIType::SHELL_HOST; }

	/// @brief The shell's clock, in milliseconds, for every hook that has to stamp one, and for
	///        host code that reads a scene's clock-driven state between ticks.
	///
	/// It is legacy's UI clock: `AudioEngine::audioSampleTimer` at 44 samples a millisecond, the count
	/// `UITimerManager::setTimer` arms legacy's timers in and the guest's own wake (`UI_SPECIFIC`) with
	/// them. On one base, a blink the shell times and one legacy times keep their phase, and both stop
	/// when the audio stops, as legacy's do. A wall clock is forbidden inside `src/shell/**` and
	/// `src/ui/**` (`shell-no-clock`); supplying one is exactly what a host is for.
	///
	/// @return Milliseconds since start, wrapping every ~49.7 days, carried on across the counter's own
	///         wrap (`SampleClockMs`). The shell's own arithmetic is unsigned differences throughout, so
	///         the wrap costs one interval, not correctness.
	uint32_t nowMs() const;

	/// @brief The clock legacy times an encoder's acceleration on: the scheduler's seconds (`getSystemTime()`), as
	///        `ContinuousEncoder::calcNextKnobSpeed` and `HorizontalMenu::calcNextKnobSpeed` read them, in
	///        milliseconds. Legacy keeps the two apart, and so does the guest (`UiContext::acceleration_clock_ms`).
	/// @return Milliseconds since the scheduler started; 0 before it has.
	static uint32_t accelerationClockMs();

private:
	/// The milliseconds `nowMs()` counts from the audio-sample counter; a reading moves it on, hence `mutable`.
	mutable SampleClockMs clock_;

	/// @brief Copy the shell's grid into legacy's pad image, for the columns \p x0 to \p x1 and the
	///        rows \p whichRows selects, and report whether the guest owns \p region.
	///
	/// The one body behind `renderMainPads` and `renderSidebar`, which differ only in their column
	/// span and in which of legacy's two send paths picks the result up.
	/// @param whichRows Bit `y` selects row `y`; legacy's own convention
	///                  (`PadGrid`'s row mask, matching what `renderMainPads(whichRows)` consumes).
	/// @param image     Legacy's pad image, or null for the opacity-query form.
	/// @param x0        First grid/image column to copy.
	/// @param x1        One past the last.
	/// @param region    The region those columns are.
	/// @return Whether a stacked scene draws \p region (`GuestSurfaceSet::owned_regions`), which is
	///         legacy's "did I use these rows up" (`UI::renderMainPads`). While it is false nothing
	///         is written: those pads are the UI underneath's. `MenuScene` draws no region, so a
	///         menu alone answers false for both, as `SoundEditor` does (`gui/ui/sound_editor.cpp:217-247`).
	bool serveGrid(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth], int32_t x0, int32_t x1,
	               deluge::ui::shell::PadRegions region);

	/// @brief Feed one already-detented encoder turn to the shell as A-pin edges.
	///
	/// Legacy's four function encoders reach a `UI` in **detents** -- `interpretEncoders` calls
	/// `DetentedEncoder::take()` and hands the result over (`hid/encoder_input.cpp:112`,
	/// `hid/encoders.cpp:28-38`) -- while the shell's entry point takes **edges** and divides by
	/// `LoweringConfig::edges_per_detent` itself. So this
	/// multiplies back up. Feeding the detent count straight through would halve every turn.
	///
	/// @warning **The `int8_t` ceiling, and why this saturates instead of wrapping.**
	///          `Shell::on_encoder_edges` takes an `int8_t`, so the product
	///          must fit in -128..127; the warning on `InputLowering::apply_encoder_edges` records what
	///          happens when it does not. **The guest can exceed it.** Legacy's
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
	///                (`InputLowering::apply_encoder_edges`) and a profile may order it differently.
	///                A role no index carries is dropped.
	/// @param detents The signed detent count legacy handed over.
	void feedDetents(deluge::ui::EncoderRole role, int32_t detents);

	/// @brief Feed one decoded board event, stamped with `nowMs()`.
	/// @param ev The event.
	void feed(const DelugeInputEvent& ev);
	/// @brief Re-read legacy's hold time into the lowering's tap threshold (`FlashStorage::holdTime`, in samples): the
	///        first thing every feed does.
	void syncHoldTime();
	/// @brief Feed a pad the guest claimed, with why the menu may take it as a shortcut without SHIFT
	///        (`Shell::on_host_pad`).
	/// @param shortcutWithoutShift An audition held over the instrument clip view made the press a shortcut
	///        (`ShortcutPadGate::Audition`); false for every other press and every release.
	void feedPad(uint8_t x, uint8_t y, int32_t velocity, bool shortcutWithoutShift);

	/// @brief Re-arm `TimerName::UI_SPECIFIC` from the shell's own deadline.
	///
	/// Called from `timerCallback` and nowhere else, because `Shell::next_wake()` is only
	/// authoritative immediately after a `tick()` recomputed it.
	/// See `@par The unit mismatch` on the class for the conversion and both clamps.
	///
	/// A `std::nullopt` deadline means "nothing is armed; sleep until the next input" -- so nothing
	/// is armed here either, and the timer stays inactive (`UITimerManager::routine` cleared it
	/// before calling, `gui/ui_timer_manager.cpp:67`). **With one exception**: while the lowering
	/// holds the SD-routine lock its deferred queue is waiting on a fact no scene can express as a
	/// deadline, so the guest keeps polling. That is what makes the release time-driven rather than
	/// input-driven.
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
	/// `MenuScene::handle_event`'s Back arm, `scenes/menu/menu_scene.cpp`). Neither the ring hand-back nor the close
	/// may be skipped on either path; see the definition for why the order between them is fixed.
	///
	/// Arms no timer: `close()` is `closeUI(this)`, which unsets `TimerName::UI_SPECIFIC` itself
	/// (`gui/ui/ui.cpp:227`). Callers must not re-arm after it.
	void closeGuest();

	/// @brief Does the scene that would receive this pad claim it? The grid-claim predicate.
	///
	/// `input_top()`, not `top()`: a passive overlay (popup, console, working animation) is
	/// `Scene::input_transparent()` and is not the scene whose grid this is. Asking
	/// `top()` would let such an overlay's default `All` swallow a pad the menu beneath it would
	/// have forwarded.
	/// @param x        The pad's column.
	/// @param y        The pad's row.
	/// @param velocity The press velocity; 0 is a release.
	/// @return Whether the guest's scenes claim the pad.
	bool claimsGridPad(int32_t x, int32_t y, int32_t velocity);
	/// @brief The pads whose press the shell was fed and whose release it has not yet been: their releases are fed
	///        too, claimed or not.
	deluge::ui::PadMask fedPads_{};
	/// Whether the visit now open is one legacy runs with no editor (`markVisitWithoutEditor`).
	bool visitWithoutEditor_ = false;

	/// @brief Hand each pad region between legacy and the guest when the stack changes who owns it,
	///        and re-read the greyout when the stack changes it.
	///
	/// A region the stack comes to draw is taken (`GuestSurfaceSet::take_grid`); a region it stops
	/// drawing is handed back by asking legacy to redraw just that half, and so is the sidebar when
	/// the marker editor starts or stops answering for it (`answeredRegions`). Legacy re-reads the greyout
	/// only when a UI opens or closes (`PadLEDs::reassessGreyout`), so the guest asks again when its
	/// own stack moves.
	void syncPads();

	/// @brief The pad regions the guest answers `renderMainPads`/`renderSidebar` for itself.
	///
	/// The regions its stack draws, plus the sidebar while the marker editor is the input top and
	/// the sidebar is not drawn: `renderSidebar` then answers through
	/// `renderClipViewSidebarUnderMarker` rather than leaving it to the UI underneath.
	/// @return The owned regions, with `Sidebar` added in the marker's case.
	deluge::ui::shell::PadRegions answeredRegions();

	/// @brief Ask legacy to redraw every row of @p regions, starting from this guest.
	/// @param regions The regions whose answer changed; `None` asks nothing.
	void handBack(deluge::ui::shell::PadRegions regions);

	/// @brief The greyout the guest reports: the topmost non-empty mask on its stack
	///        (`SceneStack::topmost_greyout`), whichever regions it draws. Legacy applies it once,
	///        in its send path.
	deluge::ui::grid::GreyoutMask greyoutMask();

	/// The scene `opened()` pushes; `SceneKind::Null` until a caller sets one.
	deluge::ui::shell::SceneId initial_{};
	/// The hook `closeGuest()` runs; see `CloseHook`.
	static inline CloseHook closeHook_ = nullptr;
	/// @brief The config `shell_` was built with, kept so `feedDetents` can read
	///        `edges_per_detent` back.
	///
	/// `InputLowering` exposes `profile()` and `shift_is_sticky()` but not the edge ratio, and a
	/// literal `2` here would silently go stale the day a real config is seated. Declared before `shell_` so it is
	/// alive when the constructor hands it over.
	///
	/// `greys_in_send_path` is set because legacy's `prepareColour` greys every pad the guest sends.
	deluge::ui::shell::ShellConfig config_{.greys_in_send_path = true};
	GuestSurfaceSet surfaces_;
	deluge::ui::shell::Shell shell_;
	/// The regions the guest owned, the regions it answered for (`answeredRegions`) and the greyout
	/// it reported when `syncPads` last ran.
	deluge::ui::shell::PadRegions padsOwned_ = deluge::ui::shell::PadRegions::All;
	deluge::ui::shell::PadRegions padsAnswered_ = deluge::ui::shell::PadRegions::All;
	deluge::ui::grid::GreyoutMask padsGreyout_{};
};

} // namespace deluge::gui::ui

/// @brief The one guest, named the way legacy names its UIs (`extern SoundEditor soundEditor;`).
///
/// Constructed over `deluge::scenes::scene_registry()`. The menu entry points push it with
/// `openUI(&shellHostUI)` while `RuntimeFeatureSettingType::LegacyMenu` is Off (`shellMenuEnabled`).
extern deluge::gui::ui::ShellHostUI shellHostUI;
