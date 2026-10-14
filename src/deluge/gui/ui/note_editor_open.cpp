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

#include "util/note_editor_open.h"

#include "gui/ui/shell_note_mode.h" // activeNoteMode(): `None` without the UI shell
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"

namespace deluge::gui::ui {

bool noteEditorOpen() {
	// Legacy's expression, then its port for the guest: `inNoteEditor()` reads
	// `menuItemNavigationRecord[0]` (`gui/ui/sound_editor.cpp:1916-1918`), which no guest path writes,
	// and `activeNoteMode()` is that read's port. Off the guest's stack the second half is false.
	return (getCurrentUI() == &soundEditor && soundEditor.inNoteEditor())
	       || activeNoteMode() == NoteModeKind::NoteEditor;
}

bool noteRowEditorOpen() {
	// `noteEditorOpen`'s shape, over `inNoteRowEditor()` (`gui/ui/sound_editor.cpp:1920-1922`).
	return (getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor())
	       || activeNoteMode() == NoteModeKind::NoteRowEditor;
}

} // namespace deluge::gui::ui
