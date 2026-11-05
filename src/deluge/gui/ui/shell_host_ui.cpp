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

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_slice_ops.h"

#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_automation_glue.h"
#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/shell_drum_creator.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_note_mode.h"
#include "gui/ui/shell_pad_forward.h"
#include "gui/ui_timer_manager.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/scheduler.h" // getSystemTime()
#include "model/settings/runtime_feature_settings.h"
#include "scenes/marker/marker_scene.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "shell/profiles/deluge.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::gui::ui {

using deluge::ui::EncoderRole;
using deluge::ui::shell::Scene;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::Services;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

ShellHostUI::ShellHostUI(const SceneRegistry& registry, const DelugeBoard& board,
                         const deluge::ui::shell::BoardProfile& profile)
    : surfaces_(board, profile), shell_(surfaces_, profile, registry, Services{}, config_) {
	surfaces_.observe(shell_.stack());
	// `Services{}` and a `ShellConfig` whose only set field is `greys_in_send_path` on purpose, and
	// both stay that way.
	//
	// The three real services each take this shell's own `SceneStack&`, so they cannot exist before
	// it. They are built over `shell()` and installed through `Shell::set_services`
	// (`shell/shell.h`), which is the whole reason that setter exists -- from
	// `wireShellGuestServices()`, called at boot and from `opened()` below. This object is a
	// static-init global, so the constructor is not a place a real service (or a real setting) can
	// be reached from at all.
	//
	// **That `ShellConfig` is otherwise a placeholder, and the two fields that are settings are seated
	// afterwards**: `screensaver_idle_ms` through `Shell::set_screensaver_idle_ms`, pushed from
	// `Screensaver::settingsChanged()`, and `lowering.hold_time_ms` through
	// `InputLowering::set_hold_time_ms`, re-read on every event in `feed()`. Neither could be read
	// here -- flash has not been read when this runs.
	//
	// `config_` rather than a `ShellConfig{}` temporary, so `feedDetents` can read
	// `edges_per_detent` back out of the same object the shell was built from. `Shell` copies it, so
	// the two are only equal by construction; `edges_per_detent` is a board fact rather than a
	// setting, so nothing moves it and they stay equal.
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

bool ShellHostUI::opened() {
	// A profile whose Main surface legacy's single `OLED::main` cannot hold has nowhere to put its
	// bytes: `GuestSurfaceSet::blit` would drop every frame and the user would see the last thing
	// legacy drew, forever. Refusing here is what `canReachMainPanel()` was built for, and legacy
	// acts on the refusal: `openUI` unwinds `numUIsOpen`, re-assesses the greyout and resumes the
	// previous UI when `opened()` returns false (`openUI` in `gui/ui/ui.cpp`, the rollback). `changeUIAtLevel` has its
	// own copy of that unwind; `openUI` does not go through it.
	if (!canOpen()) {
		return false;
	}
	// A pad held from an earlier visit was released to legacy once that visit closed; this visit starts with none.
	fedPads_ = deluge::ui::PadMask{};
	shell_.lowering().forget_held_pads();

	// Start from an empty stack rather than pushing onto whatever the last session left. This is
	// not tidiness: `SceneStack` holds one `Scene*` per kind, so pushing a kind that is already
	// stacked puts one object at two indices, and no lifecycle hook is coherent under that -- the
	// scene takes `on_enter` twice with no `on_exit` between and both indices share a single
	// `SceneState` slot (`SceneStack`'s one-scene-per-kind rule). Legacy's equivalent is
	// `SoundEditor::opened` calling `beginScreen()`, which
	// begins a menu session rather than resuming one.
	shell_.stack().reset();
	// Legacy has owned the LEDs since the last session closed.
	surfaces_.begin_session();

	// **Seed the sticky-shift setting from flash, every time the guest opens.**
	//
	// The shell has its own shift state machine -- there is no seam to read legacy's through, see
	// the class's `@par Shift` -- and `InputLowering::toggle_shift` is a faithful port of
	// `Buttons::commandToggleShift` (`InputLowering::toggle_shift` against
	// `hid/buttons.cpp`) *except* that legacy reads the runtime feature at release time
	// while the shell reads a `LoweringConfig` field that defaults to false
	// (`shell/input_lowering.h`). Left unseeded, a sticky-shift user's latched shift would be
	// invisible to `ctx.shift`, which is the whole gate on the menu's shortcut-pad jump
	// (`MenuPads::handle_pad`, `scenes/menu/pads.cpp`) -- so shortcut pads would silently stop working under the
	// toggle.
	//
	// `opened()` is the right place and the setter is the right tool: `set_shift_is_sticky` exists
	// precisely because "the settings menu toggles it live" (`InputLowering::set_shift_is_sticky`), and this function
	// -- unlike the constructor, which runs at static-init before flash is read -- runs long after
	// `readSettingsFromFile`. The live half is mirrored in `ShiftIsSticky::writeCurrentValue`
	// (`gui/menu_item/runtime_feature/shift_is_sticky.cpp`), which already had to pair with
	// `Buttons::clearShiftSticky()` for exactly the same reason; this seed is what covers a guest
	// opened after a setting changed while it was shut.
	shell_.lowering().set_shift_is_sticky(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky)
	                                      == RuntimeFeatureStateToggle::On);
	// And the SHIFT itself: one pressed before the guest opened -- SHIFT then a shortcut pad in a clip view -- is still
	// held, and the shell saw no press for it.
	shell_.lowering().seed_shift(Buttons::isShiftButtonPressed(), Buttons::isShiftStuck(), nowMs());

	// **No `Shell::boot()` here, and its absence is deliberate.** The panels want a repaint on the way in,
	// and they get one without it: `SceneStack::reset` sets `changed_` unconditionally and a
	// successful `push` sets it too, and `Shell::tick` reacts to `changed_since_last_query()` by
	// requesting a repaint on **every** OLED surface, the grid and the widgets gate before it renders
	// anything -- which is the same three things `boot()` does. A call here would be redundant, and a
	// redundant repaint request reads as a load-bearing one. `displayOrLanguageChanged` does call
	// `boot()`, where there is no stack change to ride on.

	const size_t before = shell_.stack().depth();
	shell_.init(initial_);
	// `Shell::init` is `stack_.push(initial)` with the result dropped,
	// and `SceneStack::push` refuses an unregistered kind -- which is
	// exactly what a caller that never called `setInitialScene` gets, since a default `SceneId` is
	// `SceneKind::Null`. An empty guest is worse than no guest: it would swallow every pad
	// (`UI::padAction`'s `DEALT_WITH` default) while drawing nothing. So a refused push is a
	// refused open.
	if (shell_.stack().depth() <= before) {
		return false;
	}

	// **Install the three services, and re-seed everything they borrow from legacy.** They are not
	// members of this class -- see `gui/ui/shell_guest_services.h` for why the host spec suite
	// forbids that -- but they are installed into *this* shell, over its own `SceneStack` and
	// `InputLowering`. Called here as well as at boot because `currentSong` and `View`'s active mod
	// controllable can both have moved since the last open, and an unseeded `ModKnobs` does not
	// merely do nothing: it declares every mod LED off and both knob indicators at zero, on every
	// tick the guest runs.
	//
	// **After the refusal check**, like the arm below and for a weaker version of the same reason:
	// a refused open is rolled back to the previous UI (`openUI`), so there is no
	// guest to serve and re-seating slots on a shell nothing is about to tick is work with no
	// reader. The two lowering seeds above are deliberately not moved with it -- they are pure
	// writes to this object's own state and reading flash twice is cheaper than the reader having
	// to check where the boundary is.
	wireShellGuestServices();

	// `SoundEditor::opened` asks for the root's pads (`gui/ui/sound_editor.cpp:498`): while the guest
	// owns none they are the UI underneath's, greyed by the mask `openUI` read before the scene was
	// on the stack -- so the greyout is read again now.
	using deluge::ui::shell::PadRegions;
	using deluge::ui::shell::regions_without;
	padsOwned_ = surfaces_.owned_regions();
	padsAnswered_ = answeredRegions();
	padsGreyout_ = greyoutMask();
	handBack(regions_without(PadRegions::Main, padsOwned_) | regions_without(padsAnswered_, padsOwned_));
	PadLEDs::reassessGreyout();

	// **The first tick, and the only reason anything inside the guest ever runs.** Nothing above
	// armed a deadline: `Shell::init` is a bare `push` and `Shell::boot` calls `request_repaint()`,
	// which fires a gate without giving it a deadline, so
	// `Shell::next_wake()` is still `std::nullopt` here and there is nothing to convert. See
	// `armTickSoon`.
	//
	// **Only on the success path**, and that matters rather than being tidy: `openUI` unsets
	// `TimerName::UI_SPECIFIC` *before* calling this (`openUI` in `gui/ui/ui.cpp`) and
	// on a refusal rolls the hierarchy back to the previous UI. A timer armed by a
	// refused open would therefore deliver this guest's `timerCallback` to whichever UI is current
	// instead -- `UITimerManager::routine` dispatches to `getCurrentUI()`, not to the arming object
	// (`UITimerManager::routine`).

	armTickSoon();
	return true;
}

void ShellHostUI::focusRegained() {
	// Legacy calls this on the UI that is the current one again after something above it closed
	// (`closeUI`, `gui/ui/ui.cpp:223-228`), and `Scene::on_resume` is the port of exactly that
	// call, in as many words (`Scene::on_resume`).
	//
	// **`input_top()`, not `top()`**, and `on_resume`'s own contract is that it fires on the scene
	// *input reaches* again, which is why `SceneStack` fires it on `input_top()` and never on a
	// passive layer (`SceneStack`). Handing it to `top()` would give the resume to a
	// popup or a console line that never took input in the first place, and would skip the scene that genuinely gets
	// input back.
	shell_.stack().input_top().on_resume();

	// **And re-arm, because legacy just disarmed us.** Both routes into this hook clear
	// `TimerName::UI_SPECIFIC` on the line before they call it -- `closeUI`
	// and `openUI`'s refusal rollback. Without this the
	// guest would tick normally until any UI was opened over it and then never again: frozen pads,
	// a frozen panel, and input piling up in a queue only `Shell::tick` drains.
	armTickSoon();
}

void ShellHostUI::displayOrLanguageChanged() {
	// The shell is retained-mode: a surface is re-rendered only when a gate asks for it
	// (`Shell::boot` is the one public way to ask for all of them). A language change
	// invalidates every string on screen without changing anything the shell watches, so without
	// this the guest would keep serving the frame it last rendered until something else happened.
	// `SoundEditor` overrides this hook too, for the same reason
	// (`readValueAgain`).
	//
	// **What this does not buy.** The repaint lands on the shell's next tick, whereas the base's
	// `renderUIsForOled()` makes legacy pull `renderOLED` before that -- so exactly one frame of the
	// old language can still go out. Chaining to the base keeps that pull, because the alternative
	// is legacy never being told the panel is stale at all.
	shell_.boot();
	// `boot()` requests repaints without arming any deadline, so
	// `compute_next_wake` has nothing to report and the "next tick" above would be whatever tick
	// happened to come along. On an idle guest that is never. Hence the explicit ask.
	armTickSoon();
	::UI::displayOrLanguageChanged();
}

// ---------------------------------------------------------------------------------------------
// Composition queries
// ---------------------------------------------------------------------------------------------

bool ShellHostUI::canSeeViewUnderneath() {
	// **Not `oled_transparent(Main)`.** Pairing this hook with the scene's OLED transparency would be
	// wrong, and the shipped tree contradicts it twice over:
	//
	//  - Legacy's OLED transparency is a *data member*, `UI::oledShowsUIUnderneath`
	//    (`gui/ui/ui.h`), set false by `UI::UI` and read by
	//    `doAnyPendingOLEDRendering` to decide how far down the stack to start drawing. `SoundEditor` leaves it false
	//    and `MenuScene::oled_transparent` returns false (`scenes/menu/menu_scene.h`) -- those two already agree, and
	//    neither is this hook.
	//  - `Slicer` sets `oledShowsUIUnderneath = true` and returns **false** here
	//    (`gui/ui/slicer.h`), on the same object. One legacy UI holding both values at once is
	//    proof the two questions are independent.
	//
	// What this hook actually gates is whether the *root view* is still live underneath:
	// `UI::graphicsRoutine` runs the root's per-frame animation only when it is true
	// (`gui/ui/ui.cpp`), and `Session::armingChanged` re-arms the play flash and the OLED
	// dirty bit through it. The shell's
	// question of the same shape is `Scene::occludes_grid` -- "does this scene cover the grid
	// completely, so scenes beneath need not render?" -- so this is its
	// negation.
	//
	// It lands where legacy lands for the first tenant: `SoundEditor::canSeeViewUnderneath` is true
	// (`gui/ui/sound_editor.h`) and `MenuScene::occludes_grid` is false
	// (`scenes/menu/menu_scene.h`). Mapping it to `oled_transparent(Main)` would have made
	// it false and frozen the clip view's playhead under an open menu.
	return !shell_.stack().top().occludes_grid();
}

bool ShellHostUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	// `getUIGreyoutColsAndRows` walks the open UIs top down and takes the first one that returns
	// true (`getUIGreyoutColsAndRows`); false means "obey the UI under me" (`UI::getGreyoutColsAndRows`).
	//
	// The port spells `doGreyout == false` as an **all-zero mask**, deliberately and at the code:
	// `MenuScene::greyout_mask` returns `{}` for the arms where
	// `SoundEditor::getGreyoutColsAndRows` sets `doGreyout = false` (`menu::greyout_mask`, `scenes/menu/pads.cpp`,
	// against `gui/ui/sound_editor.cpp:252-294`). Every arm of legacy's switch that leaves
	// `doGreyout` true also writes a non-zero `*cols` (`:270`, `:275`, `:283`, `:290`), so
	// "non-zero mask" and "doGreyout" are the same predicate on the shipped tree, and this is the
	// one place that equivalence is turned back into legacy's two-out-parameters-plus-bool shape.
	//
	// Legacy greys once, in the *send* path: `prepareColour` (`hid/led/pad_leds.cpp:283-298`) reads
	// the `greyoutCols`/`greyoutRows` that `reassessGreyout` fills from this function, and greys
	// whatever `PadLEDs::image` holds -- the root view's pixels and the guest's alike. The guest's
	// shell greys nothing itself (`ShellConfig::greys_in_send_path`), so every pad is greyed once.
	const deluge::ui::grid::GreyoutMask mask = greyoutMask();
	if (mask.cols == 0 && mask.rows == 0) {
		return false;
	}
	// Written only on the true path. Legacy's callers pass in accumulators they have already
	// zeroed (`getUIGreyoutColsAndRows`) and a UI that returns false must leave them alone, because the
	// next UI down is about to be asked the same question with the same two pointers.
	*cols = mask.cols;
	*rows = mask.rows;
	return true;
}

deluge::ui::grid::GreyoutMask ShellHostUI::greyoutMask() {
	// The stack's own top-down walk, the guest's half of `getUIGreyoutColsAndRows`: a popup over
	// the menu leaves the menu's greyout standing, and a keyboard over it replaces it.
	return shell_.stack().topmost_greyout();
}

deluge::ui::shell::PadRegions ShellHostUI::answeredRegions() {
	using deluge::ui::shell::PadRegions;
	const PadRegions owned = surfaces_.owned_regions();
	if (!deluge::ui::shell::holds(owned, PadRegions::Sidebar)
	    && shell_.stack().input_top().kind() == deluge::scenes::marker::kMarkerSceneKind) {
		return owned | PadRegions::Sidebar;
	}
	return owned;
}

void ShellHostUI::handBack(deluge::ui::shell::PadRegions regions) {
	using deluge::ui::shell::holds;
	using deluge::ui::shell::PadRegions;
	if (regions == PadRegions::None) {
		return;
	}
	uiNeedsRendering(this, holds(regions, PadRegions::Main) ? 0xFFFFFFFF : 0,
	                 holds(regions, PadRegions::Sidebar) ? 0xFFFFFFFF : 0);
}

void ShellHostUI::refreshRootFacts() {
	refreshShellRootFacts();
	syncPads();
}

void ShellHostUI::syncPads() {
	using deluge::ui::shell::PadRegions;
	using deluge::ui::shell::regions_without;
	const PadRegions owned = surfaces_.owned_regions();
	const PadRegions answered = answeredRegions();
	const deluge::ui::grid::GreyoutMask mask = greyoutMask();
	const bool ownerChanged = owned != padsOwned_;
	const bool greyoutChanged = mask.cols != padsGreyout_.cols || mask.rows != padsGreyout_.rows;
	const PadRegions gained = regions_without(owned, padsOwned_);
	// Every region whose answer changed hands, less those the guest now draws itself: a region
	// handed back, and the sidebar a marker starts or stops answering for, as
	// `SampleMarkerEditor::opened` re-asks for it (`gui/ui/sample_marker_editor.cpp`).
	const PadRegions moved = regions_without(answered | padsAnswered_, answered & padsAnswered_);
	padsOwned_ = owned;
	padsAnswered_ = answered;
	padsGreyout_ = mask;
	if (gained != PadRegions::None) {
		surfaces_.take_grid();
	}
	handBack(regions_without(moved, owned));
	if (ownerChanged || greyoutChanged) {
		// A scene that takes the main pads and greys every column is the menu's SHIFT overlay, whose greyout
		// `renderMainShortcutsOnly` applies at once (`PadLEDs::doGreyoutInstantly()`) rather than fading it in.
		const bool overlay = deluge::ui::shell::holds(gained, PadRegions::Main) && mask.cols == 0xFFFFFFFFu;
		PadLEDs::reassessGreyout(overlay);
	}
}

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------

void ShellHostUI::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	// **This exists because legacy clears the panel first.** `doAnyPendingOLEDRendering` calls
	// `OLED::clearMainImage()` and then asks each open UI to draw into `OLED::main`
	// (`gui/ui/ui.cpp`: the clear, then the `renderOLED` call). The shell is
	// retained-mode -- `Shell::flush` blits a surface only when a repaint raised its `blit_pending`
	// (`Shell::flush`) -- so a frame legacy zeroed between two shell renders would stay
	// zeroed with nothing to tell the shell to paint it again. The guest keeping its *own* copy of
	// the frame (the copy-not-alias decision in `gui/ui/shell_guest_surfaces.h`) is what makes that recoverable: this
	// hook hands that copy back every time legacy asks.
	//
	// It is the same bytes `GuestSurfaceSet::blit` writes, so the two paths are idempotent with
	// each other rather than in competition; `blit` additionally raises `OLED::markChanged`, which
	// this must not, because legacy has already decided to send this frame by the time it calls
	// here (`doAnyPendingOLEDRendering`'s "Don't need to mark dirty because clearMainImage has already done that").
	if (!canOpen()) {
		return;
	}
	const deluge::ui::shell::OledSurface* main = surfaces_.oled(SurfaceRole::Main);
	if (main == nullptr) {
		return;
	}
	const std::span<const uint8_t> src = main->canvas.bytes();
	const std::span<uint8_t> dest = canvas.bytes();
	if (src.size() != dest.size()) {
		// **Unreachable, and stated as unreachable rather than left to look like coverage.**
		// `canOpen()` above is a *shape* predicate (`GuestSurfaceSet::canReachMainPanel`), so past
		// it the Main surface is 128x48 and `Canvas::bytes()` is geometry-sized; `dest` is an
		// `oled_canvas::Canvas`, which is `OwnedCanvas<kDelugeOled>` by type
		// (`hid/display/oled_canvas/canvas.h`) and therefore the same 768 bytes on every
		// instance that exists. No board this suite can build reaches here, and deleting this line
		// is a mutation nothing kills. It stays because the alternative failure is a `std::copy` past the end of
		// legacy's buffer, not a wrong pixel.
		return;
	}
	std::copy(src.begin(), src.end(), dest.begin());
}

bool ShellHostUI::serveGrid(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth], int32_t x0, int32_t x1,
                            deluge::ui::shell::PadRegions region) {
	const bool isOpaque = deluge::ui::shell::holds(surfaces_.owned_regions(), region);
	// The query form. `UI`'s own contract: "These *must* check whether image has been supplied - if
	// not, just return, saying whether opaque or not" (`UI::renderMainPads`). Legacy asks it that
	// way in two places -- `uiNeedsRendering`'s walk down the stack and `closeUI`'s "does anything
	// need redrawing after this close" pass (both in `gui/ui/ui.cpp`).
	if (image == nullptr || !isOpaque) {
		return isOpaque;
	}

	const deluge::gfx::grid::PadGrid& grid = surfaces_.grid().grid();
	const deluge::gfx::grid::GridDimensions dims = grid.dims();
	// **Both clamps are load-bearing, and neither is covered by `canOpen()`.**
	// `GuestSurfaceSet::canReachMainPanel` asks about the *panel's* shape and says nothing about the
	// grid (`gui/ui/shell_guest_surfaces.h`), and `SurfaceSet::build` sizes the grid straight from
	// `DelugeBoard::pad_grid_width`/`pad_grid_height` and `BoardProfile::sidebar_cols`
	// (`SurfaceSet::build` in `shell/surfaces.cpp`) with no reference to legacy's numbers at all -- a `PadGrid` may be
	// up to `kMaxGridRows` x `kMaxGridCols` = 16 x 32 (`gfx/grid/pad_grid.h`) while
	// `PadLEDs::image` is fixed at `[kDisplayHeight][kDisplayWidth + kSideBarWidth]` = 8 x 18
	// (`hid/led/pad_leds.h:33`). So a profile with a taller or wider grid than legacy's is the case
	// these two `min`s exist for, and dropping either writes past the end of legacy's buffer rather
	// than merely painting a wrong pad. Both are mutation-covered by the tall/wide-grid board in
	// `tests/spec/gui/shell_host_ui_spec.cpp`.
	const int32_t rows = std::min<int32_t>(dims.rows, kDisplayHeight);
	const int32_t last = std::min<int32_t>(x1, dims.cols());
	for (int32_t y = 0; y < rows; y++) {
		if ((whichRows & (1u << y)) == 0) {
			continue;
		}
		for (int32_t x = x0; x < last; x++) {
			image[y][x] = grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
		}
	}
	// **Two paths write `PadLEDs::image` while the guest owns a region**: `GuestSurfaceSet::send_columns`
	// on the shell's own flush, and this when legacy pulls. They agree -- same grid, same rows. For a
	// region it does not own, neither writes: the UI underneath draws those pads, as under
	// `SoundEditor` (`SoundEditor::renderMainPads`); an overlay's shortcut blink reaches the PIC as
	// a flash (`send_columns`). Legacy greys every pad once, owned or not, by the mask
	// `getGreyoutColsAndRows` reports.
	//
	// `occupancyMask` is deliberately untouched, and the same gap `GuestSurfaceSet::send_columns`
	// has: nothing in the shell populates an occupancy map, and
	// inventing one here would be the guest making up a fact about legacy's buffer.
	return isOpaque;
}

bool ShellHostUI::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	(void)occupancyMask;
	// `drawUndefinedArea` is a timeline concept -- how far past a clip's end to paint -- and the
	// shell has no notion of one. `SoundEditor` ignores it by not overriding this hook at all.
	(void)drawUndefinedArea;
	return serveGrid(whichRows, image, 0, kDisplayWidth, deluge::ui::shell::PadRegions::Main);
}

bool ShellHostUI::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (deluge::ui::shell::holds(surfaces_.owned_regions(), deluge::ui::shell::PadRegions::Sidebar)) {
		return serveGrid(whichRows, image, kDisplayWidth, kDisplayWidth + kSideBarWidth,
		                 deluge::ui::shell::PadRegions::Sidebar);
	}
	// `SampleMarkerEditor::renderSidebar` answers for the sidebar it does not draw only over the
	// keyboard screen (`SampleMarkerEditor::renderSidebar`); the marker scene cannot read the
	// root UI, so the guest asks on its behalf.
	if (shell_.stack().input_top().kind() == deluge::scenes::marker::kMarkerSceneKind) {
		return renderClipViewSidebarUnderMarker(whichRows, image, occupancyMask);
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// The tick
//
// The whole of the guest's clock, in three functions. `timerCallback` is the only place
// `Shell::tick` is called from, so it is the only place any of the shell runs at all -- routing,
// rendering, LEDs and the wake computation are all steps of that one function
// (`Shell::tick`). Everything else in this file either feeds a queue or answers a
// legacy query.
// ---------------------------------------------------------------------------------------------

ActionResult ShellHostUI::timerCallback() {
	const uint32_t now = nowMs();

	// The screensaver's legacy-side inhibitors (`hid/display/screensaver.cpp:151`); the working animation is the
	// shell's own scene.
	shell_.set_screensaver_inhibit(legacyInhibitsScreensaver());
	shell_.tick(now);

	// **The action drain, and both halves of where it sits are load-bearing.**
	//
	// `MenuScene::take_action()` is a one-slot mailbox with a silent overwrite
	// (`MenuScene::take_action`, `scenes/menu/menu_scene.h`), and it announces nothing: "`after_render` returns no
	// `Action` announcing that one is waiting, so nothing enforces the polling: **poll after every
	// tick, or requests are dropped or stale**" (the same warning). This line is the polling for the
	// **menu's** outbox. `dispatchShellActions` performs whatever it finds.
	//
	// It is not the only such poll: `dispatchShellChoices()` below is the same poll for the *browser's*
	// own outbox, which is a second mailbox with a second producer.
	//
	// **After the tick**, because a request is filed by `MenuScene::after_render`, and `after_render`
	// is what `Shell::tick` runs as its step 7 -- a poll above
	// `shell_.tick` would read the slot before this tick's press could have filled it, and would
	// therefore always deliver a tick late, against whatever state the next tick brings.
	//
	// **Above the close check below**, not beside `rearmTimer`, because that check `return`s. On the
	// tick a short Back empties the stack there is nothing wrong with a request having been filed
	// earlier in the same tick, and a poll placed after the return would leave it in the outbox --
	// where it would sit, undelivered, until the next time the guest is opened and ticked, and would
	// then run against a song and a root UI chosen since. That is exactly the "delivered whenever
	// someone finally does" hazard, and the cheapest way not to have it is to have no path through
	// this function that skips the poll.
	//
	// **Unconditional**, for the same reason: not gated on input, not on whether anything rendered,
	// not on which scene is frontmost. Nothing here can know whether a request is waiting without
	// asking, and asking is what takes it.
	dispatchShellActions();

	// The menu's transition record, the automation view's half of the same wall: filed by `after_render` too, so
	// drained after the tick and above the close check for the same reasons, and after the action poll -- a menu action
	// never moves the value node, so nothing turns on the order.
	dispatchMenuTransitions();

	// **The choice drain -- the noun half of the same wall, and it sits in the same two places for
	// the same two reasons.**
	//
	// `BrowserScene::take_choice()` is the menu outbox's twin:
	// one slot, filled by `Descriptor::accept`'s `Defer` answer, announcing nothing. **After the
	// tick**, because the choice is filed while `Shell::tick` routes the press
	// (`BrowserScene::enter_key_press`, reached from step 3's `pump_input` or step 7's
	// `after_render`). **Above the close check**, so
	// no path through this function can skip it. **Unconditional**, because asking is what takes it.
	//
	// **After `dispatchShellActions()`, and that order is written down rather than incidental.** Both
	// mailboxes can hold something on the same tick: `Defer` closes the browser in the same statement
	// that parks the choice, so the ported menu underneath is frontmost again by steps 5-6 and gets
	// its own `after_render()` at step 7 -- on the very tick the choice
	// was filed. Neither drain reads the other's mailbox, so nothing observable turns on the
	// order except the interaction `shell_choice_dispatch.h` states in full.
	//
	// **By the time this runs the browser is already off the shell's stack** -- `Defer` parks *and*
	// closes -- so there is no "drain while the browser is still open" state for this line to be in.
	dispatchShellChoices();

	// The slicer's loads -- the open a SLICE asked for above, an audition, a slice -- advance on the guest's ticks, as
	// the legacy slicer's do on its `graphicsRoutine`; `rearmTimer` keeps those ticks coming while one is out.
	pollShellSlicer();

	// **The short-Back exit, which is the only exit a user actually performs.**
	//
	// `exitUI()` below is the LONG press: `Buttons` arms `TimerName::BACK_MENU_EXIT` for
	// `LONG_PRESS_DURATION` on Back down (`hid/buttons.cpp`) and the manager calls `exitUI`
	// from that arm (`UITimerManager::routine`). A short Back never reaches it -- it is fed
	// into the shell like any other button (`buttonAction` below) and routed inside `Shell::tick`,
	// where `MenuScene` answers Back at depth 0 with `Action::Kind::Remove{.pop_to = FirstApp}`
	// (`MenuScene::handle_event`'s Back arm, `scenes/menu/menu_scene.cpp`), the port of `sound_editor.cpp`'s BACK
	// case -> `goUpOneLevel()` -> `SoundEditor::goUpOneLevel`, whose depth-0 arm is
	// `exitCompletely()`. That removes the scene and empties the stack, and **nothing in
	// legacy closes this UI when it happens**: `close()` lived only on `exitUI`'s path.
	//
	// What that left is precisely the state `opened()` refuses to create -- "An empty guest is worse
	// than no guest: it would swallow every pad while drawing nothing" (`ShellHostUI::opened`). With the toggle on,
	// Back from the ported menu's root left `shellHostUI` current with an empty stack, `renderOLED` painting nothing
	// over legacy's cleared frame, and every later button (Session, Select) swallowed. The toggle-off leg of the same
	// script exited to the clip view. So the close belongs on both paths, and this is the one that carries the short
	// press.
	//
	// **After the tick, not before**: the tick is what routes the Back and applies the `Remove`
	// (`Shell::tick`), so the stack cannot be seen empty until it has run. And **before
	// `rearmTimer`**, which is skipped entirely on this path for the same reason `exitUI`'s close
	// arms nothing: `closeUI` unsets `TimerName::UI_SPECIFIC` itself (`gui/ui/ui.cpp`) before
	// handing focus to the UI beneath, so a re-arm here would either be undone one line later or
	// deliver this guest's `timerCallback` to that UI (`UITimerManager::routine`).
	// Nothing left that takes input -- an empty stack, or only passive overlays (a knob popup, a console line) -- is a
	// guest with nothing to offer: it closes, and `closeGuest` hands a showing knob popup to legacy and drops the rest.
	if (!shell_.stack().has_input_scene()) {
		closeGuest();
		return ActionResult::DEALT_WITH;
	}

	syncPads();

	// Unconditional here, but **not unconditional inside**: `rearmTimer` refuses when this guest is
	// no longer the current UI, which is exactly what the drain above leaves behind when an arm ends
	// in `openUI(...)`. The whole argument, including why the guard is in the callee, is at
	// `rearmTimer` itself.
	rearmTimer(now);

	// `DEALT_WITH` is inert
	// here: nothing in that arm reads any other value.
	return ActionResult::DEALT_WITH;
}

void ShellHostUI::rearmTimer(uint32_t now) {
	// **Only while this guest is the current UI -- which the action drain does not guarantee.**
	//
	// `TimerName::UI_SPECIFIC` is not this object's timer. It is *the current UI's* timer:
	// `UITimerManager::routine` dispatches it to `getCurrentUI()->timerCallback()`, never to
	// whatever armed it (`UITimerManager::routine`). Arming it while something else is
	// current does not delay this guest's tick, it **delivers this guest's tick to a stranger**.
	//
	// The same hazard shows up three times already. `opened()` guards it for a refused open,
	// `timerCallback`'s close check for the short-Back exit, and `closePortedMenu` rules out
	// `closeGuest()` as an implementation for it (`gui/ui/shell_action_dispatch.cpp`). The path none
	// of the three covers: **three of the `MenuAction` arms end in `openUI(...)`** --
	// `openPerformanceView` and the two context menus (`OpenSongMidiLearn`, `OpenSongMacros`) --
	// while `SetShortcutsVersion` and `ScreensaverSettingsChanged` open nothing. And legacy's own `NO_NAVIGATION`
	// semantics mean the ported menu correctly stays on the shell stack when they do. So `timerCallback`'s
	// no-input-scene close does not fire, control reaches the `rearmTimer` below, and the UI that has just been opened
	// over the guest gets the timer.
	//
	// **The guard is on `getCurrentUI()`, not on that list**, so it does not go stale when the list
	// does -- which matters, because the `take_choice` dispatcher shares the same drain.
	//
	// **What that costs, bounded honestly.** The two context menus inherit `UI::timerCallback`'s
	// `return DEALT_WITH` (`gui/ui/ui.h`) and no `gui/context_menu/` class overrides it, so for
	// those two it is inert. `PerformanceView::timerCallback` is not inert: it forwards into
	// `sessionView.timerCallback()` or `arrangerView.timerCallback()`
	// (`PerformanceView::timerCallback`). Toggle-ON only, and not a wedge -- but a
	// `timerCallback` legacy never delivers.
	//
	// **Returning early cannot strand the guest, and that is by construction rather than by luck.**
	// When the UI opened above it closes, `closeUI` unsets `UI_SPECIFIC` and
	// calls `newUI->focusRegained()` -- which for this guest is an **unconditional**
	// `armTickSoon()` (`ShellHostUI::focusRegained`), written for exactly this shape of disarm. So the guest sleeps
	// while it is covered and re-arms the instant it is uncovered.
	//
	// The guard is here rather than at the one call site because arming a timer that will be
	// delivered elsewhere violates *this function's* contract, not its caller's -- and because the
	// early-return branch below arms a second time for the SD poll, which a call-site guard
	// would leave uncovered on the day someone adds a third. `armTickSoon` is mostly unguarded by
	// design: of its fourteen call sites, ten -- the other input hooks, `opened()`,
	// `displayOrLanguageChanged()` and `exitUI()` -- are on which the guest is current by
	// construction, and `focusRegained`'s is the one arm that MUST fire while this guard is
	// suppressing, so guarding it too would be the wedge this one avoids. **The eleventh is not**:
	// `padAction`'s forward branch can leave a legacy view current when it returns --
	// `performanceView.padAction` and `keyboardScreen.padAction` can both `openUI(&soundEditor)`
	// (`PerformanceView::padAction`, `KeyboardScreen::padAction`) -- so that
	// one site carries its own `getCurrentUI() == this` guard rather than relying on this
	// precondition; see the comment there. Nor are the three note-mode forwards in
	// `buttonAction`, `horizontalEncoderAction` and `verticalEncoderAction`, which call legacy
	// handlers from inside this guest's hooks in the same way, and carry the same guard.
	if (getCurrentUI() != this) {
		return;
	}

	std::optional<uint32_t> deadline = shell_.next_wake(now);
	if (shellSlicerBusy()) {
		const uint32_t poll = now + kShellSlicerPollMs;
		deadline = deadline.has_value() && static_cast<int32_t>(*deadline - poll) < 0 ? *deadline : poll;
	}
	if (!deadline.has_value()) {
		// Nothing armed anywhere in the shell -- no gate deadline, no scene or service timer, no
		// declared blink (`Shell::next_wake`). Legacy has already cleared the timer
		// (`UITimerManager::routine`), so leaving it alone *is* "sleep until the next input",
		// and every path that can wake the guest re-arms it (`armTickSoon`).
		//
		// Except one, which no scene can express as a deadline: a press held back by the SD-routine
		// lock is waiting on a fact outside the shell entirely. Poll for it. At `kMinRearmMs` that
		// is slower than legacy's own retry, which runs from `readButtonsAndPads` on every pass of
		// the main loop (`deluge.cpp`), so it is not a new cost -- and it only runs while
		// the lock is actually held.
		if (shell_.lowering().sd_busy()) {
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kMinRearmMs);
		}
		return;
	}
	// Absolute deadline minus the clock the tick ran on, in the wrap-safe difference form. A
	// deadline at or before `now` -- which `Shell::next_wake()` reports routinely, and reports on
	// every tick while a zero-`next_timer()` scene is stacked -- clamps up to `kMinRearmMs` rather
	// than arming zero. See the class's `@par The unit mismatch` and `@par Why the floor is 1 ms`.
	const int32_t in = static_cast<int32_t>(*deadline - now);
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, std::clamp(in, kMinRearmMs, kMaxRearmMs));
}

void ShellHostUI::wakeForPendingFacts() {
	if (getCurrentUI() == this && deluge::notify::dirty().pending(deluge::notify::DirtyConsumer::Shell)) {
		armTickSoon();
	}
}

void ShellHostUI::armTickSoon() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kMinRearmMs);
}

// ---------------------------------------------------------------------------------------------
// Input
//
// Every hook here does the same three things in the same order: take or release the shell's
// SD-routine lock, feed the event, and return the value legacy's own dispatcher wants. Nothing
// asks the shell what happened, because nothing can -- routing runs in `Shell::pump_input`, which
// only `Shell::tick` calls (`Shell::pump_input`).
//
// **Input is never deferred.** No card routine runs re-entrantly under an input handler, so the
// shell's `InputLowering::set_sd_busy` lock is never taken and every event is fed as it arrives.
// ---------------------------------------------------------------------------------------------

uint32_t ShellHostUI::nowMs() const {
	const double seconds = getSystemTime();
	if (!(seconds > 0.0)) {
		// Also the NaN arm: every comparison against a NaN is false, so the negated `>` catches it.
		// Before the scheduler starts there is no elapsed time to report and 0 is the honest answer.
		return 0;
	}
	return static_cast<uint32_t>(static_cast<uint64_t>(seconds * 1000.0));
}

void ShellHostUI::feed(const DelugeInputEvent& ev) {
	// **Re-read the tap threshold on every event, which is what legacy does.**
	// `LoweringConfig::hold_time_ms` is `FlashStorage::holdTime` -- a setting, in **audio samples**
	// (`defaultHoldTime * kSampleRate / 20`, `storage/flash_storage.cpp`), which legacy reads
	// *live* at shift-release time (`hid/buttons.cpp`) and the shell reads out of a config field.
	// `InputLowering::set_hold_time_ms` (`shell/input_lowering.h`) is the setter written for this
	// line, and the conversion to milliseconds is `* 1000 / kSampleRate`. Legacy's default 2 gives
	// 4410 samples and 100 ms, exactly what `LoweringConfig`'s own default documents; the settings
	// item's range is 1..20, re-clamped on read (`storage/flash_storage.cpp`), i.e.
	// 50..1000 ms, so the `uint16_t` cannot overflow.
	//
	// **Here rather than in `opened()`.** Seeding once per open would assume that "a guest that is
	// not open cannot be observing the threshold" -- which is backwards for the one writer that can
	// only be reached *while the guest is open*: `nodes::set_default_hold_time` (`scenes/menu/nodes/settings.cpp`) is
	// the ported settings item, and it writes `FlashStorage::holdTime` from inside the shell. A per-open seed lags it
	// by a whole guest session. This is the same hole the screensaver settings have from the ported tree, and unlike
	// that one it is closable here, because the *reader* is on this side of the seam.
	//
	// This is the funnel every pad and every button passes through (`padAction`, `buttonAction`), so
	// it is where legacy's own read happens: two loads, a divide and a compare-free store, on a path
	// that already builds an event and takes the SD lock. Encoder turns deliberately do not come
	// through here and do not need to -- `hold_time_ms` gates the sticky-shift tap and nothing else.
	shell_.lowering().set_hold_time_ms(
	    static_cast<uint16_t>(static_cast<uint32_t>(FlashStorage::holdTime) * 1000u / kSampleRate));

	shell_.on_board_event(ev, nowMs());
}

void ShellHostUI::feedDetents(EncoderRole role, int32_t detents) {
	if (detents == 0) {
		// `interpretEncoders` only calls a hook when `fe.pending()` (`hid/encoder_input.cpp:109`),
		// so a zero is not something legacy produces -- but `selectEncoderAction` is also reachable
		// from the SD test mode (`:83`) and from anything else that decides to drive a UI directly,
		// and a zero-edge feed would still set `had_input_this_tick` and wake the repaint gate.
		return;
	}
	const std::span<const deluge::ui::EncoderRole> byIndex = shell_.lowering().profile().encoder_by_index;
	const int32_t per = config_.lowering.edges_per_detent > 0 ? config_.lowering.edges_per_detent : 1;
	// See the `@warning` on the declaration: the clamp is in detents, before the multiply, so the
	// product lands inside `int8_t` for any `edges_per_detent`.
	const int32_t limit = 127 / per;
	const int8_t edges = static_cast<int8_t>(std::clamp(detents, -limit, limit) * per);
	for (size_t i = 0; i < byIndex.size(); i++) {
		if (byIndex[i] != role) {
			continue;
		}
		shell_.on_encoder_edges(static_cast<uint8_t>(i), edges, nowMs());
		return;
	}
	// A profile with no encoder in that role. Dropped rather than guessed at, the same way
	// `InputLowering::apply_encoder_edges` drops an index past the table
	// (`InputLowering::apply_encoder_edges`).
}

bool ShellHostUI::claimsGridPad(int32_t x, int32_t y, int32_t velocity) {
	switch (shell_.stack().input_top().grid_pad_claim()) {
	case deluge::ui::shell::GridPadClaim::All:
		return true;
	case deluge::ui::shell::GridPadClaim::None:
		return false;
	case deluge::ui::shell::GridPadClaim::ShortcutsOnly:
		// `SoundEditor::potentialShortcutPadAction`'s gate, minus the `isUIModeActive(UI_MODE_AUDITIONING)` half, which
		// stays on legacy's side and which `MenuScene` already drops with the reason recorded (`MenuPads::handle_pad`,
		// `scenes/menu/pads.cpp`). A pad the root view keeps (`rootKeepsShortcutPad`: the automation view's
		// interpolation pads) is no shortcut. Past the gate legacy claims unconditionally, even where no shortcut table
		// has an entry -- and so does `MenuScene`, which returns `Result::unhandled()` and lets the pad die
		// (`MenuScene::handle_event`'s fall-through). The two agree.
		if (rootKeepsShortcutPad(x, y)) {
			return false;
		}
		return velocity != 0 && x < kDisplayWidth && Buttons::isShiftButtonPressed();
	}
	return true; // `-Werror=switch` makes this unreachable; it is here for the enum's future.
}

ActionResult ShellHostUI::padAction(int32_t x, int32_t y, int32_t velocity) {
	// **The range guard is about the narrowing, not about the shell.** `on_pad` already drops a
	// coordinate outside `PadMask`'s 32x32 (`InputLowering::on_pad`), but only after
	// this side has cast to `uint8_t`, and that cast folds: `x = 256` becomes column 0 and the
	// shell is handed a pad press that never happened. `MatrixDriver::padAction` bounds nothing of
	// its own (`MatrixDriver::padAction`), and this is a public virtual that legacy UIs
	// call on each other directly -- `SoundEditor::padAction` forwards straight into
	// `keyboardScreen.padAction` (`SoundEditor::padAction`) -- so the caller's domain is
	// not `MatrixDriver`'s alone.
	//
	// Unlike `buttonAction`'s (see there) this guard is not redundant with `on_pad`'s and is
	// covered by an example that folds a coordinate on purpose.
	if (x >= 0 && y >= 0 && x <= UINT8_MAX && y <= UINT8_MAX) {
		// The release of a press the shell was fed is the shell's too, whatever the claim now says: a Shift + pad
		// shortcut is released with Shift up, and leaving that release with legacy would leave the pad held in the
		// shell for good.
		const auto px = static_cast<uint8_t>(x);
		const auto py = static_cast<uint8_t>(y);
		const bool release_of_fed = velocity == 0 && fedPads_.test(px, py);
		if (release_of_fed || claimsGridPad(x, y, velocity)) {
			fedPads_.set(px, py, velocity != 0);
			feed(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
			                      static_cast<int16_t>(velocity)});
		}
		else {
			// The pad is the legacy root view's, as it is in `SoundEditor::padAction`.
			//
			// **The guest may not be current when this returns**, and nothing below may assume it
			// is: `performanceView.padAction` opens the legacy sound editor on a shift press
			// (`PerformanceView::padAction`) and `keyboardScreen.padAction` reaches the
			// same call unconditionally (`KeyboardScreen::padAction`); the `Exit` arm's
			// `closePortedMenu()` empties this guest's own stack instead, which leaves the guest
			// current. `armTickSoon` itself is unguarded (see `rearmTimer`'s docblock) -- arming
			// `UI_SPECIFIC` while some other UI is current would deliver the tick to *that* UI
			// (`UITimerManager::routine`), not to this one -- so the
			// guard is here instead: arm only when this guest is still the current UI. `Exit` is
			// unaffected by the guard -- the guest is current after the menu leaves its stack -- and the
			// arm is *required* there: the tick is what notices nothing stacked takes input and closes
			// the guest (`timerCallback`).
			shell_.note_activity(nowMs()); // `deluge.cpp`: every pad is activity, forwarded or not
			(void)forwardGridPad(x, y, velocity);
			if (getCurrentUI() == this) {
				// A pad the root took can move it between views (the automation view's editor and overview, a kit row).
				refreshRootFacts();
				armTickSoon();
			}
			return ActionResult::DEALT_WITH;
		}
	}

	// `DEALT_WITH`, the base's own value: a pad the guest swallows stays swallowed, which is what an
	// open full-screen UI should do. (Reached only on the fed path and on a coordinate the range guard
	// above dropped -- the forwarded path returns above.)
	armTickSoon();
	return ActionResult::DEALT_WITH;
}

ActionResult ShellHostUI::buttonAction(deluge::hid::Button b, bool on) {
	// The two gold-knob pushes are the knobs' service's: fed here, ahead of the note mode, which has no arm for them.
	// `Buttons::buttonAction` delivers each press twice -- here, and to `modEncoderButtonAction` in its tail, which
	// takes it only in the automation view's editor -- so it is performed once. `NOT_DEALT_WITH` keeps that tail, and
	// the shift and transport handling above it, running (see the end of this function).
	if (b == deluge::hid::button::MOD_ENCODER_0 || b == deluge::hid::button::MOD_ENCODER_1) {
		if (automationEditorTakesKnobs()) {
			return ActionResult::NOT_DEALT_WITH; // the view's, through `modEncoderButtonAction`
		}
		const Cartesian xy = deluge::hid::button::toXY(b);
		feed(DelugeInputEvent{DELUGE_EVENT_BUTTON, static_cast<uint8_t>(xy.x), static_cast<uint8_t>(xy.y),
		                      static_cast<int16_t>(on ? 255 : 0)});
		armTickSoon();
		return ActionResult::NOT_DEALT_WITH;
	}

	// **A note mode's button is legacy's handler's, not the shell's** -- the port of
	// `SoundEditor::buttonAction`'s two note arms.
	// `forwardNoteButton` (`gui/ui/shell_note_mode.h`) is the whole classification: `std::nullopt` for
	// every button that is not the mode's -- outside the note modes, the six legacy's chain takes
	// first, the four the ported menu's column focus owns, and the four the handlers answer
	// `NOT_DEALT_WITH` -- which falls through to the feed below exactly as before; otherwise the
	// handler's own `ActionResult`, returned as it is.
	//
	// Below the gold-knob early return rather than above it: those two are `std::nullopt` either way
	// (`InstrumentClipView::buttonAction`), and above it the drop's own
	// argument would have to be made twice.
	//
	// **The handler may have opened or closed a UI by the time it returns**, so nothing after it
	// touches this guest's state except through `padAction`'s `getCurrentUI() == this` guard, for its reason:
	// `TimerName::UI_SPECIFIC` is delivered to whoever is current (`UITimerManager::routine`). The
	// arm itself is still wanted while the guest is current, for the gold-knob arm's reason above.
	if (const std::optional<ActionResult> result = forwardNoteButton(b, on)) {
		shell_.note_activity(nowMs()); // `deluge.cpp`
		if (getCurrentUI() == this) {
			armTickSoon();
		}
		return *result;
	}

	// CLIP, X_ENC and KEYBOARD where the automation view takes them from under the menu (`forwardAutomationButton`):
	// the param item's own CLIP and X_ENC, and the sound editor's KEYBOARD arm over the view. Below the note forward,
	// whose roots these never share; the handler may swap the root, so the same guard applies.
	if (const std::optional<ActionResult> result = forwardAutomationButton(b, on)) {
		shell_.note_activity(nowMs()); // `deluge.cpp`
		if (getCurrentUI() == this) {
			armTickSoon();
		}
		return *result;
	}

	// The exact inverse of `deluge.cpp`'s `deluge::hid::button::fromXY(ev.x, ev.y)`: a
	// `hid::Button` is `9 * (y + kDisplayHeight * 2) + x` and `toXY` undoes it
	// (`hid/button.h`, `hid/button.cpp`). Feeding matrix coordinates rather than a
	// `ButtonId` is deliberate -- `InputLowering::on_matrix` is what owns the matrix-to-`ButtonId`
	// table, through `BoardProfile::button_by_matrix` (`InputLowering::on_matrix`), and it
	// is the same table the level-3 harness cross-checks against.
	//
	// **No range guard here, unlike `padAction` above, and the arithmetic is why.** `toXY` returns
	// `int32_t`s, and legacy does have a "fake button" column past the matrix
	// (`buttonStates[NUM_BUTTON_COLS + 1][...]` in `hid/buttons.cpp`, reachable through
	// `button::fromChar` in `hid/button.h` -- which has no callers in `src/`). But a
	// `Button` is a `uint8_t`, so `toXY` gives `x = b % 9` in 0..8 and `y = b / 9 - 16` in -16..12,
	// and every value outside the real matrix is dropped by `on_matrix` either way:
	// x is always inside `button_cols` (9); a negative y narrows to 240..255 and a y of 4..12 stays
	// itself, and both are at or past `button_rows` (4), which `on_matrix` tests before it indexes
	// (`InputLowering::on_matrix`). There is no value of `b` for which the narrowing
	// changes the answer -- **on a profile whose `button_rows` is at most 13**, which is what makes
	// the Deluge's 4 (`shell/profiles/deluge.cpp`, and the matrix table it indexes) safe. The proof
	// is board-specific, not universal: a hypothetical profile with 14 or more button rows would let a `y` of 13 index
	// a real cell for a `b` legacy never means as a matrix button. Stated rather than guarded, because no such profile
	// exists and a guard against one would be untestable. So a guard here would be a second copy of a check that
	// already holds. A mutation removing one is unkillable, which is the evidence.
	const Cartesian xy = deluge::hid::button::toXY(b);
	feed(DelugeInputEvent{DELUGE_EVENT_BUTTON, static_cast<uint8_t>(xy.x), static_cast<uint8_t>(xy.y),
	                      static_cast<int16_t>(on ? 255 : 0)});

	// **`NOT_DEALT_WITH`, and it is load-bearing three times over.** It is what `SoundEditor`
	// returns for every button with an observable consequence in `Buttons::buttonAction`'s tail:
	// its `if`/`else if` chain has no arm for Play, Shift, Record, Tempo-enc or either mod-encoder
	// button, so those fall into the final `else` and take `MenuItem::buttonAction`'s default
	// `NOT_DEALT_WITH` (`gui/ui/sound_editor.cpp`, `gui/menu_item/menu_item.h`).
	// Returning `DEALT_WITH` here instead would `goto dealtWith` (`hid/buttons.cpp`) and:
	//
	//   1. **stop `Buttons::commandToggleShift` running** (`hid/buttons.cpp`), which is the
	//      one thing maintaining legacy's shift state -- see the class's `@par Shift`. The
	//      transport would go with it, which `SoundEditor` does not do;
	//   2. **stop the mod-encoder buttons reaching `modEncoderButtonAction`** which
	//      is where the gold-knob presses live;
	//   3. buy nothing, because every button the shell's menu actually claims -- Back, Select-enc,
	//      Save, Load, Learn, Keyboard, Session -- has **no arm in that tail at all** and falls
	//      straight through to `dealtWith:`. Read the braces rather than the shape:
	//      the tail's chain names Play, Shift, Record, Tempo-enc, Mod-enc-0 and
	//      Mod-enc-1 and nothing else.
	//
	// **The long-press-Back timer is not in that tail.** `Buttons` arms `TimerName::BACK_MENU_EXIT`
	// *above* the `getCurrentUI()->buttonAction(...)` call and unconditionally on the value it
	// returns. So `exitUI()` keeps being driven whatever this returns.
	armTickSoon();
	return ActionResult::NOT_DEALT_WITH;
}

void ShellHostUI::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	// In the automation view's editor the knobs edit the view's param (`SoundEditor::modEncoderAction`).
	if (automationEditorTakesKnobs()) {
		forwardAutomationGoldTurn(whichModEncoder, offset);
		armTickSoon();
		return;
	}
	// 0 is the lower knob, 1 the upper, as `interpretEncoders` numbers them; @p offset is legacy's lowered turn.
	(void)shell_.route_lowered_turn(whichModEncoder == 0 ? EncoderRole::GoldLower : EncoderRole::GoldUpper,
	                                static_cast<int8_t>(std::clamp<int32_t>(offset, -127, 127)), nowMs());
	armTickSoon();
}

void ShellHostUI::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	// `Buttons::buttonAction`'s tail delivers the push here as well as to `buttonAction`, which feeds the knobs'
	// service; in the automation view's editor it is the view's instead (`SoundEditor::modEncoderButtonAction`).
	if (automationEditorTakesKnobs()) {
		forwardAutomationGoldPush(whichModEncoder, on);
		armTickSoon();
	}
}

void ShellHostUI::modButtonAction(uint8_t whichButton, bool on) {
	if (whichButton >= std::size(deluge::hid::button::ZmodButtonX)) {
		return;
	}
	feed(DelugeInputEvent{DELUGE_EVENT_BUTTON, deluge::hid::button::ZmodButtonX[whichButton],
	                      deluge::hid::button::ZmodButtonY[whichButton], static_cast<int16_t>(on ? 255 : 0)});
	armTickSoon();
}

ActionResult ShellHostUI::horizontalEncoderAction(int32_t offset) {
	// `SoundEditor::horizontalEncoderAction`'s note arms, which come before
	// anything else it does. The forwarded handler's own result is returned.
	if (const std::optional<ActionResult> result = forwardNoteHorizontalEncoder(offset)) {
		shell_.note_activity(nowMs()); // `hid/encoder_input.cpp`
		if (getCurrentUI() == this) {
			armTickSoon();
		}
		return *result;
	}
	if (const std::optional<ActionResult> result = forwardMenuHorizontal(offset, nowMs())) {
		shell_.note_activity(nowMs());
		if (getCurrentUI() == this) {
			armTickSoon();
		}
		return *result;
	}
	feedDetents(EncoderRole::Horizontal, offset);
	// `DEALT_WITH`, the base's value.
	armTickSoon();
	return ActionResult::DEALT_WITH;
}

ActionResult ShellHostUI::verticalEncoderAction(int32_t offset) {
	// `SoundEditor::verticalEncoderAction`'s note arms. The same shape as
	// `horizontalEncoderAction`'s.
	if (const std::optional<ActionResult> result = forwardNoteVerticalEncoder(offset)) {
		shell_.note_activity(nowMs()); // `hid/encoder_input.cpp`
		if (getCurrentUI() == this) {
			armTickSoon();
		}
		return *result;
	}
	if (const std::optional<ActionResult> result = forwardMenuVertical(offset)) {
		shell_.note_activity(nowMs());
		if (getCurrentUI() == this) {
			armTickSoon();
		}
		return *result;
	}
	feedDetents(EncoderRole::Vertical, offset);
	armTickSoon();
	return ActionResult::DEALT_WITH;
}

void ShellHostUI::selectEncoderAction(int8_t offset) {
	if (forwardAutomationSelectTurn(offset)) {
		armTickSoon();
		return;
	}
	// `void`: legacy discards nothing because there is nothing to discard
	// (`UI::selectEncoderAction`), so unlike the other two this hook has no way to say
	// "not now" even in legacy. The offset arrives already saturated to `int8_t`
	// (`hid/encoder_input.cpp`), so `feedDetents`'s clamp can only bite on
	// the outer 64 values of that range.
	feedDetents(EncoderRole::Select, offset);
	armTickSoon();
}

// ---------------------------------------------------------------------------------------------
// Leaving
// ---------------------------------------------------------------------------------------------

ActionResult ShellHostUI::exitUI() {
	// **What this hook is in legacy.** Chiefly the long-press-Back exit: `Buttons` arms
	// `TimerName::BACK_MENU_EXIT` for `LONG_PRESS_DURATION` when Back goes down and unsets it on
	// release (`hid/buttons.cpp`), and the timer manager calls this from that arm
	// (`UITimerManager::routine`). `SoundEditor::exitUI` is `exitCompletely()`
	// (`gui/ui/sound_editor.h`), which leaves the whole menu however deep it was, not one
	// level of it.
	//
	// **A UI that exits closes itself; it does not return `NOT_DEALT_WITH` for `closeUI` to act on.**
	// Neither caller does that, and every legacy implementation closes *itself*. `RenameUI::exitUI` is `close(); return
	// ACTIONED_AND_CAUSED_CHANGE;`
	// (`gui/ui/rename/rename_ui.cpp`), `SampleMarkerEditor::exitUI` is the same two lines
	// (`gui/ui/sample_marker_editor.cpp`), `Browser::exitUI` is `exitAction()` and the same
	// value (`gui/ui/browser/browser.h`), and `exitCompletely` calls `close()`. A guest that only returned a value
	// would trap the user inside itself, because nothing on the other end acts on it.
	//
	// @par The two readers of the return value, measured
	//
	// `grep -rn "exitUI" src/ | grep -v exitUIMode` is 26 lines; two of them read what comes back,
	// and they read it for different values:
	//
	//   * `gui/ui_timer_manager.cpp` stores it. There is no `closeUI` in that path.
	//   * `gui/ui/keyboard/layout/column_controls.cpp` tests for
	//     `ACTIONED_AND_CAUSED_CHANGE`, and on it calls `keyboardScreen.killColumnSwitchKey(LEFT_COL)`
	//     -- the keyboard sidebar's menu-exit pad, behind
	//     `RuntimeFeatureSettingType::EnableKeyboardViewSidebarMenuExit`.
	//
	// **The close path answers `ACTIONED_AND_CAUSED_CHANGE`, and the reason is not the keyboard
	// reader above.** Routing pads into the *shell* does not put a pad anywhere near
	// `KeyboardScreen`: `MatrixDriver::padAction` calls `getCurrentUI()->padAction` and nothing else,
	// so while the guest is open the only way `ColumnControlsKeyboard::evaluatePads` runs at all is
	// if the current UI hands the pad on -- which is precisely what `SoundEditor::padAction` does
	// when the keyboard screen is the root and what this guest does **not** do. `evaluatePads` has
	// **one route in**, not one caller: `grep -rn evaluatePads src/deluge` gives six call sites, of
	// which five are subclasses chaining up to `ColumnControlsKeyboard::evaluatePads` (the layouts
	// under `gui/ui/keyboard/layout/`) and only `KeyboardScreen` enters from outside the hierarchy.
	// So that reader stays unreachable under the guest.
	//
	// The value is what it is for the reasons that do hold:
	//
	//   * `ui.h`'s own contract for this hook is "returns whether a UI exited" (`UI::exitUI`).
	//     On the close path a UI did exit, so `NOT_DEALT_WITH` would be the wrong answer to the
	//     question asked, however honest it is about the press.
	//   * Every legacy implementation that closes itself answers `ACTIONED_AND_CAUSED_CHANGE`,
	//     including the tenant this guest stands in for: `SoundEditor::exitUI` is `exitCompletely()`
	//     (`gui/ui/sound_editor.h`), whose last line is that value. With the toggle on, the guest
	//     does what `SoundEditor` does.
	//   * It is provably safe: the two readers above are the only ones, one tests a different
	//     value and the other cannot run.
	//
	// **The pop depth is not a divergence.** Legacy's long-press Back leaves the *whole* menu
	// however deep it was, not one level. So does this, for the first tenant: the ported menu keeps
	// its navigation depth inside one scene (`MenuScene`'s `st_.depth`) and only asks the shell to
	// remove itself when Back arrives at depth 0 (`MenuScene::handle_event`'s Back arm). One `pop()`
	// here is therefore one whole menu, exactly as `exitCompletely` is. A future tenant that stacks a
	// scene per level would make this differ, and that is the condition to watch.
	//
	// **Leaving the *settings* menu persists settings.** It is what writes `FlashStorage`, the MIDI
	// devices file, the runtime-feature settings and the MIDI-follow defaults, and `closeGuest` does
	// that through `saveSettingsBeforeClose`, which keys on the ported menu's root rather than on
	// the stack (the guest itself cannot know it is in a settings root).
	//
	// Both directions of the contract, spelled out:
	//
	//  - **A scene that takes input is still stacked after the exit** -- something inside the guest
	//    took it. `DEALT_WITH`, and the guest stays open. Returning `NOT_DEALT_WITH` here would be a
	//    lie about a press that was consumed, and closing here would pop the guest a level early.
	//  - **None is** -- the stack is empty or holds only passive overlays (a knob popup, a console
	//    line): the guest has nothing left to offer and is itself what Back leaves. `closeGuest()`
	//    hands a showing knob popup to legacy, drops the other overlays and closes, then
	//    `ACTIONED_AND_CAUSED_CHANGE`.
	//
	// What Back leaves is the input top, not the raw top: a passive overlay drawn over it is not
	// the scene the user is in.
	// A slice that is out lands in the kit it was asked for, so the guest stays until it has: as the slicer refuses
	// a short Back while it commits.
	if (shellSliceOut()) {
		return ActionResult::DEALT_WITH;
	}
	if (shell_.stack().has_input_scene()) {
		(void)shell_.stack().remove(shell_.stack().input_top().kind());
	}
	if (shell_.stack().has_input_scene()) {
		// The stack moved, so the next tick will re-render everything (`SceneStack::pop` sets
		// `changed_`, which `Shell::tick` turns into a repaint request
		// on every gate) -- but nothing schedules that tick. This hook is
		// reached from `TimerName::BACK_MENU_EXIT` (`UITimerManager::routine`), not from
		// `UI_SPECIFIC`, so whatever the guest last armed is still standing and may be a long way off.
		armTickSoon();
		return ActionResult::DEALT_WITH;
	}
	closeGuest();
	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

void ShellHostUI::closeGuest() {
	// **Hand the two knob indicator rings back to legacy, coherently.** `GuestSurfaceSet::set_indicator` writes the
	// hardware through `deluge_control_set_indicator` without touching legacy's `knobIndicatorLevels[]` shadow -- there
	// is no scalar at that seam to hand `actuallySetKnobIndicatorLevel`, because a `KnobIndicator` is already the four
	// segment levels. So after the guest has driven a ring, legacy's shadow describes a frame the hardware is no longer
	// showing, and `actuallySetKnobIndicatorLevel`'s early-out (`hid/led/indicator_leds.cpp`) then
	// **suppresses** the write that would put it right. `ModKnobs` is the one declarer of
	// `frame.indicator(...)` in `src/scenes` (`services/mod_knobs.cpp`), so it is the one that drives
	// the rings.
	//
	// `clearKnobIndicatorLevels()` (`hid/led/indicator_leds.h`) is `setKnobIndicatorLevel(i, 0)`
	// for both, which puts shadow and hardware back in step at zero, and it must run
	// **before** `close()`: `closeUI` hands focus to the UI beneath on the same call
	// (`closeUI` in `gui/ui/ui.cpp`), and that is what re-fills the rings -- every root view's
	// `focusRegained` reaches `view.setActiveModControllableTimelineCounter(...)`
	// (`SessionView::focusRegained`, `ArrangerView::focusRegained`,
	// `InstrumentClipMinder::focusRegained`), whose tail is `setKnobIndicatorLevels()`
	// (`View::setActiveModControllableTimelineCounter`). Clearing after the close would blank rings nothing then
	// refreshes.
	//
	// **One residual, legacy's own.** `SessionView` and `ArrangerView` skip that refresh while
	// `UI_MODE_CLIP_PRESSED_IN_SONG_VIEW` / `UI_MODE_HOLDING_ARRANGEMENT_ROW` is active, so a guest
	// closed during either leaves the rings dark until the next thing that sets levels.
	//
	// This function is the body of both of the guest's own close paths -- `exitUI`'s long-press Back
	// and `timerCallback`'s close once nothing stacked takes input. A `closeUI(&shellHostUI)` from
	// elsewhere has no hook to run it from, since `UI` declares none.
	indicator_leds::clearKnobIndicatorLevels();

	// **Then the note mode's own exit, which `SoundEditor::exitCompletely` runs before its `close()`**:
	// `instrumentClipView.exitNoteEditor()` and a grid repaint, or `exitNoteRowEditor()`
	// and `close()`. `exitNoteModeBeforeClose`
	// (`gui/ui/shell_note_mode.h`) is that arm. It keys on the menu's root rather than on the stack,
	// because on both paths into this function the menu has already left the stack. Here, below the
	// indicator clear and before `close()`, because a close first would run the root
	// view's `focusRegained` (`closeUI` in `gui/ui/ui.cpp`) while the note selection legacy clears first was
	// still set.
	//
	// **`close()` is not re-checked against what the exit did**, which is legacy's order too:
	// `exitCompletely` asks nothing between its note arms and its `close()`, and `closeUI` searches the
	// hierarchy for this guest rather than assuming it is on top (`closeUI` in `gui/ui/ui.cpp`).
	exitNoteModeBeforeClose();
	// And its settings arm, which comes first there; the two roots never meet.
	saveSettingsBeforeClose();
	// A drum the creator made and the browser was backed out of: `SampleBrowser::exitAction` deletes it before closing.
	discardShellDrumCreatorBeforeClose();
	releaseShellSlicerBeforeClose();

	// The knob popup still showing goes to legacy's display, then the stack empties so the next open starts clean.
	if (closeHook_ != nullptr) {
		closeHook_(nowMs());
	}
	// `closeUI` asks the closing UI whether it drew the pads (`closeUI` in `gui/ui/ui.cpp`), and the
	// guest's emptied stack answers no, so the regions it answered for are handed back here, while
	// it is still on legacy's stack for `uiNeedsRendering` to find. They are the regions `syncPads`
	// last recorded, not the stack's: on the short-Back path the tick has already emptied it.
	const deluge::ui::shell::PadRegions answered = padsAnswered_;
	shell_.stack().reset();
	padsOwned_ = deluge::ui::shell::PadRegions::None;
	padsAnswered_ = deluge::ui::shell::PadRegions::None;
	handBack(answered);

	// No arm on the close path: `UI::close()` is `closeUI(this)`, which unsets
	// `TimerName::UI_SPECIFIC` itself before handing focus to the UI beneath. Arming here would either be undone one
	// line later or, worse, deliver this guest's `timerCallback` to that UI -- `UITimerManager::routine` dispatches to
	// `getCurrentUI()`.
	close();
}

// -- MIDI learn ---------------------------------------------------------------------
//
// See `@par MIDI learn` on the class. Each offer goes to the guest's chain -- the input-top scene, then the song view's
// learn -- and, when nothing there uses it, to the base answer, which is legacy's own learn mode reaching `View`.

namespace {

/// @brief A MIDI learn offer as the shell's event.
deluge::ui::shell::UiEvent midi_event(deluge::ui::shell::UiEvent::Kind kind, MIDICable& cable, int32_t channel,
                                      int32_t data1, int32_t data2) {
	return deluge::ui::shell::UiEvent{.kind = kind,
	                                  .channel = static_cast<uint8_t>(channel),
	                                  .data1 = static_cast<uint8_t>(data1),
	                                  .data2 = static_cast<uint8_t>(data2),
	                                  .cable = &cable};
}

} // namespace

bool ShellHostUI::noteOnReceivedForMidiLearn(MIDICable& cable, int32_t channelOrZone, int32_t note, int32_t velocity) {
	using Kind = deluge::ui::shell::UiEvent::Kind;
	return shell_.route_midi(midi_event(Kind::MidiNoteOn, cable, channelOrZone, note, velocity), nowMs())
	       || UI::noteOnReceivedForMidiLearn(cable, channelOrZone, note, velocity);
}

bool ShellHostUI::pcReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t program) {
	using Kind = deluge::ui::shell::UiEvent::Kind;
	return shell_.route_midi(midi_event(Kind::MidiProgramChange, cable, channel, program, 0), nowMs())
	       || UI::pcReceivedForMidiLearn(cable, channel, program);
}

bool ShellHostUI::ccReceivedForMidiLearn(MIDICable& cable, int32_t channelOrZone, int32_t cc, int32_t value) {
	using Kind = deluge::ui::shell::UiEvent::Kind;
	return shell_.route_midi(midi_event(Kind::MidiCc, cable, channelOrZone, cc, value), nowMs())
	       || UI::ccReceivedForMidiLearn(cable, channelOrZone, cc, value);
}

bool ShellHostUI::pitchBendReceivedForMidiLearn(MIDICable& cable, int32_t channel, uint8_t data1, uint8_t data2) {
	using Kind = deluge::ui::shell::UiEvent::Kind;
	return shell_.route_midi(midi_event(Kind::MidiPitchBend, cable, channel, data1, data2), nowMs())
	       || UI::pitchBendReceivedForMidiLearn(cable, channel, data1, data2);
}

} // namespace deluge::gui::ui

// SDRAM: the guest and its scene stack are menu-time state, never read by the audio tier or an interrupt. The scenes'
// profile, not the shell's: it carries the l10n bridge, so a popup a scene raises by string id shows its text.
PLACE_SDRAM_BSS deluge::gui::ui::ShellHostUI shellHostUI{
    deluge::scenes::scene_registry(), deluge::ui::shell::kDelugeBoard, deluge::scenes::deluge_profile()};
