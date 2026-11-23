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

#include "gui/menu_item/note/param_locks.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include <cstdio>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::note {

namespace {

// A lock's value is in the param's preset-value domain; these are PatchedParamSet's knob mappings (which need a
// model stack to tell which param they're converting)
int32_t lockValueToKnobPos(int32_t param, int32_t value) {
	if (param == params::LOCAL_OSC_A_PHASE_WIDTH || param == params::LOCAL_OSC_B_PHASE_WIDTH) {
		return (value >> 24) - 64;
	}
	if (value >= (int32_t)(0x80000000 - (1 << 24))) {
		return 64;
	}
	return (value + (1 << 24)) >> 25;
}

int32_t knobPosToLockValue(int32_t param, int32_t knobPos) {
	if (knobPos >= 64) {
		return 2147483647;
	}
	if (param == params::LOCAL_OSC_A_PHASE_WIDTH || param == params::LOCAL_OSC_B_PHASE_WIDTH) {
		return (knobPos + 64) << 24;
	}
	return knobPos << 25;
}

/// The value the param's own menu (and the knob popup) would show
int32_t displayValue(int32_t param, int32_t knobPos) {
	return view.calculateKnobPosForDisplay(params::Kind::PATCHED, param, knobPos + kKnobPosOffset);
}

const char* paramName(int32_t param) {
	// The Sound's filter modes rename the morph params. Notes keep their locks if their clip becomes a MIDI / CV clip,
	// whose ModControllable isn't a ModControllableAudio.
	OutputType type = getCurrentOutputType();
	auto* modControllable = (type == OutputType::SYNTH || type == OutputType::KIT)
	                            ? static_cast<ModControllableAudio*>(soundEditor.currentModControllable)
	                            : nullptr;
	return params::getPatchedParamShortName(param, modControllable);
}

} // namespace

// ParamLockValue ----------------------------------------------------------------------------------------------------

std::string_view ParamLockValue::getTitle() const {
	return paramName(param_);
}

void ParamLockValue::readCurrentValue() {
	dirty_ = false;
	if (std::optional<int32_t> locked = instrumentClipView.noteLocks.getMenuLocks().get(param_)) {
		setValue(lockValueToKnobPos(param_, *locked));
	}
}

void ParamLockValue::readValueAgain() {
	if (dirty_) {
		return; // Keep showing the pending edit until it's written
	}
	if (!instrumentClipView.noteLocks.getMenuLocks().has(param_)) {
		// The notes now targeted don't have this lock (or it was deleted): back to the list, rather than show a value
		// that isn't theirs
		soundEditor.goUpOneLevel();
		return;
	}
	Integer::readValueAgain();
}

int32_t ParamLockValue::getDisplayValue() {
	return displayValue(param_, getValue());
}

void ParamLockValue::selectEncoderAction(int32_t offset) {
	// Step the knob position until the displayed value changes, so each detent moves the number by one like the
	// param's own menu does (knob positions are finer than menu units)
	int32_t direction = offset > 0 ? 1 : -1;
	for (int32_t steps = std::abs(offset); steps > 0; steps--) {
		int32_t before = getDisplayValue();
		while (getValue() + direction >= getMinValue() && getValue() + direction <= getMaxValue()) {
			setValue(getValue() + direction);
			if (getDisplayValue() != before) {
				break;
			}
		}
	}
	Number::selectEncoderAction(offset); // writeCurrentValue (marks it dirty) and redraw
}

void ParamLockValue::drawValue() {
	display->setTextAsNumber(getDisplayValue(), 3); // The dot marks a lock, as in the knob popup
}

void ParamLockValue::commit() {
	if (!dirty_) {
		return;
	}
	dirty_ = false;
	instrumentClipView.noteLocks.setMenuLock(param_, knobPosToLockValue(param_, getValue()));
}

void ParamLockValue::endSession() {
	commit();
	Integer::endSession();
}

void ParamLockValue::deletePress() {
	dirty_ = false;
	if (instrumentClipView.noteLocks.removeMenuLock(param_)) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTE_LOCK_DELETED));
	}
	soundEditor.goUpOneLevel();
}

// ParamLocks --------------------------------------------------------------------------------------------------------

bool ParamLocks::shouldEnterSubmenu() {
	if (instrumentClipView.getLeftMostNotePressed()) {
		return true;
	}
	display->displayPopup("Select Note");
	return false;
}

void ParamLocks::readLocks() {
	locks_ = instrumentClipView.noteLocks.getMenuLocks();

	constexpr int32_t kLen = 32;
	PLACE_SDRAM_BSS static char bufs[note_locks::kMaxLocksPerNote][kLen];
	options_.clear();
	for (int32_t i = 0; i < locks_.size(); i++) {
		int32_t param = locks_.params()[i];
		snprintf(bufs[i], kLen, "%s %d", paramName(param),
		         static_cast<int>(displayValue(param, lockValueToKnobPos(param, locks_.values()[i]))));
		options_.push_back(bufs[i]);
	}
	options_.push_back(l10n::getView(!locks_.empty() ? l10n::String::STRING_FOR_NOTE_LOCKS_CLEAR_ALL
	                                                 : l10n::String::STRING_FOR_NOTE_LOCKS_NONE));
	currentValue_ = std::clamp<int32_t>(currentValue_, 0, numEntries() - 1);
	int32_t maxScroll = std::max<int32_t>(0, numEntries() - kOLEDMenuNumOptionsVisible);
	scrollPos_ = std::clamp<int32_t>(scrollPos_, std::max<int32_t>(0, currentValue_ - kOLEDMenuNumOptionsVisible + 1),
	                                 std::min<int32_t>(currentValue_, maxScroll));
}

void ParamLocks::beginSession(MenuItem* navigatedBackwardFrom) {
	if (navigatedBackwardFrom == nullptr) {
		currentValue_ = 0;
		scrollPos_ = 0;
	}
	readValueAgain();
}

void ParamLocks::readValueAgain() {
	readLocks();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

void ParamLocks::drawPixelsForOled() {
	drawItemsForOled(options_, currentValue_ - scrollPos_, scrollPos_);
}

void ParamLocks::drawValue() {
	display->setScrollingText(options_[currentValue_].data());
}

void ParamLocks::renderInHorizontalMenu(const SlotPosition& slot) {
	// The number of locks, where the other note editor columns show their values
	char text[4];
	snprintf(text, sizeof(text), "%d", static_cast<int>(instrumentClipView.noteLocks.getMenuLocks().size()));
	hid::display::OLED::main.drawStringCentered(text, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
	                                            kTextTitleSpacingX, kTextTitleSizeY, slot.width);
}

void ParamLocks::selectEncoderAction(int32_t offset) {
	int32_t newValue = std::clamp<int32_t>(currentValue_ + offset, 0, numEntries() - 1);
	if (newValue == currentValue_) {
		return;
	}
	currentValue_ = newValue;
	readValueAgain();
}

MenuItem* ParamLocks::selectButtonPress() {
	if (locks_.empty()) {
		return nullptr; // "None": back up
	}
	if (currentValue_ == locks_.size()) { // "Clear all"
		if (instrumentClipView.noteLocks.clearMenuLocks()) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTE_LOCKS_CLEARED));
		}
		currentValue_ = 0;
		readValueAgain();
		return NO_NAVIGATION;
	}
	noteParamLockValueMenu.setParam(locks_.params()[currentValue_]);
	return &noteParamLockValueMenu;
}

void ParamLocks::deletePress() {
	if (currentValue_ >= locks_.size()) {
		return;
	}
	if (instrumentClipView.noteLocks.removeMenuLock(locks_.params()[currentValue_])) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTE_LOCK_DELETED));
	}
	readValueAgain();
}

} // namespace deluge::gui::menu_item::note
