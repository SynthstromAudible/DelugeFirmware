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

#include "model/edits/setting_edit.h"
#include "processing/sound/sound.h"
#include "serialization/value.h"

namespace deluge::scenes::menu::nodes {

int32_t get_unison_count(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->snapshot().numUnison : 1; // `unison/count.h`
}
void set_unison_count(const MenuContext& ctx, int32_t value) {
	// `Count::writeCurrentValue` (`unison/count.h`): the affect-entire kit loop, a setting Edit on
	// each sound.
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, "unison/num", deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

int32_t get_unison_detune(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->snapshot().unisonDetune : 0; // `unison/detune.h`
}
void set_unison_detune(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, "unison/detune", deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

int32_t get_unison_spread(const MenuContext& ctx) {
	return ctx.sound != nullptr ? ctx.sound->snapshot().unisonStereoSpread : 0; // `unison/stereoSpread.h`
}
void set_unison_spread(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, "unison/spread", deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

namespace {
constexpr const Node* kRoots[] = {&unison};
} // namespace

const std::span<const Node* const> kSoundUnisonChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
