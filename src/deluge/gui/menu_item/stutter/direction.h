/*
 * Copyright (c) 2014-2025 Synthstrom Audible Limited
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
#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

#include <hid/display/oled.h>

namespace deluge::gui::menu_item::stutter {

class StutterDirection final : public Selection {
public:
	using Selection::Selection;

	enum Direction : uint8_t { USE_SONG_STUTTER = 0, FORWARD, REVERSED, FORWARD_PING_PONG, REVERSED_PING_PONG };

	deluge::vector<std::string_view> getOptions(OptType optType = OptType::FULL) override {
		using namespace deluge::l10n;

		deluge::vector<std::string_view> result;

		if (showUseSongOption()) {
			result.push_back(l10n::getView(optType == OptType::SHORT ? String::STRING_FOR_USE_SONG_SHORT
			                                                         : String::STRING_FOR_USE_SONG));
		}
		result.push_back(l10n::getView(String::STRING_FOR_FORWARD));
		result.push_back(l10n::getView(String::STRING_FOR_REVERSED));
		result.push_back(l10n::getView(String::STRING_FOR_FORWARD_PING_PONG));
		result.push_back(l10n::getView(String::STRING_FOR_REVERSED_PING_PONG));

		return result;
	}

	void readCurrentValue() override {
		const auto* stutter = &soundEditor.currentModControllable->stutterConfig;

		if (showUseSongOption() && stutter->useSongStutter) {
			setValue(USE_SONG_STUTTER);
		}
		else if (stutter->reversed && stutter->pingPong) {
			setValue(REVERSED_PING_PONG);
		}
		else if (stutter->reversed) {
			setValue(REVERSED);
		}
		else if (stutter->pingPong) {
			setValue(FORWARD_PING_PONG);
		}
		else {
			setValue(FORWARD);
		}
	}

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		Direction value = getValue();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					applyOptionToStutterConfig(value, soundDrum->stutterConfig);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			applyOptionToStutterConfig(value, soundEditor.currentModControllable->stutterConfig);
		}
	}

private:
	Direction getValue() {
		const auto value = Selection::getValue();
		const auto shift = showUseSongOption() ? 0 : 1;
		return static_cast<Direction>(value + shift);
	}

	void setValue(Direction value) {
		const auto shift = showUseSongOption() ? 0 : 1;
		return Selection::setValue(value - shift);
	}

	static bool showUseSongOption() { return !soundEditor.currentModControllable->isSong(); }

	static void applyOptionToStutterConfig(const Direction value, StutterConfig& stutter) {
		stutter.useSongStutter = value == USE_SONG_STUTTER;
		stutter.reversed = value == REVERSED || value == REVERSED_PING_PONG;
		stutter.pingPong = value == FORWARD_PING_PONG || value == REVERSED_PING_PONG;

		if (stutter.useSongStutter) {
			stutter.quantized = currentSong->globalEffectable.stutterConfig.quantized;
			stutter.reversed = currentSong->globalEffectable.stutterConfig.reversed;
			stutter.pingPong = currentSong->globalEffectable.stutterConfig.pingPong;
		}
	}

	void getValueForPopup(StringBuf& valueBuf) override {
		const auto value = getValue();
		valueBuf.append(getOptions(OptType::SHORT)[value]);
	}

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		using namespace deluge::hid::display;
		oled_canvas::Canvas& image = OLED::main;

		const auto value = getValue();

		if (value == USE_SONG_STUTTER) {
			const auto& icon = OLED::songIcon;
			constexpr int32_t songIconWidth = 9;

			// Draw a song icon centered
			const int32_t x = startX + ((width - songIconWidth) / 2) - 2;
			const int32_t y = startY + ((height - songIconWidth) / 2);
			return image.drawGraphicMultiLine(icon, x, y, songIconWidth);
		}

		constexpr int32_t numBytesTall = 2;
		const bool reversed = value == REVERSED || value == REVERSED_PING_PONG;
		const auto icon =
		    reversed ? reverseBitmap(OLED::stutterDirectionIcon, numBytesTall) : OLED::stutterDirectionIcon;

		constexpr int32_t iconHeight = numBytesTall * 8;
		const int32_t iconWidth = icon.size() / numBytesTall;

		if (value == FORWARD_PING_PONG || value == REVERSED_PING_PONG) {
			// Draw the "P" indicator and the icon centered
			constexpr int32_t iconOffset = 4;
			image.drawChar('P', startX + 3, startY + 3, 5, kTextSpacingY);
			image.drawGraphicMultiLine(reversed ? icon.data() : icon.data() + iconOffset * numBytesTall, startX + 11,
			                           startY, iconWidth - iconOffset, iconHeight, numBytesTall);
		}
		else {
			// Draw the icon centered
			const int32_t x = startX + (width - iconWidth) / 2 - 1;
			image.drawGraphicMultiLine(icon.data(), x, startY, iconWidth, iconHeight, numBytesTall);
		}
	}

	static std::vector<uint8_t> reverseBitmap(const std::vector<uint8_t>& bitmap, int32_t numBytesTall) {
		const int32_t columnCount = bitmap.size() / numBytesTall;

		std::vector<uint8_t> output(bitmap.size());

		for (size_t col = 0; col < columnCount; ++col) {
			const int32_t inputIndex = col * numBytesTall;
			const int32_t reversedCol = columnCount - 1 - col;
			const int32_t outputIndex = reversedCol * numBytesTall;

			for (int32_t byte = 0; byte < numBytesTall; ++byte) {
				uint8_t b = bitmap[inputIndex + byte];
				const uint8_t reversed = std::bit_cast<uint8_t>(b);
				output[outputIndex + byte] = reversed;
			}
		}

		return output;
	}
};

} // namespace deluge::gui::menu_item::stutter
