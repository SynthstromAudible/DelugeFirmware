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

/// @brief Timeline/playback context the region patch needs: exactly what AutoParam::setValueForRegion reads off
///        the engine ModelStack.
///
/// It is gathered at the emission point from the borrowed engine stack. Every field is the value the engine
/// reads at the same site.
struct RegionContext {
	int32_t effectiveLength;       ///< ModelStackWithAutoParam::getLoopLength()
	bool interpolateLeftNode;      ///< automation-view interpolation-before (false when not on the automation view)
	bool interpolateRightNode;     ///< automation-view interpolation-after
	bool recordingLinearly;        ///< the Clip's getCurrentlyRecordingLinearly()
	bool clockActive;              ///< playbackHandler.isEitherClockActive()
	int32_t livePos;               ///< ModelStackWithAutoParam::getLivePos()
	bool currentlyPlayingReversed; ///< ModelStackWithAutoParam::isCurrentlyPlayingReversed()
};

/// @brief The engine-side read-view and patch protocol over the project model (docs/dev/project_model_north_star.md
///        § Snapshot).
///
/// The param cursor: applyPatch is the incremental, model-first update of one param lane, keyed by its durable
/// ParamAddress and driven by a SetParamValue event, equivalent to the in-line AutoParam poke but sourced from the
/// model store.
///
/// @note Depends on deluge-project only; the model never references this type, so the dependency stays
///       engine to model.
class ParamSnapshot {
public:
	/// @brief Patch one param lane in the model, addressed by its durable id.
	///
	/// The incremental alternative to a full projectFromStoreAfterRead rebuild (which is the fallback for load and
	/// coarse invalidation, never this per-edit path). With @p region absent this is the scalar path: it makes the
	/// store's current_value authoritative and leaves nodes untouched. With @p region present it is the region
	/// path: it reproduces AutoParam::setValueForRegion's node/current_value mutation via the DOM kernel, which is
	/// bit-equivalent to it (the engine==DOM matrix in tests/integration/scenarios/drivers/param_projection.cpp).
	///
	/// @param model   The project model to patch.
	/// @param address Durable address of the param lane.
	/// @param value   The new param value.
	/// @param pos     Start position of the edited region, in ticks.
	/// @param length  Length of the edited region, in ticks.
	/// @param region  Timeline context for the region path; absent selects the scalar path.
	/// @return The region kernel's `automationChanged` (what the engine's notifyParamModifiedInSomeWay needs), or
	///         `std::nullopt` for the scalar path and when the address does not resolve (no-op).
	static std::optional<deluge::project::RegionWriteResult> applyPatch(deluge::project::Song& model,
	                                                                    const deluge::project::ParamAddress& address,
	                                                                    int32_t value, int32_t pos, int32_t length,
	                                                                    const std::optional<RegionContext>& region);
};

} // namespace deluge::snapshot
