/*
 * Copyright © 2025 Synthstrom Audible Limited
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

#include "gui/context_menu/clip_settings/time_signature.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

// ============ TimeSignatureMenu (top-level: Numerator / Denominator) ============

TimeSignatureMenu timeSignatureMenu{};

char const* TimeSignatureMenu::getTitle() {
	return "Time Signature";
}

std::span<char const*> TimeSignatureMenu::getOptions() {
	using enum l10n::String;
	static const char* options[] = {
	    l10n::get(STRING_FOR_TIME_SIG_NUMERATOR),
	    l10n::get(STRING_FOR_TIME_SIG_DENOMINATOR),
	};
	return {options, 2};
}

bool TimeSignatureMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;
	this->currentOption = scrollPos = 0;
	return true;
}

void TimeSignatureMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
}

bool TimeSignatureMenu::acceptCurrentOption() {
	if (this->currentOption == 0) {
		timeSignatureNumerator.clip = clip;
		timeSignatureNumerator.setupAndCheckAvailability();
		openUI(&timeSignatureNumerator);
	}
	else {
		timeSignatureDenominator.clip = clip;
		timeSignatureDenominator.setupAndCheckAvailability();
		openUI(&timeSignatureDenominator);
	}
	return true;
}

// ============ TimeSignatureNumeratorMenu (1-32) ============

TimeSignatureNumeratorMenu timeSignatureNumerator{};

static const char* kNumeratorOptions[] = {
    "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10", "11", "12", "13", "14", "15", "16",
    "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32",
};

char const* TimeSignatureNumeratorMenu::getTitle() {
	return "Beats Per Bar";
}

std::span<char const*> TimeSignatureNumeratorMenu::getOptions() {
	return {kNumeratorOptions, 32};
}

bool TimeSignatureNumeratorMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;
	this->currentOption = clip->timeSignature.numerator - 1; // 0-indexed

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void TimeSignatureNumeratorMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
	clip->timeSignature.numerator = static_cast<uint8_t>(this->currentOption + 1);
}

// ============ TimeSignatureDenominatorMenu (2, 4, 8, 16) ============

TimeSignatureDenominatorMenu timeSignatureDenominator{};

static constexpr uint8_t kDenominatorValues[] = {2, 4, 8, 16};
static constexpr size_t kNumDenominators = 4;

char const* TimeSignatureDenominatorMenu::getTitle() {
	return "Beat Unit";
}

std::span<char const*> TimeSignatureDenominatorMenu::getOptions() {
	static const char* options[] = {"Half", "Quarter", "Eighth", "Sixteenth"};
	return {options, kNumDenominators};
}

bool TimeSignatureDenominatorMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;

	// Find index of current denominator
	this->currentOption = 1; // default to quarter
	for (size_t i = 0; i < kNumDenominators; i++) {
		if (kDenominatorValues[i] == clip->timeSignature.denominator) {
			this->currentOption = static_cast<int32_t>(i);
			break;
		}
	}

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void TimeSignatureDenominatorMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
	clip->timeSignature.denominator = kDenominatorValues[this->currentOption];
}

} // namespace deluge::gui::context_menu::clip_settings
