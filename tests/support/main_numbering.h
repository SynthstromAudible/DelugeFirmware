#pragma once

/// @file
/// @brief Main's numbering of the arp's params, as this tree numbers them, for the specs whose records were taken from
///        main. Main keeps the arp's params in a sound's and a chain's unpatched sets: a sound's at runtime indices 11
///        to 25 (ids 101 to 115), a chain's the same plus its arp rate at index 30 (id 120); and a sound's arp rate is
///        its patched param 54. Here they are the arp's own (arp/params.h), the patched row 54 is the matrix's arp-rate
///        destination alone, and every unpatched param past them sits 15 lower (16 past a chain's arp rate).

#include "arp/params.h"
#include "instruments/classic/params.h"
#include "project/param_address.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::ui::scenes::spec::main_numbering {

/// Main's first id of the unpatched sets.
inline constexpr uint32_t kUnpatchedFirstId = 90;

/// Main's patched id (and runtime index) of a sound's arp rate.
inline constexpr uint32_t kPatchedArpRate = 54;

/// @brief The arp id of main's patched param @p id, or nullopt for one that is not an arp param.
[[nodiscard]] constexpr std::optional<uint32_t> arp_id_of_patched(uint32_t id) {
	return id == kPatchedArpRate ? std::optional<uint32_t>{arp::param::kRate.value} : std::nullopt;
}

/// @brief This tree's runtime index of main's patched index @p index: main numbers a patched param by its id, which
///        this tree's runtime keeps with the per-voice params first (instruments::classic::flat_index).
/// @param index Main's patched index, not the arp rate's.
[[nodiscard]] constexpr int32_t patched_index_of(int32_t index) {
	return static_cast<int32_t>(instruments::classic::flat_index(plugin::ParamId{static_cast<uint32_t>(index)}));
}

/// @brief The arp id of main's unpatched runtime index @p index, or nullopt for one that is not an arp param.
/// @param chain Whether the index is a chain's (where index 30 is the kit's arp rate).
/// @param index Main's runtime index in the unpatched set.
[[nodiscard]] constexpr std::optional<uint32_t> arp_id_of_index(bool chain, int32_t index) {
	// Main's sound indices 11 to 25, in main's order, as the arp's ids.
	constexpr std::array<uint32_t, 15> kArpIds{0, 2, 3, 4, 5, 6, 10, 7, 8, 9, 11, 12, 14, 15, 13};
	if (chain && index == 30) {
		return arp::param::kRate.value;
	}
	if (index >= 11 && index <= 25) {
		return kArpIds[static_cast<std::size_t>(index - 11)];
	}
	return std::nullopt;
}

/// @brief This tree's runtime index of main's unpatched index @p index, for a param that is not an arp param.
/// @param index Main's runtime index (not one of the arp's).
[[nodiscard]] constexpr int32_t index_of(int32_t index) {
	if (index <= 10) {
		return index;
	}
	return index > 30 ? index - 16 : index - 15;
}

/// @brief Main's unpatched param id @p id, as this tree names it: its arp id in ARP space, or its id past the arp's.
/// @param chain Whether the id is in a chain's table.
/// @param id    Main's id, at or above kUnpatchedFirstId.
[[nodiscard]] constexpr project::ParamTarget target_of_id(bool chain, uint32_t id) {
	const auto index = static_cast<int32_t>(id - kUnpatchedFirstId);
	if (const std::optional<uint32_t> arp = arp_id_of_index(chain, index)) {
		return {.space = project::ParamSpace::ARP, .id = plugin::ParamId{*arp}};
	}
	return project::engine_target(kUnpatchedFirstId + static_cast<uint32_t>(index_of(index)));
}

/// @brief Main's id of this tree's unpatched param id @p ours (not an arp param): the inverse of target_of_id.
/// @param chain Whether the id is in a chain's table.
/// @param ours  This tree's id, at or above kUnpatchedFirstId.
[[nodiscard]] constexpr uint32_t main_id_of(bool chain, uint32_t ours) {
	const uint32_t index = ours - kUnpatchedFirstId;
	if (index <= 10) {
		return ours;
	}
	return ours + (chain && index > 14 ? 16 : 15);
}

/// @brief Main's id of the arp param @p arp: its unpatched id (a sound's, or a chain's when @p chain), or for a
///        sound's rate nullopt (main keeps it patched).
[[nodiscard]] constexpr std::optional<uint32_t> main_id_of_arp(bool chain, uint32_t arp) {
	for (int32_t index = 11; index <= 30; ++index) {
		if (arp_id_of_index(chain, index) == arp) {
			return kUnpatchedFirstId + static_cast<uint32_t>(index);
		}
	}
	return std::nullopt;
}

} // namespace deluge::ui::scenes::spec::main_numbering
