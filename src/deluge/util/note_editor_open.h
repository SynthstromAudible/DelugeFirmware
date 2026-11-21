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

/// @file
/// @brief "Is a note editor open?", asked by the functions the note mode forwards into and writes through. The
///        answer is the note mode's, `gui::ui::activeNoteMode()` (`gui/ui/shell_note_mode.h`), which is `None`
///        with the menu off the guest's stack.
///
/// @par Why the header is in `util/` and the implementation in `gui/ui/`
///
/// `util/menu_entry.h`'s reason, for two of the same kind of caller:
/// `model/action/action_logger.cpp` and `model/note/note_row.cpp` ask this question, and both are
/// inside the `model-includes-ui` rule's glob (`scripts/layers.toml`), each already at its baseline.
/// A `gui/ui/` header there would raise a ratchet that may only shrink. This header includes nothing,
/// so it passes that rule on its own terms, and the definitions -- which read the menu -- are
/// `activeNoteMode()`'s neighbours in `gui/ui/shell_note_mode.cpp`.

namespace deluge::gui::ui {

/// @brief The menu on the guest's stack at `MenuContext::Root::NoteEditor`.
bool noteEditorOpen();

/// @brief The menu on the guest's stack at `MenuContext::Root::NoteRowEditor`.
bool noteRowEditorOpen();

} // namespace deluge::gui::ui
