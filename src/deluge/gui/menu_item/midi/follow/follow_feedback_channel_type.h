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
#include "util/etl_string.h"
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

	/// Build localized labels for combined feedback modes without adding separate l10n strings.
	/// The returned view points into trackAndChannelOptionBuffers_, so it stays valid for as long
	/// as this menu item does.
	std::string_view getTrackAndChannelOption(size_t optionIndex, l10n::String channelString) {
		auto& option = trackAndChannelOptionBuffers_[optionIndex];
		option.clear();
		const auto trackOption = getTrackOptionForFeedback();
		const auto channelOption = l10n::getView(channelString);
		option.append(trackOption.data(), trackOption.size());
		option.append(" + ");
		option.append(channelOption.data(), channelOption.size());
		return {option.data(), option.size()};
	}

	static constexpr size_t kTrackAndChannelOptionBufferSize = 64;
	std::array<etl::string<kTrackAndChannelOptionBufferSize>, 3> trackAndChannelOptionBuffers_{};
};
} // namespace deluge::gui::menu_item::midi
