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

#include "deluge-project/param_automation.h"
#include "model/project_bridge/param_ids.h"
#include "modulation/params/param.h"

#include <optional>

namespace deluge::scenarios::driver {

/// @brief The model store a manager's runtime unpatched set binds to (a sound's and a global effectable's alike).
inline std::optional<deluge::project::DocParamSet>& unpatchedStore(deluge::project::DocParamManager& doc) {
	return *deluge::bridge::storeFor(doc, deluge::modulation::params::Kind::UNPATCHED_SOUND);
}

/// @brief The model store a manager's runtime patched set binds to.
inline std::optional<deluge::project::DocParamSet>& patchedStore(deluge::project::DocParamManager& doc) {
	return *deluge::bridge::storeFor(doc, deluge::modulation::params::Kind::PATCHED);
}

} // namespace deluge::scenarios::driver
