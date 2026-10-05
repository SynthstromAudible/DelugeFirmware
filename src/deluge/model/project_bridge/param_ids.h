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

#include "deluge-project/param_address.h"
#include "deluge-project/param_automation.h"
#include "engine-api/types/param.h"
#include "modulation/params/param.h"

#include <cstdint>
#include <optional>

namespace deluge::bridge {

/// @brief The id the runtime's param at @p index of a @p kind collection has. Engine params go through their
/// engine's map (the Deluge instrument's for a sound's patched and unpatched params, the provisional
/// global-effectable table's for a global effectable's, the instrument's depth ids for a patch cable, whose index is
/// the runtime's packed cable word); a host space's param (MPE expression, a MIDI CC) keeps its index as its id. An
/// unpatched set not yet named by its owner's init (kind NONE) is numbered as unpatched.
[[nodiscard]] deluge::engine::ParamId paramIdOf(deluge::modulation::params::Kind kind, int32_t index);

/// @brief Where a @p kind collection keeps the param @p id: its runtime index, through the same engine map as
/// paramIdOf; for a host space's param, the id itself. nullopt when the collection's engine has no such param.
[[nodiscard]] std::optional<int32_t> runtimeIndexOf(deluge::modulation::params::Kind kind, deluge::engine::ParamId id);

/// @brief The id of a @p kind collection's first param: the id its store's first lane has.
[[nodiscard]] deluge::engine::ParamId firstParamId(deluge::modulation::params::Kind kind);

/// @brief The depth id of the runtime's packed cable word @p word: a cable's destination descriptor with its source
/// added (the param in bits 0-7, the sources from the param outward in bits 8-15, 16-23 and 24-31, 0xFF when absent).
[[nodiscard]] deluge::engine::ParamId cableDepthIdOf(uint32_t word);

/// @brief The runtime's packed cable word of the depth id @p depth; the inverse of cableDepthIdOf.
[[nodiscard]] uint32_t cableWordOf(deluge::engine::ParamId depth);

/// @brief The address space a @p kind collection's ids are in: an engine's table for a sound's or a global
/// effectable's params and a patch cable's depth, else the host's own (expression, MIDI CC).
[[nodiscard]] deluge::project::ParamSpace spaceOf(deluge::modulation::params::Kind kind);

/// @brief The model store a runtime @p kind collection binds to, or nullptr when it has none (a MIDI CC collection,
/// patch cables). The runtime's patched set binds an engine's first block, its unpatched set the second.
[[nodiscard]] std::optional<deluge::project::DocParamSet>* storeFor(deluge::project::DocParamManager& doc,
                                                                    deluge::modulation::params::Kind kind);
[[nodiscard]] const std::optional<deluge::project::DocParamSet>* storeFor(const deluge::project::DocParamManager& doc,
                                                                          deluge::modulation::params::Kind kind);

/// @brief The address of the runtime's param at @p index of a @p kind collection owned by @p owner.
[[nodiscard]] deluge::project::ParamAddress addressOf(deluge::project::OwnerId owner,
                                                      deluge::modulation::params::Kind kind, int32_t index,
                                                      std::optional<uint32_t> clip_id = std::nullopt);

} // namespace deluge::bridge
