#pragma once

#include "deluge-project/controller_mapping.h"
#include "model/edits/edit.h"

#include <vector>

namespace deluge::edits {

/// Set an owner's controller mappings: a sound's mod knobs and learned MIDI knobs, or a kit's, an audio track's or the
/// song's learned MIDI knobs. apply() writes the model's record, the truth for them, and makes the runtime's knobs what
/// it says. Records no undo.
class SetControls final : public Edit {
public:
	SetControls(deluge::project::ControlsAddress owner, std::vector<deluge::project::ControllerMapping> controls)
	    : owner_(owner), controls_(std::move(controls)) {}

	Error apply(Context& ctx) override;

private:
	deluge::project::ControlsAddress owner_;
	std::vector<deluge::project::ControllerMapping> controls_;
};

} // namespace deluge::edits
