#include "ui/grid/favourites.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
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
		expect(row[0]).to_equal(RGB::fromHue(4 * 12));
		expect(row[15]).to_equal(RGB::fromHue(0));
		expect(row[1]).to_equal(RGB::fromHue(8 * 12).dim(6));
		expect(row[1]).not_().to_equal(row[0]);
	});

	it("paints bank i in the hue of colour i", _ {
		const std::array<RGB, kFavourites> banks = favourite_bank_colours();
		for (int32_t i = 0; i < static_cast<int32_t>(kFavourites); i++) {
			expect(banks[static_cast<size_t>(i)]).to_equal(RGB::fromHue(i * 12));
		}
	});
});
// clang-format on

DELUGE_SPEC(favourites_spec, favourites_row)
