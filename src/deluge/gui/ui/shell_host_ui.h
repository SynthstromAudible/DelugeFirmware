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
/// served out of the shell's surfaces; legacy's input hooks are Task 6's, and `timerCallback` /
/// the LED sink are Task 7's.
///
/// **camelCase, deliberately** (plan F1's global constraint 3): everything here is a legacy `UI`
/// override or something a legacy call site names, and a façade over legacy spells things the way
/// legacy does. The `Shell` it drives is `snake_case` on the other side of that seam.
///
/// @par The virtuals this class does NOT override, and why
///
/// The failure mode this list exists for is a guest that silently inherits a `UI` default where it
/// should have forwarded: nothing breaks until a scene needs that hook, and by then it reads as a
/// scene bug. `::UI` declares 32 virtuals (`gui/ui/ui.h:94-173`, `grep -n virtual`); ten are
/// overridden below and the other twenty-two are listed here with the reason.
///
// clang-format off
/// | not overridden | line | why |
/// |---|---|---|
/// | `~UI` | `ui.h:94` | Defaulted. This class owns its members by value; the implicit destructor is correct. |
/// | `padAction` | `ui.h:97` | **Task 6** (input). Until then the base's `DEALT_WITH` swallows pads, which is what an open guest should do rather than let them fall through to the view underneath. |
/// | `buttonAction` | `ui.h:98` | **Task 6**. The base returns `NOT_DEALT_WITH`, so buttons still reach `Buttons::buttonAction`'s tail -- including the long-press-Back timer (`hid/buttons.cpp:114-121`) that drives `exitUI()` below. |
/// | `horizontalEncoderAction` | `ui.h:101` | **Task 6**. |
/// | `verticalEncoderAction` | `ui.h:102` | **Task 6**. |
/// | `selectEncoderAction` | `ui.h:103` | **Task 6**. |
/// | `modEncoderAction` | `ui.h:104` | **Task 6**. The base forwards to `view` (`ui.cpp:35-37`), which is legacy's own mod-knob path and the right behaviour until the shell's `ModKnobs` service is installed (Task 8). |
/// | `modButtonAction` | `ui.h:105` | **Task 6**, same reason (`ui.cpp:39-41`). |
/// | `modEncoderButtonAction` | `ui.h:106` | **Task 6**, same reason (`ui.cpp:43-45`); shell spec §15.5 has the scene-then-service order it has to land in. |
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

	/// The scene `opened()` pushes; `SceneKind::Null` until a caller sets one.
	deluge::ui::shell::SceneId initial_{};
	GuestSurfaceSet surfaces_;
	deluge::ui::shell::Shell shell_;
};

} // namespace deluge::gui::ui

/// @brief The one guest, named the way legacy names its UIs (`extern SoundEditor soundEditor;`).
///
/// Constructed over `deluge::scenes::scene_registry()`. Task 9's entry points push it with
/// `openUI(&shellHostUI)` when `RuntimeFeatureSettingType::ShellMenu` is on.
extern deluge::gui::ui::ShellHostUI shellHostUI;
