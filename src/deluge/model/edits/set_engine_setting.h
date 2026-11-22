#pragma once

#include "instruments/deluge/codec.h"
#include "model/edits/edit.h"
#include "project/instrument_slot.h"
#include "serialization/value.h"

#include <expected>
#include <string>
#include <string_view>

class Song;
class Sound;

namespace deluge::edits {

/// @brief The change writing @p value at @p path makes to @p sound's state: the setting's field written by its family's
/// writer, or where its menu reshapes more (an oscillator's type or loop mode, the synth mode) the state it reshapes
/// to; with the entries whose fields changed.
/// @param sound The sound.
/// @param path  The setting's path.
/// @param value The canonical value, taken and allowed by the sound's state.
/// @return The new state and the entries touched.
[[nodiscard]] std::expected<deluge::instrument::SlotChange, deluge::plugin::StateError>
settingChange(const ::Sound& sound, std::string_view path, const deluge::serialization::Value& value);

/// @brief Commit @p value, already canonical and taken and allowed by @p sound's state, at @p path into @p slot and
/// the sound playing it: the step SetEngineSetting and SetKitSetting share once their checks pass.
/// @param sound The sound playing @p slot.
/// @param slot  Its instrument slot, or null for a sound in none (the sample browser's preview): only the write audit
///              reads it.
/// @param path  The setting's path.
/// @param value The canonical value.
/// @param song  The song.
/// @return Error::NONE when the value landed (or the runtime holds one the state does not read back), else why not.
Error commitEngineSetting(::Sound& sound, const deluge::project::InstrumentSlot* slot, std::string_view path,
                          const deluge::serialization::Value& value, ::Song& song);

/// @brief Set @p sound's setting at @p path to @p value (its file form) where the sound is in no slot (the sample
/// browser's preview): the checks, rules, write and runtime effects SetEngineSetting has, on the sound's own state.
/// @return Error::NONE when the value landed, else why not.
Error setUnslottedSetting(::Sound& sound, std::string_view path, const deluge::serialization::Value& value,
                          ::Song& song);

/// @brief Set one setting of a sound's instrument (a path of its settings schema, its effects' included) to a value.
///
/// apply() writes the value into the slot @c slot names and commits the slot: the sound playing it is taken to the
/// state the slot now says, through the setters of what changed.
///
/// @note Settings record no undo.
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
