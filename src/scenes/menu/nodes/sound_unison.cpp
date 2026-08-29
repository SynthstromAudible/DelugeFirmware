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
/// @brief The unison subtree's `get`/`set` hooks and its exported root span. The `Node` literals are
///        in `sound_unison.h`.

#include "scenes/menu/nodes/sound_unison.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "processing/sound/sound.h"

namespace deluge::scenes::menu::nodes {

int32_t get_unison_count(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->numUnison : 1; // `unison/count.h:35`
}
void set_unison_count(const MenuContext& ctx, int32_t value) {
	// `Count::writeCurrentValue` (`unison/count.h:37-63`): the affect-entire kit loop, then
	// `setNumUnison` with the sound's own model stack -- which is why `apply_to_targets` hands one
	// over (`bindings.h`'s `SoundTarget::stack`).
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->setNumUnison(v, t.stack);
		                           }
	                           });
}

int32_t get_unison_detune(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->unisonDetune : 0; // `unison/detune.h:29`
}
void set_unison_detune(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->setUnisonDetune(v, t.stack); // `unison/detune.h:31-57`
		                           }
	                           });
}

int32_t get_unison_spread(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->unisonStereoSpread : 0; // `unison/stereoSpread.h:28`
}
void set_unison_spread(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound != nullptr) {
			    t.sound->setUnisonStereoSpread(v); // `unison/stereoSpread.h:30-51` -- no stack needed
		    }
	    });
}

namespace {
constexpr const Node* kRoots[] = {&unison};
} // namespace

const std::span<const Node* const> kSoundUnisonChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
