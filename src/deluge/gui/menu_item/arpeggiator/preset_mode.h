/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "definitions_cxx.hpp"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

#include <hid/display/oled.h>
#include <numeric>

namespace deluge::gui::menu_item::arpeggiator {
class PresetMode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->preset); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ArpPreset>();

		// If affect-entire button held, do the whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			// If was off, or is now becoming off...
			if (soundEditor.currentArpSettings->mode == ArpMode::OFF || current_value == ArpPreset::OFF) {
				if (getCurrentClip()->isActiveOnOutput() && !soundEditor.editingKitAffectEntire()) {
					kit->cutAllSound();
				}
			}

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				thisDrum->arpSettings.preset = current_value;
				thisDrum->arpSettings.updateSettingsFromCurrentPreset();
				thisDrum->arpSettings.flagForceArpRestart = true;
			}
		}
		// Or, the normal case of just one sound
		else {
			// If was off, or is now becoming off...
			if (soundEditor.currentArpSettings->mode == ArpMode::OFF || current_value == ArpPreset::OFF) {
				if (getCurrentClip()->isActiveOnOutput() && !soundEditor.editingKitAffectEntire()) {
					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

					if (soundEditor.editingKit()) {
						// Drum
						Drum* currentDrum = ((Kit*)getCurrentClip()->output)->selectedDrum;
						if (currentDrum != nullptr) {
							currentDrum->killAllVoices();
						}
					}
					else if (soundEditor.editingCVOrMIDIClip()) {
						getCurrentInstrumentClip()->stopAllNotesForMIDIOrCV(modelStack->toWithTimelineCounter());
					}
					else {
						ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();
						soundEditor.currentSound->allNotesOff(
						    modelStackWithSoundFlags,
						    soundEditor.currentSound
						        ->getArp()); // Must switch off all notes when switching arp on / off
						soundEditor.currentSound->reassessRenderSkippingStatus(modelStackWithSoundFlags);
					}
				}
			}

			soundEditor.currentArpSettings->preset = current_value;
			soundEditor.currentArpSettings->updateSettingsFromCurrentPreset();
			soundEditor.currentArpSettings->flagForceArpRestart = true;
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF),    //<
		    l10n::getView(STRING_FOR_UP),     //<
		    l10n::getView(STRING_FOR_DOWN),   //<
		    l10n::getView(STRING_FOR_BOTH),   //<
		    l10n::getView(STRING_FOR_RANDOM), //<
		    l10n::getView(STRING_FOR_WALK),   //<
		    l10n::getView(STRING_FOR_CUSTOM), //<
		};
	}

	MenuItem* selectButtonPress() override {
		auto current_value = this->getValue<ArpPreset>();
		if (current_value == ArpPreset::CUSTOM) {
			if (soundEditor.editingKitRow()) {
				return &arpOctaveModeToNoteModeMenuForDrums;
			}
			return &arpOctaveModeToNoteModeMenu;
		}
		return nullptr;
	}

	[[nodiscard]] int32_t getColumnSpan() const override { return 2; }
	[[nodiscard]] bool showColumnLabel() const override { return false; }
	[[nodiscard]] bool showPopup() const override { return false; }

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		using namespace deluge::hid::display;
		oled_canvas::Canvas& image = OLED::main;

		const std::vector<std::reference_wrapper<const std::vector<uint8_t>>> bitmaps = [&] {
			switch (this->getValue<ArpPreset>()) {
			case ArpPreset::OFF:
				return std::vector{std::cref(OLED::switcherIconOff)};
			case ArpPreset::UP:
				return std::vector{std::cref(OLED::arpModeIconUp)};
			case ArpPreset::DOWN:
				return std::vector{std::cref(OLED::arpModeIconDown)};
			case ArpPreset::BOTH:
				return std::vector{std::cref(OLED::arpModeIconUp), std::cref(OLED::arpModeIconDown)};
			case ArpPreset::RANDOM:
				return std::vector{std::cref(OLED::diceIcon)};
			case ArpPreset::WALK:
				return std::vector{std::cref(OLED::arpModeIconWalk)};
			case ArpPreset::CUSTOM:
				return std::vector{std::cref(OLED::arpModeIconCustom)};
			}

			return std::vector{std::cref(OLED::switcherIconOff)};
		}();

		const std::string_view option = getOptions(OptType::FULL)[this->getValue()];

		constexpr int32_t paddingBetween = 3;
		constexpr int32_t numBytesTall = 2;
		const int32_t textWidth = image.getStringWidthInPixels(option.data(), kTextSpacingY);
		const int32_t bitmapsWidth = std::accumulate(
		    bitmaps.begin(), bitmaps.end(), 0, [](int32_t acc, auto v) { return acc + v.get().size() / numBytesTall; });

		const int32_t totalWidth = textWidth + paddingBetween + bitmapsWidth;

		// Calc center position
		int32_t x = startX + ((width - totalWidth) / 2) - 1;
		int32_t y = startY + ((height - numBytesTall * 8) / 2) + 1;

		// Draw icons
		for (auto bitmap : bitmaps) {
			image.drawGraphicMultiLine(bitmap.get().data(), x, y, bitmap.get().size() / numBytesTall, numBytesTall * 8,
			                           numBytesTall);
			x += bitmap.get().size() / numBytesTall;
		}

		// Draw mode text
		x += paddingBetween;
		y = startY + ((height - kTextSpacingY) / 2) + 2;
		image.drawString(option.data(), x, y, kTextSpacingX, kTextSpacingY, 0, x + width - kTextSpacingX);
	}
};
} // namespace deluge::gui::menu_item::arpeggiator