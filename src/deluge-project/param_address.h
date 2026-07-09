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
		CLIP,       // a clip's own params, identified by its Clip::id (audio clips; S2b)
		INSTRUMENT, // an instrument-shared owner (SoundInstrument / Kit), identified by its Output::id
		DRUM,       // a per-drum owner (SoundDrum), identified by its Drum::id
	};

	Kind kind{Kind::SONG};
	uint32_t id{0};         // the owner's id (Clip / Output / Drum, per kind); a persisted id, or — when
	                        // is_session — a transient session id (S5-b: not-yet-saved owners). Unused when SONG.
	bool is_session{false}; // true = id is a transient session id (session_* tree maps), not a persisted one

	static constexpr OwnerId song() { return {Kind::SONG, 0}; }
	static constexpr OwnerId clip(uint32_t id) { return {Kind::CLIP, id}; }
	static constexpr OwnerId instrument(uint32_t outputSerialId) { return {Kind::INSTRUMENT, outputSerialId}; }
	static constexpr OwnerId drum(uint32_t drumSerialId) { return {Kind::DRUM, drumSerialId}; }
	// S5-b: session-keyed (transient, not-yet-saved) owners — addressed by a session id. CLIP resolves in the
	// session_clip_params map; INSTRUMENT/DRUM resolve in the SAME backed_params map (the is_session flag makes the
	// key distinct from a id owner's, so no separate map is needed there).
	static constexpr OwnerId clipSession(uint32_t sessionId) { return {Kind::CLIP, sessionId, true}; }
	static constexpr OwnerId instrumentSession(uint32_t sessionId) { return {Kind::INSTRUMENT, sessionId, true}; }
	static constexpr OwnerId drumSession(uint32_t sessionId) { return {Kind::DRUM, sessionId, true}; }

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
	std::optional<uint32_t> clip_id{};
	// S5-b: for INSTRUMENT/DRUM owners, whether `clip_id` is a transient session id (a not-yet-saved clip) vs a
	// persisted id — the clip dimension's `is_session`, independent of the owner's. Disambiguates the "mixed"
	// case: a saved instrument (id owner) carrying a runtime-added clip (session clip). Session and serial ids
	// share a numeric space, so this flag is what keeps their keys distinct. Unused for SONG/CLIP owners.
	bool clip_is_session{false};
};

} // namespace deluge::project
