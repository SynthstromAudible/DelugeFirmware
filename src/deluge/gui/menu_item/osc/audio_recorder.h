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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/menu_item.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "processing/sound/sound.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::osc {
class AudioRecorder final : public MenuItem, FormattedTitle {
public:
	AudioRecorder(l10n::String newName, uint8_t sourceId)
	    : MenuItem(newName), FormattedTitle(newName, sourceId + 1), source_id_{sourceId} {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }
	[[nodiscard]] std::string_view getName() const override { return FormattedTitle::title(); }

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		soundEditor.shouldGoUpOneLevelOnBegin = true;
		soundEditor.setCurrentSource(source_id_);

		if (parentMenuHeadingTo != nullptr && menuItemHeadingTo != nullptr) {
			parentMenuHeadingTo->focusChild(menuItemHeadingTo);
			soundEditor.navigationDepth = 0;
			soundEditor.menuItemNavigationRecord[soundEditor.navigationDepth] = parentMenuHeadingTo;
			soundEditor.shouldGoUpOneLevelOnBegin = false;

			parentMenuHeadingTo = nullptr;
			menuItemHeadingTo = nullptr;
		}

		if (bool success = openUI(&audioRecorder); !success) {
			if (getCurrentUI() == &soundEditor) {
				soundEditor.goUpOneLevel();
			}
			return uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
		}

		audioRecorder.process();
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t) override {
		const auto sound = static_cast<Sound*>(modControllable);
		return sound->getSynthMode() == SynthMode::SUBTRACTIVE;
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t,
	                                             ::MultiRange** currentRange) override {
		if (!isRelevant(modControllable, source_id_)) {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_RECORD_AUDIO_FM_MODE));
			return MenuPermission::NO;
		}

		Sound* sound = static_cast<Sound*>(modControllable);
		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, source_id_, currentRange);
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		options.show_label = false;
		options.show_notification = false;
		options.allow_to_begin_session = true;
		options.occupied_slots = 2;
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		using namespace hid::display;
		oled_canvas::Canvas& image = OLED::main;

		// Draw "rec" part
		const uint8_t start_x = slot.start_x + 8;
		const uint8_t start_y = slot.start_y + kHorizontalMenuSlotYOffset + 6;
		constexpr uint8_t circle_radius = 3;
		image.drawCircle(start_x + circle_radius + 1, start_y + circle_radius + 1, circle_radius, true);
		image.drawString("rec", start_x + circle_radius * 2 + 5, start_y, kTextSpacingX, kTextSpacingY);

		// Draw the source number
		DEF_STACK_STRING_BUF(buf, kShortStringBufferSize);
		buf.appendInt(source_id_ + 1);
		uint8_t source_x = slot.start_x + slot.width - kTextBigSpacingX - 7;
		uint8_t source_y = slot.start_y + kHorizontalMenuSlotYOffset + 4;

		const bool full_inversion = FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::FULL_INVERSION;
		if (full_inversion || parent->getCurrentItem() == this) {
			image.drawString(buf.data(), source_x, source_y, kTextBigSpacingX, kTextBigSizeY);
		}
		else {
			image.drawString(buf.data(), source_x - 1, source_y + 2, kTextSpacingX, kTextSpacingY);
		}

		// Draw separator in the middle
		if (!full_inversion && source_id_ == 0) {
			const uint8_t y0 = slot.start_y + kHorizontalMenuSlotYOffset + 1;
			const uint8_t y1 = y0 + 18;
			for (uint8_t y = y0; y <= y1; y += 2) {
				image.drawPixel(slot.start_x + slot.width - 1, y);
			}
		}
	}

	HorizontalMenu* parentMenuHeadingTo{nullptr};
	MenuItem* menuItemHeadingTo{nullptr};

private:
	uint8_t source_id_;
};
} // namespace deluge::gui::menu_item::osc
