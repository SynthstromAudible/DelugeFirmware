/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "serialization/value.h"

#include <string>

namespace deluge::project {

/// @brief The instrument a synth track or a sound kit row plays: which engine, and its element's entries the model does
/// not own, kept as read and written back as they are. The model never interprets `state`.
struct InstrumentSlot {
	std::string engine;                                           ///< the engine's id, e.g. "deluge.instrument"
	serialization::Value state = serialization::Value::makeMap(); ///< the element's other entries, in order
	bool operator==(const InstrumentSlot&) const = default;
};

} // namespace deluge::project
