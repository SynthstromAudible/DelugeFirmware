#include "ui/grid/favourites.h"

namespace deluge::ui::grid {

namespace {

/// @brief `QwertyUI`'s `colourStep`: the 16 colours spread over 192 of the hue wheel.
constexpr int32_t kColourStep = 192 / static_cast<int32_t>(kFavourites);

} // namespace

std::array<RGB, kFavourites> favourite_row_colours(std::span<const std::optional<uint8_t>, kFavourites> colours) {
	std::array<RGB, kFavourites> out{};
	for (size_t i = 0; i < kFavourites; i++) {
		out[i] =
		    colours[i].has_value() ? RGB::fromHue(*colours[i] * kColourStep) : RGB::fromHue(8 * kColourStep).dim(6);
	}
	return out;
}

std::array<RGB, kFavourites> favourite_bank_colours() {
	std::array<RGB, kFavourites> out{};
	for (size_t i = 0; i < kFavourites; i++) {
		out[i] = RGB::fromHue(static_cast<int32_t>(i) * kColourStep);
	}
	return out;
}

} // namespace deluge::ui::grid
