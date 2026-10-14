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
/// @brief "Is a note editor open?" -- legacy's own question, asked so that it also sees the ported
///        note mode.
///
/// Legacy asks `getCurrentUI() == &soundEditor && soundEditor.inNoteEditor()` (or
/// `inNoteRowEditor()`) in the functions the note mode forwards into and writes through (design spec
/// §4, "Beyond the eight sites -- correction 7"). Under the guest the current UI is `ShellHostUI` and
/// `SoundEditor`'s navigation record is never written, so both halves are false. Each function below
/// is legacy's expression **or** the ported mode's answer, `gui::ui::activeNoteMode()`
/// (`gui/ui/shell_note_mode.h`). With the ported menu off the guest's stack `activeNoteMode()` answers
/// `None`, so each is exactly legacy's expression.
///
/// @par Why the header is in `util/` and the implementation in `gui/ui/`
///
/// `util/shell_menu_entry.h`'s reason, for two of the same kind of caller:
/// `model/action/action_logger.cpp` and `model/note/note_row.cpp` ask this question, and both are
/// inside `check_layer_rules.py`'s `NON_UI` glob, each already at its `model-includes-ui` baseline
/// (10 and 3). A `gui/ui/` header there would raise a ratchet that may only shrink. This header
/// includes nothing, so it passes that rule on its own terms, and the definitions -- which name
/// `soundEditor` and read the ported menu -- stay in `gui/ui/shell_note_mode.cpp`.

namespace deluge::gui::ui {

/// @brief Legacy's `getCurrentUI() == &soundEditor && soundEditor.inNoteEditor()`, or the ported menu
///        on the guest's stack at `MenuContext::Root::NoteEditor`. `SoundEditor::inNoteEditor`
///        (`gui/ui/sound_editor.cpp:1916-1918`) is the record read the second half ports.
bool noteEditorOpen();

/// @brief Legacy's `getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor()`, or the ported
///        menu on the guest's stack at `MenuContext::Root::NoteRowEditor`. `SoundEditor::inNoteRowEditor`
///        (`gui/ui/sound_editor.cpp:1920-1922`) is the record read the second half ports.
bool noteRowEditorOpen();

} // namespace deluge::gui::ui
