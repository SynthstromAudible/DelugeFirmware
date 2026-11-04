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

#include "model/generative/topo/topo_engine.h"
#include <cstdint>

namespace deluge::model::topo {

/// Per-clip settings for the "Topo Drums" generative drum mode. Persisted with the
/// clip (see InstrumentClip). The engine itself lives on the Kit and is driven from
/// these settings on every tick.
struct TopoDrumsSettings {
	bool operator==(TopoDrumsSettings const&) const = default;

	/// Master switch for this clip. Only honoured when the community feature
	/// RuntimeFeatureSettingType::EnableTopoDrums is on.
	bool enabled{false};

	TopoMode mode{TopoMode::Drums};
	/// When true, outputs stay high for the whole step; otherwise short triggers.
	bool gateMode{false};
	/// Grids-style swing on alternating step pairs. (Off in v1 of the integration -
	/// the Deluge transport's own swing already affects the clip position the engine
	/// follows; this stays for a later refinement.)
	bool swing{false};

	/// Engine parameters (X/Y, randomness, per-bank density, accent threshold).
	TopoSettings engine;

	/// Which note row (kit lane) each engine bank drives. -1 = unassigned.
	int8_t bankRows[kNumBanks]{-1, -1, -1};
};

} // namespace deluge::model::topo
