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

/// @file
/// @brief The action seam's consumer. See `shell_action_dispatch.h` for why it lives here and why
///        its caller may not gate it.

#include "gui/ui/shell_action_dispatch.h"

#include "definitions_cxx.hpp" // PerformanceEditingMode
#include "gui/ui/ui.h"         // openUI
#include "gui/views/performance_view.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "scenes/menu/action.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"

namespace deluge::gui::ui {

using deluge::scenes::menu_scene;
using deluge::scenes::menu::MenuAction;
using deluge::scenes::menu::MenuActionRequest;

namespace {

/// @brief `EditingMode::selectButtonPress`
///        (`gui/menu_item/performance_session_view/editing_mode.h:49-74`), performed on the real
///        `performanceView`.
///
/// @param mode What `MenuActionRequest::arg` carried: a `PerformanceEditingMode`
///        (`scenes/menu/action.h:73-79`).
///
/// @par What `DISABLED` does here, and why it is not an assert
///
/// **It returns, doing nothing** -- and the reason is that legacy's `DISABLED` arm has no meaning on
/// this side of the seam. `editing_mode.h:50-52` answers it with `return nullptr`, which is
/// `SelectChainFn`'s "go up a level" (`scenes/menu/node.h`): a *navigation* answer to the select
/// press. The dispatcher runs after `MenuScene::after_render` has finished with that press, so there
/// is no navigation left for it to ask for.
///
/// It has also already happened. The port reproduces the whole of legacy's `DISABLED` arm on the
/// **producing** side: `PerformEditorItem::render` leaves `MenuState::render_resp.entered` set so
/// `after_render`'s leaf arm ascends, and files no request at all
/// (`scenes/menu/nodes/actions.cpp:153-158`). That is what `scenes/menu/action.h:73-74`'s "never
/// `DISABLED`" promise is made of, and `sim/scenes_spec/menu_actions_spec.cpp`'s "goes up a level on
/// DISABLED and files nothing" example is what pins it. So this branch is unreachable through every
/// real path, and is defence in depth rather than behaviour -- and, being unreachable, it is the one
/// line here no example can redden.
///
/// Ignoring rather than asserting, deliberately: the only assert this tree has on the device is
/// `FREEZE_WITH_ERROR`, a hard hang, and hanging the instrument over a menu press would be a worse
/// answer than a press that does nothing. What must not happen is falling through -- the branch
/// below is an `if`/`else` exactly as legacy's is, so a `DISABLED` reaching it would take the `else`
/// that means `PARAM` and open the view in a mode nobody chose.
void openPerformanceView(PerformanceEditingMode mode) {
	if (mode == PerformanceEditingMode::DISABLED) {
		return;
	}

	// `editing_mode.h:54-61`, transcribed branch for branch rather than collapsed into one
	// assignment, so the two writes stay legible against their source. (`scenes/menu/action.h:76`
	// scopes what is owed here to `editing_mode.h:55-72`, which begins one line INSIDE this
	// `else if` because `editing_mode.h:50-52` is the `DISABLED` arm handled above.)
	if (mode == PerformanceEditingMode::VALUE) {
		performanceView.defaultEditingMode = true;
		performanceView.editingParam = false;
	}
	else { // PerformanceEditingMode::PARAM
		performanceView.defaultEditingMode = true;
		performanceView.editingParam = true;
	}

	// `editing_mode.h:63-70`, guard and reason included: reset the view when the mode changes, but
	// not in param-editing mode, "cause that would reset param assignments to FX columns" (`:65`).
	if (!performanceView.editingParam) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		performanceView.resetPerformanceView(modelStack);
	}

	// `editing_mode.h:72`. `:73`'s `return NO_NAVIGATION` -- stay on the menu page while the view
	// opens over it -- is the producer's half and is already ported: `PerformEditorItem::render`
	// clears `render_resp.entered` (`scenes/menu/nodes/actions.cpp:167`).
	openUI(&performanceView);
}

} // namespace

void dispatchShellActions() {
	// One read, unconditionally, and the read is what empties the slot -- `take_action()` returns
	// the request and clears it in the same call (`scenes/menu/menu_scene.h:609-610`). So an early
	// return above this line would not be "nothing to do", it would be "the request stays in the
	// outbox until something else asks", which is the stale delivery the mailbox's own warning
	// describes (`:593-598`).
	const MenuActionRequest req = menu_scene().take_action();
	if (!req.engaged()) {
		return;
	}

	switch (req.action) {
	case MenuAction::None:
		// Unreachable: `engaged()` IS `action != None` (`scenes/menu/action.h:109`), so the guard
		// above has already returned. Spelled out anyway because the alternative is a `default:`,
		// and a `default:` here would silently absorb the enumerators later tasks add -- see the
		// header's note on `-Werror=switch`.
		break;

	case MenuAction::OpenPerformanceView:
		// `arg` is a `PerformanceEditingMode` and is never `DISABLED` (`scenes/menu/action.h:73-79`);
		// the helper says what it does with one anyway, and why.
		openPerformanceView(static_cast<PerformanceEditingMode>(req.arg));
		break;

	// **The four verbs still empty.** Each one's body is one legacy statement (or, for
	// `StartStemExport`, one legacy statement plus the `getRootUI()` fan-out that chooses its
	// argument), tabulated with its legacy source at `scenes/menu/action.h:13-19` and owed in detail
	// by each enumerator's own doc comment (`:81-98`). They arrive with the tasks that port them.
	//
	// Until then the drain is the whole of their behaviour, and it is not nothing: the request is
	// consumed once, on the tick it was filed, rather than sitting in a one-slot outbox to be
	// delivered against unrelated state whenever something finally polls. What a user sees for these
	// four is unchanged -- a select press on `start_stem_export` still exports nothing, which is item
	// 1 of `util/shell_menu_entry.h`'s "what works under the toggle" list.
	case MenuAction::StartStemExport:
	case MenuAction::SetClipLengthToSampleLength:
	case MenuAction::OpenSongMidiLearn:
	case MenuAction::OpenSongMacros:
		break;
	}
}

} // namespace deluge::gui::ui
