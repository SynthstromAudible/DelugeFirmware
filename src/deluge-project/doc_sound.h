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

#include <cstdint>

namespace deluge::project {

/// The DOM-owned structural record for one engine Sound (M3 owner-5). NESTED as std::optional<DocSound> inside the
/// owner's DocOutput (SoundInstrument) or DocDrum (SoundDrum) — a Sound is owned 1:1. Params are already in the DOM
/// param tier; this holds Sound's NON-param structure. Slice B = cold scalars; later slices grow it (hot scalars,
/// LFO, modKnobs, sources). Enums stored as underlying values.
struct DocSound {
	uint8_t voice_priority{0};
	uint8_t max_voice_count{0};
	int32_t side_chain_send_level{0};
	uint8_t output_midi_channel{0};
	uint8_t output_midi_note_for_drum{0};
	bool oscillator_sync{false};
};

} // namespace deluge::project
