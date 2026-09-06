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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sync.h"
#include "modulation/sidechain/sidechain.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

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
// The shared `SideChain` conversions

int32_t sidechain_attack_to_menu(const SideChain* sc) {
	// `sidechain::Attack::readCurrentValue` (`sidechain/attack.h:34-37`).
	return sc != nullptr ? lookup_index(sc->attack >> 2, attackRateTable, 50) : 0;
}
void sidechain_attack_from_menu(SideChain* sc, int32_t menu_value) {
	if (sc == nullptr || menu_value < 0 || menu_value > 50) {
		return;
	}
	sc->attack = attackRateTable[menu_value] << 2;              // `attack.h:39`
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true; // `attack.h:61 writeCurrentValue`
}

int32_t sidechain_release_to_menu(const SideChain* sc) {
	// `sidechain::Release::readCurrentValue` (`sidechain/release.h:34-37`).
	return sc != nullptr ? lookup_index(sc->release >> 3, releaseRateTable, 50) : 0;
}
void sidechain_release_from_menu(SideChain* sc, int32_t menu_value) {
	if (sc == nullptr || menu_value < 0 || menu_value > 50) {
		return;
	}
	sc->release = releaseRateTable[menu_value] << 3; // `release.h:39`
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

int32_t sidechain_sync_to_menu(const SideChain* sc) {
	if (sc == nullptr) {
		return 0;
	}
	// `sidechain::Sync::readCurrentValue` (`sidechain/sync.h:34-37`) via
	// `SyncLevel::syncTypeAndLevelToMenuOption` (`gui/menu_item/sync_level.cpp:79-81`).
	return static_cast<int32_t>(sc->syncType)
	       + (static_cast<int32_t>(sc->syncLevel) - (sc->syncType != SYNC_TYPE_EVEN ? 1 : 0));
}
void sidechain_sync_from_menu(SideChain* sc, int32_t menu_value) {
	if (sc == nullptr) {
		return;
	}
	sc->syncType = syncValueToSyncType(menu_value); // `sync.h:44-45`
	sc->syncLevel = syncValueToSyncLevel(menu_value);
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true; // `sidechain/sync.h:64 writeCurrentValue`
}

// ---------------------------------------------------------------------------------------------
// The sound's own sidechain page

int32_t get_sidechain_sync(const MenuContext& ctx) {
	return sidechain_sync_to_menu(ctx.sidechain);
}
void set_sidechain_sync(const MenuContext& ctx, int32_t value) {
	// The affect-entire sweep writes each `SoundDrum`'s own `sidechain` (`sidechain/sync.h:36-42`);
	// `SoundTarget::mod` is that object.
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           sidechain_sync_from_menu(&t.mod->sidechain, v);
		                           }
	                           });
}

int32_t get_sidechain_send(const MenuContext& ctx) {
	// `sidechain::Send::readCurrentValue` (`sidechain/send.h:32-34`).
	if (ctx.sound == nullptr) {
		return 0;
	}
	return (static_cast<uint64_t>(ctx.sound->sideChainSendLevel) * kMaxMenuValue + 1073741824) >> 31;
}
void set_sidechain_send(const MenuContext& ctx, int32_t value) {
	// `sidechain::Send::writeCurrentValue` (`send.h:27-54`).
	const int32_t stored = value == kMaxMenuValue ? 2147483647 : value * 42949673;
	bindings::apply_to_targets(ctx, stored, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.sound != nullptr) {
			                           t.sound->sideChainSendLevel = v;
		                           }
	                           });
}

int32_t get_sidechain_attack(const MenuContext& ctx) {
	return sidechain_attack_to_menu(ctx.sidechain);
}
void set_sidechain_attack(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           sidechain_attack_from_menu(&t.mod->sidechain, v);
		                           }
	                           });
}

int32_t get_sidechain_release(const MenuContext& ctx) {
	return sidechain_release_to_menu(ctx.sidechain);
}
void set_sidechain_release(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           sidechain_release_from_menu(&t.mod->sidechain, v);
		                           }
	                           });
}

namespace {
constexpr const Node* kRoots[] = {&sidechain};
} // namespace

const std::span<const Node* const> kSoundSidechainChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
