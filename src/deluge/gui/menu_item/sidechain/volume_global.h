#pragma once

#include <gui/menu_item/unpatched_param/updating_reverb_params.h>

namespace deluge::gui::menu_item::sidechain {
class GlobalVolume final : public unpatched_param::UpdatingReverbParams {
public:
	using UpdatingReverbParams::UpdatingReverbParams;

	bool showColumnLabel() const override { return false; }

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		oled_canvas::Canvas& image = OLED::main;

		constexpr int32_t barWidth = 26;
		constexpr int32_t dotsInterval = 4;
		constexpr int32_t xOffset = 4;

		const int32_t leftPadding = (width - barWidth) / 2;
		const int32_t minX = startX + leftPadding;
		const int32_t maxX = minX + barWidth;
		const int32_t minY = startY + 3;
		const int32_t maxY = startY + height - 4;

		// Draw the dots at left and right sides
		for (int32_t y = minY; y <= maxY; y += dotsInterval) {
			if (y == minY + dotsInterval * 3) {
				// small offset for better balance
				y++;
			}
			image.drawPixel(minX + xOffset, y);
			image.drawPixel(maxX - xOffset, y);
		}

		// Calculate shape height based on value
		const float normalizedValue = getValue() / 50.0f;
		const int32_t barHeight = maxY - minY;
		const int32_t fillHeight = normalizedValue * barHeight;

		const int32_t yOffset = (barHeight - fillHeight) / 2;
		const int32_t yStart = minY + yOffset;
		const int32_t yEnd = yStart + fillHeight;
		const int32_t y0 = yEnd;
		const int32_t y1 = yStart;

		// Draw a sidechain level shape
		image.drawLine(minX + xOffset, y0, maxX - xOffset, y1, {.thick = true});
		image.drawVerticalLine(minX + xOffset, yStart, yEnd);
		image.drawVerticalLine(minX + xOffset + 1, yStart, yEnd);
		image.drawHorizontalLine(y1 - 1, maxX - xOffset, maxX - 1);
		image.drawHorizontalLine(y1, maxX - xOffset, maxX - 1);
		image.drawHorizontalLine(y1 - 1, minX + 1, minX + xOffset + 1);
		image.drawHorizontalLine(y1, minX + 1, minX + xOffset + 1);
	}
};
} // namespace deluge::gui::menu_item::sidechain
