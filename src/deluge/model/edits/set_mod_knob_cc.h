#pragma once

#include "deluge-project/doc_output.h"
#include "deluge-project/setting_group.h"
#include "model/edits/edit.h"

#include <cstdint>

namespace deluge::edits {

/// @brief Set what one of a MIDI track's mod knobs sends, in the record and the engine, and mark the track edited.
///
/// Not an undo step: legacy's knob CC change logs nothing.
class SetModKnobCC final : public Edit {
public:
	/// @param track The MIDI track.
	/// @param knob  Which knob: knob 0 then 1 of each mod button in turn.
	/// @param cc    What it sends.
	SetModKnobCC(project::TrackAddress track, uint8_t knob, project::ModKnobCc cc)
	    : track_(track), knob_(knob), cc_(cc) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED for a track that is not a MIDI track of the song, or a knob past the last.
	Error apply(Context& ctx) override;

private:
	project::TrackAddress track_;
	uint8_t knob_;
	project::ModKnobCc cc_;
};

/// @brief Set @p track's knob @p knob to send @p cc through SetModKnobCC on the current song.
/// @return Whether the Edit applied.
bool modKnobCCFor(project::TrackAddress track, uint8_t knob, project::ModKnobCc cc);

} // namespace deluge::edits
