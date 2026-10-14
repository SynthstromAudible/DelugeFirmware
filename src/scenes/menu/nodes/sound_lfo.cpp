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
/// @brief The four LFO pages' `get`/`set` hooks and their exported root span. The `Node` literals
///        are in `sound_lfo.h`.

#include "scenes/menu/nodes/sound_lfo.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "engine-api/types/sync.h"
#include "engines/deluge/words.h"
#include "model/edits/setting_edit.h"
#include "modulation/sync.h"
#include "processing/sound/sound.h"
#include "serialization/value.h"

#include <string>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief The settings path of LFO @p index's setting @p name: `lfoN/name`.
std::string lfo_path(int32_t index, std::string_view name) {
	return "lfo" + std::to_string(index + 1) + "/" + std::string(name);
}

/// @brief `lfo::Type::readCurrentValue` (`lfo/type.h`) for LFO @p index.
int32_t lfo_shape(const MenuContext& ctx, uint8_t index) {
	return ctx.sound != nullptr ? static_cast<int32_t>(ctx.sound->snapshot().lfoConfig[index].waveType) : 0;
}

/// @brief `lfo::Sync::readCurrentValue` (`lfo/sync.h`) for LFO @p index.
int32_t lfo_sync(const MenuContext& ctx, uint8_t index) {
	if (ctx.sound == nullptr) {
		return 0;
	}
	const auto& config = ctx.sound->snapshot().lfoConfig[index];
	// `SyncLevel::syncTypeAndLevelToMenuOption` (`gui/menu_item/sync_level.cpp`).
	return static_cast<int32_t>(config.syncType)
	       + (static_cast<int32_t>(config.syncLevel) - (config.syncType != SYNC_TYPE_EVEN ? 1 : 0));
}

/// @brief `lfo::Type::writeCurrentValue` (`lfo/type.h`) for LFO @p index, affect-entire loop
///        included: a setting Edit on each target sound.
void set_lfo_shape(const MenuContext& ctx, int32_t value, uint8_t index) {
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t lfo, const MenuContext&) {
		    if (t.sound == nullptr) {
			    return;
		    }
		    deluge::edits::settingFor(*t.sound, lfo_path(lfo, "type"),
		                              deluge::serialization::Value::makeScalar(
		                                  std::string(deluge::instrument::to_string(static_cast<LFOType>(v)))));
	    });
}

/// @brief `lfo::Sync::writeCurrentValue` (`lfo/sync.h`) for LFO @p index: the type, then the level.
void set_lfo_sync(const MenuContext& ctx, int32_t value, uint8_t index) {
	bindings::apply_to_targets(
	    ctx, value, index, [](const bindings::SoundTarget& t, int32_t v, int32_t lfo, const MenuContext&) {
		    if (t.sound == nullptr) {
			    return;
		    }
		    deluge::edits::settingFor(
		        *t.sound, lfo_path(lfo, "syncType"),
		        deluge::serialization::Value::makeNumber(static_cast<int32_t>(syncValueToSyncType(v))));
		    deluge::edits::settingFor(
		        *t.sound, lfo_path(lfo, "syncLevel"),
		        deluge::serialization::Value::makeNumber(deluge::engine::index_of(syncValueToSyncLevel(v))));
	    });
}

constexpr const Node* kRoots[] = {&lfo1, &lfo2, &lfo3, &lfo4};

} // namespace

/// @brief The four hook pairs `sound_lfo.h`'s `DELUGE_LFO` declared, each forwarding its LFO index.
#define DELUGE_LFO_HOOKS(n, index)                                                                                     \
	int32_t get_lfo##n##_shape(const MenuContext& ctx) {                                                               \
		return lfo_shape(ctx, (index));                                                                                \
	}                                                                                                                  \
	void set_lfo##n##_shape(const MenuContext& ctx, int32_t v) {                                                       \
		set_lfo_shape(ctx, v, (index));                                                                                \
	}                                                                                                                  \
	int32_t get_lfo##n##_sync(const MenuContext& ctx) {                                                                \
		return lfo_sync(ctx, (index));                                                                                 \
	}                                                                                                                  \
	void set_lfo##n##_sync(const MenuContext& ctx, int32_t v) {                                                        \
		set_lfo_sync(ctx, v, (index));                                                                                 \
	}

DELUGE_LFO_HOOKS(1, 0)
DELUGE_LFO_HOOKS(2, 1)
DELUGE_LFO_HOOKS(3, 2)
DELUGE_LFO_HOOKS(4, 3)

#undef DELUGE_LFO_HOOKS

const std::span<const Node* const> kSoundLfoChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
