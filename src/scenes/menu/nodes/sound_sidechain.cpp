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
/// @brief The sidechain page's `get`/`set` hooks, the shared `SideChain` conversions and the
///        exported root span. The `Node` literals are in `sound_sidechain.h`.

#include "scenes/menu/nodes/sound_sidechain.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "engine-api/types/sync.h"
#include "engines/effects/state.h"
#include "model/edits/setting_edit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/sync.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "serialization/value.h"

#include <algorithm>

// `util/lookuptables/lookuptables.h:122-123`, at global scope where they are defined. `util/` is
// outside `src/scenes`' include allow-list (`scenes-includes-outside`), so the two tables are
// *declared* here rather than included -- the same treatment `sound_compressor.cpp` gives
// `lshiftAndSaturate<24>`, and the definitions still come from the one place that owns them.
// `tables_2_spec` compares the resulting menu values against the legacy items, so a mismatch would
// fail loudly rather than drift.
extern const int32_t attackRateTable[51];
extern const int32_t releaseRateTable[51];

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief `getLookupIndexFromValue` (`util/functions.cpp:1428-1443`), transcribed for the same
///        reason as the tables above. The `bestDistance` comparison is legacy's own signed/unsigned
///        mix, reproduced exactly rather than "fixed": changing it would change which index a given
///        stored rate shows as.
int32_t lookup_index(int32_t value, const int32_t* table, int32_t max_index) {
	uint32_t best_distance = 0xFFFFFFFF;
	int32_t closest = 0;
	for (int32_t i = 0; i <= max_index; i++) {
		int64_t distance = static_cast<int64_t>(value) - static_cast<int64_t>(table[i]);
		if (distance < 0) {
			distance = -distance;
		}
		if (distance < best_distance) {
			best_distance = static_cast<uint32_t>(distance);
			closest = i;
		}
	}
	return closest;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The shared sidechain conversions

int32_t sidechain_attack_to_menu(int32_t attack) {
	// `sidechain::Attack::readCurrentValue` (`sidechain/attack.h`).
	return lookup_index(attack >> 2, attackRateTable, 50);
}
int32_t sidechain_attack_from_menu(int32_t menu_value) {
	return attackRateTable[std::clamp<int32_t>(menu_value, 0, 50)] << 2; // `attack.h`'s write
}

int32_t sidechain_release_to_menu(int32_t release) {
	// `sidechain::Release::readCurrentValue` (`sidechain/release.h`).
	return lookup_index(release >> 3, releaseRateTable, 50);
}
int32_t sidechain_release_from_menu(int32_t menu_value) {
	return releaseRateTable[std::clamp<int32_t>(menu_value, 0, 50)] << 3; // `release.h`'s write
}

int32_t sidechain_sync_to_menu(deluge::engine::SyncType type, deluge::engine::SyncLevel level) {
	// `sidechain::Sync::readCurrentValue` (`sidechain/sync.h`) via
	// `SyncLevel::syncTypeAndLevelToMenuOption` (`gui/menu_item/sync_level.cpp`).
	return static_cast<int32_t>(type) + (static_cast<int32_t>(level) - (type != SYNC_TYPE_EVEN ? 1 : 0));
}

// ---------------------------------------------------------------------------------------------
// The sound's own sidechain page
//
// Each write is an effect setting on the target's own sidechain, as the legacy items write it, so
// the owner's record and the runtime stay one.

namespace {

/// @brief The sidechain settings the shown owner reports.
const deluge::effects::SidechainState* shown_sidechain(const MenuContext& ctx) {
	return ctx.mod != nullptr ? &ctx.mod->effects().sidechain : nullptr;
}

/// @brief The sidechain settings `set_sidechain_number` writes, by index.
constexpr char const* kSidechainNumberPaths[] = {"sidechain/attack", "sidechain/release"};

/// @brief Write @p value to every target's `kSidechainNumberPaths[which]` and ask the engine to refresh.
void set_sidechain_number(const MenuContext& ctx, int32_t value, int32_t which) {
	bindings::apply_to_targets(ctx, value, which,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t w, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           deluge::edits::effectSettingFor(*t.mod, kSidechainNumberPaths[w],
			                                                           deluge::serialization::Value::makeNumber(v));
		                           }
	                           });
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

} // namespace

int32_t get_sidechain_sync(const MenuContext& ctx) {
	const deluge::effects::SidechainState* sc = shown_sidechain(ctx);
	return sc != nullptr ? sidechain_sync_to_menu(sc->sync_type, sc->sync_level) : 0;
}
void set_sidechain_sync(const MenuContext& ctx, int32_t value) {
	// `sidechain::Sync::writeCurrentValue`: the type, then the level.
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.mod == nullptr) {
			    return;
		    }
		    deluge::edits::effectSettingFor(
		        *t.mod, "sidechain/syncType",
		        deluge::serialization::Value::makeNumber(static_cast<int32_t>(syncValueToSyncType(v))));
		    deluge::edits::effectSettingFor(
		        *t.mod, "sidechain/syncLevel",
		        deluge::serialization::Value::makeNumber(deluge::engine::index_of(syncValueToSyncLevel(v))));
	    });
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

int32_t get_sidechain_send(const MenuContext& ctx) {
	// `sidechain::Send::readCurrentValue` (`sidechain/send.h`).
	if (ctx.sound == nullptr) {
		return 0;
	}
	return (static_cast<uint64_t>(ctx.sound->sideChainSendLevel) * kMaxMenuValue + 1073741824) >> 31;
}
void set_sidechain_send(const MenuContext& ctx, int32_t value) {
	// `sidechain::Send::writeCurrentValue` (`send.h`).
	const int32_t stored = value == kMaxMenuValue ? 2147483647 : value * 42949673;
	bindings::apply_to_targets(
	    ctx, stored, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.sound != nullptr) {
			    deluge::edits::settingFor(*t.sound, "sideChainSend", deluge::serialization::Value::makeNumber(v));
		    }
	    });
}

int32_t get_sidechain_attack(const MenuContext& ctx) {
	const deluge::effects::SidechainState* sc = shown_sidechain(ctx);
	return sc != nullptr ? sidechain_attack_to_menu(sc->attack) : 0;
}
void set_sidechain_attack(const MenuContext& ctx, int32_t value) {
	set_sidechain_number(ctx, sidechain_attack_from_menu(value), 0);
}

int32_t get_sidechain_release(const MenuContext& ctx) {
	const deluge::effects::SidechainState* sc = shown_sidechain(ctx);
	return sc != nullptr ? sidechain_release_to_menu(sc->release) : 0;
}
void set_sidechain_release(const MenuContext& ctx, int32_t value) {
	set_sidechain_number(ctx, sidechain_release_from_menu(value), 1);
}

namespace {
constexpr const Node* kRoots[] = {&sidechain};
} // namespace

const std::span<const Node* const> kSoundSidechainChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
