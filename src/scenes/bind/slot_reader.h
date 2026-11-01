#pragma once

/// @file
/// @brief A sound's settings, read by its slot's address.

#include "deluge-project/instrument_slot.h"
#include "deluge-project/song.h"
#include "plugin-api/types/setting.h"
#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief Reads the settings of the sound at a slot address: each row's held value, as instrument::held_setting gives
/// it, whether or not the row applies.
class SlotReader {
public:
	/// @param model The song.
	/// @param slot  The sound's slot.
	/// @param facts The host's runtime facts, which give the sound's typed state.
	SlotReader(const project::Song& model, project::SlotAddress slot, const RuntimeFacts& facts);

	/// @brief Whether the slot exists and its sound has a typed state.
	[[nodiscard]] bool resolved() const { return state_ != nullptr; }
	/// @brief The held value of the row at @p path.
	/// @param path A path of instrument::kSettings.
	/// @return The value, or nullopt for a path the table does not hold or a slot that does not resolve.
	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const;
	/// @brief This reader as the engine's SettingReader; valid while this object lives.
	[[nodiscard]] plugin::SettingReader reader() const;

private:
	const instrument::SoundState* state_{nullptr};
};

} // namespace deluge::scenes::bind
