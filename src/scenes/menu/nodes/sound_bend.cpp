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
/// @brief The bend-range items' `get`/`set` hooks and the exported root span. The `Node` literals
///        are in `sound_bend.h`.

#include "scenes/menu/nodes/sound_bend.h"

#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "storage/flash_storage.h"

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief `bend_range::Main::readCurrentValue` / `PerFinger`'s, for whichever of the two ranges
///        (`bend_range/main.h:27-33`, `per_finger.h:27-33`).
///
/// `getOrCreateExpressionParamSet(soundEditor.editingKit())` is `ctx.is_kit` here; a null result
/// means the set could not be created, and legacy falls back to the flash default.
int32_t bend_range(const MenuContext& ctx, int32_t which) {
	if (ctx.params == nullptr) {
		return FlashStorage::defaultBendRange[which];
	}
	ExpressionParamSet* expression = ctx.params->getOrCreateExpressionParamSet(ctx.is_kit);
	return expression != nullptr ? expression->bendRanges[which] : FlashStorage::defaultBendRange[which];
}

/// @brief The matching write (`main.h:34-40`, `per_finger.h:34-40`): silently does nothing when the
///        expression param set cannot be created, exactly as legacy does.
void set_bend_range(const MenuContext& ctx, int32_t which, int32_t value) {
	if (ctx.params == nullptr) {
		return;
	}
	ExpressionParamSet* expression = ctx.params->getOrCreateExpressionParamSet(ctx.is_kit);
	if (expression != nullptr) {
		expression->bendRanges[which] = static_cast<uint8_t>(value);
	}
}

} // namespace

int32_t get_bend_range_main(const MenuContext& ctx) {
	return bend_range(ctx, BEND_RANGE_MAIN);
}
void set_bend_range_main(const MenuContext& ctx, int32_t value) {
	set_bend_range(ctx, BEND_RANGE_MAIN, value);
}

int32_t get_bend_range_per_finger(const MenuContext& ctx) {
	return bend_range(ctx, BEND_RANGE_FINGER_LEVEL);
}
void set_bend_range_per_finger(const MenuContext& ctx, int32_t value) {
	set_bend_range(ctx, BEND_RANGE_FINGER_LEVEL, value);
}

namespace {
constexpr const Node* kRoots[] = {&bend, &drum_bend_range};
} // namespace

const std::span<const Node* const> kSoundBendChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
