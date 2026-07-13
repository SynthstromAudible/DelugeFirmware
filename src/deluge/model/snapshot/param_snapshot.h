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

#include "deluge-project/param_edit.h" // RegionWriteResult (applyPatch's region return)
#include <cstdint>
#include <optional>

namespace deluge::project {
class Song;
struct ParamAddress;
} // namespace deluge::project

namespace deluge::snapshot {

/// Timeline/playback context the region patch needs — exactly what AutoParam::setValueForRegion reads off the
/// engine ModelStack. In M4a it is gathered at the emission point from the still-borrowed engine stack (mirroring
/// apply()'s own funnel-phase borrow); the north-star read-side flip (docs/dev/project_model_north_star.md) will
/// source it from the model/snapshot instead. Every field is the value the engine reads at the same site:
struct RegionContext {
	int32_t effectiveLength;       ///< ModelStackWithAutoParam::getLoopLength()
	bool interpolateLeftNode;      ///< automation-view interpolation-before (false when not on the automation view)
	bool interpolateRightNode;     ///< automation-view interpolation-after
	bool recordingLinearly;        ///< the Clip's getCurrentlyRecordingLinearly()
	bool clockActive;              ///< playbackHandler.isEitherClockActive()
	int32_t livePos;               ///< ModelStackWithAutoParam::getLivePos()
	bool currentlyPlayingReversed; ///< ModelStackWithAutoParam::isCurrentlyPlayingReversed()
};

/// The engine-side read-view + patch protocol of the project-model migration's M4 read boundary (docs/dev/
/// project_model_north_star.md § Snapshot). Its first instance is the param cursor: applyPatch is the incremental,
/// model-first update of one param lane, keyed by its durable ParamAddress and driven by a SetParamValue event —
/// equivalent to the in-line AutoParam poke but sourced from the model store rather than an engine poke. Depends on
/// deluge-project only; the model never references this type, so the net dependency stays engine→model.
class ParamSnapshot {
public:
	/// Patch one param lane in `model`, addressed by its durable id — the incremental alternative to a full
	/// projectFromStoreAfterRead rebuild (which stays the fallback for load / coarse invalidation, never this
	/// per-edit path). `region` absent → the scalar path: make the store's current_value authoritative (the in-line
	/// poke's characterized scalar effect; nodes untouched). `region` present → the region path: reproduce
	/// AutoParam::setValueForRegion's node/current_value mutation via the DOM kernel proven bit-equivalent to it
	/// (the engine==DOM matrix in tests/projection/param_automation_test.cpp).
	///
	/// Returns the region kernel's `automationChanged` (what the engine's notifyParamModifiedInSomeWay needs), or
	/// `std::nullopt` for the scalar path and when the address does not resolve (no-op).
	static std::optional<deluge::project::RegionWriteResult> applyPatch(deluge::project::Song& model,
	                                                                    const deluge::project::ParamAddress& address,
	                                                                    int32_t value, int32_t pos, int32_t length,
	                                                                    const std::optional<RegionContext>& region);
};

} // namespace deluge::snapshot
