#pragma once

#include "deluge-project/controller_mapping.h"
#include "model/edits/edit.h"

#include <vector>

namespace deluge::edits {

/// @brief Set an owner's controller mappings: a sound's mod knobs.
///
/// apply() writes the model's record, the truth for the mod knobs, and makes the runtime's mod knobs what it says. The
/// owner's learned MIDI knobs are the song's mapping table's (LearnMapping, UnlearnMapping) and are left as they are.
///
/// @note Records no undo.
class SetControls final : public Edit {
public:
	/// @param owner    Address of the owner whose mappings change.
	/// @param controls The complete mapping list the owner takes.
	SetControls(deluge::project::ControlsAddress owner, std::vector<deluge::project::ControllerMapping> controls)
	    : owner_(owner), controls_(std::move(controls)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the owner cannot be resolved.
	Error apply(Context& ctx) override;

private:
	deluge::project::ControlsAddress owner_;
	std::vector<deluge::project::ControllerMapping> controls_;
};

} // namespace deluge::edits
