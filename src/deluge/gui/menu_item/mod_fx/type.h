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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::mod_fx {

class Type : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->modFXType_); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ModFXType>();
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			bool some_error = false;
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					if (!soundDrum->setModFXType(current_value)) {
						some_error = true;
					}
				}
			}
			if (some_error) {
				display->displayError(Error::INSUFFICIENT_RAM);
			}
		}
		// Or, the normal case of just one sound
		else {
			if (!soundEditor.currentModControllable->setModFXType(current_value)) {
				display->displayError(Error::INSUFFICIENT_RAM);
			}
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return modfx::getModNames();
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		options.show_label = false;
		options.show_notification = false;
		options.occupied_slots = 4; // Occupy the whole page in the horizontal menu
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		DEF_STACK_STRING_BUF(shortOpt, kShortStringBufferSize);
		getShortOption(shortOpt);

		constexpr int32_t arrow_space = 10;

		// Get the main text width and trim if needed
		int32_t text_width = image.getStringWidthInPixels(shortOpt.c_str(), kTextSpacingY);
		while (text_width >= slot.width - 2 * arrow_space) {
			shortOpt.truncate(shortOpt.size() - 1);
			text_width = image.getStringWidthInPixels(shortOpt.c_str(), kTextSpacingY);
		}

		const int32_t text_start_x = slot.start_x + (slot.width - text_width) / 2 + 1;
		const int32_t text_start_y = slot.start_y + (slot.height - kTextSpacingY) / 2 + 1;

		// Draw the left arrow
		if (getValue() > 0) {
			image.drawString("<", slot.start_x + 5, text_start_y, kTextTitleSpacingX, kTextTitleSizeY);
		}

		// Draw main text
		image.drawString(shortOpt.c_str(), text_start_x, text_start_y, kTextSpacingX, kTextSpacingY);

		// Highlight the text
		constexpr int32_t highlight_offset = 21;
		switch (FlashStorage::accessibilityMenuHighlighting) {
		case MenuHighlighting::FULL_INVERSION:
			image.invertAreaRounded(slot.start_x + highlight_offset, slot.width - highlight_offset * 2,
			                        text_start_y - 2, text_start_y + kTextSpacingY + 1);
			break;
		case MenuHighlighting::PARTIAL_INVERSION:
		case MenuHighlighting::NO_INVERSION:
			image.drawRectangleRounded(slot.start_x + highlight_offset, text_start_y - 4,
			                           slot.start_x + slot.width - highlight_offset, text_start_y + kTextSpacingY + 3,
			                           oled_canvas::BorderRadius::BIG);
			break;
		}

		// Draw the right arrow
		if (getValue() < size() - 1) {
			image.drawString(">", OLED_MAIN_WIDTH_PIXELS - arrow_space, text_start_y, kTextTitleSpacingX,
			                 kTextTitleSizeY);
		}
	}
};
} // namespace deluge::gui::menu_item::mod_fx
