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

#include "engine-api/types/param.h"
#include <compare>
#include <cstdint>
#include <optional>

namespace deluge::project {

/// @brief Stable, engine-pointer-free identity of an automation lane's OWNER.
///
/// The substrate for id-addressable DOM param resolution (`Song::resolveParam`): an op names *what* it acts on by
/// stable ids instead of a threaded `ParamCollection*`/`AutoParam*` pointer chain.
struct OwnerId {
	enum class Kind : uint8_t {
		SONG,       // song-global params (the master effect chain's)
		CLIP,       // a clip's own params, identified by its Clip::id (audio clips)
		INSTRUMENT, // an instrument-shared owner (SoundInstrument / Kit), identified by its Output::id
		DRUM,       // a per-drum owner (SoundDrum), identified by its Drum::id
	};

	Kind kind{Kind::SONG};
	uint32_t id{0}; // the owner's durable id (Clip / Output / Drum, per kind). Unused when SONG.

	static constexpr OwnerId song() { return {Kind::SONG, 0}; }
	static constexpr OwnerId clip(uint32_t id) { return {Kind::CLIP, id}; }
	static constexpr OwnerId instrument(uint32_t outputSerialId) { return {Kind::INSTRUMENT, outputSerialId}; }
	static constexpr OwnerId drum(uint32_t drumSerialId) { return {Kind::DRUM, drumSerialId}; }

	// Ordered so OwnerId (and keys built from it) can index the ordered `fast_map` backed-param store.
	auto operator<=>(const OwnerId&) const = default;
};

/// @brief Which numbering a param address's id is in: its owner's engine's param table, or one of the host's own
/// spaces (an MPE expression dimension, a MIDI CC number). A patch cable's depth is an engine param.
enum class ParamSpace : uint8_t { ENGINE, EXPRESSION, MIDI_CC };

/// @brief Names one automation lane: which owner, which space, which param in it. `Song::resolveParam` maps this to the
/// live `DocAutoParam` in the persistent tree. An engine param's id is its engine's (the owner's slot names the
/// engine).
struct ParamAddress {
	OwnerId owner{};
	ParamSpace space{ParamSpace::ENGINE};
	deluge::engine::ParamId id{};
	// For INSTRUMENT/DRUM owners, which clip's override this lane belongs to (nullopt = the owner's shared
	// default). Unused for SONG/CLIP owners.
	std::optional<uint32_t> clip_id{};
};

} // namespace deluge::project
