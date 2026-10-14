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
#include "gui/ui/shell_note_mode.h"       // activeNoteMode(), noteModeLegacyCalls()
#include "gui/ui/ui.h"                    // getRootUI(), currentUIMode, UI_MODE_NONE
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "model/clip/clip.h"
#include "model/song/song.h" // currentSong, getCurrentClip()

#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/settings.h" // nodes::firmware_version
#include "scenes/registry.h"

namespace deluge::gui::ui {
namespace {

/// @brief `SoundEditor::isEditingAutomationViewParam` (`gui/ui/sound_editor.cpp:1564-1590`), asked
///        of the ported menu instead of of `SoundEditor`.
///
/// **Not a call into legacy's copy, and this is the whole reason this function exists.** That copy
/// opens with `getCurrentMenuItem()->getParamKind()`, and `getCurrentMenuItem()` is
/// `menuItemNavigationRecord[navigationDepth]` (`:1908-1910`) -- a zero-initialised member
/// (`gui/ui/sound_editor.h:111`) written only by `SoundEditor::setup`/`navigate`, neither of which
/// runs under the guest. `adoptResolvedContext` deliberately does not write it and says so
/// (`sound_editor.h:216`). So legacy's copy dereferences null here on any boot that never opened a
/// legacy menu. F3 task 4 met the same shape from the other side and supplied the state; the state
/// cannot be supplied here, because under the toggle the open menu genuinely is `MenuScene`.
///
/// The `clip != nullptr` guard is legacy's missing one (`:1582-1586`). Writing it in this copy is
/// not the fix migration §10.5 banked -- that fix edits `sound_editor.cpp`, and this does not.
bool menuShowsAutomationParam() {
	const auto* focused = deluge::scenes::menu_scene().focused();
	if (focused == nullptr) {
		return false;
	}
	// `:1576`'s test, in this layer's representation. Legacy tests two things -- `kind != NONE`
	// and `paramID != kNoParamID` -- but `kNoParamID` is `0xFFFFFFFF`
	// (`modulation/params/param.h:304`) and `ParamRef::index` is a `uint8_t` whose unset value is
	// 255 (`scenes/menu/node.h:132`), so that second comparison cannot be transcribed: it would be
	// false for every possible index. `ParamRef::valid()` (`:133`) is the whole of the test here --
	// an unbound `Binding` carries `Kind::NONE`.
	const deluge::scenes::menu::ParamRef& param = focused->bind.param;
	if (!param.valid()) {
		return false;
	}
	const auto kind = param.kind;
	const int32_t paramID = param.index;

	if (automationView.onArrangerView) { // `:1578-1580`
		return currentSong != nullptr && kind == currentSong->lastSelectedParamKind
		       && paramID == currentSong->lastSelectedParamID;
	}
	const Clip* clip = getCurrentClip(); // `:1582`
	return clip != nullptr && kind == clip->lastSelectedParamKind
	       && paramID == clip->lastSelectedParamID; // `:1585-1586`
}

/// @brief The "stays open" half of the firmware-version diagnostic gap (I2), asked of the ported
///        menu instead of of `SoundEditor`.
///
/// Legacy's tail (`gui/ui/sound_editor.cpp:1521-1543`) tests `getCurrentMenuItem() ==
/// &firmwareVersionMenu && y == 7`, then for `x` in {15, 14, 13} prints a popup and `return`s
/// `DEALT_WITH` **without** falling to `exitCompletely()` below it. This function is only the
/// second half of that test -- the popups themselves are `shell_pad_forward.h`'s own named gap and
/// stay unported -- so the caller still has to test `x` and `y` itself; see there.
///
/// `getCurrentMenuItem()` is unavailable here for the same reason `menuShowsAutomationParam` above
/// does not call `SoundEditor::isEditingAutomationViewParam`: it is a `SoundEditor` member no guest
/// path writes. The ported equivalent of "the firmware-version item is what's open" is the focused
/// node's identity, the same shape the automation arm already asks for.
bool onFirmwareVersionMenu() {
	return deluge::scenes::menu_scene().focused() == &deluge::scenes::menu::nodes::firmware_version;
}

/// The real performer. Replaced by `setPadForwardPerform`; see its note.
void performForward(PadForwardTarget target, int32_t x, int32_t y, int32_t velocity) {
	switch (target) {
	case PadForwardTarget::Keyboard:
		keyboardScreen.padAction(x, y, velocity); // `sound_editor.cpp:1475`
		break;
	case PadForwardTarget::AuditionColumn:
		instrumentClipView.padAction(x, y, velocity); // `:1501`
		break;
	case PadForwardTarget::Automation:
		automationView.padAction(x, y, velocity); // `:1558`
		break;
	case PadForwardTarget::Performance:
		(void)performanceView.padAction(x, y, velocity); // `:1516`
		break;
	case PadForwardTarget::Exit:
		closePortedMenu(); // `:1545`'s `exitCompletely()`; see that function's own docblock
		break;
	case PadForwardTarget::NoteEditorEdit:
		noteModeLegacyCalls().noteEditorEditPad(x, y, velocity); // `:1484`
		break;
	case PadForwardTarget::NoteRowEditor:
		// The handler may have changed what is open by the time it answers; `closePortedMenu()` is
		// safe whatever it did, because it only asks this guest's stack to remove the menu, which
		// touches nothing when the menu is not there (`shell/scene_stack.h:99-100`). The arm is
		// `sound_editor.cpp:1493-1496`: `bool handled = ...; if (!handled) { exitCompletely(); }`.
		if (!noteModeLegacyCalls().noteRowEditorPad(x, y, velocity)) {
			closePortedMenu();
		}
		break;
	case PadForwardTarget::None:
		break;
	}
}

PadForwardPerform perform = &performForward;

} // namespace

PadForwardTarget forwardGridPad(int32_t x, int32_t y, int32_t velocity) {
	RootUI* root = getRootUI();
	const NoteModeKind noteMode = activeNoteMode();

	PadForwardTarget target = PadForwardTarget::None;
	if (root == &keyboardScreen) {
		target = PadForwardTarget::Keyboard; // `:1474-1477`
	}
	else if (root == &instrumentClipView && noteMode == NoteModeKind::NoteEditor) {
		// `:1481-1490`: the main grid to the note editor's edit action, the sidebar -- both columns,
		// the audition column included -- to `exitCompletely()`. Neither half tests `on` or
		// `currentUIMode`, so neither does this, and the tail's `currentUIMode == UI_MODE_NONE && on`
		// (`:1520`) is never reached: legacy `return`s at `:1490`. The mode is folded into this
		// `else if`'s condition for the reason the `automationView` arm below gives.
		target = x < kDisplayWidth ? PadForwardTarget::NoteEditorEdit : PadForwardTarget::Exit;
	}
	else if (root == &instrumentClipView && noteMode == NoteModeKind::NoteRowEditor) {
		// `:1492-1497`: every pad to the note-row editor's handler, which decides the exit itself.
		target = PadForwardTarget::NoteRowEditor;
	}
	else if (root == &instrumentClipView && x == kDisplayWidth + 1) {
		// `:1499-1502`, below the two note arms, as in legacy.
		target = PadForwardTarget::AuditionColumn;
	}
	else if (root == &automationView && (x >= kDisplayWidth || menuShowsAutomationParam())) {
		// `:1507-1512`, whose body is `handleAutomationViewPadAction` (`:1551-1562`): the sidebar
		// half needs no param agreement, the main-grid half needs the menu to be showing the very
		// param automation view is editing. The claim test is folded into THIS `else if`'s own
		// condition, the same shape `instrumentClipView`'s arm above uses -- a bare
		// `else if (root == &automationView) { if (claim) {...} }` would make this branch of the
		// chain "taken" the moment the root matched, even when the inner claim failed, and the
		// chain would never reach the tail below. `NOT_DEALT_WITH` from
		// `handleAutomationViewPadAction` does not return in legacy either -- it falls to the tail
		// the same way a declined claim here does.
		target = PadForwardTarget::Automation;
	}
	else if (root == &performanceView) {
		target = PadForwardTarget::Performance; // `:1515-1517`
	}
	else if (currentUIMode == UI_MODE_NONE && velocity != 0
	         && !(y == 7 && (x == 13 || x == 14 || x == 15) && onFirmwareVersionMenu())) {
		// `:1520-1545`, minus the firmware-version diagnostics -- `shell_pad_forward.h`'s own note
		// names that gap and why. The popups are the missing half of that gap; the "menu stays
		// open" half is not (I2) -- legacy's tail `return`s `DEALT_WITH` without `exitCompletely()`
		// for exactly the three pads the added clause above names, and before this fix the guest
		// closed the menu there instead, a divergence this branch would otherwise have introduced.
		// Leaving `target` at `None` reproduces "swallowed, menu open" without porting the popups.
		target = PadForwardTarget::Exit;
	}

	perform(target, x, y, velocity);
	return target;
}

void setPadForwardPerform(PadForwardPerform p) {
	perform = (p != nullptr) ? p : &performForward;
}

} // namespace deluge::gui::ui
