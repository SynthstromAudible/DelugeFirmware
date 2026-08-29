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
/// @brief The filters subtree's `get`/`set` hooks and its exported root span. The `Node` literals
///        themselves are in `sound_filters.h` -- see the note there on why they are header-visible.

#include "scenes/menu/nodes/sound_filters.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"

#include <algorithm>

namespace deluge::scenes::menu::nodes {

// ---------------------------------------------------------------------------------------------
// Filter mode -- `filter::FilterModeSelection` (`gui/menu_item/filter/mode.h:34-85`) over
// `FilterInfo` (`filter/info.h:50-74`).

/// `FilterInfo::getModeValue()`'s LPF arm (`filter/info.h:54-58`): "Off is located past the
/// HPFLadder, which isn't an option for the low pass filter".
int32_t get_lpf_mode(const MenuContext& ctx) {
	if (ctx.mod == nullptr) {
		return 0;
	}
	return std::min(static_cast<int32_t>(ctx.mod->lpfMode), kNumLPFModes);
}

/// `FilterInfo::setModeForModControllable`'s LPF arm (`filter/info.h:65-73`), applied through the
/// affect-entire loop `FilterModeSelection::writeCurrentValue` spells out (`filter/mode.h:40-59`).
void set_lpf_mode(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    t.mod->lpfMode = v >= kNumLPFModes ? ::FilterMode::OFF : static_cast<::FilterMode>(v);
	    });
}

/// `FilterInfo::getModeValue()`'s HPF arm (`filter/info.h:51-53`).
int32_t get_hpf_mode(const MenuContext& ctx) {
	if (ctx.mod == nullptr) {
		return 0;
	}
	return static_cast<int32_t>(ctx.mod->hpfMode) - kFirstHPFMode;
}

/// `FilterInfo::setModeForModControllable`'s HPF arm (`filter/info.h:62-64`).
void set_hpf_mode(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->hpfMode = static_cast<::FilterMode>(v + kFirstHPFMode);
	                           });
}

// ---------------------------------------------------------------------------------------------
// Filter routing -- `FilterRouting` (`gui/menu_item/filter_route.h:27-70`).

int32_t get_filter_route(const MenuContext& ctx) {
	return ctx.mod != nullptr ? static_cast<int32_t>(ctx.mod->filterRoute) : 0; // `filter_route.h:30`
}

void set_filter_route(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    // `filter_route.h:45`: the kit loop -- and only the kit loop -- skips a drum whose filters
		    // are not both on. The single-target path (`:53`) has no such test.
		    if (t.whole_kit && (t.mod->lpfMode == ::FilterMode::OFF || t.mod->hpfMode == ::FilterMode::OFF)) {
			    return;
		    }
		    t.mod->filterRoute = static_cast<::FilterRoute>(v);
	    });
}

namespace {
constexpr const Node* kRoots[] = {&sound_filters};
} // namespace

const std::span<const Node* const> kSoundFiltersChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
