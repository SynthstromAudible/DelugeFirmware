#pragma once

/// @file
/// @brief MIDI learn and unlearn as Edits on the song's mapping table, and the gestures that apply them.

#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "model/edits/edit.h"

#include <utility>

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
	/// @param mapping The binding to learn (MappingTable::learn's rules decide what it replaces).
	explicit LearnMapping(deluge::project::Mapping mapping) : mapping_(std::move(mapping)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED, with the table as it was, when the target's owner is not in the song; otherwise
	///         Error::NONE.
	Error apply(Context& ctx) override;

private:
	deluge::project::Mapping mapping_;
};

/// @brief Unlearn a target: drop its entries from the song's mapping table (for a param, every knob on it), then
/// project what is left onto the engine field that plays it. A target the table holds nothing for is left as it is.
///
/// @note Unlearning records no undo.
class UnlearnMapping final : public Edit {
public:
	/// @param target What to unlearn.
	explicit UnlearnMapping(deluge::project::MappingTarget target) : target_(target) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED, with the table as it was, when the target's owner is not in the song; otherwise
	///         Error::NONE.
	Error apply(Context& ctx) override;

private:
	deluge::project::MappingTarget target_;
};

/// @brief A gesture's learn: @p source to @p target in the current song, through LearnMapping (no-op without a song).
/// @param source The command learned.
/// @param target What it controls.
void learnMappingFor(deluge::project::MidiCommand source, deluge::project::MappingTarget target);

/// @brief A gesture's unlearn of @p target in the current song, through UnlearnMapping (no-op without a song).
/// @param target What to unlearn.
void unlearnMappingFor(deluge::project::MappingTarget target);

} // namespace deluge::edits
