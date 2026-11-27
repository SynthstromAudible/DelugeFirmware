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
/// @brief Where the model crosses from a param's target to the runtime's numbering: the automation selection, a lane
/// on a model stack, the cable collection.

#include "model/project_bridge/param_ids.h"
#include "project/owner_plugin.h"

#include <cstdint>
#include <optional>

class ParamCollection;

namespace deluge::bridge {

/// @brief The param the runtime pair (@p kind, @p index) names, with its owner's plugin: a global effectable's for an
/// UNPATCHED_GLOBAL pair, the instrument's for any other (an arp, expression or CC param is in no plugin's table).
/// @param kind  The runtime collection kind.
/// @param index The param's runtime index (a cable's packed word).
/// @return The param, or nullopt for NONE or a pair that names no param.
[[nodiscard]] std::optional<deluge::project::PluginParam> pluginParamOf(deluge::modulation::params::Kind kind,
                                                                        int32_t index);

/// @brief The runtime pair of @p selection.
/// @param selection The selection.
/// @return The pair, or nullopt when there is none or its plugin has no such param.
[[nodiscard]] std::optional<RuntimeParam> selectionPairOf(const std::optional<deluge::project::PluginParam>& selection);

/// @brief The runtime pair of @p selection as the legacy views read it.
/// @param selection The selection.
/// @return The pair, or NONE with kNoParamID when there is none.
[[nodiscard]] RuntimeParam runtimePairOf(const std::optional<deluge::project::PluginParam>& selection);

/// @brief A clip's selection from its file's lastSelectedParamKind and lastSelectedParamID.
///
/// A MIDI clip chose a CC by its id alone, leaving the kind it had: none, or an expression's. Kind NONE with an id,
/// or EXPRESSION with an id past the expression dimensions, is that CC.
/// @param kind The file's kind value.
/// @param id   The file's id.
/// @return The selection, or nullopt for no id, a value that is no kind, or a pair that names no param.
[[nodiscard]] std::optional<deluge::project::PluginParam> clipSelectionOfFile(int32_t kind, int32_t id);

/// @brief The song's selection from its file's lastSelectedParamKind and lastSelectedParamID.
/// @param kind The file's kind value.
/// @param id   The file's id.
/// @return The selection, or nullopt for no id, a value that is no kind, or a pair that names no param.
[[nodiscard]] std::optional<deluge::project::PluginParam> songSelectionOfFile(int32_t kind, int32_t id);

/// @brief Whether @p collection is a sound's patch cables.
/// @param collection The collection, or nullptr.
/// @return True for a patch cable set.
[[nodiscard]] bool isCableCollection(ParamCollection* collection);

} // namespace deluge::bridge
