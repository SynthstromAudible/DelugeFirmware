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
/// param_stack.h), or taken from an engine stack (target_of) where a param change is notified on either tier. It is
/// used at once, so a clip, row or collection that moves later is never read through a stale one; the one holder past
/// its operation is a playhead move's deferred delay notify (playback::MoveEffects), held only until the move's
/// section ends. Its owner is typed, so code that needs the sound or the MIDI instrument behind a lane
/// reaches it without a cast from the engine stack's owner slot.

#include "model/lane_owner.h"
#include "plugin/types/param_kind.h"
#include "project/param_address.h"

#include <cstdint>
#include <optional>
#include <type_traits>

class AutoParam;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class NoteRow;
class ParamCollection;
class ParamCollectionSummary;
class ParamManager;
class Song;
class TimelineCounter;

namespace deluge::project {
struct DocAutoParam;
} // namespace deluge::project

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
	/// The collection's param kind, as the stack carries it.
	plugin::ParamKind kind{};
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

/// @brief The store @p target's lane reads, by the engine stack's tier rule: where its collection keeps it on the
///        render tier or inside a playhead move, the model's own on the interaction tier (which may move the owner's
///        lanes into the tree).
/// @param target The lane.
/// @return The store, or null for a lane nothing holds.
[[nodiscard]] project::DocAutoParam* doc_of(const LaneTarget& target);

/// @brief [render] or [interaction] Tells @p stack's collection its param changed, through the lane @p stack stands
///        for (target_of): the one place an engine stack becomes a notify's lane.
/// @param stack             The changed param.
/// @param oldValue          Its value before the change.
/// @param automationChanged Whether its automation changed.
/// @param automatedBefore   Whether it was automated before the change.
/// @param automatedNow      Whether it is automated now.
void notify_modified(const ::ModelStackWithAutoParam& stack, int32_t oldValue, bool automationChanged,
                     bool automatedBefore, bool automatedNow);

/// A lane is plain data: the move section's queue copies it, and holds it zero-initialised.
static_assert(std::is_trivially_copyable_v<LaneTarget>);

} // namespace deluge::bridge
