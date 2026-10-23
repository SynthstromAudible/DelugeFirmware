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

#include "deluge-project/knob_focus.h"

#include "deluge-project/song.h"

#include <algorithm>

namespace deluge::project {

namespace {

/// The highest mod button: a mode past it is the last.
constexpr uint8_t kLastModButton = 7;

uint8_t capped(uint8_t mode) {
	return std::min(mode, kLastModButton);
}

/// The output whose mode the focus's knobs follow, or none for the song's or no focus.
std::optional<uint32_t> mode_output(const KnobFocus& focus) {
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		if (sound->kit) {
			return sound->kit;
		}
		return sound->slot.kind == SlotAddress::Kind::OUTPUT ? std::optional{sound->slot.id} : std::nullopt;
	}
	if (const auto* chain = std::get_if<ChainFocus>(&focus.owner)) {
		return chain->chain.kind == ChainAddress::Kind::OUTPUT ? std::optional{chain->chain.id} : std::nullopt;
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner)) {
		return midi->track.output_id;
	}
	return std::nullopt;
}

} // namespace

uint8_t knob_mode(const Song& song, const KnobFocus& focus) {
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return capped(song.mod_knob_mode);
	}
	if (const std::optional<uint32_t> id = mode_output(focus)) {
		const DocOutput* output = song.output_structure.find(*id);
		return output != nullptr ? capped(output->mod_knob_mode) : 0;
	}
	return 0;
}

} // namespace deluge::project
