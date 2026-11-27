#pragma once

/// @file
/// @brief What an arp plays by in one block: its params' words, as its owner's param set resolved them.

#include "arp/params.h"

#include <array>
#include <cstdint>

namespace deluge::arp {

/// @brief An arp's param words for one block.
struct Values {
	std::array<int32_t, kParamCount> words{};

	/// @brief The word at @p id.
	[[nodiscard]] constexpr int32_t operator[](plugin::ParamId id) const { return words[id.value]; }

	/// @brief The word at @p id as the engine's unsigned number: word + 2^31.
	[[nodiscard]] constexpr uint32_t unsigned_word(plugin::ParamId id) const {
		return static_cast<uint32_t>(words[id.value]) ^ 0x80000000U;
	}

	/// @brief Set the word at @p id.
	constexpr void set(plugin::ParamId id, int32_t word) { words[id.value] = word; }

	/// @brief The values an arp held by @p owner starts with.
	[[nodiscard]] static constexpr Values defaults(Owner owner) {
		Values v;
		for (std::size_t i = 0; i < kParamCount; ++i) {
			v.words[i] = default_value(owner, id_at(i));
		}
		return v;
	}
};

} // namespace deluge::arp
