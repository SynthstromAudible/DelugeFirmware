#pragma once

#include "deluge-project/setting_group.h"
#include "model/edits/edit.h"

#include <cstdint>
#include <optional>

namespace deluge::edits {

/// @brief Move one of a MIDI track's mod knobs to another CC, carrying that CC's automation in every clip of the
///        track, and mark the track edited.
///
/// The new CC is the next selectable one @p offset away that no clip of the track automates. Not an undo step, as
/// legacy's knob CC change logs nothing.
class MoveModKnobCC final : public Edit {
public:
	/// @param track  The MIDI track.
	/// @param knob   Which knob: knob 0 then 1 of each mod button in turn.
	/// @param offset Which way, and how far, to look for the next CC.
	MoveModKnobCC(project::TrackAddress track, uint8_t knob, int32_t offset)
	    : track_(track), knob_(knob), offset_(offset) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED for a track that is not a MIDI track of the song, a knob past the last, or no CC
	///         free in every clip. A knob that sends no CC is left as it is, and moved_to() names its word.
	Error apply(Context& ctx) override;

	/// @brief The knob's CC after a successful apply.
	[[nodiscard]] int32_t moved_to() const { return moved_to_; }

private:
	project::TrackAddress track_;
	uint8_t knob_;
	int32_t offset_;
	int32_t moved_to_{-1};
};

/// @brief Move @p track's knob @p knob @p offset CCs on through MoveModKnobCC on the current song.
/// @return The knob's new CC, or nullopt where the Edit refused.
std::optional<int32_t> moveModKnobCCFor(project::TrackAddress track, uint8_t knob, int32_t offset);

} // namespace deluge::edits
