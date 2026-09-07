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

#include "hid/button.h"
#include "libdeluge/scheduler.h" // isSDRoutineActive(), getSystemTime()
#include "scenes/registry.h"
#include "shell/profiles/deluge.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

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
	// `Services{}` and a default `ShellConfig` on purpose: the three real services each take this
	// shell's own `SceneStack&`, so they cannot exist before it. F1 task 8 builds them over
	// `shell()` and installs them through `Shell::set_services` (`shell/shell.h:143-175`), which is
	// the whole reason that setter exists.
	//
	// `config_` rather than a `ShellConfig{}` temporary, so `feedDetents` can read
	// `edges_per_detent` back out of the same object the shell was built from. `Shell` copies it,
	// so the two are only equal by construction -- which is why F1 task 8, when it seats a real
	// config, has to set `config_` and construct from it rather than reach into the shell.
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

	// **No `Shell::boot()` here, and its absence is deliberate.** The first version of this function
	// called it, on the reasoning that the panels want a repaint on the way in. They do, and they
	// get one without it: `SceneStack::reset` sets `changed_` unconditionally
	// (`shell/scene_stack.cpp:201-202`) and a successful `push` sets it too (`:39`), and
	// `Shell::tick` reacts to `changed_since_last_query()` by requesting a repaint on **every** OLED
	// surface, the grid and the widgets gate before it renders anything
	// (`shell/shell.cpp:328-340`) -- which is the same three things `boot()` does
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
	//
	// @warning **KNOWN DEFECT, in this hook: under the guest the greyout is applied TWICE.** Legacy
	//          greys exactly once, in the *send* path -- `prepareColour`
	//          (`hid/led/pad_leds.cpp:283-298`, the tint at `:293-296`) reads the module-level
	//          `greyoutCols`/`greyoutRows` that `reassessGreyout` fills from this very function
	//          (`hid/led/pad_leds.cpp:734-735`, via `getUIGreyoutColsAndRows`), and never mutates
	//          `PadLEDs::image`. The shell has no send stage to hook, so it greys its own grid in
	//          place instead: `Shell::render_grid` runs `ui::grid::greyout` over `greyout_`
	//          (`shell/shell.cpp:753-759`), the OR of every stacked scene's mask (`:328-334`). Those
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
	//     For the first tenant that is exactly right: `SoundEditor` renders no pads at all.
	//  2. **Legacy's menu blink is a hardware flash; the port's is a framebuffer write.**
	//     `SoundEditor`'s shortcut blink calls `PadLEDs::flashMainPad`
	//     (`gui/ui/sound_editor.cpp:959-1002`, the calls at `:983` and `:994`), which goes straight
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
// Input
//
// Every hook here does the same three things in the same order: take or release the shell's
// SD-routine lock, feed the event, and return the value legacy's own dispatcher wants. Nothing
// asks the shell what happened, because nothing can -- routing runs in `Shell::pump_input`, which
// only `Shell::tick` calls (`shell/shell.cpp:561-571`).
//
// **`inCardRoutine` is not a shell concept, and this is where it stops being one.** Legacy's
// answer is `ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE`: `SoundEditor::padAction` opens with it
// (`gui/ui/sound_editor.cpp:1405-1408`) and `dispatchInputEvent` stashes the offending event in
// `heldInputEvent`, sets `waitingForSDRoutineToEnd`, and re-dispatches it at the top of the next
// `readButtonsAndPads` once the routine has ended (`deluge.cpp:235-279`, the stash at `:245-248`
// and `:256-259`, the replay at `:295-305`). One event deep. The port of that is
// `InputLowering::set_sd_busy`, which queues up to `kDeferredCapacity` (8) events and replays them
// in arrival order, each with the clock it arrived at, when the lock is released
// (`shell/input_lowering.cpp:134-160`). So the guest **defers rather than drops**, and it never
// returns `REMIND_ME_OUTSIDE_CARD_ROUTINE`.
//
// @warning **The release is input-driven, not time-driven.** The guest only hears "the routine
//          ended" when the next hook fires, because that is the only place it can ask. Legacy
//          retries from the poll loop the moment `isSDRoutineActive()` goes false. So a press
//          deferred here waits for the *next* input rather than for the routine's end -- order and
//          content are preserved, latency is not. F1 task 7's `timerCallback` is the natural place
//          to make the release periodic, and it is one `set_sd_busy` call there.
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
	// why `SoundEditor::padAction` reads it directly (`gui/ui/sound_editor.cpp:1406`).
	shell_.set_sd_busy(isSDRoutineActive());

	// **The range guard is about the narrowing, not about the shell.** `on_pad` already drops a
	// coordinate outside `PadMask`'s 32x32 (`shell/input_lowering.cpp:235-238`), but only after
	// this side has cast to `uint8_t`, and that cast folds: `x = 256` becomes column 0 and the
	// shell is handed a pad press that never happened. `MatrixDriver::padAction` bounds nothing of
	// its own (`hid/matrix/matrix_driver.cpp:65-84`), and this is a public virtual that legacy UIs
	// call on each other directly -- `SoundEditor::padAction` forwards straight into
	// `keyboardScreen.padAction` (`gui/ui/sound_editor.cpp:1419-1422`) -- so the caller's domain is
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
	return ActionResult::DEALT_WITH;
}

ActionResult ShellHostUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	// The parameter, not `isSDRoutineActive()`: it is the caller's own answer, computed once at
	// `deluge.cpp:254` for the event being dispatched, and `Buttons::noPressesHappening` passes its
	// own (`hid/buttons.cpp:315-323`).
	shell_.set_sd_busy(inCardRoutine);

	// The exact inverse of `deluge.cpp:253`'s `deluge::hid::button::fromXY(ev.x, ev.y)`: a
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
	// changes the answer, so a guard here would be a second copy of a check that already holds --
	// the same dead-code shape F1 task 5's review deleted from `opened()`. A mutation removing one
	// is unkillable, which is the evidence.
	const Cartesian xy = deluge::hid::button::toXY(b);
	feed(DelugeInputEvent{DELUGE_EVENT_BUTTON, static_cast<uint8_t>(xy.x), static_cast<uint8_t>(xy.y),
	                      static_cast<int16_t>(on ? 255 : 0)});

	// **`NOT_DEALT_WITH`, and it is load-bearing three times over.** It is what `SoundEditor`
	// returns for every button with an observable consequence in `Buttons::buttonAction`'s tail:
	// its `if`/`else if` chain has no arm for Play, Shift, Record, Tempo-enc or either mod-encoder
	// button, so those fall into the final `else` and take `MenuItem::buttonAction`'s default
	// `NOT_DEALT_WITH` (`gui/ui/sound_editor.cpp:595-629`, `gui/menu_item/menu_item.h:78-80`).
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
	return ActionResult::DEALT_WITH;
}

ActionResult ShellHostUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	// This one *is* called during the routine: `interpretEncoders` exempts `scrollY` from the
	// skip (`hid/encoder_input.cpp:95-102`, `isScrollY` at `:95`) and passes `skipActioning` down
	// as this parameter (`:140`). So the deferral matters here in a way it does not for horizontal.
	shell_.set_sd_busy(inCardRoutine);
	feedDetents(EncoderRole::Vertical, offset);
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
	// level of it (`gui/ui/sound_editor.cpp:693-734`).
	//
	// **The brief's stated mechanism does not exist, and this is the correction.** F1's task-5
	// brief and spec F §2.1 both say this "returns `NOT_DEALT_WITH` when the stack is empty so
	// `closeUI` pops the guest". Neither caller does that; every legacy implementation closes
	// *itself*. `RenameUI::exitUI` is `close(); return ACTIONED_AND_CAUSED_CHANGE;`
	// (`gui/ui/rename/rename_ui.cpp:88-91`), `SampleMarkerEditor::exitUI` is the same two lines
	// (`gui/ui/sample_marker_editor.cpp:680-683`), `Browser::exitUI` is `exitAction()` and the same
	// value (`gui/ui/browser/browser.h:133-136`), and `exitCompletely` calls `close()` at
	// `gui/ui/sound_editor.cpp:723`. A guest that only returned a value would trap the user inside
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
	// (`gui/ui/sound_editor.cpp:1419-1422`) and what this guest does **not** do. `evaluatePads` has
	// one caller, `gui/ui/keyboard/keyboard_screen.cpp:205`. So that reader stays unreachable under the guest, and
	// the reprieve task 5 described never ends by that route.
	//
	// The value changed anyway, for the reasons that do hold:
	//
	//   * `ui.h`'s own contract for this hook is "returns whether a UI exited" (`gui/ui/ui.h:152-153`).
	//     On the close path a UI did exit, so `NOT_DEALT_WITH` is the wrong answer to the question
	//     asked, however honest it is about the press.
	//   * Every legacy implementation that closes itself answers `ACTIONED_AND_CAUSED_CHANGE`,
	//     including the tenant this guest stands in for: `SoundEditor::exitUI` is `exitCompletely()`
	//     (`gui/ui/sound_editor.h:130`), whose last line is that value
	//     (`gui/ui/sound_editor.cpp:733`). Plan F1's global constraint 1 -- with the toggle on, do
	//     what `SoundEditor` does -- settles it.
	//   * It is provably safe today: the two readers above are the only ones, one tests a different
	//     value and the other cannot run.
	//
	// **The pop depth is not a divergence, and that took reading `MenuScene` to know.** Legacy's
	// long-press Back leaves the *whole* menu however deep it was, not one level. So does this,
	// for the first tenant: the ported menu keeps its navigation depth inside one scene
	// (`MenuScene`'s `st_.depth`) and only asks the shell to remove itself when Back arrives at
	// depth 0 (`scenes/menu/menu_scene.cpp:1445-1450`). One `pop()` here is therefore one whole
	// menu, exactly as `exitCompletely` is. A future tenant that stacks a scene per level would
	// make this differ, and that is the condition to watch.
	//
	// **One divergence from legacy remains, recorded rather than fixed.**
	//
	//      The guest can never return `REMIND_ME_OUTSIDE_CARD_ROUTINE`, which `exitCompletely` can
	//      (`gui/ui/sound_editor.cpp:695-709`, the return at `:698`) when the settings menu is
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
		return ActionResult::DEALT_WITH;
	}
	close();
	return ActionResult::ACTIONED_AND_CAUSED_CHANGE;
}

} // namespace deluge::gui::ui

deluge::gui::ui::ShellHostUI shellHostUI{deluge::scenes::scene_registry()};
