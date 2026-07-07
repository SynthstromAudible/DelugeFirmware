/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "modulation/params/param.h" // deluge::modulation::params::Kind
#include <compare>
#include <cstdint>
#include <optional>

namespace deluge::project {

/// Stable, engine-pointer-free identity of an automation lane's OWNER. This is the substrate for
/// id-addressable DOM param resolution (`Song::resolveParam`): an op names *what* it acts on by stable ids
/// instead of a threaded `ParamCollection*`/`AutoParam*` pointer chain — the first step toward retiring the
/// ModelStack's param-resolution role.
struct OwnerId {
	enum class Kind : uint8_t {
		SONG,       // song-global params (the GlobalEffects tier)
		CLIP,       // a clip's own params, identified by its Clip::serialId (audio clips; S2b)
		INSTRUMENT, // an instrument-shared owner (SoundInstrument / Kit), identified by its Output::serialId
		DRUM,       // a per-drum owner (SoundDrum), identified by its Drum::serialId
	};

	Kind kind{Kind::SONG};
	uint32_t serial_id{0}; // the owner's serial id (Clip / Output / Drum, per kind); unused when kind == SONG

	static constexpr OwnerId song() { return {Kind::SONG, 0}; }
	static constexpr OwnerId clip(uint32_t serialId) { return {Kind::CLIP, serialId}; }
	static constexpr OwnerId instrument(uint32_t outputSerialId) { return {Kind::INSTRUMENT, outputSerialId}; }
	static constexpr OwnerId drum(uint32_t drumSerialId) { return {Kind::DRUM, drumSerialId}; }

	// Ordered so OwnerId (and keys built from it) can index the ordered `fast_map` backed-param store.
	auto operator<=>(const OwnerId&) const = default;
};

/// Names one automation lane: which owner, which param collection, which param within it. `Song::resolveParam`
/// maps this to the live `DocAutoParam` in the persistent tree. For MIDI-CC / patch-cable collections a later
/// slice widens `param_id` to the collection's own key (cc; encoded from/dest).
struct ParamAddress {
	OwnerId owner{};
	deluge::modulation::params::Kind kind{deluge::modulation::params::Kind::NONE};
	int32_t param_id{0};
	// For INSTRUMENT/DRUM owners, which clip's override this lane belongs to (nullopt = the owner's shared
	// default). Mirrors ParamBackup's (ModControllable, Clip) key. Unused for SONG/CLIP owners.
	std::optional<uint32_t> clip_serial{};
};

} // namespace deluge::project
