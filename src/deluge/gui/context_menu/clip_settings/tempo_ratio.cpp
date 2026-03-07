#include "gui/context_menu/clip_settings/tempo_ratio.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

struct RatioPreset {
	uint16_t numerator;
	uint16_t denominator;
};

static constexpr RatioPreset kPresets[] = {
    {1, 1}, {1, 2}, {2, 1}, {3, 4}, {4, 3}, {7, 8}, {8, 7}, {2, 3},
    {3, 2}, {5, 4}, {4, 5}, {5, 8}, {8, 5}, {3, 8}, {8, 3},
};

static constexpr size_t kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

// Formatted labels generated once with ratio + percentage
static char labelBuffers[kNumPresets][24];
static const char* labelPtrs[kNumPresets];
static bool labelsGenerated = false;

// Write an unsigned integer to buf, return pointer past last char written
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
	for (size_t i = 0; i < kNumPresets; i++) {
		unsigned int pct = (kPresets[i].numerator * 100 + kPresets[i].denominator / 2) / kPresets[i].denominator;
		char* p = labelBuffers[i];
		p = writeUint(p, kPresets[i].numerator);
		*p++ = ':';
		p = writeUint(p, kPresets[i].denominator);
		*p++ = ' ';
		*p++ = '(';
		p = writeUint(p, pct);
		*p++ = '%';
		*p++ = ')';
		*p = '\0';
		labelPtrs[i] = labelBuffers[i];
	}
	labelsGenerated = true;
}

TempoRatioMenu tempoRatio{};

char const* TempoRatioMenu::getTitle() {
	static char const* title = "Tempo Ratio";
	return title;
}

std::span<char const*> TempoRatioMenu::getOptions() {
	generateLabels();
	return {labelPtrs, kNumPresets};
}

bool TempoRatioMenu::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;

	// Find which preset matches the clip's current ratio
	this->currentOption = 0; // Default to 1:1
	for (size_t i = 0; i < kNumPresets; i++) {
		if (clip->tempoRatioNumerator == kPresets[i].numerator
		    && clip->tempoRatioDenominator == kPresets[i].denominator) {
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
	auto& preset = kPresets[this->currentOption];
	clip->tempoRatioNumerator = preset.numerator;
	clip->tempoRatioDenominator = preset.denominator;
	clip->tempoRatioAccumulator = 0; // Reset accumulator on ratio change
}

} // namespace deluge::gui::context_menu::clip_settings
