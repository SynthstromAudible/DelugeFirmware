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
/// @brief The four LFO subtrees: `lfo1Menu` .. `lfo4Menu`, each {shape, sync, rate}.
///
/// From `gui/menu_item/generate/dmenus/lfo/{lfo,__init__}.py` and `generate/g_menus.inc:203-238`:
///
/// | node | legacy global | legacy class / param |
/// |---|---|---|
/// | `lfo<n>_type` | `lfo<n>TypeMenu` | `lfo::Type` (`gui/menu_item/lfo/type.h:27`) |
/// | `lfo<n>_sync` | `lfo<n>SyncMenu` | `lfo::Sync` (`gui/menu_item/lfo/sync.h:26`) |
/// | `lfo1_rate` | `lfo1RateMenu` | `patched_param::Integer`, `GLOBAL_LFO_FREQ_1` |
/// | `lfo2_rate` | `lfo2RateMenu` | `patched_param::Integer`, `LOCAL_LFO_LOCAL_FREQ_1` |
/// | `lfo3_rate` | `lfo3RateMenu` | `patched_param::Integer`, `GLOBAL_LFO_FREQ_2` |
/// | `lfo4_rate` | `lfo4RateMenu` | `patched_param::Integer`, `LOCAL_LFO_LOCAL_FREQ_2` |
/// | `lfo<n>` | `lfo<n>Menu` | `HorizontalMenu` |
///
/// The odd/even alternation of global and local rate params is the Python spec's own
/// (`dmenus/lfo/lfo.py:29-33`: `isGlobal = i % 2 == 0`, `index = 1 if i < 2 else 2`), and it matches
/// `definitions_cxx.hpp:390-393`'s "LFO 1 (global), LFO 2 (local), LFO 3 (global), LFO 4 (local)".
///
/// @note **`Kind::SyncLevel` is built here by hand**, not through a `node.h` builder: there is no
///       `sync_level_item()` (Task 2 declared the kind for exactly this family and left the builder
///       to whichever task first needed one). The value is legacy's own menu option index,
///       `SyncLevel::syncTypeAndLevelToMenuOption` (`gui/menu_item/sync_level.cpp:79-81`), over
///       `SyncLevel::size() == NUM_SYNC_VALUES` (`sync_level.h:31`, `model/sync.h:37`). The option
///       *names* are `getNoteLengthName`, which formats from the song's
///       `insideWorldTickMagnitude` at render time (`sync_level.cpp:83-91`) -- dynamic text this
///       generation's interpreter does not draw yet, which is why the node carries no `names`.
///
/// @note **`lfo::Type`'s title is a `FormattedTitle`** substituting the LFO number into
///       `STRING_FOR_LFO_TYPE` (`lfo/type.h:30,33`), as `lfo::Sync`'s is for `STRING_FOR_LFO_SYNC`
///       (`lfo/sync.h:29,32`). A `Node::title` is one static id, so all four carry the
///       unsubstituted one -- the same gap the envelope submenus have.

#include "scenes/menu/nodes/nodes.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp"
#include "model/sync.h"
#include "processing/sound/sound.h"

#include <array>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;
namespace params = deluge::modulation::params;

/// @brief `lfo::Type::getOptions` (`gui/menu_item/lfo/type.h:34-46`).
constexpr uint16_t kLfoShapeNames[] = {
    sid(String::STRING_FOR_SINE),   sid(String::STRING_FOR_TRIANGLE),        sid(String::STRING_FOR_SQUARE),
    sid(String::STRING_FOR_SAW),    sid(String::STRING_FOR_SAMPLE_AND_HOLD), sid(String::STRING_FOR_RANDOM_WALK),
    sid(String::STRING_FOR_WARBLE),
};

/// @brief A `Kind::SyncLevel` leaf. See the file note on why this is not a `node.h` builder.
constexpr Node sync_level_item(uint16_t name, uint16_t title, GetFn get, SetFn set) {
	Node n{};
	n.name = name;
	n.title = title;
	n.kind = Kind::SyncLevel;
	n.bind.get = get;
	n.bind.set = set;
	n.bind.fixed_range = IntRange{0, NUM_SYNC_VALUES - 1, 1}; // `sync_level.h:31`, `model/sync.h:37`
	return n;
}

/// @brief `lfo::Type::readCurrentValue` (`lfo/type.h:49`) for LFO @p index.
int32_t lfo_shape(const MenuContext& ctx, uint8_t index) {
	return ctx.sound != nullptr ? static_cast<int32_t>(ctx.sound->lfoConfig[index].waveType) : 0;
}

/// @brief `lfo::Sync::readCurrentValue` (`lfo/sync.h:35-38`) for LFO @p index.
int32_t lfo_sync(const MenuContext& ctx, uint8_t index) {
	if (ctx.sound == nullptr) {
		return 0;
	}
	const auto& config = ctx.sound->lfoConfig[index];
	// `SyncLevel::syncTypeAndLevelToMenuOption` (`gui/menu_item/sync_level.cpp:79-81`).
	return static_cast<int32_t>(config.syncType)
	       + (static_cast<int32_t>(config.syncLevel) - (config.syncType != SYNC_TYPE_EVEN ? 1 : 0));
}

/// @brief `lfo::Type::writeCurrentValue` (`lfo/type.h:51-73`) for LFO @p index, affect-entire loop
///        and `resyncGlobalLFOs` included.
void set_lfo_shape(const MenuContext& ctx, int32_t value, uint8_t index) {
	bindings::apply_to_targets(ctx, value, index,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t lfo, const MenuContext&) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           t.sound->lfoConfig[lfo].waveType = static_cast<LFOType>(v);
		                           t.sound->resyncGlobalLFOs();
	                           });
}

/// @brief `lfo::Sync::writeCurrentValue` (`lfo/sync.h:40-68`) for LFO @p index.
void set_lfo_sync(const MenuContext& ctx, int32_t value, uint8_t index) {
	bindings::apply_to_targets(ctx, value, index,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t lfo, const MenuContext& ctx) {
		                           if (t.sound == nullptr) {
			                           return;
		                           }
		                           LFOConfig& config = t.sound->lfoConfig[lfo];
		                           config.syncType = syncValueToSyncType(v);
		                           config.syncLevel = syncValueToSyncLevel(v);
		                           t.sound->resyncGlobalLFOs();
		                           t.sound->setupPatchingForAllParamManagers(ctx.song);
	                           });
}

/// @brief One LFO page. A macro for the same reason `sound_env.cpp` uses one -- and because the
///        `get`/`set` hooks are plain function pointers, so each LFO needs its own pair.
#define DELUGE_LFO(n, index, rate_param, rate_title)                                                                   \
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
	}                                                                                                                  \
	constexpr Node lfo##n##_type =                                                                                     \
	    with_title(enumeration(sid(String::STRING_FOR_SHAPE), kLfoShapeNames, get_lfo##n##_shape, set_lfo##n##_shape), \
	               sid(String::STRING_FOR_LFO_TYPE));                                                                  \
	constexpr Node lfo##n##_sync = sync_level_item(sid(String::STRING_FOR_SYNC), sid(String::STRING_FOR_LFO_SYNC),     \
	                                               get_lfo##n##_sync, set_lfo##n##_sync);                              \
	constexpr Node lfo##n##_rate = param(sid(String::STRING_FOR_RATE), {params::Kind::PATCHED, params::rate_param},    \
	                                     Hint::Knob, nullptr, sid(String::rate_title));                                \
	constexpr const Node* kLfo##n##Children[] = {&lfo##n##_type, &lfo##n##_sync, &lfo##n##_rate};                      \
	constexpr Node lfo##n = submenu(sid(String::STRING_FOR_LFO##n), kLfo##n##Children, Layout::Horizontal)

DELUGE_LFO(1, 0, GLOBAL_LFO_FREQ_1, STRING_FOR_LFO1_RATE);
DELUGE_LFO(2, 1, LOCAL_LFO_LOCAL_FREQ_1, STRING_FOR_LFO2_RATE);
DELUGE_LFO(3, 2, GLOBAL_LFO_FREQ_2, STRING_FOR_LFO3_RATE);
DELUGE_LFO(4, 3, LOCAL_LFO_LOCAL_FREQ_2, STRING_FOR_LFO4_RATE);

#undef DELUGE_LFO

constexpr const Node* kRoots[] = {&lfo1, &lfo2, &lfo3, &lfo4};

} // namespace

const std::span<const Node* const> kSoundLfoChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
