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

#include "definitions_cxx.hpp"

#include <cstdint>

class Song;

/// Lanes as the render plays them. Outside record mode the render reads each collection's lanes from an immutable
/// snapshot the interaction tier publishes after every edit; it reads no model lane and writes none.
namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
/// Lanes the render resolved through the model outside record mode: must stay 0.
extern uint32_t renderModelReads;
/// Collections whose published snapshot differed from the model after a publish (bits, order, base or nodes): must stay
/// 0.
extern uint32_t staleLanes;
/// Snapshots that could not be built (out of memory): the previous one stayed published.
extern uint32_t publishFailures;
/// Writes the render made to a lane outside record mode: must stay 0.
extern uint32_t renderPublishedWrites;
#endif

/// @brief [render] or [interaction] Whether the render reads lanes from the model as they are written: in record mode,
///        when the latch and recorded values may write any lane.
bool lanesLive();

/// @brief [render] or [interaction] Count a lane the render resolved through the model, if it should have read a
///        snapshot.
void noteRenderModelRead();
/// @brief [render] or [interaction] Count a write to a lane made on the render outside record mode.
void noteRenderLaneWrite();

/// @brief [interaction] Compare every collection of @p song's snapshot with its model (automated bits, key order, each
///        lane's base and nodes).
/// @return The collections that differ (0 while no snapshot exists: a collection with none is not compared).
int32_t checkPublished(Song& song);

} // namespace deluge::modulation
