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

#include "model/snapshot/param_snapshot.h"

#include "deluge-project/param_automation.h" // DocAutoParam
#include "deluge-project/param_edit.h"       // deluge::project::setValueForRegion
#include "deluge-project/song.h"             // Song::resolveParam

namespace deluge::snapshot {

std::optional<bool> ParamSnapshot::applyPatch(deluge::project::Song& model,
                                              const deluge::project::ParamAddress& address, int32_t value, int32_t pos,
                                              int32_t length, const std::optional<RegionContext>& region) {
	deluge::project::DocAutoParam* doc = model.resolveParam(address);
	if (doc == nullptr) {
		// Engine-only lane (never-saved owner not yet id-addressed) — nothing to patch in the model; the engine's
		// own store still carries it. Coarse fallback (projectFromStoreAfterRead) covers the load path.
		return std::nullopt;
	}

	if (!region.has_value()) {
		// Scalar path (the length==0 / no-timeline route in setValuePossiblyForRegion): the in-line poke's
		// characterized effect is a plain current_value sync (M4a Task 1). Nodes are untouched for an
		// unautomated param, and an automated param's node array is likewise not rewritten by the scalar poke.
		doc->current_value = value;
		return std::nullopt;
	}

	// Region path (nonzero length + a live timeline counter): reproduce AutoParam::setValueForRegion's node/value
	// mutation exactly, via the DOM kernel proven bit-equivalent to it across the full engine==DOM matrix in
	// tests/projection/param_automation_test.cpp. The kernel writes doc->nodes and — when the playhead is inside
	// the edited region, or playback is stopped — doc->current_value, and returns the engine's `automationChanged`
	// (nullopt when pos is at/past effectiveLength).
	const RegionContext& r = *region;
	return deluge::project::setValueForRegion(*doc, static_cast<uint32_t>(pos), static_cast<uint32_t>(length), value,
	                                          r.effectiveLength, r.interpolateLeftNode, r.interpolateRightNode,
	                                          r.recordingLinearly, r.clockActive, r.livePos,
	                                          r.currentlyPlayingReversed);
}

} // namespace deluge::snapshot
