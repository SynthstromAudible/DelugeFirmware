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
/// @brief One param lane resolved for one operation: what it is, whose it is, and where the engine keeps it.
///
/// A LaneTarget is resolved from a ParamAddress on the interaction tier (the bridge's lane functions in
/// param_stack.h) and used at once; it is never stored, so a clip, row or collection that moves later is never read
/// through a stale one. Its owner is typed, so code that needs the sound or the MIDI instrument behind a lane reaches
/// it without a cast from the engine stack's owner slot.

#include "model/lane_owner.h"
#include "project/param_address.h"

#include <cstdint>
#include <optional>

class AutoParam;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class NoteRow;
class ParamCollection;
class ParamCollectionSummary;
class ParamManager;
class Song;
class TimelineCounter;

namespace deluge::bridge {

using model::LaneOwner;
using model::owner_of;

/// @brief One lane resolved for one operation. Never stored past it.
struct LaneTarget {
	/// The lane's address; absent for a lane with no owner in a song (a sound being set up, a preview).
	std::optional<project::ParamAddress> address;
	::Song* song{nullptr};
	/// The clip whose params these are, or the song for the song's own; null for an owner's backed-up params.
	::TimelineCounter* timeline{nullptr};
	int32_t note_row_id{0};
	/// The kit row or melodic row whose params these are; null otherwise.
	::NoteRow* note_row{nullptr};
	LaneOwner owner;
	::ParamManager* manager{nullptr};
	::ParamCollection* collection{nullptr};
	::ParamCollectionSummary* summary{nullptr};
	/// The param's runtime index in @ref collection.
	int32_t param_id{0};
	::AutoParam* auto_param{nullptr};
};

/// @brief The engine stack of @p target's owner (no collection or param), built in @p memory.
/// @param target The lane.
/// @param memory MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack.
[[nodiscard]] ::ModelStackWithThreeMainThings* owner_stack_of(const LaneTarget& target, void* memory);

/// @brief The engine stack @p target stands for, built in @p memory, for engine code that takes one.
/// @param target The lane.
/// @param memory MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack.
[[nodiscard]] ::ModelStackWithAutoParam* stack_of(const LaneTarget& target, void* memory);

/// @brief The target @p stack names, its owner the stack's own.
/// @param stack   The engine stack.
/// @param address The lane's address, if it has one.
/// @return The target.
[[nodiscard]] LaneTarget target_of(const ::ModelStackWithAutoParam& stack,
                                   std::optional<project::ParamAddress> address);

} // namespace deluge::bridge
