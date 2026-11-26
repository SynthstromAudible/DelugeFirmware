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

#include "project/param_address.h"

#include <cstdint>
#include <optional>
#include <variant>

class AudioOutput;
class AutoParam;
class Drum;
class Kit;

class ModControllable;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class NonAudioDrum;
class NonAudioInstrument;
class NoteRow;
class Output;
class ParamCollection;
class ParamCollectionSummary;
class ParamManager;
class Song;
class Sound;
class TimelineCounter;

namespace deluge::model {
class MasterChannel;
}

namespace deluge::bridge {

/// @brief The object a lane's params belong to: a synth or a sound drum (its Sound), a kit's own effects, an audio
/// track, a MIDI or CV track, a MIDI or gate drum, or the song's master channel. Empty for none.
using LaneOwner = std::variant<std::monostate, ::Sound*, ::Kit*, ::AudioOutput*, ::NonAudioInstrument*, ::NonAudioDrum*,
                               model::MasterChannel*>;

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

/// @brief The typed owner of @p output's params: its Sound for a synth, the kit, the audio output, the MIDI or CV
/// instrument.
/// @param output The output.
/// @return The owner; empty for an output of no type.
[[nodiscard]] LaneOwner owner_of(::Output& output);

/// @brief The typed owner of @p drum's params: its Sound for a sound drum, else the MIDI or gate drum.
/// @param drum The drum.
/// @return The owner.
[[nodiscard]] LaneOwner owner_of(::Drum& drum);

/// @brief @p owner as the engine stack's owner slot holds it.
/// @param owner The typed owner.
/// @return The owner slot's value; null for none.
[[nodiscard]] ::ModControllable* mod_controllable_of(const LaneOwner& owner);

/// @brief The engine stack of @p target's owner (no collection or param), built in @p memory.
/// @param target The lane.
/// @param memory MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack.
[[nodiscard]] ::ModelStackWithThreeMainThings* owner_stack_of(const LaneTarget& target, void* memory);

/// @brief The engine stack @p target stands for, built in @p memory, for engine code that still takes one.
/// @param target The lane.
/// @param memory MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack.
[[nodiscard]] ::ModelStackWithAutoParam* stack_of(const LaneTarget& target, void* memory);

/// @brief The target @p stack names.
/// @param stack   The engine stack.
/// @param owner   The object its owner slot holds, typed.
/// @param address The lane's address, if it has one.
/// @return The target.
[[nodiscard]] LaneTarget target_of(const ::ModelStackWithAutoParam& stack, LaneOwner owner,
                                   std::optional<project::ParamAddress> address);

} // namespace deluge::bridge
