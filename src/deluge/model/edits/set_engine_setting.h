#pragma once

#include "deluge-project/instrument_slot.h"
#include "model/edits/edit.h"
#include "serialization/value.h"

#include <string>

namespace deluge::edits {

/// Set one setting of a sound's instrument (a path of its settings schema) to a value. apply() resolves the slot @c
/// slot names in the model and the sound playing it, runs the runtime's own setter for the path, then makes the slot
/// hold the sound's state again. Settings record no undo.
class SetEngineSetting final : public Edit {
public:
	SetEngineSetting(deluge::project::SlotAddress slot, std::string path, deluge::serialization::Value value)
	    : slot_(slot), path_(std::move(path)), value_(std::move(value)) {}

	Error apply(Context& ctx) override;

private:
	deluge::project::SlotAddress slot_;
	std::string path_;
	deluge::serialization::Value value_;
};

} // namespace deluge::edits
