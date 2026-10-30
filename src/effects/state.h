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

#include "effects/words.h"
#include "plugin-api/types/sync.h"

#include <cstdint>
#include <limits>

namespace deluge::effects {

/// @brief The filters' circuits and their order. A sound runs its filters per voice; a host chain runs one pair.
struct FilterState {
	FilterMode lpf_mode{FilterMode::TRANSISTOR_24DB};
	FilterMode hpf_mode{FilterMode::HPLADDER};
	FilterRoute route{FilterRoute::HIGH_TO_LOW};
	bool operator==(const FilterState&) const = default;
};

/// @brief How hard the saturation clips: 0 is off.
struct SaturationState {
	uint8_t clipping_amount{0};
	bool operator==(const SaturationState&) const = default;
};

/// @brief The mod-FX unit's effect.
struct ModFxState {
	ModFXType type{ModFXType::NONE};
	bool operator==(const ModFxState&) const = default;
};

/// @brief The delay's character and, when synced, its rate.
struct DelayState {
	bool ping_pong{true};
	DelayType type{DelayType::OFF};
	plugin::SyncType sync_type{plugin::SyncType::EVEN};
	/// The level itself (NONE = not synced); a file stores it relative to the song's input tick magnitude.
	plugin::SyncLevel sync_level{plugin::SyncLevel::SIXTEENTH};
	bool operator==(const DelayState&) const = default;
};

/// @brief How a stutter repeats, and whether it takes the song's settings instead of these.
struct StutterState {
	/// Saved only when false; a stutter tag without it (every older file's) reads as true.
	bool use_song_stutter{true};
	bool quantized{true};
	bool reversed{false};
	bool ping_pong{false};
	bool operator==(const StutterState&) const = default;
};

/// @brief The compressor's knob positions, each Q31 (the menu's 0–127 shifted up by 24).
struct CompressorState {
	int32_t attack{5 << 24};
	int32_t release{5 << 24};
	/// The setting. The master renders it (its knob); a sound and a clip chain render their threshold param every block
	/// instead. A host chain's record and file hold the setting; a sound's file holds its param's.
	int32_t threshold{0};
	int32_t ratio{64 << 24};
	int32_t sidechain_hpf{0};
	int32_t blend{std::numeric_limits<int32_t>::max()};
	bool operator==(const CompressorState&) const = default;
};

/// @brief The sidechain envelope's shape and, when synced, its rate.
struct SidechainState {
	/// The envelope's attack rate as the runtime's param scaling makes it; set by the owner at construction.
	int32_t attack{0};
	/// The envelope's release rate, likewise; set by the owner at construction.
	int32_t release{0};
	plugin::SyncType sync_type{plugin::SyncType::EVEN};
	/// The level itself (NONE = not synced); a file stores it relative to the song's input tick magnitude. Its
	/// default depends on the song and is set by the owner at construction.
	plugin::SyncLevel sync_level{plugin::SyncLevel::NONE};
	bool operator==(const SidechainState&) const = default;
};

/// @brief Every setting of an owner's effects: a sound's, a kit's, an audio track's or the song master's.
struct EffectStates {
	FilterState filter;
	SaturationState saturation;
	ModFxState mod_fx;
	DelayState delay;
	StutterState stutter;
	CompressorState compressor;
	SidechainState sidechain;
	bool operator==(const EffectStates&) const = default;
};

} // namespace deluge::effects
