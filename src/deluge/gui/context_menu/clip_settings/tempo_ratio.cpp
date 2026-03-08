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

#include "gui/context_menu/clip_settings/tempo_ratio.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

static constexpr size_t kMaxValue = 16;

static const char* kValueOptions[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
};

// ============ TempoRatioMenu (top-level: Numerator / Denominator) ============

TempoRatioMenu tempoRatioMenu{};

char const* TempoRatioMenu::getTitle() {
	return "Tempo Ratio";
}

std::span<char const*> TempoRatioMenu::getOptions() {
	using enum l10n::String;
	static const char* options[] = {
	    l10n::get(STRING_FOR_TEMPO_RATIO_NUMERATOR),
	    l10n::get(STRING_FOR_TEMPO_RATIO_DENOMINATOR),
	};
	return {options, 2};
}

bool TempoRatioMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;
	this->currentOption = scrollPos = 0;
	return true;
}

void TempoRatioMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
}

bool TempoRatioMenu::acceptCurrentOption() {
	if (this->currentOption == 0) {
		tempoRatioNumerator.clip = clip;
		tempoRatioNumerator.setupAndCheckAvailability();
		openUI(&tempoRatioNumerator);
	}
	else {
		tempoRatioDenominator.clip = clip;
		tempoRatioDenominator.setupAndCheckAvailability();
		openUI(&tempoRatioDenominator);
	}
	return true;
}

// ============ TempoRatioNumeratorMenu (1-16) ============

TempoRatioNumeratorMenu tempoRatioNumerator{};

char const* TempoRatioNumeratorMenu::getTitle() {
	return "Numerator";
}

std::span<char const*> TempoRatioNumeratorMenu::getOptions() {
	return {kValueOptions, kMaxValue};
}

bool TempoRatioNumeratorMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;
	this->currentOption = clip->tempoRatioNumerator - 1; // 0-indexed

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void TempoRatioNumeratorMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
	clip->tempoRatioNumerator = static_cast<uint16_t>(this->currentOption + 1);
	clip->tempoRatioAccumulator = 0;
}

// ============ TempoRatioDenominatorMenu (1-16) ============

TempoRatioDenominatorMenu tempoRatioDenominator{};

char const* TempoRatioDenominatorMenu::getTitle() {
	return "Denominator";
}

std::span<char const*> TempoRatioDenominatorMenu::getOptions() {
	return {kValueOptions, kMaxValue};
}

bool TempoRatioDenominatorMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;
	this->currentOption = clip->tempoRatioDenominator - 1; // 0-indexed

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void TempoRatioDenominatorMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
	clip->tempoRatioDenominator = static_cast<uint16_t>(this->currentOption + 1);
	clip->tempoRatioAccumulator = 0;
}

} // namespace deluge::gui::context_menu::clip_settings
