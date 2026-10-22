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

/// @file
/// @brief The actions page, the play direction and the output-MIDI page: `get`/`set` hooks and the
///        exported root span. The `Node` literals are in `sound_actions.h`.

#include "scenes/menu/nodes/sound_actions.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "processing/sound/sound.h"
#include "serialization/value.h"

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

// ---------------------------------------------------------------------------------------------
// Play direction

size_t sequence_direction_names(const MenuContext& ctx, std::span<std::string_view> out) {
	// `sequence::Direction::getOptions` (`gui/menu_item/sequence/direction.h:92-108`): three fixed
	// options, plus NONE (obey-parent) only when an individual note row was resolved.
	static constexpr String kBase[] = {
	    String::STRING_FOR_FORWARD,
	    String::STRING_FOR_REVERSED,
	    String::STRING_FOR_PING_PONG,
	};
	size_t n = 0;
	for (String s : kBase) {
		if (n < out.size()) {
			out[n++] = deluge::scenes::text_lookup(sid(s));
		}
	}
	bool from_note_row = false;
	(void)bindings::read_sequence_direction(ctx, &from_note_row);
	if (from_note_row && n < out.size()) {
		out[n++] = deluge::scenes::text_lookup(sid(String::STRING_FOR_NONE));
	}
	return n;
}

int32_t get_sequence_direction(const MenuContext& ctx) {
	return bindings::read_sequence_direction(ctx);
}
void set_sequence_direction(const MenuContext& ctx, int32_t value) {
	bindings::write_sequence_direction(ctx, value);
}

// ---------------------------------------------------------------------------------------------
// Output MIDI

namespace {
constexpr const Node* kRoots[] = {&sound_actions, &sequence_direction, &output_midi};
} // namespace

const std::span<const Node* const> kSoundActionsChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
