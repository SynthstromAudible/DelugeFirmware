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
/// @brief See `util/sound_menu_open.h`.

#include "util/sound_menu_open.h"

#include "gui/menu_item/dx/param.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/shell_choice_dispatch.h" // shellMenuModControllable, shellMenuParamManager
#include "gui/ui/shell_guest_services.h"  // the shell* arms
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "processing/source.h"
#include "storage/flash_storage.h"
#include "util/shell_menu_entry.h"

#include <iterator>

namespace deluge::gui::ui {

bool soundMenuOpen() {
	return (getCurrentUI() == &soundEditor && !soundEditor.inSettingsMenu()) || shellSoundMenuOpen();
}

bool soundMenuKitGlobalFx() {
	return shellKitGlobalFx();
}

bool soundMenuCurrent() {
	return getCurrentUI() == &soundEditor || shellSoundMenuOpen();
}

ModControllableAudio* soundMenuModControllable() {
	return getCurrentUI() == &soundEditor ? soundEditor.currentModControllable : shellMenuModControllable();
}

ParamManagerForTimeline* soundMenuParamManager() {
	return getCurrentUI() == &soundEditor ? soundEditor.currentParamManager : shellMenuParamManager();
}

bool menuCurrent() {
	return getCurrentUI() == &soundEditor || shellMenuFrontmost();
}

void readSoundMenuValueAgain() {
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->readValueAgain();
	}
	else if (shellMenuFrontmost()) {
		shellMenuReadValueAgain();
	}
}

void mpeZonesPotentiallyUpdated() {
	soundEditor.mpeZonesPotentiallyUpdated();
	shellMpeZonesPotentiallyUpdated();
}

void legacyShortcutsVersionChanged() {
	soundEditor.setShortcutsVersion(FlashStorage::shortcutsVersion);
}

std::optional<int32_t> dxColumnFlashRow() {
	if (shellSoundMenuOpen()) {
		return shellDxColumnFlashRow();
	}
	if (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::dxParam
	    && menu_item::dxParam.blink_next) {
		return menu_item::dxParam.flash_row;
	}
	return std::nullopt;
}

bool drumSelectionFrozen() {
	const UI* current = getCurrentUI();
	return current == &sampleBrowser || current == &sampleMarkerEditor || current == &renameDrumUI
	       || shellFreezesDrumSelection();
}

bool rangePickerOpen() {
	return (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu)
	       || shellRangePickerOpen();
}

bool rangeDependentMenuOpen() {
	return getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
	       || (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())
	       || shellRangeDependentOpen();
}

bool noteWithinCurrentRange(int32_t untransposed) {
	if (shellRangeDependentOpen()) {
		return shellNoteWithinCurrentRange(untransposed);
	}
	return soundEditor.isUntransposedNoteWithinRange(untransposed);
}

void changeRangeForNote(int32_t note) {
	if (getCurrentUI() == &soundEditor) {
		menu_item::multiRangeMenu.noteOnToChangeRange(note);
	}
	else if (shellRangePickerOpen()) {
		shellChangeRangeForNote(note);
	}
}

void legacyRangesRetyped(Source& source, bool cleared) {
	if (cleared) {
		soundEditor.currentMultiRangeIndex = 0;
	}
	if (soundEditor.currentMultiRangeIndex >= 0 && soundEditor.currentMultiRangeIndex < std::ssize(source.ranges)) {
		soundEditor.currentMultiRange = source.ranges.getElement(soundEditor.currentMultiRangeIndex);
	}
}

ActionResult menuShortcutPadAction(int32_t x, int32_t y, bool on) {
	if (getCurrentUI() != &soundEditor && getCurrentUI() != &sampleMarkerEditor) {
		return shellShortcutPadAction(x, y, on);
	}
	return soundEditor.potentialShortcutPadAction(x, y, on);
}

} // namespace deluge::gui::ui
