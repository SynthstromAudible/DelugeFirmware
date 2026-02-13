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

// Standalone enum definitions so instrument_clip.h can include this lightweight header
// without pulling in namespace deluge::midi (which conflicts with deluge::gui::menu_item::midi).

enum class HarmonizerMappingMode : uint8_t {
	Nearest = 0,
	RoundDown,
	RoundUp,
	Root,
	Root5th,
};

enum class HarmonizerTightness : uint8_t {
	Strict = 0,
	Scale,
	Loose,
	Extensions,
};

/// Diatonic interval type for parallel voice harmonization.
enum class DiatonicInterval : uint8_t {
	Off = 0,
	Third_Above,
	Third_Below,
	Sixth_Above,
	Sixth_Below,
	Octave_Above,
};

/// Per-clip harmonizer settings, stored alongside ArpeggiatorSettings in InstrumentClip.
/// Scale root/bits are NOT stored here — they come from currentSong->key at harmonize-time.
struct HarmonizerSettings {
	HarmonizerMappingMode mode{HarmonizerMappingMode::Nearest};
	HarmonizerTightness tightness{HarmonizerTightness::Strict};
	bool voiceLeading{false};
	bool retrigger{false};
	int8_t transpose{0}; // -24 to +24
	DiatonicInterval interval{DiatonicInterval::Off};
	int8_t chordChannel{0};           // 0-based MIDI channel for chord input (0-15)
	int8_t intervalVelocityOffset{0}; // -64 to +64, offset applied to interval voice velocity
	uint8_t probability{100};         // 0-100%, chance of harmonization (100 = always)
	bool chordLatch{false};           // When on, chord persists after keys released

	void reset() { *this = HarmonizerSettings{}; }
};
