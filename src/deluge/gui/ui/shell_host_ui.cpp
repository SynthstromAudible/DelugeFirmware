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

#include "scenes/registry.h"
#include "shell/profiles/deluge.h"

#include <algorithm>
#include <cstddef>

namespace deluge::gui::ui {

using deluge::ui::shell::Scene;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::Services;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

ShellHostUI::ShellHostUI(const SceneRegistry& registry, const DelugeBoard& board,
                         const deluge::ui::shell::BoardProfile& profile)
    : surfaces_(board, profile), shell_(surfaces_, profile, registry, Services{}, ShellConfig{}) {
	// `Services{}` and a default `ShellConfig` on purpose: the three real services each take this
	// shell's own `SceneStack&`, so they cannot exist before it. F1 task 8 builds them over
	// `shell()` and installs them through `Shell::set_services` (`shell/shell.h:143-175`), which is
	// the whole reason that setter exists.
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

bool ShellHostUI::opened() {
	// A profile whose Main surface legacy's single `OLED::main` cannot hold has nowhere to put its
	// bytes: `GuestSurfaceSet::blit` would drop every frame and the user would see the last thing
	// legacy drew, forever. Refusing here is what `canReachMainPanel()` was built for, and legacy
	// acts on the refusal: `openUI` unwinds `numUIsOpen`, re-assesses the greyout and resumes the
	// previous UI when `opened()` returns false (`gui/ui/ui.cpp:227-245`, the rollback at
	// `:237-242`). `changeUIAtLevel` has its own copy of that unwind at `:96-101`; `openUI` does
	// not go through it.
	if (!canOpen()) {
		return false;
	}

	// Start from an empty stack rather than pushing onto whatever the last session left. This is
	// not tidiness: `SceneStack` holds one `Scene*` per kind, so pushing a kind that is already
	// stacked puts one object at two indices, and no lifecycle hook is coherent under that -- the
	// scene takes `on_enter` twice with no `on_exit` between and both indices share a single
	// `SceneState` slot (`shell/scene_stack.h:200-206`). Legacy's equivalent is
	// `SoundEditor::opened` calling `beginScreen()` (`gui/ui/sound_editor.cpp:259-273`), which
	// begins a menu session rather than resuming one.
	shell_.stack().reset();
	// Ports legacy's "the panel is stale, draw it whatever the gates think" -- a repaint requested
	// on every surface, nothing pushed (`shell/shell.cpp:230-239`).
	shell_.boot();

	const size_t before = shell_.stack().depth();
	shell_.init(initial_);
	// `Shell::init` is `stack_.push(initial)` with the result dropped (`shell/shell.cpp:226-228`),
	// and `SceneStack::push` refuses an unregistered kind (`shell/scene_stack.h:61-67`) -- which is
	// exactly what a caller that never called `setInitialScene` gets, since a default `SceneId` is
	// `SceneKind::Null`. An empty guest is worse than no guest: it would swallow every pad
	// (`UI::padAction`'s `DEALT_WITH` default) while drawing nothing. So a refused push is a
	// refused open.
	return shell_.stack().depth() > before;
}

void ShellHostUI::focusRegained() {
	// Legacy calls this on the UI that is the current one again after something above it closed
	// (`closeUI`, `gui/ui/ui.cpp:194-199`), and `Scene::on_resume` is the port of exactly that
	// call, in as many words (`shell/scene.h:311-326`).
	//
	// **`input_top()`, not `top()`**, and this is a deliberate departure from the brief's wording
	// ("`on_resume` on the shell's top"). `on_resume`'s own contract is that it fires on the scene
	// *input reaches* again, which is why `SceneStack` fires it on `input_top()` and never on a
	// passive layer (`shell/scene_stack.h:38-46`). Handing it to `top()` would give the resume to a
	// popup or a console line that never took input in the first place -- the §15.7 defect the
	// stack was fixed for -- and would skip the scene that genuinely gets input back.
	shell_.stack().input_top().on_resume();
}

void ShellHostUI::displayOrLanguageChanged() {
	// The shell is retained-mode: a surface is re-rendered only when a gate asks for it
	// (`shell/shell.cpp:230-239` is the one public way to ask for all of them). A language change
	// invalidates every string on screen without changing anything the shell watches, so without
	// this the guest would keep serving the frame it last rendered until something else happened.
	// `SoundEditor` overrides this hook too, for the same reason
	// (`gui/ui/sound_editor.cpp:298-300`, `readValueAgain`).
	//
	// **What this does not buy.** The repaint lands on the shell's next tick (F1 task 7), whereas
	// the base's `renderUIsForOled()` makes legacy pull `renderOLED` before that -- so exactly one
	// frame of the old language can still go out. Chaining to the base keeps that pull, because the
	// alternative is legacy never being told the panel is stale at all.
	shell_.boot();
	::UI::displayOrLanguageChanged();
}

// ---------------------------------------------------------------------------------------------
// Composition queries
// ---------------------------------------------------------------------------------------------

bool ShellHostUI::canSeeViewUnderneath() {
	// **Not `oled_transparent(Main)`, and this is a correction to spec F §2.1's table.** That row
	// pairs this hook with the scene's OLED transparency in both directions (§2.1 for the guest,
	// §2.2's `LegacySceneAdapter` for the inverse), and the shipped tree contradicts it twice over:
	//
	//  - Legacy's OLED transparency is a *data member*, `UI::oledShowsUIUnderneath`
	//    (`gui/ui/ui.h:158`), set false by `UI::UI` (`gui/ui/ui.cpp:31-33`) and read by
	//    `doAnyPendingOLEDRendering` to decide how far down the stack to start drawing
	//    (`gui/ui/ui.cpp:351-371`, the walk at `:353-356`). `SoundEditor` leaves it false and
	//    `MenuScene::oled_transparent` returns false (`scenes/menu/menu_scene.h:448-449`) -- those
	//    two already agree, and neither is this hook.
	//  - `Slicer` sets `oledShowsUIUnderneath = true` and returns **false** here
	//    (`gui/ui/slicer.h:33-38`), on the same object. One legacy UI holding both values at once is
	//    proof the two questions are independent.
	//
	// What this hook actually gates is whether the *root view* is still live underneath:
	// `UI::graphicsRoutine` runs the root's per-frame animation only when it is true
	// (`gui/ui/ui.cpp:47-51`), and `Session::armingChanged` re-arms the play flash and the OLED
	// dirty bit through it (`playback/mode/session.cpp:1290-1305`, the test at `:1295`). The shell's
	// question of the same shape is `Scene::occludes_grid` -- "does this scene cover the grid
	// completely, so scenes beneath need not render?" (`shell/scene.h:338-339`) -- so this is its
	// negation.
	//
	// It lands where legacy lands for the first tenant: `SoundEditor::canSeeViewUnderneath` is true
	// (`gui/ui/sound_editor.h:121`) and `MenuScene::occludes_grid` is false
	// (`scenes/menu/menu_scene.h:443-444`). Mapping it to `oled_transparent(Main)` would have made
	// it false and frozen the clip view's playhead under an open menu.
	return !shell_.stack().top().occludes_grid();
}

bool ShellHostUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	// `getUIGreyoutColsAndRows` walks the open UIs top down and takes the first one that returns
	// true (`gui/ui/ui.cpp:73-83`); false means "obey the UI under me" (`gui/ui/ui.h:136-138`).
	//
	// The port spells `doGreyout == false` as an **all-zero mask**, deliberately and at the code:
	// `MenuScene::greyout_mask` returns `{}` for the arms where
	// `SoundEditor::getGreyoutColsAndRows` sets `doGreyout = false` (`scenes/menu/menu_scene.cpp:1458-1490`,
	// against `gui/ui/sound_editor.cpp:214-256`). Every arm of legacy's switch that leaves
	// `doGreyout` true also writes a non-zero `*cols` (`:232`, `:237`, `:245`, `:252`), so
	// "non-zero mask" and "doGreyout" are the same predicate on the shipped tree, and this is the
	// one place that equivalence is turned back into legacy's two-out-parameters-plus-bool shape.
	const deluge::ui::grid::GreyoutMask mask = shell_.stack().top().greyout_mask();
	if (mask.cols == 0 && mask.rows == 0) {
		return false;
	}
	// Written only on the true path. Legacy's callers pass in accumulators they have already
	// zeroed (`gui/ui/ui.cpp:74-75`) and a UI that returns false must leave them alone, because the
	// next UI down is about to be asked the same question with the same two pointers.
	*cols = mask.cols;
	*rows = mask.rows;
	return true;
}

bool ShellHostUI::opaque() {
	return !shell_.stack().top().is_grid_overlay();
}

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------

void ShellHostUI::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	// **This exists because legacy clears the panel first.** `doAnyPendingOLEDRendering` calls
	// `OLED::clearMainImage()` and then asks each open UI to draw into `OLED::main`
	// (`gui/ui/ui.cpp:351-371`: the clear at `:358`, the `renderOLED` call at `:362`). The shell is
	// retained-mode -- `Shell::flush` blits a surface only when a repaint raised its `blit_pending`
	// (`shell/shell.cpp:884-1013`) -- so a frame legacy zeroed between two shell renders would stay
	// zeroed with nothing to tell the shell to paint it again. The guest keeping its *own* copy of
	// the frame (F1 task 4's copy-not-alias decision) is what makes that recoverable: this hook
	// hands that copy back every time legacy asks.
	//
	// It is the same bytes `GuestSurfaceSet::blit` writes, so the two paths are idempotent with
	// each other rather than in competition; `blit` additionally raises `OLED::markChanged`, which
	// this must not, because legacy has already decided to send this frame by the time it calls
	// here (`:365`'s "Don't need to mark dirty because clearMainImage has already done that").
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
		// (`hid/display/oled_canvas/canvas.h:47-48`) and therefore the same 768 bytes on every
		// instance that exists. No board this suite can build reaches here, and deleting this line
		// is a mutation nothing kills -- recorded as F1 task 5's one surviving mutation rather than
		// dressed up. It stays because the alternative failure is a `std::copy` past the end of
		// legacy's buffer, not a wrong pixel.
		return;
	}
	std::copy(src.begin(), src.end(), dest.begin());
}

bool ShellHostUI::serveGrid(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth], int32_t x0, int32_t x1) {
	const bool isOpaque = opaque();
	// The query form. `UI`'s own contract: "These *must* check whether image has been supplied - if
	// not, just return, saying whether opaque or not" (`gui/ui/ui.h:140-142`). Legacy asks it that
	// way in two places -- `uiNeedsRendering`'s walk down the stack (`gui/ui/ui.cpp:284-302`, the
	// calls at `:292` and `:295`) and `closeUI`'s "does anything need redrawing after this close"
	// pass (`gui/ui/ui.cpp:177-192`, at `:186-187`).
	if (image == nullptr) {
		return isOpaque;
	}

	const deluge::gfx::grid::PadGrid& grid = surfaces_.grid().grid();
	const deluge::gfx::grid::GridDimensions dims = grid.dims();
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
	// `occupancyMask` is deliberately untouched, and this is the same gap F1 task 4 recorded for
	// `GuestSurfaceSet::send_columns`: nothing in the shell populates an occupancy map, and
	// inventing one here would be the guest making up a fact about legacy's buffer.
	return isOpaque;
}

bool ShellHostUI::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	(void)occupancyMask;
	// `drawUndefinedArea` is a timeline concept -- how far past a clip's end to paint -- and the
	// shell has no notion of one. `SoundEditor` ignores it by not overriding this hook at all.
	(void)drawUndefinedArea;
	return serveGrid(whichRows, image, 0, kDisplayWidth);
}

bool ShellHostUI::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	(void)occupancyMask;
	return serveGrid(whichRows, image, kDisplayWidth, kDisplayWidth + kSideBarWidth);
}

// ---------------------------------------------------------------------------------------------
// Leaving
// ---------------------------------------------------------------------------------------------

ActionResult ShellHostUI::exitUI() {
	// **What this hook is in legacy.** It is the long-press-Back exit, and nothing else: `Buttons`
	// arms `TimerName::BACK_MENU_EXIT` for `LONG_PRESS_DURATION` when Back goes down and unsets it
	// on release (`hid/buttons.cpp:109-121`), and the timer manager is the *only* caller
	// (`gui/ui_timer_manager.cpp:149-155`). `SoundEditor::exitUI` is `exitCompletely()`
	// (`gui/ui/sound_editor.h:130`), which leaves the whole menu however deep it was, not one
	// level of it (`gui/ui/sound_editor.cpp:693-734`).
	//
	// **The brief's stated mechanism does not exist, and this is the correction.** F1's task-5
	// brief and spec F §2.1 both say this "returns `NOT_DEALT_WITH` when the stack is empty so
	// `closeUI` pops the guest". No caller reads that: `ui_timer_manager.cpp:150-153` stores the
	// result and tests it against `REMIND_ME_OUTSIDE_CARD_ROUTINE` alone, and there is no `closeUI`
	// anywhere in that path. I grepped every reference to `exitUI` under `src/` -- the only call
	// sites are that one, plus `RenameUI` and `SampleMarkerEditor` calling their own. Every legacy
	// implementation closes *itself*: `RenameUI::exitUI` is `close(); return
	// ACTIONED_AND_CAUSED_CHANGE;` (`gui/ui/rename/rename_ui.cpp:88-91`),
	// `SampleMarkerEditor::exitUI` is the same two lines
	// (`gui/ui/sample_marker_editor.cpp:680-683`), and `exitCompletely` calls `close()` at
	// `gui/ui/sound_editor.cpp:723`. A guest that only returned a value would trap the user inside
	// itself, because nothing on the other end acts on it.
	//
	// So both directions, spelled out:
	//
	//  - **Shell stack still occupied after the pop** -- something inside the guest took the exit.
	//    `DEALT_WITH`, and the guest stays open. Returning `NOT_DEALT_WITH` here would be a lie
	//    about a press that was consumed, and closing here would pop the guest a level early.
	//  - **Shell stack empty after the pop** -- the guest has nothing left to show and is itself
	//    what Back leaves. `close()` (which is `closeUI(this)`, `gui/ui/ui.cpp:57-59`), then
	//    `NOT_DEALT_WITH`: the value the brief asks for, and honest, since after `close()` this UI
	//    is no longer on the hierarchy and dealt with nothing that outlives it.
	if (shell_.stack().depth() != 0) {
		// `pop()` with no argument is "just the top" (`shell/scene_stack.h:69-73`).
		shell_.stack().pop();
	}
	if (shell_.stack().depth() != 0) {
		return ActionResult::DEALT_WITH;
	}
	close();
	return ActionResult::NOT_DEALT_WITH;
}

} // namespace deluge::gui::ui

deluge::gui::ui::ShellHostUI shellHostUI{deluge::scenes::scene_registry()};
