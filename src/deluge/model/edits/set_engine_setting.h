#pragma once

#include "deluge-project/instrument_slot.h"
#include "model/edits/edit.h"
#include "serialization/value.h"

#include <string>

namespace deluge::edits {

/// Set one setting of a sound's instrument (a path of its settings schema, its effects' included) to a value. apply()
/// writes the value into the slot @c slot names and commits the slot: the sound playing it is taken to the state the
/// slot now says, through the setters of what changed. Settings record no undo.
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
