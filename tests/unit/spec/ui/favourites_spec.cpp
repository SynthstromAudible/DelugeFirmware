#include "ui/grid/favourites.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <array>
#include <cstdint>
#include <optional>

using namespace deluge::ui::grid;

// `QwertyUI::renderFavourites`: a set favourite in its colour's hue (`colour * colourStep`, the step 192 / 16), an
// empty one the hue of colour 8 dimmed six times; bank `i` in the hue of colour `i`.

// clang-format off
describe favourites_row("ui::grid favourites row colours", $ {
	it("paints a set favourite in its colour's hue and an empty one dimmed", _ {
		std::array<std::optional<uint8_t>, kFavourites> colours{};
		colours[0] = 4;
		colours[15] = 0;
		const std::array<RGB, kFavourites> row = favourite_row_colours(colours);
		expect(row[0] == RGB::fromHue(4 * 12)).to_be_true();
		expect(row[15] == RGB::fromHue(0)).to_be_true();
		expect(row[1] == RGB::fromHue(8 * 12).dim(6)).to_be_true();
		expect(row[1] == row[0]).to_be_false();
	});

	it("paints bank i in the hue of colour i", _ {
		const std::array<RGB, kFavourites> banks = favourite_bank_colours();
		for (int32_t i = 0; i < static_cast<int32_t>(kFavourites); i++) {
			expect(banks[static_cast<size_t>(i)] == RGB::fromHue(i * 12)).to_be_true();
		}
	});
});
// clang-format on

DELUGE_SPEC(ui_favourites_spec, favourites_row)
