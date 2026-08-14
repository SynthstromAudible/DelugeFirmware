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
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "string.h"

namespace deluge::gui::menu_item::submenu {
class ActualSource final : public HorizontalMenu {
public:
	ActualSource(l10n::String newName, std::span<MenuItem*> newItems, uint8_t sourceId)
	    : HorizontalMenu(newName, newItems), source_id_{sourceId} {}

	[[nodiscard]] std::string_view getName() const override { return getNameOrTitle(title); }
	[[nodiscard]] std::string_view getTitle() const override {
		// On a multi-zone source everything this page edits applies to one keyboard zone, which the
		// title otherwise never named. There is only room for about eleven characters beside the page
		// counter, so the oscillator's name gives way to "O1" - "Oscillator 1 C3-F#4" would be drawn
		// straight over the counter. Held across every page too: the usual switch to "Osc1 sample" on
		// page 2 made the title move about while scrolling, right when it is being relied on for
		// context.
		std::string zone;
		soundEditor.appendCurrentZoneDescription(zone, source_id_);
		if (!zone.empty()) {
			name_or_title_ = "O";
			name_or_title_ += static_cast<char>('1' + source_id_);
			name_or_title_ += zone;
			return name_or_title_;
		}

		auto l10nString = title;

		// If we are in the sample oscillator menu and not on the first page,
		// we display OSC1/2 SAMPLE as the menu title
		const auto& source = soundEditor.currentSound->sources[source_id_];
		if (renderingStyle() == HORIZONTAL && source.oscType == OscType::SAMPLE && paging.visiblePageNumber > 0) {
			l10nString = l10n::String::STRING_FOR_OSC_SAMPLE_MENU_TITLE;
		}

		return getNameOrTitle(l10nString);
	}

	// 7seg Only
	void drawName() override {
		if (soundEditor.currentSound->getSynthMode() == SynthMode::FM) {
			char buffer[5];
			strcpy(buffer, "CAR");
			intToString(source_id_ + 1, buffer + 3);
			display->setText(buffer);
		}
		else {
			Submenu::drawName();
		}
	}

private:
	uint8_t source_id_;
	mutable std::string name_or_title_;

	std::string_view getNameOrTitle(l10n::String l10n) const {
		std::string result = l10n::get(l10n);
		asterixToInt(result.data(), source_id_ + 1);
		name_or_title_ = result;
		return name_or_title_;
	}
};

} // namespace deluge::gui::menu_item::submenu
