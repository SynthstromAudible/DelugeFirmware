#pragma once

/// @file
/// @brief A sound's settings, read by its slot's address.

#include "plugin/types/setting.h"
#include "project/instrument_slot.h"
#include "project/song.h"
#include "ui/scenes/bind/ports/sound_facts.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::ui::scenes::bind {

/// @brief Reads the settings of the sound at a slot address: each row's held value, as
/// instruments::classic::held_setting gives it, whether or not the row applies.
class SlotReader {
public:
	/// @param model The song.
	/// @param slot  The sound's slot.
	/// @param sound The sound facts, which give the sound's typed state.
	SlotReader(const project::Song& model, project::SlotAddress slot, const SoundFacts& sound);

	/// @brief Whether the slot exists and its sound has a typed state.
	[[nodiscard]] bool resolved() const { return state_ != nullptr; }
	/// @brief The held value of the row at @p path.
	/// @param path A path of instruments::classic::kSettings.
	/// @return The value, or nullopt for a path the table does not hold or a slot that does not resolve.
	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const;
	/// @brief This reader as the engine's SettingReader; valid while this object lives.
	[[nodiscard]] plugin::SettingReader reader() const;

private:
	const instruments::classic::SoundState* state_{nullptr};
};

} // namespace deluge::ui::scenes::bind
