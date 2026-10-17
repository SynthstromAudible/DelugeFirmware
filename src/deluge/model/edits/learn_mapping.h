#pragma once

/// @file
/// @brief MIDI learn and unlearn as Edits on the song's mapping table, and the gestures that apply them.

#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "model/edits/edit.h"
#include "modulation/params/param_descriptor.h"

#include <optional>
#include <utility>

class LearnedMIDI;
class MIDICable;
class ModControllableAudio;
class Song;

namespace deluge::edits {

/// @brief Learn a binding: write it into the song's mapping table, then project the target's entries onto the engine
/// field that plays them.
///
/// The table (`deluge-project/mapping_table.h`) is the bindings' truth; the projection
/// (`model/project_bridge/mapping_bridge.h`) brings the engine's copy into line.
///
/// @note Learning records no undo.
class LearnMapping final : public Edit {
public:
	/// @param mapping  The binding to learn (MappingTable::learn's rules decide what it replaces).
	/// @param replaces For a ParamTarget: the source of the knob it takes the place of (MappingTable::relearn).
	explicit LearnMapping(deluge::project::Mapping mapping,
	                      std::optional<deluge::project::MidiCommand> replaces = std::nullopt)
	    : mapping_(std::move(mapping)), replaces_(std::move(replaces)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED, with the table as it was, when the target's owner is not in the song; otherwise
	///         Error::NONE.
	Error apply(Context& ctx) override;

private:
	deluge::project::Mapping mapping_;
	std::optional<deluge::project::MidiCommand> replaces_;
};

/// @brief Unlearn a target: drop its entries from the song's mapping table (for a param, every knob on it), then
/// project what is left onto the engine field that plays it. A target the table holds nothing for is left as it is.
///
/// @note Unlearning records no undo.
class UnlearnMapping final : public Edit {
public:
	/// @param target What to unlearn.
	explicit UnlearnMapping(deluge::project::MappingTarget target) : target_(std::move(target)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED, with the table as it was, when the target's owner is not in the song; otherwise
	///         Error::NONE.
	Error apply(Context& ctx) override;

private:
	deluge::project::MappingTarget target_;
};

/// @brief A gesture's write of @p learned as @p target's binding in @p song: LearnMapping of the command it holds, or
/// UnlearnMapping when it holds nothing.
/// @param song    The song.
/// @param target  What the command controls.
/// @param learned The command, as the gesture learned it.
/// @return The Edit's result.
Error learnedMappingIn(::Song& song, deluge::project::MappingTarget target, const ::LearnedMIDI& learned);

/// @brief learnedMappingIn on the current song (no-op without a song).
/// @param target  What the command controls.
/// @param learned The command, as the gesture learned it.
void learnedMappingFor(deluge::project::MappingTarget target, const ::LearnedMIDI& learned);

/// @brief A menu's learn of a MIDI knob: @p cable's @p noteOrCC on @p channel turns @p param of @p owner. Through
/// LearnMapping when @p owner holds controls of the current song; on @p owner's knob list alone otherwise. A knob on
/// @p param that this MIDI would trigger (LearnedMIDI's rule, devices told apart only by the setting that tells inputs
/// apart) is rebound to it in place; otherwise the knob is added.
/// @param owner    The owner whose param the knob turns.
/// @param param    The param.
/// @param cable    The cable the knob was heard on.
/// @param noteOrCC The CC number (128 for pitch bend).
/// @param channel  The channel it was heard on.
/// @return Whether the knob was learned.
bool learnKnobFor(::ModControllableAudio& owner, ParamDescriptor param, ::MIDICable* cable, uint8_t noteOrCC,
                  uint8_t channel);

/// @brief A menu's unlearn of every MIDI knob on @p param of @p owner. Through UnlearnMapping when @p owner holds
/// controls of the current song; on @p owner's knob list alone otherwise. A preset value no knob can reach any more is
/// zeroed, as an unlearn always has.
/// @param owner The owner.
/// @param param The param.
/// @return Whether any knob was unlearned.
bool unlearnKnobsFor(::ModControllableAudio& owner, ParamDescriptor param);

/// @brief A gesture's learn: @p source to @p target in the current song, through LearnMapping (no-op without a song).
/// @param source The command learned.
/// @param target What it controls.
void learnMappingFor(deluge::project::MidiCommand source, deluge::project::MappingTarget target);

/// @brief A gesture's unlearn of @p target in the current song, through UnlearnMapping (no-op without a song).
/// @param target What to unlearn.
void unlearnMappingFor(deluge::project::MappingTarget target);

} // namespace deluge::edits
