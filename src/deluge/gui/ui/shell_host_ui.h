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
/// `timerCallback` / the LED sink are Task 7's.
///
/// @par Input is fed, not answered
///
/// **Nothing the input hooks do is synchronous.** `Shell::on_board_event` and
/// `Shell::on_encoder_edges` push into `InputLowering`'s queues; routing to a scene happens in
/// `Shell::pump_input`, which only `Shell::tick` calls (`shell/shell.cpp:561-571`). So a hook can
/// never report what a scene did with a press, and every return value below is decided statically
/// from what legacy does with it -- see each hook's note. The tick that drains the queue is F1
/// task 7's; until it exists, a spec drives `shell().tick(now)` itself.
///
/// **camelCase, deliberately** (plan F1's global constraint 3): everything here is a legacy `UI`
/// override or something a legacy call site names, and a façade over legacy spells things the way
/// legacy does. The `Shell` it drives is `snake_case` on the other side of that seam.
///
/// @par The virtuals this class does NOT override, and why
///
/// The failure mode this list exists for is a guest that silently inherits a `UI` default where it
/// should have forwarded: nothing breaks until a scene needs that hook, and by then it reads as a
/// scene bug. `::UI` declares 32 virtuals (`gui/ui/ui.h:94-173`, `grep -n virtual`); fifteen are
/// overridden below and the other seventeen are listed here with the reason.
///
// clang-format off
/// | not overridden | line | why |
/// |---|---|---|
/// | `~UI` | `ui.h:94` | Defaulted. This class owns its members by value; the implicit destructor is correct. |
/// | `modEncoderAction` | `ui.h:104` | **The three gold-knob hooks stay with `view`, and F1 task 6 decided that deliberately against its own brief -- see `@par The gold knobs` below.** The base forwards to `view` (`ui.cpp:35-37`), which is legacy's own mod-knob path and exactly what `SoundEditor` ends up doing (`gui/ui/sound_editor.cpp:1604`). Task 8's, with `ModKnobs`. |
/// | `modButtonAction` | `ui.h:105` | Base forwards to `view` (`ui.cpp:39-41`); `SoundEditor` does not override it at all, so this is legacy's behaviour unchanged. Legacy never routes a mod button to `buttonAction` either -- `Buttons::buttonAction` intercepts the eight of them and `goto dealtWith`s (`hid/buttons.cpp:99-107`) -- so **the shell never sees `ButtonId::Mod0`..`Mod7` under the guest at all.** Recorded as a gap, not a decision, and it has a second-order cost: see `@par Shift`. |
/// | `modEncoderButtonAction` | `ui.h:106` | Base forwards to `view` (`ui.cpp:43-45`), as `SoundEditor` does (`gui/ui/sound_editor.cpp:1614`). Shell spec §15.5 has the scene-then-service order it has to land in; that lands with Task 8. Note the dispatch is *gated on this class's `buttonAction`*: `Buttons` only reaches it when the current UI returned `NOT_DEALT_WITH` (`hid/buttons.cpp:134-141`, the arms at `:226-231`), which is one more reason `buttonAction` below returns that value. |
/// | `graphicsRoutine` | `ui.h:108` | **Deliberately kept.** The base runs the *root* view's per-frame animation whenever `canSeeViewUnderneath()` (`ui.cpp:47-51`), and `SoundEditor` does not override it either -- so the clip view under an open menu keeps its playhead moving. Overriding it would stop that. |
/// | `timerCallback` | `ui.h:109` | **Task 7** (the tick). |
/// | `toClipMinder` | `ui.h:122` | Null, as `SoundEditor` leaves it. A guest is not a clip minder; `currentUIIsClipMinderScreen()` must keep answering about the view underneath. |
/// | `toTimelineView` | `ui.h:127` | Null, as `SoundEditor` leaves it, for the same reason. |
/// | `scrollFinished` | `ui.h:129` | `SoundEditor` overrides it to clear `UI_MODE_HORIZONTAL_SCROLL` and re-render the root (`sound_editor.cpp:1024-1029`); both halves are about legacy UI modes and legacy rendering, neither of which the shell owns. **Recorded as an uncovered gap**, not a decision to forward: a scroll begun under an open guest ends without the guest hearing about it. |
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
/// @par The gold knobs: why `modEncoderAction` is NOT wired, against the brief
///
/// Spec F §2.1 and F1's task-6 brief both list `modEncoderAction` among the hooks that "forward
/// into the shell's `InputLowering`". Doing that would corrupt the value, arithmetically:
///
///  - The shell's only encoder entry points take **raw A-pin edges**
///    (`Shell::on_encoder_edges`, `shell/shell.h:268`; `DELUGE_EVENT_ENCODER`'s `value`,
///    `shell/input_lowering.h:133-138`). For an `EncoderKind::Accelerating` role the lowering takes the
///    edge count as the magnitude and applies its own `calc_next_knob_speed`
///    (`shell/input_lowering.cpp:187-193`).
///  - Legacy has already applied all of that before it calls this hook.
///    `interpretEncoders` hands over `offset * encoder.calcNextKnobSpeed(offset)`
///    (`hid/encoder_input.cpp:192-194`), and `calcNextKnobSpeed` returns
///    `clamp(speed, 1.0, 3.0)` (`hid/encoders.cpp:76`). So a forwarded turn would be accelerated
///    **twice**, up to 9x.
///  - It would also acquire a debounce legacy does not have. `InputLowering::action_gold_turn`
///    swallows the first turn after a 500 ms pause; legacy's counterpart is dead code -- the field
///    the wiggle test reads is only ever written in a branch guarded by `offset != 0` that also
///    requires `offset == 0` (`hid/encoder_input.cpp:187-202`), which the lowering's own note at
///    `shell/input_lowering.cpp:206-213` already records.
///
/// So the mapping is not a mapping. Leaving all three mod hooks on the base is *also* what the
/// first tenant does -- `SoundEditor::modEncoderAction` ends at `UI::modEncoderAction`
/// (`gui/ui/sound_editor.cpp:1583-1607`) and it does not override `modButtonAction` at all -- which
/// is what plan F1's global constraint 1 asks for. What the gold knobs need is F1 task 8's
/// `ModKnobs` service plus a shell entry point that accepts an already-lowered turn.
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
///     (`shell/shell.h:30-40`).
///  2. **`hold_time_ms` is a constant here and a setting there.** The tap threshold is
///     `FlashStorage::holdTime` in legacy, `LoweringConfig::hold_time_ms` (100) in the shell.
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

	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void selectEncoderAction(int8_t offset) override;
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
	/// `MenuScene::is_grid_overlay()` is true (`scenes/menu/menu_scene.h:441-442`), so the first
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
	///          `Shell::on_encoder_edges` takes an `int8_t` (`shell/shell.h:268`), so the product
	///          must fit in -128..127; the lowering's own warning records what happens when it does
	///          not (`shell/input_lowering.h:387-410`). **The guest can exceed it.** Legacy's
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

	/// The scene `opened()` pushes; `SceneKind::Null` until a caller sets one.
	deluge::ui::shell::SceneId initial_{};
	/// @brief The config `shell_` was built with, kept so `feedDetents` can read
	///        `edges_per_detent` back.
	///
	/// `InputLowering` exposes `profile()` (`shell/input_lowering.h:258`) and `shift_is_sticky()`
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
