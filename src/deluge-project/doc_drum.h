/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/drum_type.h"
#include "deluge-project/instrument_slot.h"
#include <cstdint>
#include <optional>
#include <string>

namespace deluge::project {

/// The DOM-owned structural record for one engine Drum (M3 owner-3 — kit/drum structure). Kept in `DrumStructure`
/// keyed by the drum's durable `Drum::id()`. `drum_type` is what the drum plays;
/// `channel`/`note` apply to the non-audio kinds (MIDI channel or gate channel; MIDI note) and are 0 for SOUND;
/// `name` shadows the engine `Drum::drumName` (slice 2, write-only — dual-written via Drum::setName, seeded in
/// Song::adoptDrum; nothing reads it yet). Engine-light: enums stored as underlying values, no engine includes.
struct DocDrum {
	DrumType drum_type{DrumType::SOUND};
	uint8_t channel{0};
	uint8_t note{0};
	std::string name;
	/// The instrument a sound kit row plays; absent for every other kind.
	std::optional<InstrumentSlot> instrument;

	/// Equal when the identity fields and the instrument are.
	bool operator==(const DocDrum& other) const {
		return drum_type == other.drum_type && channel == other.channel && note == other.note && name == other.name
		       && instrument == other.instrument;
	}
};

} // namespace deluge::project
