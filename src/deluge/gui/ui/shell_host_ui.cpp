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

#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui_timer_manager.h"
#include "hid/button.h"
#include "hid/led/indicator_leds.h"
#include "libdeluge/scheduler.h" // isSDRoutineActive(), getSystemTime()
#include "model/settings/runtime_feature_settings.h"
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
	// `Services{}` and a default `ShellConfig` on purpose, and both stay that way.
	//
	// The three real services each take this shell's own `SceneStack&`, so they cannot exist before
	// it. F1 task 8 builds them over `shell()` and installs them through `Shell::set_services`
	// (`shell/shell.h`), which is the whole reason that setter exists -- from
	// `wireShellGuestServices()`, called at boot and from `opened()` below. This object is a
	// static-init global, so the constructor is not a place a real service (or a real setting) can
	// be reached from at all.
	//
	// **The default `ShellConfig` is a placeholder, and the two fields that are settings are seated
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
	// `SoundEditor::opened` calling `beginScreen()` (`gui/ui/sound_editor.cpp:297-311`), which
	// begins a menu session rather than resuming one.
	shell_.stack().reset();

	// **Seed the sticky-shift setting from flash, every time the guest opens.**
	//
	// The shell has its own shift state machine -- there is no seam to read legacy's through, see
	// the class's `@par Shift` -- and `InputLowering::toggle_shift` is a faithful port of
	// `Buttons::commandToggleShift` (`shell/input_lowering.cpp:306-326` against
	// `hid/buttons.cpp:237-278`) *except* that legacy reads the runtime feature at release time
	// (`hid/buttons.cpp:250`) while the shell reads a `LoweringConfig` field that defaults to false
	// (`shell/input_lowering.h:29-31`). Left unseeded, a sticky-shift user's latched shift would be
	// invisible to `ctx.shift`, which is the whole gate on the menu's shortcut-pad jump
	// (`scenes/menu/menu_scene.cpp:1631`) -- so shortcut pads would silently stop working under the
	// toggle.
	//
	// `opened()` is the right place and the setter is the right tool: `set_shift_is_sticky` exists
	// precisely because "the settings menu toggles it live" (`shell/input_lowering.h:29-30`,
	// `shell/input_lowering.cpp:334-341`), and this function -- unlike the constructor, which runs
	// at static-init before flash is read -- runs long after `readSettingsFromFile`. The live half
	// is mirrored in `ShiftIsSticky::writeCurrentValue`
	// (`gui/menu_item/runtime_feature/shift_is_sticky.cpp:23-49`), which already had to pair with
	// `Buttons::clearShiftSticky()` for exactly the same reason; this seed is what covers a guest
	// opened after a setting changed while it was shut.
	shell_.lowering().set_shift_is_sticky(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky)
	                                      == RuntimeFeatureStateToggle::On);

	// **No `Shell::boot()` here, and its absence is deliberate.** The first version of this function
	// called it, on the reasoning that the panels want a repaint on the way in. They do, and they
	// get one without it: `SceneStack::reset` sets `changed_` unconditionally
	// (`shell/scene_stack.cpp:201-202`) and a successful `push` sets it too (`:39`), and
	// `Shell::tick` reacts to `changed_since_last_query()` by requesting a repaint on **every** OLED
	// surface, the grid and the widgets gate before it renders anything
	// (`shell/shell.cpp:346-358`) -- which is the same three things `boot()` does
	// (`shell/shell.cpp:230-239`). So the call was dead, and the F1 task 5 review round caught it
	// the only way dead code gets caught here: a mutation that deleted it survived. Deleted rather
	// than left in with a comment, because a redundant repaint request reads as a load-bearing one
	// to the next person. `displayOrLanguageChanged` still calls `boot()`, where there is no stack
	// change to ride on and the mutation dropping it is killed.

	const size_t before = shell_.stack().depth();
	shell_.init(initial_);
	// `Shell::init` is `stack_.push(initial)` with the result dropped (`shell/shell.cpp:226-228`),
	// and `SceneStack::push` refuses an unregistered kind (`shell/scene_stack.h:61-67`) -- which is
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
	// a refused open is rolled back to the previous UI (`gui/ui/ui.cpp:237-242`), so there is no
	// guest to serve and re-seating slots on a shell nothing is about to tick is work with no
	// reader. The two lowering seeds above are deliberately not moved with it -- they are pure
	// writes to this object's own state and reading flash twice is cheaper than the reader having
	// to check where the boundary is.
	wireShellGuestServices();

	// **The first tick, and the only reason anything inside the guest ever runs.** Nothing above
	// armed a deadline: `Shell::init` is a bare `push` and `Shell::boot` calls `request_repaint()`,
	// which fires a gate without giving it a deadline (`shell/shell.cpp:230-239`), so
	// `Shell::next_wake()` is still `std::nullopt` here and there is nothing to convert. See
	// `armTickSoon`.
	//
	// **Only on the success path**, and that matters rather than being tidy: `openUI` unsets
	// `TimerName::UI_SPECIFIC` *before* calling this (`gui/ui/ui.cpp:233`, the call at `:235`) and
	// on a refusal rolls the hierarchy back to the previous UI (`:237-242`). A timer armed by a
	// refused open would therefore deliver this guest's `timerCallback` to whichever UI is current
	// instead -- `UITimerManager::routine` dispatches to `getCurrentUI()`, not to the arming object
	// (`gui/ui_timer_manager.cpp:143`).

	armTickSoon();
	return true;
}

void ShellHostUI::focusRegained() {
	// Legacy calls this on the UI that is the current one again after something above it closed
	// (`closeUI`, `gui/ui/ui.cpp:194-199`), and `Scene::on_resume` is the port of exactly that
	// call, in as many words (`shell/scene.h:359-374`).
	//
	// **`input_top()`, not `top()`**, and this is a deliberate departure from the brief's wording
	// ("`on_resume` on the shell's top"). `on_resume`'s own contract is that it fires on the scene
	// *input reaches* again, which is why `SceneStack` fires it on `input_top()` and never on a
	// passive layer (`shell/scene_stack.h:38-46`). Handing it to `top()` would give the resume to a
	// popup or a console line that never took input in the first place -- the §15.7 defect the
	// stack was fixed for -- and would skip the scene that genuinely gets input back.
	shell_.stack().input_top().on_resume();

	// **And re-arm, because legacy just disarmed us.** Both routes into this hook clear
	// `TimerName::UI_SPECIFIC` on the line before they call it -- `closeUI` at `gui/ui/ui.cpp:197`
	// with the call at `:199`, and `openUI`'s refusal rollback at `:233`/`:240`. Without this the
	// guest would tick normally until any UI was opened over it and then never again: frozen pads,
	// a frozen panel, and input piling up in a queue only `Shell::tick` drains.
	armTickSoon();
}

void ShellHostUI::displayOrLanguageChanged() {
	// The shell is retained-mode: a surface is re-rendered only when a gate asks for it
	// (`shell/shell.cpp:230-239` is the one public way to ask for all of them). A language change
	// invalidates every string on screen without changing anything the shell watches, so without
	// this the guest would keep serving the frame it last rendered until something else happened.
	// `SoundEditor` overrides this hook too, for the same reason
	// (`gui/ui/sound_editor.cpp:336-338`, `readValueAgain`).
	//
	// **What this does not buy.** The repaint lands on the shell's next tick, whereas the base's
	// `renderUIsForOled()` makes legacy pull `renderOLED` before that -- so exactly one frame of the
	// old language can still go out. Chaining to the base keeps that pull, because the alternative
	// is legacy never being told the panel is stale at all.
	shell_.boot();
	// `boot()` requests repaints without arming any deadline (`shell/shell.cpp:230-239`), so
	// `compute_next_wake` has nothing to report and the "next tick" above would be whatever tick
	// happened to come along. On an idle guest that is never. Hence the explicit ask.
	armTickSoon();
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
	//    `MenuScene::oled_transparent` returns false (`scenes/menu/menu_scene.h:480-481`) -- those
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
	// completely, so scenes beneath need not render?" (`shell/scene.h:386-387`) -- so this is its
	// negation.
	//
	// It lands where legacy lands for the first tenant: `SoundEditor::canSeeViewUnderneath` is true
	// (`gui/ui/sound_editor.h:121`) and `MenuScene::occludes_grid` is false
	// (`scenes/menu/menu_scene.h:475-476`). Mapping it to `oled_transparent(Main)` would have made
	// it false and frozen the clip view's playhead under an open menu.
	return !shell_.stack().top().occludes_grid();
}

bool ShellHostUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	// `getUIGreyoutColsAndRows` walks the open UIs top down and takes the first one that returns
	// true (`gui/ui/ui.cpp:73-83`); false means "obey the UI under me" (`gui/ui/ui.h:136-138`).
	//
	// The port spells `doGreyout == false` as an **all-zero mask**, deliberately and at the code:
	// `MenuScene::greyout_mask` returns `{}` for the arms where
	// `SoundEditor::getGreyoutColsAndRows` sets `doGreyout = false` (`scenes/menu/menu_scene.cpp:1697-1729`,
	// against `gui/ui/sound_editor.cpp:252-294`). Every arm of legacy's switch that leaves
	// `doGreyout` true also writes a non-zero `*cols` (`:232`, `:237`, `:245`, `:252`), so
	// "non-zero mask" and "doGreyout" are the same predicate on the shipped tree, and this is the
	// one place that equivalence is turned back into legacy's two-out-parameters-plus-bool shape.
	//
	// @warning **KNOWN DEFECT, in this hook: under the guest the greyout is applied TWICE.** Legacy
	//          greys exactly once, in the *send* path -- `prepareColour`
	//          (`hid/led/pad_leds.cpp:283-298`, the tint at `:293-296`) reads the module-level
	//          `greyoutCols`/`greyoutRows` that `reassessGreyout` fills from this very function
	//          (`hid/led/pad_leds.cpp:734-735`, via `getUIGreyoutColsAndRows`), and never mutates
	//          `PadLEDs::image`. The shell has no send stage to hook, so it greys its own grid in
	//          place instead: `Shell::render_grid` runs `ui::grid::greyout` over `greyout_`
	//          (`shell/shell.cpp:789-795`), the OR of every stacked scene's mask (`:346-352`). Those
	//          already-greyed colours then reach the pads through `GuestSurfaceSet::send_columns`,
	//          which writes `PadLEDs::image` and calls `sortLedsForCol` -- i.e. straight back through
	//          `prepareColour`, which greys them a second time for whatever this function reported.
	//          A pad the menu greys is therefore darker under the guest than under `SoundEditor`.
	//
	//          Not fixed here, and the alternative was considered rather than skipped: answering
	//          `false` unconditionally would stop the double tint but would hand the greyout decision
	//          to the UI *underneath* (`gui/ui/ui.cpp:73-83` takes the first UI that says true),
	//          which is a different wrong answer -- the clip view would grey by its own rules while
	//          the menu is up. The real fixes are a `Shell` seam that separates "compose" from
	//          "tint", or the guest reporting an empty mask while the shell owns the tint end to
	//          end; both are shell changes and belong with the tick (F1 task 7) or the acceptance
	//          round, not with this hook. It is a brightness difference, not a wrong pad.
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
	// (`shell/shell.cpp:920-1049`) -- so a frame legacy zeroed between two shell renders would stay
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
	// **Both clamps are load-bearing, and neither is covered by `canOpen()`.**
	// `GuestSurfaceSet::canReachMainPanel` asks about the *panel's* shape and says nothing about the
	// grid (`gui/ui/shell_guest_surfaces.h`), and `SurfaceSet::build` sizes the grid straight from
	// `DelugeBoard::pad_grid_width`/`pad_grid_height` and `BoardProfile::sidebar_cols`
	// (`shell/surfaces.cpp:26-28`) with no reference to legacy's numbers at all -- a `PadGrid` may be
	// up to `kMaxGridRows` x `kMaxGridCols` = 16 x 32 (`gfx/grid/pad_grid.h:30-32`) while
	// `PadLEDs::image` is fixed at `[kDisplayHeight][kDisplayWidth + kSideBarWidth]` = 8 x 18
	// (`hid/led/pad_leds.h:33`). So a profile with a taller or wider grid than legacy's is the case
	// these two `min`s exist for, and dropping either writes past the end of legacy's buffer rather
	// than merely painting a wrong pad. Both are mutation-covered by the tall/wide-grid board in
	// `tests/spec/gui/shell_host_ui_spec.cpp` (F1 task 5 review round, Important 1: both survived
	// the first pass because no example varied grid dimensions at all).
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
	// **Two things about this buffer that are true and are not defects here, recorded because the
	// next reader of this function is the one who needs them** (F1 task 5 review round).
	//
	//  1. **Two paths now write `PadLEDs::image`.** `GuestSurfaceSet::send_columns` writes it and
	//     calls `sortLedsForCol` on the shell's own flush; this writes it again when legacy pulls.
	//     They agree -- same grid, same rows -- but when the guest answers transparent, legacy keeps
	//     walking down and the UI underneath overwrites these rows
	//     (`doAnyPendingGridRendering`, `gui/ui/ui.cpp:305-349`, the `usedUp` test at `:329-337`).
	//     This used to end "For the first tenant that is exactly right: `SoundEditor` renders no
	//     pads at all", and **F1 task 11's A/B falsified the conclusion, not the premise.**
	//     `SoundEditor` does render no pads -- it overrides neither hook -- but it does not BLANK
	//     them either: it overrides `getGreyoutColsAndRows` (`gui/ui/sound_editor.cpp:252-294`)
	//     and legacy keeps painting the view underneath, dimmed. Measured on `menu.txt`: with the
	//     toggle off the clip view's sidebar column is still lit under the open menu, greyed
	//     (`pad 16 0 3 160 7` against boot's `0 255 6`); with it on there is no `pad` line at all.
	//     **Under the guest the sidebar is wrong for TWO independent reasons, and fixing either
	//     alone still leaves it wrong.** Opacity is not one of them: `MenuScene::is_grid_overlay()`
	//     is true (`scenes/menu/menu_scene.h:474`), so `opaque()` is false and this function
	//     answers transparent exactly as the paragraph above describes.
	//       (a) **The shell's own flush paints black.** `GuestSurfaceSet::send_columns`
	//           (`gui/ui/shell_guest_surfaces.cpp:137-173`, the writes at `:169-170`) writes
	//           `PadLEDs::image` and calls
	//           `sortLedsForCol` itself, so the menu's all-black grid reaches the hardware without
	//           passing through legacy's walk at all.
	//       (b) **Legacy's walk never runs while the guest is open.** Nothing in this class calls
	//           `uiNeedsRendering` (`grep` it: this file names it only in a comment), so
	//           `doAnyPendingGridRendering` has nothing pending and the UI underneath is never
	//           asked to repaint; `PadLEDs::reassessGreyout` likewise runs only from `openUI` /
	//           `closeUI` (`gui/ui/ui.cpp:93`, `:99`, `:115`, `:126`, `:198`, `:234`, `:239`),
	//           i.e. at transitions, not during. Measured by the F1 task 11 reviewer, by disabling
	//           (a)'s write and re-running the ON leg: the sidebar came back at boot's **ungreyed**
	//           `0 255 6`, not the OFF leg's greyed `3 160 7`. So (a) alone is a stale undimmed
	//           sidebar -- still divergent, differently.
	//     Recorded as a divergence in F1 task 11's report rather than fixed here: (a) is in the
	//     flush path (F1 task 4/5) and (b) is a repaint-request the guest never makes; neither is
	//     this hook's, and fixing one without the other is not an improvement.
	//  2. **Legacy's menu blink is a hardware flash; the port's is a framebuffer write.**
	//     `SoundEditor`'s shortcut blink calls `PadLEDs::flashMainPad`
	//     (`gui/ui/sound_editor.cpp:997-1040`, the calls at `:1021` and `:1032`), which goes straight
	//     to `deluge_control_flash_pad` (`hid/led/pad_leds.h:128-135`) and never touches `image`, so
	//     a root-view repaint cannot wipe it. `MenuScene` paints its blinks into the shell's grid,
	//     which reaches the pads through `image` -- so a root-view repaint *can* erase one until the
	//     shell's next flush re-sends it. Whether that is visible is a question about the tick's
	//     cadence, i.e. F1 task 7's.
	//
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
// The tick
//
// The whole of the guest's clock, in three functions. `timerCallback` is the only place
// `Shell::tick` is called from, so it is the only place any of the shell runs at all -- routing,
// rendering, LEDs and the wake computation are all steps of that one function
// (`shell/shell.cpp:315-538`). Everything else in this file either feeds a queue or answers a
// legacy query.
// ---------------------------------------------------------------------------------------------

ActionResult ShellHostUI::timerCallback() {
	const uint32_t now = nowMs();

	// **Before the tick, not after**, and it is the same ordering every input hook below uses:
	// `InputLowering::set_sd_busy(false)` replays the deferred queue into the ordinary one
	// (`shell/input_lowering.cpp:134-156`), and `Shell::tick`'s step 3 is what drains that
	// (`shell/shell.cpp:329`). Released after the tick, a press would wait a whole extra tick.
	//
	// This line is what makes the SD release *time*-driven. F1 task 6 wired `set_sd_busy` into the
	// five input hooks only, and recorded the gap that leaves: legacy retries from the poll loop
	// the moment `isSDRoutineActive()` goes false (`deluge.cpp:297-307`), whereas a guest that only
	// asks on input strands a deferred press until the user touches something else. `rearmTimer`
	// below is the other half -- it keeps the timer armed for exactly as long as the lock is held.
	shell_.set_sd_busy(isSDRoutineActive());

	shell_.tick(now);

	// **The action drain, and both halves of where it sits are load-bearing.**
	//
	// `MenuScene::take_action()` is a one-slot mailbox with a silent overwrite
	// (`scenes/menu/menu_scene.h:612-640`), and it announces nothing: "`after_render` returns no
	// `Action` announcing that one is waiting, so nothing enforces the polling: **poll after every
	// tick, or requests are dropped or stale**" (`:615-617`). This line is the polling for the
	// **menu's** outbox. `dispatchShellActions` performs whatever it finds.
	//
	// (This used to say "and it is the only one in the firmware". F3 task 1 made that false:
	// `dispatchShellChoices()` below is the same poll for the *browser's* own outbox, which is a
	// second mailbox with a second producer. Corrected here rather than left standing, per the plan.)
	//
	// **After the tick**, because a request is filed by `MenuScene::after_render`, and `after_render`
	// is what `Shell::tick` runs as its step 7 (`shell/shell.cpp:399-407`) -- a poll above
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

	// **The choice drain -- the noun half of the same wall, and it sits in the same two places for
	// the same two reasons.**
	//
	// `BrowserScene::take_choice()` (`scenes/browser/browser_scene.h:381`) is the menu outbox's twin:
	// one slot, filled by `Descriptor::accept`'s `Defer` answer, announcing nothing. **After the
	// tick**, because the choice is filed while `Shell::tick` routes the press
	// (`scenes/browser/browser_scene.cpp:895-897`, reached from step 3's `pump_input`,
	// `shell/shell.cpp:329`, or step 8's held-back delivery, `:409`). **Above the close check**, so
	// no path through this function can skip it. **Unconditional**, because asking is what takes it.
	//
	// **After `dispatchShellActions()`, and that order is written down rather than incidental.** Both
	// mailboxes can hold something on the same tick: `Defer` closes the browser in the same statement
	// that parks the choice, so the ported menu underneath is frontmost again by steps 5-6 and gets
	// its own `after_render()` at step 7 (`shell/shell.cpp:399-407`) -- on the very tick the choice
	// was filed. Neither drain reads the other's mailbox, so today nothing observable turns on the
	// order; it is fixed this way for the interaction F3 tasks 2 and 3 bring, which
	// `shell_choice_dispatch.h` states in full.
	//
	// **By the time this runs the browser is already off the shell's stack** -- `Defer` parks *and*
	// closes -- so there is no "drain while the browser is still open" state for this line to be in.
	dispatchShellChoices();

	// **The short-Back exit, which is the only exit a user actually performs.**
	//
	// `exitUI()` below is the LONG press: `Buttons` arms `TimerName::BACK_MENU_EXIT` for
	// `LONG_PRESS_DURATION` on Back down (`hid/buttons.cpp:114-121`) and the manager calls `exitUI`
	// from that arm (`gui/ui_timer_manager.cpp:149-155`). A short Back never reaches it -- it is fed
	// into the shell like any other button (`buttonAction` below) and routed inside `Shell::tick`,
	// where `MenuScene` answers Back at depth 0 with `Action::Kind::Remove{.pop_to = FirstApp}`
	// (`scenes/menu/menu_scene.cpp:1684-1689`), the port of `sound_editor.cpp:456`'s BACK case ->
	// `:471` `goUpOneLevel()` -> `:710-717` `SoundEditor::goUpOneLevel`, whose depth-0 arm is
	// `exitCompletely()` at `:716`. (`MenuScene`'s own comment on that block cites `:433` and
	// `:678` for those two; both are stale -- `:433` is an `AFFECT_ENTIRE` blink and `:678` is
	// `handlePotentialParamMenuChange`. Reported in F1 task 11's report, not corrected here: they
	// are pre-existing and belong to the sweep task, and a citation moved by the wrong hand is how
	// this plan has lost seven of them.) That removes the scene and empties the
	// stack, and **nothing in legacy closes this UI when it happens**: `close()` lived only on
	// `exitUI`'s path.
	//
	// What that left is precisely the state `opened()` refuses to create -- "An empty guest is worse
	// than no guest: it would swallow every pad while drawing nothing" (`:135-137`). Measured on the
	// F1 task 11 A/B: with the toggle on, Back from the ported menu's root left `shellHostUI`
	// current with an empty stack, `renderOLED` painting nothing over legacy's cleared frame, and
	// every later button (Session, Select) swallowed. The toggle-off leg of the same script exited
	// to the clip view. So the close belongs on both paths, and this is the one that carries the
	// short press.
	//
	// **After the tick, not before**: the tick is what routes the Back and applies the `Remove`
	// (`shell/shell.cpp:315-538`), so the stack cannot be seen empty until it has run. And **before
	// `rearmTimer`**, which is skipped entirely on this path for the same reason `exitUI`'s close
	// arms nothing: `closeUI` unsets `TimerName::UI_SPECIFIC` itself (`gui/ui/ui.cpp:197`) before
	// handing focus to the UI beneath, so a re-arm here would either be undone one line later or
	// deliver this guest's `timerCallback` to that UI (`gui/ui_timer_manager.cpp:143`).
	if (shell_.stack().depth() == 0) {
		closeGuest();
		return ActionResult::DEALT_WITH;
	}

	// Unconditional here, but **not unconditional inside**: `rearmTimer` refuses when this guest is
	// no longer the current UI, which is exactly what the drain above leaves behind when an arm ends
	// in `openUI(...)`. The whole argument, including why the guard is in the callee, is at
	// `rearmTimer` itself.
	rearmTimer(now);

	// **Never `REMIND_ME_OUTSIDE_CARD_ROUTINE`.** That is the one value the `UI_SPECIFIC` arm acts
	// on, and what it does is set `timer.active = true` again *without touching `triggerTime`*
	// (`gui/ui_timer_manager.cpp:144-146`) -- which is already in the past, so the timer re-fires on
	// the very next pass of the manager, for ever. It is legacy's "come back immediately" idiom and
	// it is precisely the spin `kMinRearmMs` exists to avoid; the guest has no need of it either,
	// because it defers rather than refuses (see the Input section below). `DEALT_WITH` is inert
	// here: nothing in that arm reads any other value.
	return ActionResult::DEALT_WITH;
}

void ShellHostUI::rearmTimer(uint32_t now) {
	// **Only while this guest is the current UI -- which, since the action drain landed, is no
	// longer a given.**
	//
	// `TimerName::UI_SPECIFIC` is not this object's timer. It is *the current UI's* timer:
	// `UITimerManager::routine` dispatches it to `getCurrentUI()->timerCallback()`, never to
	// whatever armed it (`gui/ui_timer_manager.cpp:142-148`). Arming it while something else is
	// current does not delay this guest's tick, it **delivers this guest's tick to a stranger**.
	//
	// This branch already says so three times, and enforced it nowhere until now. `opened()` says it
	// for a refused open (`:165-170`), `timerCallback`'s close check says it for the short-Back exit
	// (`:547-552`), and `closePortedMenu` says it to rule out `closeGuest()` as an implementation
	// (`gui/ui/shell_action_dispatch.cpp:153-163`). The path none of the three covers is the one F2
	// tasks 2 and 4 created between them: **three of the seven `MenuAction` arms end in
	// `openUI(...)`** -- `grep -n "openUI(" gui/ui/shell_action_dispatch.cpp` gives exactly three
	// (`:115` in `openPerformanceView`, `:358` and `:368` for the two context menus), against eight
	// `case` labels; tasks 6 and 7's `SetShortcutsVersion` and `ScreensaverSettingsChanged` open
	// nothing. And legacy's own `NO_NAVIGATION` semantics mean the ported menu correctly stays on the
	// shell stack when they do. So `timerCallback`'s `depth() == 0` close does not fire, control
	// reaches the `rearmTimer` below, and the UI that has just been opened over the guest gets the
	// timer.
	//
	// **The guard is on `getCurrentUI()`, not on that list**, so it does not go stale when the list
	// does -- which matters, because F3 adds the `take_choice` dispatcher to the same drain.
	//
	// **What that costs, bounded honestly.** The two context menus inherit `UI::timerCallback`'s
	// `return DEALT_WITH` (`gui/ui/ui.h:109`) and no `gui/context_menu/` class overrides it, so for
	// those two it is inert. `PerformanceView::timerCallback` is not inert: it forwards into
	// `sessionView.timerCallback()` or `arrangerView.timerCallback()`
	// (`gui/views/performance_view.cpp:383-391`). Toggle-ON only, and not a wedge -- but a
	// `timerCallback` legacy never delivers.
	//
	// **Returning early cannot strand the guest, and that is by construction rather than by luck.**
	// When the UI opened above it closes, `closeUI` unsets `UI_SPECIFIC` (`gui/ui/ui.cpp:197`) and
	// calls `newUI->focusRegained()` (`:199`) -- which for this guest is an **unconditional**
	// `armTickSoon()` (`:189-194`), written for exactly this shape of disarm. So the guest sleeps
	// while it is covered and re-arms the instant it is uncovered.
	//
	// The guard is here rather than at the one call site because arming a timer that will be
	// delivered elsewhere violates *this function's* contract, not its caller's -- and because the
	// early-return branch below arms a second time for the SD poll (`:637`), which a call-site guard
	// would leave uncovered on the day someone adds a third. `armTickSoon` is deliberately NOT
	// guarded: its ten call sites are the input hooks, `opened()`, `displayOrLanguageChanged()` and
	// `exitUI()`, on which the guest is current by construction -- and `focusRegained`'s is the one
	// arm that MUST fire while this guard is suppressing, so guarding it too would be the wedge this
	// one avoids.
	if (getCurrentUI() != this) {
		return;
	}

	const std::optional<uint32_t> deadline = shell_.next_wake();
	if (!deadline.has_value()) {
		// Nothing armed anywhere in the shell -- no gate deadline, no scene or service timer, no
		// declared blink (`shell/shell.cpp:1051-1119`). Legacy has already cleared the timer
		// (`gui/ui_timer_manager.cpp:66`), so leaving it alone *is* "sleep until the next input",
		// and every path that can wake the guest re-arms it (`armTickSoon`).
		//
		// Except one, which no scene can express as a deadline: a press held back by the SD-routine
		// lock is waiting on a fact outside the shell entirely. Poll for it. At `kMinRearmMs` that
		// is slower than legacy's own retry, which runs from `readButtonsAndPads` on every pass of
		// the main loop (`deluge.cpp:297-307`), so it is not a new cost -- and it only runs while
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

void ShellHostUI::armTickSoon() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kMinRearmMs);
}

// ---------------------------------------------------------------------------------------------
// Input
//
// Every hook here does the same three things in the same order: take or release the shell's
// SD-routine lock, feed the event, and return the value legacy's own dispatcher wants. Nothing
// asks the shell what happened, because nothing can -- routing runs in `Shell::pump_input`, which
// only `Shell::tick` calls (`shell/shell.cpp:579-589`).
//
// **`inCardRoutine` is not a shell concept, and this is where it stops being one.** Legacy's
// answer is `ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE`: `SoundEditor::padAction` opens with it
// (`gui/ui/sound_editor.cpp:1460-1463`) and `dispatchInputEvent` stashes the offending event in
// `heldInputEvent`, sets `waitingForSDRoutineToEnd`, and re-dispatches it at the top of the next
// `readButtonsAndPads` once the routine has ended (`deluge.cpp:237-281`, the stash at `:245-248`
// and `:256-259`, the replay at `:295-305`). One event deep. The port of that is
// `InputLowering::set_sd_busy`, which queues up to `kDeferredCapacity` (8) events and replays them
// in arrival order, each with the clock it arrived at, when the lock is released
// (`shell/input_lowering.cpp:134-156`). So the guest **defers rather than drops**, and it never
// returns `REMIND_ME_OUTSIDE_CARD_ROUTINE`.
//
// **The release is time-driven, as of F1 task 7.** The hooks below still ask on every input, but
// `timerCallback` asks too, and `rearmTimer` keeps the timer armed at `kMinRearmMs` for as long as
// `InputLowering::sd_busy()` holds -- so a deferred press waits for the routine to end rather than
// for the user to touch something else. That closes the divergence task 6 recorded here; what
// remains is only that the poll is at `kMinRearmMs` where legacy's is at the main loop's rate
// (`deluge.cpp:297-307`), i.e. the guest is at worst a millisecond behind.
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
	// (`defaultHoldTime * kSampleRate / 20`, `storage/flash_storage.cpp:733`), which legacy reads
	// *live* at shift-release time (`hid/buttons.cpp:264`) and the shell reads out of a config field
	// that nothing moved. F1 task 6 left it open for want of a setter;
	// `InputLowering::set_hold_time_ms` (`shell/input_lowering.h`) is F1 task 8's, written for this
	// line, and the conversion to milliseconds is `* 1000 / kSampleRate`. Legacy's default 2 gives
	// 4410 samples and 100 ms, exactly what `LoweringConfig`'s own default documents; the settings
	// item's range is 1..20, re-clamped on read (`storage/flash_storage.cpp:730-732`), i.e.
	// 50..1000 ms, so the `uint16_t` cannot overflow.
	//
	// **Here rather than in `opened()`, and the fix round is what settled it.** The first version
	// seeded once per open, on the reasoning that "a guest that is not open cannot be observing the
	// threshold" -- which is backwards for the one writer that can only be reached *while the guest
	// is open*: `nodes::set_default_hold_time` (`scenes/menu/nodes/settings.cpp`) is the ported
	// settings item, and it writes `FlashStorage::holdTime` from inside the shell. A per-open seed
	// lags it by a whole guest session. This is the same hole the screensaver settings have from the
	// ported tree, and unlike that one it is closable here, because the *reader* is on this side of
	// the seam.
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
	// (`shell/input_lowering.cpp:166-168`).
}

ActionResult ShellHostUI::padAction(int32_t x, int32_t y, int32_t velocity) {
	// `isSDRoutineActive()` rather than a parameter, because this hook has none -- which is also
	// why `SoundEditor::padAction` reads it directly (`gui/ui/sound_editor.cpp:1461`).
	shell_.set_sd_busy(isSDRoutineActive());

	// **The range guard is about the narrowing, not about the shell.** `on_pad` already drops a
	// coordinate outside `PadMask`'s 32x32 (`shell/input_lowering.cpp:235-238`), but only after
	// this side has cast to `uint8_t`, and that cast folds: `x = 256` becomes column 0 and the
	// shell is handed a pad press that never happened. `MatrixDriver::padAction` bounds nothing of
	// its own (`hid/matrix/matrix_driver.cpp:65-84`), and this is a public virtual that legacy UIs
	// call on each other directly -- `SoundEditor::padAction` forwards straight into
	// `keyboardScreen.padAction` (`gui/ui/sound_editor.cpp:1474-1477`) -- so the caller's domain is
	// not `MatrixDriver`'s alone.
	//
	// Unlike `buttonAction`'s (see there) this guard is not redundant with `on_pad`'s and is
	// covered by an example that folds a coordinate on purpose.
	if (x >= 0 && y >= 0 && x <= UINT8_MAX && y <= UINT8_MAX) {
		feed(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
		                      static_cast<int16_t>(velocity)});
	}

	// `DEALT_WITH`, the base's own value, and the only other value legacy can act on here is
	// `REMIND_ME_OUTSIDE_CARD_ROUTINE` -- `MatrixDriver::padAction` collapses everything else to
	// `DEALT_WITH` before its caller sees it (`hid/matrix/matrix_driver.cpp:79-83`). So a pad the
	// guest swallows stays swallowed, which is what an open full-screen UI should do, and the
	// deferral above is what replaces the value legacy would have used.
	armTickSoon();
	return ActionResult::DEALT_WITH;
}

ActionResult ShellHostUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	// The parameter, not `isSDRoutineActive()`: it is the caller's own answer, computed once at
	// `deluge.cpp:256` for the event being dispatched, and `Buttons::noPressesHappening` passes its
	// own (`hid/buttons.cpp:315-323`).
	shell_.set_sd_busy(inCardRoutine);

	// **The two gold-knob pushes are deliberately not fed, and F1 task 8 is what made that
	// necessary.** `Buttons::buttonAction` delivers MOD_ENCODER_0/1 **twice**: once here at
	// `hid/buttons.cpp:134`, like any other button, and once through its tail's
	// `getCurrentUI()->modEncoderButtonAction(0, on)` / `(1, on)` (`:226-231`), whose base
	// implementation is a bare forward to `view` (`gui/ui/ui.cpp:43-45`) that this class keeps.
	// `Router::is_mod_or_gold` routes `EncoderButton{ModEnc0}`/`{ModEnc1}` to `mod_knobs` (shell
	// spec §15.5), and `ModKnobs::mod_encoder_button_action` is the port of the very function
	// `view` is about to run (`gui/views/view.cpp:1349-1366`). So with `ModKnobs` installed and its
	// target seeded, feeding the press would perform the gesture **twice** -- two
	// delete-automations, or two `ModControllable::modEncoderButtonAction` toggles -- where
	// `SoundEditor` performs it once. Task 6 recorded the double delivery as inert and left the
	// ordering to this task; it stopped being inert the moment the service got a non-null target.
	//
	// Dropping the feed rather than overriding `modEncoderButtonAction` is the conservative half of
	// that choice: it leaves the gesture running **legacy's own code**, which is parity by
	// construction (global constraint 1), where routing it to the port would make parity depend on
	// the port's fidelity for no tenant that wants it -- `MenuScene` claims neither id
	// (`grep -rn ModEnc src/scenes/menu` is empty). What it costs is one line legacy runs and this
	// path does not: `View::modEncoderButtonAction_changeModControllable`'s value refresh is
	// guarded on `getCurrentUI() == &soundEditor` (`view.cpp:1390-1392`), which is false while the
	// guest is what is open, so the ported menu's displayed value goes stale after a gold-knob
	// push. Recorded as a divergence for F1 task 9/11 rather than papered over here.
	if (b == deluge::hid::button::MOD_ENCODER_0 || b == deluge::hid::button::MOD_ENCODER_1) {
		armTickSoon();
		return ActionResult::NOT_DEALT_WITH;
	}

	// The exact inverse of `deluge.cpp:255`'s `deluge::hid::button::fromXY(ev.x, ev.y)`: a
	// `hid::Button` is `9 * (y + kDisplayHeight * 2) + x` and `toXY` undoes it
	// (`hid/button.h:9-15`, `hid/button.cpp:8-13`). Feeding matrix coordinates rather than a
	// `ButtonId` is deliberate -- `InputLowering::on_matrix` is what owns the matrix-to-`ButtonId`
	// table, through `BoardProfile::button_by_matrix` (`shell/input_lowering.cpp:257-266`), and it
	// is the same table the level-3 harness cross-checks against.
	//
	// **No range guard here, unlike `padAction` above, and the arithmetic is why.** `toXY` returns
	// `int32_t`s, and legacy does have a "fake button" column past the matrix
	// (`buttonStates[NUM_BUTTON_COLS + 1][...]`, `hid/buttons.cpp:73`, reachable through
	// `button::fromChar`, `hid/button.h:57-59` -- which today has no callers in `src/`). But a
	// `Button` is a `uint8_t`, so `toXY` gives `x = b % 9` in 0..8 and `y = b / 9 - 16` in -16..12,
	// and every value outside the real matrix is dropped by `on_matrix` either way:
	// x is always inside `button_cols` (9); a negative y narrows to 240..255 and a y of 4..12 stays
	// itself, and both are at or past `button_rows` (4), which `on_matrix` tests before it indexes
	// (`shell/input_lowering.cpp:258-260`). There is no value of `b` for which the narrowing
	// changes the answer -- **on a profile whose `button_rows` is at most 13**, which is what makes
	// the Deluge's 4 (`shell/profiles/deluge.cpp:144-145`, and the matrix table it indexes at `:12-18`) safe. The proof
	// is board-specific, not universal: a hypothetical profile with 14 or more button rows would let a `y` of 13 index
	// a real cell for a `b` legacy never means as a matrix button. Stated rather than guarded, because no such profile
	// exists and a guard against one would be untestable. So a guard here would be a second copy of a check that
	// already holds -- the same dead-code shape F1 task 5's review deleted from `opened()`. A mutation removing one is
	// unkillable, which is the evidence.
	const Cartesian xy = deluge::hid::button::toXY(b);
	feed(DelugeInputEvent{DELUGE_EVENT_BUTTON, static_cast<uint8_t>(xy.x), static_cast<uint8_t>(xy.y),
	                      static_cast<int16_t>(on ? 255 : 0)});

	// **`NOT_DEALT_WITH`, and it is load-bearing three times over.** It is what `SoundEditor`
	// returns for every button with an observable consequence in `Buttons::buttonAction`'s tail:
	// its `if`/`else if` chain has no arm for Play, Shift, Record, Tempo-enc or either mod-encoder
	// button, so those fall into the final `else` and take `MenuItem::buttonAction`'s default
	// `NOT_DEALT_WITH` (`gui/ui/sound_editor.cpp:633-667`, `gui/menu_item/menu_item.h:78-80`).
	// Returning `DEALT_WITH` here instead would `goto dealtWith` (`hid/buttons.cpp:139-141`) and:
	//
	//   1. **stop `Buttons::commandToggleShift` running** (`hid/buttons.cpp:169-171`), which is the
	//      one thing maintaining legacy's shift state -- see the class's `@par Shift`. The
	//      transport would go with it (`:144-167`, `:173-203`), which `SoundEditor` does not do;
	//   2. **stop the mod-encoder buttons reaching `modEncoderButtonAction`** (`:226-231`), which
	//      is where the gold-knob presses live until F1 task 8;
	//   3. buy nothing, because every button the shell's menu actually claims -- Back, Select-enc,
	//      Save, Load, Learn, Keyboard, Session -- has **no arm in that tail at all** and falls
	//      straight through to `dealtWith:` (`:232-234`). Read the braces rather than the shape:
	//      the chain from `:144` to `:231` names Play, Shift, Record, Tempo-enc, Mod-enc-0 and
	//      Mod-enc-1 and nothing else.
	//
	// **The long-press-Back timer is not in that tail**, which corrects a claim this file used to
	// carry. `Buttons` arms `TimerName::BACK_MENU_EXIT` at `:114-121`, *above* the
	// `getCurrentUI()->buttonAction(...)` call at `:134` and unconditionally on the value it
	// returns. So `exitUI()` keeps being driven whatever this returns.
	armTickSoon();
	return ActionResult::NOT_DEALT_WITH;
}

ActionResult ShellHostUI::horizontalEncoderAction(int32_t offset) {
	// No `inCardRoutine` parameter on this one; `interpretEncoders` does not even call it during
	// the routine (`hid/encoder_input.cpp:96-102`), so the read here is really about *releasing* a
	// lock a button press took.
	shell_.set_sd_busy(isSDRoutineActive());
	feedDetents(EncoderRole::Horizontal, offset);
	// `DEALT_WITH`, the base's value. `interpretEncoders` tests the result for exactly one thing --
	// `REMIND_ME_OUTSIDE_CARD_ROUTINE`, on which it re-arms the encoder and hands the detents back
	// to the accumulator (`hid/encoder_input.cpp:129-132`) -- and the deferral above is what
	// replaces that.
	armTickSoon();
	return ActionResult::DEALT_WITH;
}

ActionResult ShellHostUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	// This one *is* called during the routine: `interpretEncoders` exempts `scrollY` from the
	// skip (`hid/encoder_input.cpp:95-102`, `isScrollY` at `:95`) and passes `skipActioning` down
	// as this parameter (`:140`). So the deferral matters here in a way it does not for horizontal.
	shell_.set_sd_busy(inCardRoutine);
	feedDetents(EncoderRole::Vertical, offset);
	armTickSoon();
	return ActionResult::DEALT_WITH;
}

void ShellHostUI::selectEncoderAction(int8_t offset) {
	// `void`: legacy discards nothing because there is nothing to discard
	// (`gui/ui/ui.h:103`), so unlike the other two this hook has no way to say
	// "not now" even in legacy. The offset arrives already saturated to `int8_t`
	// (`hid/encoder_input.cpp:117`, applied at `:171`), so `feedDetents`'s clamp can only bite on
	// the outer 64 values of that range.
	shell_.set_sd_busy(isSDRoutineActive());
	feedDetents(EncoderRole::Select, offset);
	armTickSoon();
}

// ---------------------------------------------------------------------------------------------
// Leaving
// ---------------------------------------------------------------------------------------------

ActionResult ShellHostUI::exitUI() {
	// **What this hook is in legacy.** Chiefly the long-press-Back exit: `Buttons` arms
	// `TimerName::BACK_MENU_EXIT` for `LONG_PRESS_DURATION` when Back goes down and unsets it on
	// release (`hid/buttons.cpp:114-121`), and the timer manager calls this from that arm
	// (`gui/ui_timer_manager.cpp:149-155`). `SoundEditor::exitUI` is `exitCompletely()`
	// (`gui/ui/sound_editor.h:130`), which leaves the whole menu however deep it was, not one
	// level of it (`gui/ui/sound_editor.cpp:731-772`).
	//
	// **The brief's stated mechanism does not exist, and this is the correction.** F1's task-5
	// brief and spec F §2.1 both say this "returns `NOT_DEALT_WITH` when the stack is empty so
	// `closeUI` pops the guest". Neither caller does that; every legacy implementation closes
	// *itself*. `RenameUI::exitUI` is `close(); return ACTIONED_AND_CAUSED_CHANGE;`
	// (`gui/ui/rename/rename_ui.cpp:88-91`), `SampleMarkerEditor::exitUI` is the same two lines
	// (`gui/ui/sample_marker_editor.cpp:680-683`), `Browser::exitUI` is `exitAction()` and the same
	// value (`gui/ui/browser/browser.h:133-136`), and `exitCompletely` calls `close()` at
	// `gui/ui/sound_editor.cpp:761`. A guest that only returned a value would trap the user inside
	// itself, because nothing on the other end acts on it.
	//
	// @par The two readers of the return value, measured
	//
	// `grep -rn "exitUI" src/ | grep -v exitUIMode` is 26 lines; two of them read what comes back,
	// and they read it for different values:
	//
	//   * `gui/ui_timer_manager.cpp:150-153` stores it and tests only for
	//     `REMIND_ME_OUTSIDE_CARD_ROUTINE`, to re-arm the timer. There is no `closeUI` in that path.
	//   * `gui/ui/keyboard/layout/column_controls.cpp:89` tests for
	//     `ACTIONED_AND_CAUSED_CHANGE`, and on it calls `keyboardScreen.killColumnSwitchKey(LEFT_COL)`
	//     -- the keyboard sidebar's menu-exit pad, behind
	//     `RuntimeFeatureSettingType::EnableKeyboardViewSidebarMenuExit` (`:86-87`).
	//
	// **F1 task 6 changed the close path's value to `ACTIONED_AND_CAUSED_CHANGE`, and corrected the
	// reason task 5 recorded for leaving it alone.** Task 5 returned `NOT_DEALT_WITH` (the brief's
	// value) and wrote that the second caller "stops being inert the moment F1 task 6 routes pads".
	// **That is not the mechanism.** Routing pads into the *shell* does not put a pad anywhere near
	// `KeyboardScreen`: `MatrixDriver::padAction` calls `getCurrentUI()->padAction` and nothing else
	// (`hid/matrix/matrix_driver.cpp:79`), so while the guest is open the only way
	// `ColumnControlsKeyboard::evaluatePads` runs at all is if the current UI hands the pad on --
	// which is precisely what `SoundEditor::padAction` does when the keyboard screen is the root
	// (`gui/ui/sound_editor.cpp:1474-1477`) and what this guest does **not** do. `evaluatePads` has
	// **one route in**, not one caller: `grep -rn evaluatePads src/deluge` gives six call sites, of
	// which five are subclasses chaining up to `ColumnControlsKeyboard::evaluatePads`
	// (`layout/piano.cpp:39`, `layout/in_key.cpp:37`, `layout/isomorphic.cpp:40`,
	// `layout/chord_keyboard.cpp:59`, `layout/chord_library.cpp:57`) and only
	// `gui/ui/keyboard/keyboard_screen.cpp:205` enters from outside the hierarchy. So that reader stays unreachable
	// under the guest, and the reprieve task 5 described never ends by that route.
	//
	// The value changed anyway, for the reasons that do hold:
	//
	//   * `ui.h`'s own contract for this hook is "returns whether a UI exited" (`gui/ui/ui.h:152-153`).
	//     On the close path a UI did exit, so `NOT_DEALT_WITH` is the wrong answer to the question
	//     asked, however honest it is about the press.
	//   * Every legacy implementation that closes itself answers `ACTIONED_AND_CAUSED_CHANGE`,
	//     including the tenant this guest stands in for: `SoundEditor::exitUI` is `exitCompletely()`
	//     (`gui/ui/sound_editor.h:130`), whose last line is that value
	//     (`gui/ui/sound_editor.cpp:771`). Plan F1's global constraint 1 -- with the toggle on, do
	//     what `SoundEditor` does -- settles it.
	//   * It is provably safe today: the two readers above are the only ones, one tests a different
	//     value and the other cannot run.
	//
	// **The pop depth is not a divergence, and that took reading `MenuScene` to know.** Legacy's
	// long-press Back leaves the *whole* menu however deep it was, not one level. So does this,
	// for the first tenant: the ported menu keeps its navigation depth inside one scene
	// (`MenuScene`'s `st_.depth`) and only asks the shell to remove itself when Back arrives at
	// depth 0 (`scenes/menu/menu_scene.cpp:1684-1689`). One `pop()` here is therefore one whole
	// menu, exactly as `exitCompletely` is. A future tenant that stacks a scene per level would
	// make this differ, and that is the condition to watch.
	//
	// **One divergence from legacy remains, recorded rather than fixed.**
	//
	//      The guest can never return `REMIND_ME_OUTSIDE_CARD_ROUTINE`, which `exitCompletely` can
	//      (`gui/ui/sound_editor.cpp:733-747`, the return at `:698`) when the settings menu is
	//      exiting while `isSDRoutineActive()`. It is the tail of a larger gap in the same block:
	//      leaving the *settings* menu is what writes `FlashStorage`, the MIDI devices file, the
	//      runtime-feature settings and the MIDI-follow defaults (`:703-709`), and **nothing on the
	//      guest's exit path does any of that**. The guest cannot know it is in a settings root --
	//      that is a `MenuScene`/entry-point fact -- so it belongs with F1 task 9, which maps the
	//      fourteen callers to `SceneId`s and is the first place that knows. Under the toggle,
	//      settings edited in the ported menu are not persisted on exit.
	//
	// So both directions of the contract this task does own, spelled out:
	//
	//  - **Shell stack still occupied after the pop** -- something inside the guest took the exit.
	//    `DEALT_WITH`, and the guest stays open. Returning `NOT_DEALT_WITH` here would be a lie
	//    about a press that was consumed, and closing here would pop the guest a level early.
	//  - **Shell stack empty after the pop** -- the guest has nothing left to show and is itself
	//    what Back leaves. `close()` (which is `closeUI(this)`, `gui/ui/ui.cpp:57-59`), then
	//    `ACTIONED_AND_CAUSED_CHANGE`.
	//
	// `pop()` with no argument is "just the top" (`shell/scene_stack.h:69-73`), and it is called
	// unguarded: `SceneStack::pop` returns immediately on an empty stack
	// (`shell/scene_stack.cpp:63-66`), so a `depth() != 0` test here would be a second copy of a
	// check the stack already makes -- and one that reads as though the call were unsafe without it.
	shell_.stack().pop();
	if (shell_.stack().depth() != 0) {
		// The stack moved, so the next tick will re-render everything (`SceneStack::pop` sets
		// `changed_`, `shell/scene_stack.cpp:63-82`, which `Shell::tick` turns into a repaint request
		// on every gate, `shell/shell.cpp:346-358`) -- but nothing schedules that tick. This hook is
		// reached from `TimerName::BACK_MENU_EXIT` (`gui/ui_timer_manager.cpp:149-155`), not from
		// `UI_SPECIFIC`, so whatever the guest last armed is still standing and may be a long way off.
		armTickSoon();
		return ActionResult::DEALT_WITH;
	}
	closeGuest();
	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

void ShellHostUI::closeGuest() {
	// **Hand the two knob indicator rings back to legacy, coherently.** F1 task 4 recorded, and
	// task 7 re-measured, that `GuestSurfaceSet::set_indicator` writes the hardware through
	// `deluge_control_set_indicator` without touching legacy's `knobIndicatorLevels[]` shadow --
	// there is no scalar at that seam to hand `actuallySetKnobIndicatorLevel`, because a
	// `KnobIndicator` is already the four segment levels. So after the guest has driven a ring,
	// legacy's shadow describes a frame the hardware is no longer showing, and
	// `actuallySetKnobIndicatorLevel`'s early-out (`hid/led/indicator_leds.cpp:186-191`) then
	// **suppresses** the write that would put it right. Task 7 named this fix and named F1 task 8's
	// installation of `ModKnobs` as the condition that makes it live; that condition is now met --
	// `ModKnobs` is the one declarer of `frame.indicator(...)` in `src/scenes`
	// (`services/mod_knobs.cpp:687`, `:696`, `:703`).
	//
	// `clearKnobIndicatorLevels()` (`hid/led/indicator_leds.h:88`) is `setKnobIndicatorLevel(i, 0)`
	// for both (`:317-321`), which puts shadow and hardware back in step at zero, and it must run
	// **before** `close()`: `closeUI` hands focus to the UI beneath on the same call
	// (`gui/ui/ui.cpp:199`), and that is what re-fills the rings -- every root view's
	// `focusRegained` reaches `view.setActiveModControllableTimelineCounter(...)`
	// (`gui/views/session_view.cpp:184-186`, `gui/views/arranger_view.cpp:564-566`,
	// `model/clip/instrument_clip_minder.cpp:291-294`), whose tail is `setKnobIndicatorLevels()`
	// (`gui/views/view.cpp:1902`). Clearing after the close would blank rings nothing then
	// refreshes.
	//
	// **One residual, legacy's own.** `SessionView` and `ArrangerView` skip that refresh while
	// `UI_MODE_CLIP_PRESSED_IN_SONG_VIEW` / `UI_MODE_HOLDING_ARRANGEMENT_ROW` is active, so a guest
	// closed during either leaves the rings dark until the next thing that sets levels.
	//
	// (This function used to say it was "the only close path the guest owns". It is now the *body*
	// of both of the guest's own close paths -- `exitUI`'s long-press Back and `timerCallback`'s
	// short one -- which is what F1 task 11's A/B forced. A `closeUI(&shellHostUI)` from somewhere
	// else still has no hook to run it from, since `UI` declares none.)
	indicator_leds::clearKnobIndicatorLevels();

	// No arm on the close path: `close()` is `closeUI(this)` (`gui/ui/ui.cpp:57-59`), which unsets
	// `TimerName::UI_SPECIFIC` itself (`gui/ui/ui.cpp:197`) before handing focus to the UI beneath
	// (`:199`). Arming here would either be undone one line later or, worse, deliver this guest's
	// `timerCallback` to that UI -- `UITimerManager::routine` dispatches to `getCurrentUI()`
	// (`gui/ui_timer_manager.cpp:143`).
	close();
}

// -- MIDI learn (F1 task 10) ---------------------------------------------------------------------
//
// The two `UI` virtuals `SoundEditor` overrides, ported. `@par MIDI learn` on this class has the
// whole table -- which two of the four message types are reachable at all, why CC and pitch bend
// are not, and the `UI_MODE_MIDI_LEARN` gate that makes every one of them inert today. Read it
// before treating either of these as a live path.
//
// Both go through `gui/ui/shell_guest_services.h`'s free functions rather than naming `MenuScene`
// here, for the reason `wireShellGuestServices` does: the host spec suite compiles this file
// directly and never builds the `deluge_scenes` closure (`tests/spec/CMakeLists.txt`), so a
// `MenuScene` type in this translation unit would take `shell_host_ui_spec`'s examples off the
// board. The seam is a symbol, and the fake is one line each.

bool ShellHostUI::pcReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t program) {
	// `SoundEditor::pcReceivedForMidiLearn` (`gui/ui/sound_editor.cpp:1605-1611`), whole. There is
	// no service half and that is not an omission: `View::pcReceivedForMIDILearn` has an **empty
	// body** (`gui/views/view.h:63`), so the `view` fallback `PlaybackHandler::programChangeReceived`
	// runs at `:2900` does nothing, there is nothing for `services::MidiLearn` to port, and no
	// `UiEvent` kind was added for program change.
	return shellGuestMenuLearnProgramChange(cable, channel, program);
}

bool ShellHostUI::noteOnReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t note, int32_t velocity) {
	// **The menu first**, which is `SoundEditor::noteOnReceivedForMidiLearn` (`:1595-1597`): the
	// focused item's own answer, no mode guard, and a `true` is what makes legacy skip its `else`.
	if (shellGuestMenuLearnNoteOn(cable, channel, note)) {
		return true;
	}

	// **Then the shell's own chain**, which is where the `else` goes under the guest. Legacy's
	// fallback is `view.noteOnReceivedForMidiLearn` (`playback/playback_handler.cpp:2970`), and the
	// port of that function is `services::MidiLearn::note_on_received` -- installed in this shell's
	// `midi_learn` slot by `wireShellGuestServices()`. So the message goes to the port of `View`,
	// not to `View`: routing it and returning the chain's answer is what keeps ONE learn state
	// authoritative while the guest is open, instead of splitting a press between the port (which
	// owns the pads) and legacy's `View` (which cannot see them).
	//
	// `Shell::route_midi` is synchronous precisely so this can return a bool at all -- the queueing
	// feeds this class's other input hooks use could not (see its doc, and `@par Input is fed, not
	// answered`). `MidiLearn::handle` claims the message whenever the cable is non-null, matching
	// the dispatcher's own unconditional `return` at `:2972`, so in practice `view` does not run;
	// the value is still taken from the chain rather than hardcoded, because a scene that declines
	// with no service installed must still fall through.
	const deluge::ui::shell::UiEvent event{.kind = deluge::ui::shell::UiEvent::Kind::MidiNoteOn,
	                                       .channel = static_cast<uint8_t>(channel),
	                                       .data1 = static_cast<uint8_t>(note),
	                                       .data2 = static_cast<uint8_t>(velocity),
	                                       .cable = &cable};
	return shell_.route_midi(event, nowMs());
}

} // namespace deluge::gui::ui

deluge::gui::ui::ShellHostUI shellHostUI{deluge::scenes::scene_registry()};
