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

// All unique reduced fractions N:D with N,D in [1,8], sorted by N/D value.
// Displayed as percentage. Range: 25% (1:4) to 400% (4:1).
// Ratios beyond 4:1 or below 1:4 are excluded as musically extreme.
struct RatioEntry {
	uint16_t numerator;
	uint16_t denominator;
};

static constexpr RatioEntry kRatios[] = {
    {1, 4}, // 25%
    {2, 7}, // 29%
    {1, 3}, // 33%
    {3, 8}, // 38%
    {2, 5}, // 40%
    {3, 7}, // 43%
    {1, 2}, // 50%
    {4, 7}, // 57%
    {3, 5}, // 60%
    {5, 8}, // 63%
    {2, 3}, // 67%
    {5, 7}, // 71%
    {3, 4}, // 75%
    {4, 5}, // 80%
    {5, 6}, // 83%
    {6, 7}, // 86%
    {7, 8}, // 88%
    {1, 1}, // 100%
    {8, 7}, // 114%
    {7, 6}, // 117%
    {6, 5}, // 120%
    {5, 4}, // 125%
    {4, 3}, // 133%
    {7, 5}, // 140%
    {3, 2}, // 150%
    {8, 5}, // 160%
    {5, 3}, // 167%
    {7, 4}, // 175%
    {2, 1}, // 200%
    {7, 3}, // 233%
    {5, 2}, // 250%
    {8, 3}, // 267%
    {3, 1}, // 300%
    {7, 2}, // 350%
    {4, 1}, // 400%
};

static constexpr size_t kNumRatios = sizeof(kRatios) / sizeof(kRatios[0]);

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
		unsigned int pct =
		    (kRatios[i].numerator * 100 + kRatios[i].denominator / 2) / kRatios[i].denominator;
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

	// Find preset matching clip's current ratio, default to 1:1 (index 17)
	this->currentOption = 17;
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
}

} // namespace deluge::gui::context_menu::clip_settings
