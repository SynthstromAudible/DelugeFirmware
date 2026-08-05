/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "io/midi/midi_engine.h"
#include "util/d_stringbuf.h"
#include "util/misc.h"
#include <array>

namespace deluge::gui::menu_item::midi {
class FollowFeedbackChannelType final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(midiEngine.midiFollowFeedbackChannelType); }
	void writeCurrentValue() override {
		midiEngine.midiFollowFeedbackChannelType = this->getValue<MIDIFollowFeedbackChannelType>();
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_NONE),
		    l10n::getView(STRING_FOR_FOLLOW_CHANNEL_A),
		    l10n::getView(STRING_FOR_FOLLOW_CHANNEL_B),
		    l10n::getView(STRING_FOR_FOLLOW_CHANNEL_C),
		    getTrackOptionForFeedback(),
		    getTrackAndChannelOption(0, STRING_FOR_FOLLOW_CHANNEL_A),
		    getTrackAndChannelOption(1, STRING_FOR_FOLLOW_CHANNEL_B),
		    getTrackAndChannelOption(2, STRING_FOR_FOLLOW_CHANNEL_C),
		};
	}

private:
	/// Reuse the localized Track channel label for feedback menus without the Track 1-16 placeholder suffix.
	static std::string_view trimTrackNumberPlaceholder(std::string_view trackOption) {
		constexpr std::string_view kTrackNumberPlaceholder = "**";
		if (trackOption.size() >= kTrackNumberPlaceholder.size()
		    && trackOption.substr(trackOption.size() - kTrackNumberPlaceholder.size()) == kTrackNumberPlaceholder) {
			trackOption.remove_suffix(kTrackNumberPlaceholder.size());
		}
		return trackOption;
	}

	std::string_view getTrackOptionForFeedback() {
		return trimTrackNumberPlaceholder(l10n::getView(l10n::String::STRING_FOR_FOLLOW_CHANNEL_TRACK));
	}

	/// Use compact separators for 7SEG feedback labels, e.g. "TR+A", while keeping OLED labels readable.
	static std::string_view getTrackAndChannelSeparator(std::string_view trackOption) {
		const auto sevenSegmentTrackOption = trimTrackNumberPlaceholder(
		    l10n::getView(l10n::built_in::seven_segment, l10n::String::STRING_FOR_FOLLOW_CHANNEL_TRACK));
		return trackOption == sevenSegmentTrackOption ? "+" : " + ";
	}

	/// Build localized labels for combined feedback modes without adding separate l10n strings.
	/// OLED uses the full channel labels, while 7SEG uses compact labels such as "TR+A".
	std::string_view getTrackAndChannelOption(size_t optionIndex, l10n::String channelString) {
		StringBuf option{trackAndChannelOptionBuffers_[optionIndex].data(),
		                 trackAndChannelOptionBuffers_[optionIndex].size()};
		const auto trackOption = getTrackOptionForFeedback();
		option.append(trackOption);
		option.append(getTrackAndChannelSeparator(trackOption));
		option.append(l10n::getView(channelString));
		return option;
	}

	static constexpr size_t kTrackAndChannelOptionBufferSize = 64;
	std::array<std::array<char, kTrackAndChannelOptionBufferSize>, 3> trackAndChannelOptionBuffers_{};
};
} // namespace deluge::gui::menu_item::midi
