#pragma once
#include "gui/menu_item/horizontal_menu.h"
#include "hid/display/oled.h"
#include "segment.h"

using namespace deluge::hid::display;
using namespace gui::menu_item::envelope;

class EnvelopeMenu final : public gui::menu_item::HorizontalMenu {
public:
	using HorizontalMenu::HorizontalMenu;

	void renderOLED() override {
		OLED::main.drawScreenTitle(getTitle(), false);
		drawPixelsForOled();
		OLED::markChanged();
	}

	void drawPixelsForOled() override {
		if (renderingStyle() != HORIZONTAL) {
			return Submenu::drawPixelsForOled();
		}

		// Update the synth-kit-midi-cv buttons LEDs
		paging = splitMenuItemsByPages();
		if (paging.selectedItemPositionOnPage != lastSelectedItemPosition) {
			lastSelectedItemPosition = paging.selectedItemPositionOnPage;
			updateSelectedMenuItemLED(lastSelectedItemPosition);
		}

		// Get the values in 0-50 range
		for (const auto item : items) {
			item->readCurrentValue();
		}
		const int32_t attack = static_cast<Segment*>(items[0])->getValue();
		const int32_t decay = static_cast<Segment*>(items[1])->getValue();
		const int32_t sustain = static_cast<Segment*>(items[2])->getValue();
		const int32_t release = static_cast<Segment*>(items[3])->getValue();

		// Constants
		constexpr int32_t totalWidth = OLED_MAIN_WIDTH_PIXELS;
		constexpr int32_t totalHeight = OLED_MAIN_VISIBLE_HEIGHT - kTextTitleSizeY - 6;
		constexpr int32_t padding = 4;
		constexpr int32_t drawX = padding;
		constexpr int32_t drawY = OLED_MAIN_TOPMOST_PIXEL + kTextTitleSizeY + padding + 3;
		constexpr int32_t drawWidth = totalWidth - 2 * padding;
		constexpr int32_t drawHeight = totalHeight - 2 * padding;
		constexpr float maxSegmentWidth = drawWidth / 4;

		// Calculate widths
		const float attackWidth = (attack / 50.0f) * maxSegmentWidth;
		const float decayWidth = (decay / 50.0f) * maxSegmentWidth;

		// X positions
		const float attackX = drawX + attackWidth;
		const float decayX = attackX + decayWidth;
		constexpr float sustainX = drawX + (maxSegmentWidth * 3); // Fix sustainX
		const float releaseX =
		    sustainX + ((release / 50.0f) * (drawX + drawWidth - sustainX)); // Make releaseX dynamic, right of sustain

		// Y positions
		constexpr float baseY = drawY + drawHeight;
		constexpr float peakY = drawY;
		const float sustainY = baseY - ((sustain / 50.0f) * drawHeight);

		oled_canvas::Canvas& image = OLED::main;

		// Draw stage lines
		image.drawLine(drawX, baseY, attackX, peakY);
		image.drawLine(attackX, peakY, decayX, sustainY);
		image.drawLine(decayX, sustainY, sustainX, sustainY);
		image.drawLine(sustainX, sustainY, releaseX, baseY);
		image.drawLine(releaseX, baseY, drawX + drawWidth, baseY);

		// Draw stage transition point dotted lines
		for (int32_t y = OLED_MAIN_VISIBLE_HEIGHT; y >= drawY; y -= 4) {
			image.drawPixel(attackX, y);
			image.drawPixel(decayX, y);
			image.drawPixel(sustainX, y);
		}

		auto drawTransitionSquare = [&, selectedX = int32_t{-1}, selectedY = int32_t{-1}](const float x, const float y,
		                                                                                  const int32_t pos) mutable {
			const int32_t ix = static_cast<int32_t>(x);
			const int32_t iy = static_cast<int32_t>(y);

			if (pos != paging.selectedItemPositionOnPage && ix == selectedX && iy == selectedY) {
				// Overlap occurred, skip drawing
				return;
			}

			// Clear region inside
			constexpr int32_t squareSize = 2, innerSquareSize = squareSize - 1;
			image.clearAreaExact(ix - innerSquareSize, iy - innerSquareSize, ix + innerSquareSize,
			                     iy + innerSquareSize);

			if (pos == paging.selectedItemPositionOnPage) {
				// Invert region inside to highlight selection
				selectedX = ix, selectedY = iy;
				image.invertArea(ix - innerSquareSize, (squareSize * 2) - 1, iy - innerSquareSize,
				                 iy + innerSquareSize);
			}

			// Draw a transition square
			image.drawRectangle(ix - squareSize, iy - squareSize, ix + squareSize, iy + squareSize);
		};

		drawTransitionSquare(attackX, peakY, 0);                             // Attack → Decay
		drawTransitionSquare(decayX, sustainY, 1);                           // Decay → Sustain
		drawTransitionSquare(decayX + (sustainX - decayX) / 2, sustainY, 2); // Sustain
		drawTransitionSquare(releaseX, baseY, 3);                            // Release → End
	}
};