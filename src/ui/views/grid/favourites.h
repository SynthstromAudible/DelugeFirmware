#pragma once
#include "ui/gfx/grid/rgb.h"
#include "ui/widgets/core/response.h"
#include "ui/widgets/grid/favourites.h"
#include "ui/widgets/grid/frame.h"

#include <array>
#include <cstdint>
#include <optional>

/// @file
/// @brief The sample browser's favourites row, and the bank row beneath it, with the current ones flashing.

namespace deluge::ui::views::grid {

/// @brief What the favourites rows show.
struct FavouritePads {
	int32_t row; ///< The favourites row; the bank row, when shown, is the one below it.
	std::array<RGB, widgets::grid::kFavourites> colours;              ///< The favourites row, left to right.
	std::optional<std::array<RGB, widgets::grid::kFavourites>> banks; ///< The bank row, or none without banks.
	std::optional<uint8_t> flash_favourite;                           ///< The favourite to flash this pass, or none.
	std::optional<uint8_t> flash_bank;                                ///< The bank to flash this pass, or none.
};

/// @brief What the two rows reported.
struct FavouriteResponses {
	widgets::core::Response favourite; ///< The favourites row's `row` response.
	widgets::core::Response bank;      ///< The bank row's, empty without banks.
};

/// @brief `QwertyUI::renderFavourites`, then `timerCallback`'s flash of the current favourite and bank: each row
///        through `row`, then the flashes over them.
/// @param f A frame over the pads.
/// @param d What to show.
/// @return Each row's response.
FavouriteResponses favourites(widgets::grid::GridFrame& f, const FavouritePads& d);

} // namespace deluge::ui::views::grid
