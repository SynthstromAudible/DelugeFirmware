#pragma once

/// @file
/// @brief A host chain's effect settings, read by its address.

#include "deluge-project/fx_chain.h"
#include "deluge-project/song.h"
#include "engine-api/types/setting.h"
#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief Reads the effect settings of the chain at an address (a kit's, an audio track's or the master's): each row's
/// held value, as effects::held_effect_setting gives it, whether or not the row applies.
class ChainReader {
public:
	/// @param model The song.
	/// @param chain The chain's address.
	/// @param facts The host's runtime facts, which give the chain's typed effects.
	ChainReader(const project::Song& model, project::ChainAddress chain, const RuntimeFacts& facts);

	/// @brief Whether the song holds the chain and the host has its typed effects.
	[[nodiscard]] bool resolved() const { return effects_ != nullptr; }
	/// @brief The held value of the row at @p path.
	/// @param path A path of effects::kEffectSettings.
	/// @return The value, or nullopt for a path the table does not hold or a chain that does not resolve.
	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const;
	/// @brief This reader as the engine's SettingReader; valid while this object lives.
	[[nodiscard]] engine::SettingReader reader() const;

private:
	const effects::EffectStates* effects_{nullptr};
};

} // namespace deluge::scenes::bind
