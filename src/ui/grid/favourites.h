#pragma once
#include "gfx/grid/rgb.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::grid {

/// @brief How many favourites a bank holds, and how many banks there are: one per main column.
inline constexpr size_t kFavourites = 16;

/// @brief The favourites row's colours, as `QwertyUI::renderFavourites` paints them: a set favourite in its colour's
///        hue, an empty one dimmed.
/// @param colours Each slot's colour (0-15), or nothing for an empty slot.
/// @return The row, left to right.
std::array<RGB, kFavourites> favourite_row_colours(std::span<const std::optional<uint8_t>, kFavourites> colours);

/// @brief The bank row's colours, as `QwertyUI::renderFavourites` paints them: bank `i` in the hue of colour `i`.
/// @return The row, left to right.
std::array<RGB, kFavourites> favourite_bank_colours();

} // namespace deluge::ui::grid
