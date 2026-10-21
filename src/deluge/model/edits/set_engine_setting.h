#pragma once

#include "deluge-project/instrument_slot.h"
#include "model/edits/edit.h"
#include "serialization/value.h"

#include <string>
#include <string_view>

class Song;
class Sound;

namespace deluge::edits {

/// @brief Set one setting of a sound's instrument (a path of its settings schema, its effects' included) to a value.
///
/// apply() writes the value into the slot @c slot names and commits the slot: the sound playing it is taken to the
/// state the slot now says, through the setters of what changed.
///
/// @note Settings record no undo.
/// @brief Commit @p value, already canonical and taken and allowed by @p sound's state, at @p path into @p slot and
/// the sound playing it: the step SetEngineSetting and SetKitSetting share once their checks pass.
/// @param sound The sound playing @p slot.
/// @param slot  Its instrument slot.
/// @param path  The setting's path.
/// @param value The canonical value.
/// @param song  The song.
/// @return Error::NONE when the value landed (or the runtime holds one the slot cannot), else why not.
Error commitEngineSetting(::Sound& sound, deluge::project::InstrumentSlot& slot, std::string_view path,
                          const deluge::serialization::Value& value, ::Song& song);

class SetEngineSetting final : public Edit {
public:
	/// @param slot  Address of the instrument slot.
	/// @param path  Setting path within the instrument's settings schema.
	/// @param value New value; apply() rejects a path or value the sound does not take.
	SetEngineSetting(deluge::project::SlotAddress slot, std::string path, deluge::serialization::Value value)
	    : slot_(slot), path_(std::move(path)), value_(std::move(value)) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED if the slot, sound, path or value is invalid.
	Error apply(Context& ctx) override;

private:
	deluge::project::SlotAddress slot_;
	std::string path_;
	deluge::serialization::Value value_;
};

} // namespace deluge::edits
