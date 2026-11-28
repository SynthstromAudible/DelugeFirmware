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
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene.h"
#include "ui/shell/shell.h"
#include "ui/widgets/core/input.h"

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
/// | not overridden | declared in | why |
/// |---|---|---|
/// | `~UI` | `ui.h` | Defaulted. This class owns its members by value; the implicit destructor is correct. |
/// | `graphicsRoutine` | `ui.h` | **Deliberately kept.** The base runs the *root* view's per-frame animation whenever `canSeeViewUnderneath()` (`UI::graphicsRoutine` in `ui.cpp`), and `SoundEditor` does not override it either -- so the clip view under an open menu keeps its playhead moving. Overriding it would stop that. |
/// | `toClipMinder` | `ui.h` | Null, as `SoundEditor` leaves it. A guest is not a clip minder; `currentUIIsClipMinderScreen()` must keep answering about the view underneath. |
/// | `toTimelineView` | `ui.h` | Null, as `SoundEditor` leaves it, for the same reason. |
/// | `getUI` | `ui.h` | Returns `this`, which is right: the guest is not a chooser that opens a different UI. |
/// | `getUIContextType` | `ui.h` | Defaults to `getUIType()`, as `SoundEditor` leaves it. |
/// | `getUIModControllableContext` | `ui.h` | `NONE`, as `SoundEditor` leaves it. |
/// | `canDisplayFavourites` | `ui.h` | False, as `SoundEditor` leaves it. |
/// | `isFavouritesVisible` | `ui.h` | False, as `SoundEditor` leaves it. |
/// | `isBanksVisible` | `ui.h` | False, as `SoundEditor` leaves it. |
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
/// `UITimerManager::routine` (`gui/ui_timer_manager.cpp`). Legacy clears it out from under
/// its owner at four places -- `changeUIAtLevel` (`gui/ui/ui.cpp`), `changeRootUI` (the only conditional one: it clears
/// only while `currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW`), `closeUI` and `openUI` -- all of them *before* the
/// `opened()`/`focusRegained()` that follows, so arming from those two hooks is safe and is the only thing that makes
/// the guest survive a UI opening and closing over it. The guest arms unconditionally, so the one conditional clear
/// costs it nothing; it is named because a reader counting clears would otherwise miscount.
///
/// @par The timer's conversion and floor
///
/// The arithmetic that turns `Shell::next_wake()` into a `UI_SPECIFIC` duration, and why its floor is 1 ms, are the
/// shared runtime's (`ShellRuntime`, `@par The unit mismatch` and `@par Why the floor is 1 ms`).
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
///    (`hid/led/indicator_leds.cpp`) -- so legacy's `LED_BLINK` timeout stops
///    writing it (`updateBlinkingLedStates`, which only touches `active` blinkers).
///  - **An LED no scene declares is legacy's.** An undeclared id fails `led_send_needed`'s
///    `desired_declared` test above, so no tick writes it. One the guest wrote and nothing declares
///    any more is handed back once (`SurfaceSet::release_led`): `GuestSurfaceSet::release_led`
///    restores the state legacy had it in when the guest took it. `opened()` starts each session
///    with no LED held (`GuestSurfaceSet::begin_session`).
///  - **The handover back is coherent for LEDs and stale for indicators.** `setLedState` writes
///    legacy's `ledStates[]` shadow (`hid/led/indicator_leds.cpp`), so a legacy write after the
///    guest closes is a write onto a true shadow. `GuestSurfaceSet::set_indicator` cannot do the
///    same -- it calls `deluge_control_set_indicator` directly, because `KnobIndicator` is already
///    the four segment levels and there is no scalar to hand `actuallySetKnobIndicatorLevel` -- so
///    `knobIndicatorLevels[]` goes stale, and that function's early-out then suppresses a later
///    legacy write of the level the shadow still holds (`hid/led/indicator_leds.cpp`).
///    **This is live.** The one declarer is `services::ModKnobs` (`grep -rn "frame.indicator("
///    src/ui/scenes`), `Shell::flush` calls `set_indicator` only for an indicator declared this tick,
///    and the guest has a `ModKnobs` in its `Services`. The fix is in: `closeGuest()`, the body BOTH
///    of the guest's close paths run (`exitUI`'s long-press Back and `timerCallback`'s short one),
///    calls `indicator_leds::clearKnobIndicatorLevels()` (`hid/led/indicator_leds.h`)
///    *before* `close()`, which puts shadow and hardware back in step at level 0 in time for the
///    `focusRegained` that `closeUI` runs on the same call -- every root view's reaches
///    `view.setActiveModControllableTimelineCounter(...)` and so `setKnobIndicatorLevels()`. See
///    that function for the two residual cases.
///
/// @par The gold knobs are the scenes' `ModKnobs`
///
/// The guest's gold knobs go to the shell's router, which hands `Encoder{GoldLower/GoldUpper}`,
/// `EncoderButton{ModEnc0/1}` and `Button{Mod0..7}` to the knobs' service. With no guest open the root views
/// reach the same service through `gui/ui/root_knobs`, and the service and `View` keep one focus
/// (`View::knobFocus()`), so what the knobs point at is the same either side of an open guest.
///
///  - **A turn arrives lowered.** `interpretEncoders` has accelerated it (`ContinuousEncoder::actionTurns`) before
///    it calls `modEncoderAction`, so the turn goes to `Shell::route_lowered_turn`, not into the lowering, where
///    `ModKnobs` would accelerate it a second time.
///  - **A push is fed once.** `Buttons::buttonAction` delivers MOD_ENCODER_0/1 twice: to
///    `buttonAction`, which feeds it, and to `modEncoderButtonAction` in its tail, which does
///    nothing then.
///  - **In the automation view's editor the knobs are the view's.** A turn and a push go to
///    `automationView` (`forwardAutomationGoldTurn`, `forwardAutomationGoldPush`), which edits its selected
///    param, as `SoundEditor::modEncoderAction` hands them to the view; the push takes `modEncoderButtonAction`'s
///    delivery and is not fed. On the view's overview the knobs' service does what the view would, and keeps the
///    menu's readout following the knob.
///  - **A mod button is fed as its matrix button.** `Buttons` hands the eight to `modButtonAction`
///    and never to `buttonAction` (`hid/buttons.cpp`).
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
/// (against `hid/buttons.cpp`).
///
/// The cases that could diverge are closed:
///
///  - **The sticky-shift feature flag.** `opened()` seeds `LoweringConfig::shift_is_sticky` from
///    `RuntimeFeatureSettingType::ShiftIsSticky` through `InputLowering::set_shift_is_sticky`, and
///    `ShiftIsSticky::writeCurrentValue` mirrors a live change.
///  - **`hold_time_ms`.** `feed()` seats `FlashStorage::holdTime` (audio samples) through
///    `InputLowering::set_hold_time_ms` on every pad and button, which is where legacy's own live read
///    happens (`Buttons::commandToggleShift`): the menu's hold-time item has a DEVICE row whose store
///    (`gui/ui/shell_host/device_settings.cpp`) writes `FlashStorage::holdTime` from inside the guest, so a
///    per-open seed would lag it by a whole session.
///  - **Mod buttons.** Legacy clears `considerShiftReleaseForSticky` on every button press including
///    the eight mod buttons (`buttonAction` in `hid/buttons.cpp`); `modButtonAction` feeds them as matrix buttons,
///    so `InputLowering::on_button` clears its own on the same presses.
class ShellHostUI final : public ::UI {
public:
	/// @brief Build the guest over @p runtime's shell.
	/// @param runtime The shell, surfaces and clock the guest drives; the firmware's is `shellRuntime()`. A spec
	///                builds its own, over its own board: `canOpen()`'s refusal is drivable only with a panel shape
	///                `OLED::main` cannot hold.
	explicit ShellHostUI(ShellRuntime& runtime);

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
	deluge::ui::shell::Shell& shell() { return runtime_.shell(); }
	/// @brief The surfaces the shell renders through.
	GuestSurfaceSet& surfaces() { return runtime_.surfaces(); }

	/// @brief Can this guest be opened at all?
	///
	/// False when the board profile's `SurfaceRole::Main` surface is a shape legacy's single
	/// `OLED::main` cannot hold, in which case `blit()` would drop every frame silently
	/// (`GuestSurfaceSet::canReachMainPanel`). A configuration error, answered before anything is
	/// pushed rather than discovered as a blank screen.
	bool canOpen() const { return runtime_.surfaces().canReachMainPanel(); }

	// ---- the legacy hooks, in `gui/ui/ui.h` order --------------------------------------------

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

	/// @brief Mark this visit as one `main` runs with no editor open: the drum creator's browser or take, opened
	///        straight over the clip view. Its close then leaves `currentUIMode` alone, as `main`'s browser's and
	///        recorder's own exits do; every other close ends the mode, as `SoundEditor::exitCompletely` does. Call it
	///        after the open succeeded; the next `opened()` forgets it.
	void markVisitWithoutEditor() { visitWithoutEditor_ = true; }

	/// @brief Every UI is being taken down under the guest (`nullifyUIs`, a song load or a clear): end every scene
	///        it holds, so each scene's `on_exit` runs (a recorder's take ends, attached to nothing), and forget the
	///        menu's song. Nothing ticks a guest that is no longer open, so this cannot wait for one. The slicer's held
	///        audition and the drum creator's drum are dropped whether the guest is open or not. Installed as
	///        `nullifyUIs`'s hook by `wireShellGuestServices()`, at boot and on every open.
	void uisTakenDown();
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

private:
	/// @brief The runtime's clock (`ShellRuntime::nowMs`).
	uint32_t nowMs() const;

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
	/// @return Whether a stacked scene draws \p region (`GuestSurfaceSet::owned_regions`), which is legacy's "did I use
	///         these rows up" (`UI::renderMainPads`). While it is false nothing is written: those pads are the UI
	///         underneath's. `MenuScene` draws no region, so a menu alone answers false for both, as `SoundEditor` does
	///         (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
	bool serveGrid(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth], int32_t x0, int32_t x1,
	               deluge::ui::shell::PadRegions region);

	/// @brief Feed one already-detented encoder turn to the shell as A-pin edges.
	///
	/// Legacy's four function encoders reach a `UI` in **detents** -- `interpretEncoders` calls
	/// `DetentedEncoder::take()` and hands the result over (`hid/encoder_input.cpp`'s
	/// `interpretEncoders`, `DetentedEncoder::applyEdges`) -- while the shell's entry point takes **edges** and divides
	/// by `LoweringConfig::edges_per_detent` itself. So this multiplies back up. Feeding the detent count straight
	/// through would halve every turn.
	///
	/// @warning **The `int8_t` ceiling, and why this saturates instead of wrapping.**
	///          `Shell::on_encoder_edges` takes an `int8_t`, so the product
	///          must fit in -128..127; the warning on `InputLowering::apply_encoder_edges` records what
	///          happens when it does not. **The guest can exceed it.** Legacy's
	///          `detentDelta` is an `int32_t` accumulated in `DetentedEncoder::pos` across however
	///          many `applyEdges` calls happen before a `take()`, and `interpretEncoders` returns before
	///          any `take()` while a stem export is running, after its `applyEdges` calls. Each skipped
	///          pass can add about 32 detents (`deluge_encoder_take_quadrature_edges` reports an `int8_t`
	///          of edges, and `DetentedEncoder::applyEdges` counts 4 edges a detent after the first), so a
	///          knob turned through a stem export delivers the whole accumulation in one call. So the clamp
	///          is `+-(127 / edges_per_detent)`, i.e. +-63 detents at two edges per detent, applied
	///          **before** the multiply. That is legacy's own choice for two of its four encoders -- tempo and
	///          select get `clamp(detentDelta, -128, 127)` (`interpretEncoders`) -- and a
	///          divergence from it for the other two, which legacy hands the full `int32_t` "for
	///          natural acceleration" (the horizontal and vertical actions). Saturating loses the excess of an
	///          impossible turn; wrapping would reverse an ordinary one.
	/// @param role    Which encoder the turn belongs to. Resolved to a board index through
	///                `BoardProfile::encoder_by_index` rather than hard-coded, because that table
	///                is what the lowering resolves back through
	///                (`InputLowering::apply_encoder_edges`) and a profile may order it differently.
	///                A role no index carries is dropped.
	/// @param detents The signed detent count legacy handed over.
	/// @param time_us When the board decoded the turn, on `getSystemTime()`'s clock in microseconds, which the
	///                lowering hands the turn's consumer with it; 0 for none, which times it on the tick's clock.
	void feedDetents(deluge::ui::widgets::core::EncoderRole role, int32_t detents, uint64_t time_us = 0);

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

	/// @brief The runtime's re-arm for this guest (`ShellRuntime::rearm`), from `timerCallback` and nowhere else.
	/// @param nowMs The clock the tick just ran on.
	void rearmTimer(uint32_t nowMs);

	/// @brief The runtime's "tick soon" (`ShellRuntime::armTickSoon`), for every path that changes the shell's state
	///        from outside a tick.
	void armTickSoon();

	/// @brief Hand the knob indicator rings back to legacy and `close()` this UI.
	///
	/// The body both of the guest's close paths share: `exitUI()`'s long-press Back, and
	/// `timerCallback()`'s "the shell's stack emptied during the tick", which is what a *short* Back
	/// at the menu's root produces (`MenuScene`'s `Action::Kind::Remove`,
	/// `MenuScene::handle_event`'s Back arm, `ui/scenes/menu/menu_scene.cpp`). Neither the ring hand-back nor the close
	/// may be skipped on either path; see the definition for why the order between them is fixed.
	///
	/// Arms no timer: `close()` is `closeUI(this)`, which unsets `TimerName::UI_SPECIFIC` itself
	/// (`gui/ui/ui.cpp`). Callers must not re-arm after it.
	void closeGuest();
	/// @brief Run the tick a close would otherwise skip: input fed since the last tick reaches the scenes it was fed
	///        to, as legacy hands a turn or press to the current UI at once, before they go. Ticks only when input is
	///        pending, and never inside its own tick.
	void flushPendingInput();
	/// @brief The shell's tick, with the facts every tick reads first.
	void tickShell(uint32_t now);

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
	deluge::ui::widgets::core::PadMask fedPads_{};
	/// @brief Whether the visit now open is one legacy runs with no editor (`markVisitWithoutEditor`).
	bool visitWithoutEditor_ = false;
	/// @brief `flushPendingInput`'s tick is running.
	bool flushing_ = false;

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
	deluge::ui::widgets::grid::GreyoutMask greyoutMask();

	/// The scene `opened()` pushes; `SceneKind::Null` until a caller sets one.
	deluge::ui::shell::SceneId initial_{};
	/// The hook `closeGuest()` runs; see `CloseHook`.
	static inline CloseHook closeHook_ = nullptr;
	/// The shell, its surfaces and its clock: shared with every other host of scenes.
	ShellRuntime& runtime_;
	/// The regions the guest owned, the regions it answered for (`answeredRegions`) and the greyout
	/// it reported when `syncPads` last ran.
	deluge::ui::shell::PadRegions padsOwned_ = deluge::ui::shell::PadRegions::All;
	deluge::ui::shell::PadRegions padsAnswered_ = deluge::ui::shell::PadRegions::All;
	deluge::ui::widgets::grid::GreyoutMask padsGreyout_{};
};

} // namespace deluge::gui::ui

/// @brief The one guest, named the way legacy names its UIs (`extern SessionView sessionView;`).
///
/// Constructed over `deluge::ui::scenes::scene_registry()`. The menu entry points push it with
/// `openUI(&shellHostUI)`.
extern deluge::gui::ui::ShellHostUI shellHostUI;
