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

namespace deluge::gui::context_menu::clip_settings {

// Label buffers: "XXX% (N:D)" — max ~12 chars
static char labelBuffers[kNumRatios][16];
static const char* labelPtrs[kNumRatios];
static bool labelsGenerated = false;

static char* writeUint(char* buf, unsigned int val) {
	if (val == 0) {
		*buf++ = '0';
		return buf;
	}
	char tmp[10];
	int len = 0;
	while (val > 0) {
		tmp[len++] = '0' + (val % 10);
		val /= 10;
	}
	for (int j = len - 1; j >= 0; j--) {
		*buf++ = tmp[j];
	}
	return buf;
}

static void generateLabels() {
	if (labelsGenerated) {
		return;
	}
	for (size_t i = 0; i < kNumRatios; i++) {
		unsigned int pct = (kRatios[i].numerator * 100 + kRatios[i].denominator / 2) / kRatios[i].denominator;
		char* p = labelBuffers[i];
		p = writeUint(p, pct);
		*p++ = '%';
		*p++ = ' ';
		*p++ = '(';
		p = writeUint(p, kRatios[i].numerator);
		*p++ = ':';
		p = writeUint(p, kRatios[i].denominator);
		*p++ = ')';
		*p = '\0';
		labelPtrs[i] = labelBuffers[i];
	}
	labelsGenerated = true;
}

TempoRatioMenu tempoRatioMenu{};

char const* TempoRatioMenu::getTitle() {
	return "Tempo Ratio";
}

std::span<char const*> TempoRatioMenu::getOptions() {
	generateLabels();
	return {labelPtrs, kNumRatios};
}

bool TempoRatioMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;

	// Find preset matching clip's current ratio, default to 1:1
	this->currentOption = static_cast<int32_t>(kRatioDefault);
	for (size_t i = 0; i < kNumRatios; i++) {
		if (clip->tempoRatioNumerator == kRatios[i].numerator
		    && clip->tempoRatioDenominator == kRatios[i].denominator) {
			this->currentOption = static_cast<int32_t>(i);
			break;
		}
	}

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void TempoRatioMenu::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
	auto& ratio = kRatios[this->currentOption];
	clip->tempoRatioNumerator = ratio.numerator;
	clip->tempoRatioDenominator = ratio.denominator;
	clip->tempoRatioAccumulator = 0;
	clip->expectEvent(); // Recalculate scheduling for new ratio
}

} // namespace deluge::gui::context_menu::clip_settings
