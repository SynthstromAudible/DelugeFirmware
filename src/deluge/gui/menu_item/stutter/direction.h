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
#include "model/fx/stutterer.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound_drum.h"

#include <hid/display/oled.h>

namespace deluge::gui::menu_item::stutter {

class StutterDirection final : public Selection {
public:
	using Selection::Selection;

	// Classic mode directions
	enum Direction : uint8_t { USE_SONG_STUTTER = 0, FORWARD, REVERSED, FORWARD_PING_PONG, REVERSED_PING_PONG };

	// Scatter mode: Latch behavior
	enum ScatterLatch : uint8_t { MOMENTARY = 0, LATCH };

	deluge::vector<std::string_view> getOptions(OptType optType = OptType::FULL) override {
		using namespace deluge::l10n;

		deluge::vector<std::string_view> result;

		// In scatter mode, show Latch/Momentary options instead
		if (isScatterMode()) {
			result.push_back("Momentary"); // Normal: releases when you let go
			result.push_back("Latch");     // Stays on after release
			return result;
		}

		// Classic mode: direction options
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

		// Scatter mode: read latch state
		if (isScatterMode()) {
			Selection::setValue(stutter->latch ? LATCH : MOMENTARY);
			return;
		}

		// Classic mode: read direction
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

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Only relevant for Classic and Burst modes
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode == ScatterMode::Classic || mode == ScatterMode::Burst;
	}

	void writeCurrentValue() override {
		// Scatter mode: write latch state (shouldn't happen with isRelevant, but keep for safety)
		if (isScatterMode()) {
			bool latch = (Selection::getValue() == LATCH);

			if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
				Kit* kit = getCurrentKit();
				for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
					if (thisDrum->type == DrumType::SOUND) {
						auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
						soundDrum->stutterConfig.latch = latch;
						// Switching to momentary while scattering should end scatter
						if (!latch && stutterer.isStuttering(soundDrum)) {
							soundDrum->endStutter(nullptr);
						}
					}
				}
			}
			else {
				soundEditor.currentModControllable->stutterConfig.latch = latch;
				// Switching to momentary while scattering should end scatter
				if (!latch && stutterer.isStuttering(soundEditor.currentModControllable)) {
					soundEditor.currentModControllable->endStutter(nullptr);
				}
			}
			// Also update global stutterer for live changes
			stutterer.setLiveLatch(latch);
			return;
		}

		// Classic mode: write direction
		Direction value = getValue();

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					applyOptionToStutterConfig(value, soundDrum->stutterConfig);
				}
			}
		}
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

	static bool isScatterMode() {
		return soundEditor.currentModControllable->stutterConfig.scatterMode != ScatterMode::Classic;
	}

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

	void getNotificationValue(StringBuf& valueBuf) override {
		const auto value = Selection::getValue();
		valueBuf.append(getOptions(OptType::SHORT)[value]);
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		using namespace deluge::hid::display;
		oled_canvas::Canvas& image = OLED::main;

		// Scatter mode: show Latch/Momentary text
		if (isScatterMode()) {
			const char* label = (Selection::getValue() == LATCH) ? "Latch" : "Mom";
			image.drawStringCentered(label, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX,
			                         kTextSpacingY, slot.width);
			return;
		}

		// Classic mode: show direction icons
		const auto value = getValue();

		if (value == USE_SONG_STUTTER) {
			image.drawStringCentered("song", slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX,
			                         kTextSpacingY, slot.width);
			return;
		}

		// Draw the direction icon centered
		const bool reversed = value == REVERSED || value == REVERSED_PING_PONG;
		image.drawIconCentered(OLED::directionIcon, slot.start_x, slot.width, slot.start_y + kHorizontalMenuSlotYOffset,
		                       reversed);

		if (value == FORWARD_PING_PONG || value == REVERSED_PING_PONG) {
			// Draw ping-pong dots
			const int32_t center_x = slot.start_x + slot.width / 2;
			image.drawPixel(center_x, slot.start_y + kHorizontalMenuSlotYOffset);
			image.drawPixel(center_x, slot.start_y + kHorizontalMenuSlotYOffset + 7);
		}
	}
};

} // namespace deluge::gui::menu_item::stutter
