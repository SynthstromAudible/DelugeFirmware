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
#include "gui/menu_item/menu_item.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "processing/sound/sound.h"

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

	[[nodiscard]] bool allowToBeginSessionFromHorizontalMenu() override { return true; }
	[[nodiscard]] int32_t getColumnSpan() const override { return 2; }
	[[nodiscard]] bool showColumnLabel() const override { return false; }
	[[nodiscard]] bool showNotification() const override { return false; }

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		using namespace hid::display;
		oled_canvas::Canvas& image = OLED::main;

		// Draw record icon
		int32_t x = startX + 5;
		int32_t y = startY + 5;
		const Icon& rec_icon = OLED::recordIcon;
		image.drawIcon(rec_icon, x, y);

		// Draw a "rec" string nearby
		x += rec_icon.width + 3;
		y += 3;
		image.drawString("rec", x, y, kTextSpacingX, kTextSpacingY);

		// Draw the arrow icon next
		x += kTextSpacingX * 3 + 3;
		image.drawGraphicMultiLine(OLED::submenuArrowIconBold, x, y, 7);

		DEF_STACK_STRING_BUF(sourceNumberBuf, kShortStringBufferSize);
		sourceNumberBuf.appendInt(source_id_ + 1);

		if (FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::FULL_INVERSION
		    || parent->getCurrentItem() == this) {
			// Draw a big source number
			x = startX + width - kTextBigSpacingX - 3;
			y = startY + 6;
			image.drawString(sourceNumberBuf.data(), x, y, kTextBigSpacingX, kTextBigSizeY);
		}
		else {
			// Draw a smaller source number
			x = startX + width - kTextBigSpacingX - 2;
			y = startY + 8;
			image.drawString(sourceNumberBuf.data(), x, y, kTextSpacingX, kTextSpacingY);
		}
	}

private:
	uint8_t source_id_;
};
} // namespace deluge::gui::menu_item::osc
