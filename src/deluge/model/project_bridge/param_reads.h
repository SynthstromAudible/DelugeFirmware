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

#pragma once

/// @file
/// @brief Where a runtime read finds a param of the instrument's or the host chain's table: its index in the runtime's
/// patched or unpatched set.

#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "modulation/params/param.h"
#include "plugin/types/param.h"
#include "project/param_automation.h"

#include <cassert>
#include <cstdint>
#include <optional>

namespace deluge::bridge {

static_assert(host::chains::kFirstId == modulation::params::UNPATCHED_START
                  && instruments::classic::kUnpatchedFirstId == modulation::params::UNPATCHED_START,
              "both owners keep their unpatched params from the runtime's first unpatched id");

/// @brief The runtime's index of the instrument's patched param @p id.
/// @param id One of the instrument's patched ids.
/// @return The index into a sound's PatchedParamSet.
[[nodiscard]] constexpr int32_t patchedIndex(plugin::ParamId id) {
	assert(instruments::classic::kParamBlocks[0].contains(id));
	return static_cast<int32_t>(id.value);
}

/// @brief The runtime's index of an owner's unpatched param.
/// @param id The owner's id: one of its unpatched ids.
/// @return The index into the owner's UnpatchedParamSet.
[[nodiscard]] constexpr int32_t unpatchedIndex(plugin::ParamId id) {
	assert(id.value >= modulation::params::UNPATCHED_START
	       && id.value < modulation::params::UNPATCHED_START + modulation::params::kMaxNumUnpatchedParams);
	return static_cast<int32_t>(id.value - modulation::params::UNPATCHED_START);
}

/// @brief The store of a global effectable's unpatched params (the song's master chain, a kit's, an audio clip's).
/// @param doc The owner's param manager.
/// @return The store slot.
[[nodiscard]] std::optional<project::DocParamSet>& globalEffectableStoreOf(project::DocParamManager& doc);
/// @copydoc globalEffectableStoreOf(project::DocParamManager&)
[[nodiscard]] const std::optional<project::DocParamSet>& globalEffectableStoreOf(const project::DocParamManager& doc);

} // namespace deluge::bridge
