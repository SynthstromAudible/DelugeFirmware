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

#include "plugin/state_handle.h"
#include "serialization/schema/extras.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <string>

namespace deluge::project {

/// @brief Where a sound's output goes outside its instrument: its send to the song's sidechain and its MIDI output. The
/// host routes them, as a plugin host routes a plugin's output ports and note output.
struct SoundRouting {
	/// The whole level, the most a send can be: what the sample preview and a kick one-shot send.
	static constexpr int32_t kFullSend = INT32_MAX;
	/// The file's word for no channel and no note.
	static constexpr uint8_t kNone = 255;

	int32_t side_chain_send{0};          ///< the send to the song's sidechain: 0 (none) to kFullSend
	std::optional<uint8_t> midi_channel; ///< the MIDI channel its notes go out on, 0–15; none sends nothing
	/// The note a kit row's notes go out as, each moved from it as far as it is from the row's own; none sends each
	/// note as it plays.
	std::optional<uint8_t> midi_note;
	/// Keys a document held here that this firmware does not know, written back unchanged.
	serialization::schema::Extras extras;
	bool operator==(const SoundRouting&) const = default;
};

/// @brief The instrument a synth track or a sound kit row plays: which plugin, and its state, which the model holds
/// without knowing its type. The sound playing it binds to that state.
struct InstrumentSlot {
	std::string plugin;        ///< the plugin's id, e.g. "deluge.instrument"
	plugin::StateHandle state; ///< the plugin's state
	SoundRouting routing;      ///< where the sound playing it sends its output beyond the instrument
	/// The song's input tick magnitude when the slot was filled: the context an edit reads and writes its sync levels
	/// in.
	int32_t tick_magnitude{1};
	bool operator==(const InstrumentSlot&) const = default;
};

/// @brief How the model reads and writes a sound element's instrument without understanding it: the host supplies it.
struct InstrumentCodec {
	/// The instrument a sound element holds, its sync levels relative to @p tick_magnitude.
	plugin::StateHandle (*decode)(const serialization::Value& element, int32_t tick_magnitude) = nullptr;
	/// Writes @p state's entries into @p element, in the order a sound element holds them, its sync levels relative to
	/// @p tick_magnitude. The slot's routing is the model's, which it writes beside them.
	void (*encode)(const plugin::StateHandle& state, serialization::Value& element, int32_t tick_magnitude) = nullptr;
};

/// @brief Which slot a setting names: a synth track's, by its output's id, or a sound kit row's, by its drum's id.
struct SlotAddress {
	enum class Kind : uint8_t { OUTPUT, DRUM };
	Kind kind{Kind::OUTPUT};
	uint32_t id{0};
	bool operator==(const SlotAddress&) const = default;
};

} // namespace deluge::project
