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

#include "base/notify/notice_queue.h"
#include "definitions_cxx.hpp" // kDisplayWidth, the same include sound_editor.cpp itself uses
#include "extern.h"            // picFirmwareVersion
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_action_dispatch.h" // closePortedMenu()
#include "gui/ui/shell_note_mode.h"       // activeNoteMode(), noteModeLegacyCalls()
#include "gui/ui/ui.h"                    // getRootUI(), currentUIMode, UI_MODE_NONE
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "memory/heaps.h"
#include "model/clip/clip.h"
#include "model/project_bridge/param_ids.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h" // currentSong, getCurrentClip()
#include "processing/engines/audio_engine.h"

#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/settings.h" // nodes::firmware_version
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/registry.h"

#include <charconv>
#include <iterator>
#include <string_view>

namespace deluge::gui::ui {
namespace {

/// @brief `SoundEditor::isEditingAutomationViewParam`, asked
///        of the menu instead of of `SoundEditor`.
///
/// **Not a call into main's copy, and this is the whole reason this function exists.** That copy opens with
/// `getCurrentMenuItem()->getParamKind()`, and `getCurrentMenuItem()` is `menuItemNavigationRecord[navigationDepth]`
/// (`SoundEditor::getCurrentMenuItem`) -- a zero-initialised member (`menuItemNavigationRecord` in
/// `origin/main:src/deluge/gui/ui/sound_editor.h`) written only by `SoundEditor::setup`/`navigate`, neither of which
/// runs under the guest. So main's copy dereferences null here on any boot that never ran `SoundEditor::setup`, and
/// the state cannot be supplied here, because the open menu genuinely is `MenuScene`.
///
/// The param compared is the one `focused_value()` edits: on a horizontal page the focused column, as legacy's
/// `getCurrentMenuItem()->getParamKind()` answers through `Submenu::getParamKind`; a cable page answers with the cable
/// (kind `PATCH_CABLE`, id its descriptor word).
///
/// The `clip != nullptr` guard is the one main's `SoundEditor::isEditingAutomationViewParam` lacks, ahead of its
/// `clip->lastSelectedParamKind` reads. Writing it in this copy is
/// not a fix to `origin/main:src/deluge/gui/ui/sound_editor.cpp`; main's copy is untouched.
bool menuShowsAutomationParam() {
	// Main's `kind != NONE && paramID != kNoParamID` test is `focused_automation_param()` answering at all.
	const std::optional<deluge::ui::scenes::menu::AutomationParam> param =
	    deluge::ui::scenes::menu_scene().focused_automation_param();
	if (!param) {
		return false;
	}
	const deluge::bridge::RuntimeParam runtime =
	    deluge::bridge::runtimeParamOf(param->plugin, param->target).value_or(deluge::bridge::RuntimeParam{});
	const auto kind = runtime.kind;
	const int32_t paramID = runtime.index;

	if (automationView.onArrangerView) { // main's arranger arm
		return currentSong != nullptr && kind == currentSong->lastSelectedParamKind
		       && paramID == currentSong->lastSelectedParamID;
	}
	const Clip* clip = getCurrentClip(); // main's clip arm
	return clip != nullptr && kind == clip->lastSelectedParamKind && paramID == clip->lastSelectedParamID;
}

/// @brief Whether the firmware-version item is what the menu shows: the half of the tail's firmware-version
///        test (`getCurrentMenuItem() == &firmwareVersionMenu`) that asks the menu; the caller tests `x` and `y`.
///
/// `getCurrentMenuItem()` is unavailable here for the same reason `menuShowsAutomationParam` above
/// does not call `SoundEditor::isEditingAutomationViewParam`: it is a `SoundEditor` member no guest
/// path writes. The equivalent of "the firmware-version item is what's open" is the focused
/// node's identity, the same shape the automation arm already asks for.
bool onFirmwareVersionMenu() {
	return deluge::ui::scenes::menu_scene().focused() == &deluge::ui::scenes::menu::nodes::firmware_version;
}

/// @brief The number a firmware-version diagnostic pad shows: (15, 7) the internal memory's size, (14, 7) the voices
///        sounding, (13, 7) the PIC's firmware version (`SoundEditor::padAction`'s firmware-version arm on
///        `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
int64_t firmwareDiagnostic(int32_t x) {
	if (x == 15) {
		return static_cast<int64_t>(deluge::memory::sram_size());
	}
	if (x == 14) {
		return AudioEngine::getNumVoices() + AudioEngine::getNumAudio();
	}
	return picFirmwareVersion;
}

/// The real performer. Replaced by `setPadForwardPerform`; see its note.
void performForward(PadForwardTarget target, int32_t x, int32_t y, int32_t velocity) {
	switch (target) {
	case PadForwardTarget::Keyboard:
		keyboardScreen.padAction(x, y, velocity); // `SoundEditor::padAction`'s keyboard arm
		break;
	case PadForwardTarget::AuditionColumn:
		instrumentClipView.padAction(x, y, velocity); // `SoundEditor::padAction`'s audition-column arm
		break;
	case PadForwardTarget::Automation:
		automationView.padAction(x, y, velocity); // main's `SoundEditor::handleAutomationViewPadAction`
		break;
	case PadForwardTarget::Performance:
		(void)performanceView.padAction(x, y, velocity); // `SoundEditor::padAction`'s performance-view arm
		break;
	case PadForwardTarget::Exit:
		closePortedMenu(); // `SoundEditor::padAction`'s tail `exitCompletely()`; see that function's own docblock
		break;
	case PadForwardTarget::FirmwareDiagnostic: {
		char text[24] = {};
		const auto [end, ec] = std::to_chars(std::begin(text), std::end(text) - 1, firmwareDiagnostic(x));
		(void)ec;
		deluge::base::notify::notice_text(std::string_view{text, static_cast<size_t>(end - text)});
		break;
	}
	case PadForwardTarget::NoteEditorEdit:
		noteModeLegacyCalls().noteEditorEditPad(x, y, velocity); // `SoundEditor::padAction`'s note-editor arm
		break;
	case PadForwardTarget::NoteRowEditor:
		// The handler may have changed what is open by the time it answers; `closePortedMenu()` is safe whatever it
		// did, because it only asks this guest's stack to remove the menu, which touches nothing when the menu is not
		// there (`SceneStack::remove`). The arm is `SoundEditor::padAction`'s note-row branch
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): it calls `exitCompletely()` when
		// the handler answers false.
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
		target = PadForwardTarget::Keyboard; // `SoundEditor::padAction`'s keyboard arm
	}
	else if (root == &instrumentClipView && noteMode == NoteModeKind::NoteEditor) {
		// `SoundEditor::padAction`'s note-editor arm: the main grid to the note editor's edit action, the sidebar --
		// both columns, the audition column included -- to `exitCompletely()`. Neither half tests `on` or
		// `currentUIMode`, so neither does this, and the tail's `currentUIMode == UI_MODE_NONE && on` is never reached:
		// main's arm returns before it. The mode is folded into this `else if`'s condition for the reason the
		// `automationView` arm below gives.
		target = x < kDisplayWidth ? PadForwardTarget::NoteEditorEdit : PadForwardTarget::Exit;
	}
	else if (root == &instrumentClipView && noteMode == NoteModeKind::NoteRowEditor) {
		// The note-row-editor arm: every pad to the note-row editor's handler, which decides the exit itself.
		target = PadForwardTarget::NoteRowEditor;
	}
	else if (root == &instrumentClipView && x == kDisplayWidth + 1) {
		// The audition-column arm, below the two note arms, as in main.
		target = PadForwardTarget::AuditionColumn;
	}
	else if (root == &automationView && (x >= kDisplayWidth || menuShowsAutomationParam())) {
		// The automation arm, whose body is main's `SoundEditor::handleAutomationViewPadAction`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the sidebar half needs no param agreement, the main-grid
		// half needs the menu to be showing the very param automation view is editing. The claim test is folded into
		// THIS `else if`'s own condition, the same shape `instrumentClipView`'s arm above uses -- a bare
		// `else if (root == &automationView) { if (claim) {...} }` would make this branch of the chain "taken" the
		// moment the root matched, even when the inner claim failed, and the chain would never reach the tail below.
		// `NOT_DEALT_WITH` from `handleAutomationViewPadAction` does not return in legacy either -- it falls to the
		// tail the same way a declined claim here does.
		target = PadForwardTarget::Automation;
	}
	else if (root == &performanceView) {
		target = PadForwardTarget::Performance; // the performance-view arm
	}
	else if (currentUIMode == UI_MODE_NONE && velocity != 0) {
		// The tail: the firmware-version item's three diagnostic pads pop up their number and keep the menu open;
		// any other press closes it.
		target = (y == 7 && (x == 13 || x == 14 || x == 15) && onFirmwareVersionMenu())
		             ? PadForwardTarget::FirmwareDiagnostic
		             : PadForwardTarget::Exit;
	}

	perform(target, x, y, velocity);
	return target;
}

bool menuShowsAutomationViewParam() {
	return menuShowsAutomationParam();
}

bool rootKeepsShortcutPad(int32_t x, int32_t y) {
	return getRootUI() == &automationView && x == 0 && (y == 6 || y == 7);
}

void forwardClipAuditionPad(int32_t x, int32_t y, int32_t velocity) {
	const Clip* clip = getCurrentClip();
	if (clip != nullptr && clip->type == ClipType::INSTRUMENT) {
		perform(PadForwardTarget::AuditionColumn, x, y, velocity);
	}
}

bool rootTakesAuditionPad() {
	return getRootUI() == &instrumentClipView;
}

ShortcutPadGate shortcutPadGate(bool shift) {
	// `potentialShortcutPadAction`'s `ignoreAction`: with SHIFT up, only an audition over the instrument clip view.
	if (!shift && !(isUIModeActive(UI_MODE_AUDITIONING) && getRootUI() == &instrumentClipView)) {
		return ShortcutPadGate::Forward;
	}
	static constexpr uint32_t kShortcutPadUIModes[] = {UI_MODE_AUDITIONING,
	                                                   UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR, 0};
	if (!isUIModeWithinRange(kShortcutPadUIModes)) {
		return ShortcutPadGate::Swallow;
	}
	return shift ? ShortcutPadGate::Shift : ShortcutPadGate::Audition;
}

bool takeSequenceDirectionShortcut(int32_t x, int32_t y) {
	const auto& scene = deluge::ui::scenes::menu_scene();
	const Clip* clip = getCurrentClip();
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT || getRootUI() == &automationView
	    || runtimeFeatureSettings.get(RuntimeFeatureSettingType::HorizontalMenus) != RuntimeFeatureStateToggle::On
	    || instrumentClipView.getNumNoteRowsAuditioning() != 1) {
		return false;
	}
	const deluge::ui::scenes::menu::MenuContext& ctx = scene.menu_context();
	const deluge::ui::scenes::menu::Node* item =
	    deluge::ui::scenes::menu::shortcut_node(ctx.root, deluge::ui::scenes::menu::shortcuts_version(ctx), false,
	                                            static_cast<uint8_t>(x), static_cast<uint8_t>(y));
	if (item != &deluge::ui::scenes::menu::nodes::sequence_direction) {
		return false;
	}
	(void)instrumentClipView.enterNoteRowEditor(/*onPlayDirection=*/true);
	return true;
}

void setPadForwardPerform(PadForwardPerform p) {
	perform = (p != nullptr) ? p : &performForward;
}

bool renderClipViewSidebarUnderMarker(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (getRootUI() != &keyboardScreen) {
		return false;
	}
	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

} // namespace deluge::gui::ui
