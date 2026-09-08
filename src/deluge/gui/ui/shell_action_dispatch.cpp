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

#include "scenes/menu/action.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"

namespace deluge::gui::ui {

using deluge::scenes::menu_scene;
using deluge::scenes::menu::MenuAction;
using deluge::scenes::menu::MenuActionRequest;

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

	// **The five verbs, deliberately still empty.** Each one's body is one legacy statement (or, for
	// `StartStemExport`, one legacy statement plus the `getRootUI()` fan-out that chooses its
	// argument), tabulated with its legacy source at `scenes/menu/action.h:13-19` and owed in detail
	// by each enumerator's own doc comment (`:73-98`). They arrive with the tasks that port them.
	//
	// Until then the drain is the whole of the behaviour, and it is not nothing: the request is
	// consumed once, on the tick it was filed, rather than sitting in a one-slot outbox to be
	// delivered against unrelated state whenever something finally polls. What a user sees is
	// unchanged -- a select press on `start_stem_export` still exports nothing, which is item 1 of
	// `util/shell_menu_entry.h`'s "what works under the toggle" list.
	case MenuAction::OpenPerformanceView:
	case MenuAction::StartStemExport:
	case MenuAction::SetClipLengthToSampleLength:
	case MenuAction::OpenSongMidiLearn:
	case MenuAction::OpenSongMacros:
		break;
	}
}

} // namespace deluge::gui::ui
