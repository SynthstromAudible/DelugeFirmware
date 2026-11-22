/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// @file
/// @brief What kind of track an output is, and the kind's name as the menus and the track popup show it.

#include "project/midi_channels.h"

#include <cstdint>
#include <string_view>

namespace deluge::project {

/// @brief What kind of track an output is.
enum class TrackKind : uint8_t {
	SYNTH,    ///< a synth track
	KIT,      ///< a kit track
	MIDI_OUT, ///< a MIDI track, playing an external device
	CV,       ///< a CV/gate track
	AUDIO,    ///< an audio track
};

/// @brief A track kind's name, as the menus and the track popup show it.
/// @param kind The track's kind.
/// @param channel What tells the names within a kind apart. For a MIDI track, its channel: an ordinary channel below
///                kMidiChannelLowerZone, an MPE zone at it or kMidiChannelUpperZone, and an internal destination
///                above them. For an audio track, its mode as an index of kAudioModeWords, read as its low byte; a
///                mode past the last names the kind alone. Ignored for the other kinds.
/// @return The name, a string literal (so NUL-terminated) that lives for the program.
[[nodiscard]] constexpr std::string_view track_kind_name(TrackKind kind, int32_t channel) {
	switch (kind) {
	case TrackKind::SYNTH:
		return "Synth";
	case TrackKind::KIT:
		return "Kit";
	case TrackKind::MIDI_OUT:
		if (channel < kMidiChannelLowerZone) {
			return "MIDI";
		}
		if (channel == kMidiChannelLowerZone || channel == kMidiChannelUpperZone) {
			return "MPE";
		}
		return "Internal";
	case TrackKind::CV:
		return "CV / gate";
	case TrackKind::AUDIO:
		switch (static_cast<uint8_t>(channel)) {
		case 0:
			return "Audio Player";
		case 1:
			return "Audio Sampler";
		case 2:
			return "Audio Looper";
		default:
			return "Audio";
		}
	}
	return "";
}

} // namespace deluge::project
