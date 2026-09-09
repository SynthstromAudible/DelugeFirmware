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
/// @brief The pad seam's consumer. See `shell_pad_forward.h` for why it lives here and what stays
///        on the other side of the wall.

#include "gui/ui/shell_pad_forward.h"

#include "definitions_cxx.hpp" // kDisplayWidth, the same include sound_editor.cpp itself uses
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_action_dispatch.h" // closePortedMenu()
#include "gui/ui/ui.h"                    // getRootUI(), currentUIMode, UI_MODE_NONE
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"

namespace deluge::gui::ui {
namespace {

/// The real performer. Replaced by `setPadForwardPerform`; see its note.
void performForward(PadForwardTarget target, int32_t x, int32_t y, int32_t velocity) {
	switch (target) {
	case PadForwardTarget::Keyboard:
		keyboardScreen.padAction(x, y, velocity); // `sound_editor.cpp:1475`
		break;
	case PadForwardTarget::AuditionColumn:
		instrumentClipView.padAction(x, y, velocity); // `:1501`
		break;
	case PadForwardTarget::Performance:
		(void)performanceView.padAction(x, y, velocity); // `:1516`
		break;
	case PadForwardTarget::Exit:
		closePortedMenu(); // `:1545`'s `exitCompletely()`; see that function's own docblock
		break;
	case PadForwardTarget::None:
		break;
	}
}

PadForwardPerform perform = &performForward;

} // namespace

PadForwardTarget forwardGridPad(int32_t x, int32_t y, int32_t velocity) {
	RootUI* root = getRootUI();

	PadForwardTarget target = PadForwardTarget::None;
	if (root == &keyboardScreen) {
		target = PadForwardTarget::Keyboard; // `:1474-1477`
	}
	else if (root == &instrumentClipView && x == kDisplayWidth + 1) {
		// `:1499-1502`. The two note-editor arms above it (`:1481`, `:1492`) are deliberately
		// absent -- see `shell_pad_forward.h`'s own note: they are keyed on `inNoteEditor()` /
		// `inNoteRowEditor()`, which read a `SoundEditor` member no guest path writes.
		target = PadForwardTarget::AuditionColumn;
	}
	else if (root == &performanceView) {
		target = PadForwardTarget::Performance; // `:1515-1517`
	}
	else if (currentUIMode == UI_MODE_NONE && velocity != 0) {
		// `:1520-1545`, minus the `automationView` arm and the firmware-version diagnostics --
		// `shell_pad_forward.h`'s own note names both gaps and why.
		target = PadForwardTarget::Exit;
	}

	perform(target, x, y, velocity);
	return target;
}

void setPadForwardPerform(PadForwardPerform p) {
	perform = (p != nullptr) ? p : &performForward;
}

} // namespace deluge::gui::ui
