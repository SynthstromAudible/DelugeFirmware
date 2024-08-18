#include "gui/menu_item/lfo/type.h"

#include "hid/display/oled.h"
#include "modulation/lfo.h"

#include "io/debug/log.h"

namespace deluge::gui::menu_item::lfo {

void Type::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	// We label with the LFO type name instead of "TYPE".
	std::string_view name = getOptions()[getValue()];
	size_t len = std::min((size_t)(width / kTextSpacingX), name.size());
	// If we can fit the whole name, we do, if we can't we chop one letter off. It just looks and
	// feels better, at least with the names we have now.
	if (name.size() > len) {
		len -= 1;
	}
	std::string_view shortName(name.data(), len);
	image.drawString(shortName, startX, startY, kTextSpacingX, kTextSpacingY, 0, startX + width);

	// Draw the LFO shape
	int32_t plotHeight = height - kTextSpacingY - 2;
	int32_t plotWidth = width - 2;
	int32_t baseline = OLED_MAIN_VISIBLE_HEIGHT - 1;
	LFOType wave = soundEditor.currentSound->lfoConfig[lfoId_].waveType;
	LFOConfig config{wave};
	LFO lfo;
	if (lfoId_ == 0) {
		lfo.setGlobalInitialPhase(config);
	}
	else {
		lfo.setLocalInitialPhase(config);
	}
	int32_t phaseIncrement;
	int32_t extraScaling = 1;
	if (wave == LFOType::RANDOM_WALK) {
		phaseIncrement = UINT32_MAX / (plotWidth / 12);
		// RANDOM walk range is smaller, so we magnify for display.
		extraScaling = 5;
	}
	else if (wave == LFOType::SAMPLE_AND_HOLD) {
		phaseIncrement = UINT32_MAX / (plotWidth / 12);
	}
	else {
		phaseIncrement = UINT32_MAX / (plotWidth / 3);
	}
	bool first = true;
	int32_t prevY = 0;
	float yScale = ((int64_t)INT32_MAX * 2) / plotHeight;
	for (size_t x = 0; x < plotWidth; x++) {
		int32_t yBig = lfo.render(1, wave, phaseIncrement) * extraScaling;
		int32_t y = (yBig / yScale) + plotHeight / 2;
		// We're subtracting the Y because the screen coordinates grow down.
		image.drawVerticalLine(startX + x, baseline - y - 1, baseline - y);
		if (!first) {
			int32_t delta = y - prevY;
			if (delta > 1) {
				image.drawVerticalLine(startX + x, baseline - y, baseline - prevY);
			}
			else if (delta < -1) {
				image.drawVerticalLine(startX + x, baseline - prevY, baseline - y);
			}
		}
		first = false;
		prevY = y;
	}
}

} // namespace deluge::gui::menu_item::lfo
