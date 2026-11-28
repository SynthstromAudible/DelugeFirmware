#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/favourites.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/grid/support/flashes.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

using namespace deluge::ui::views::grid;
using deluge::ui::views::grid::test::flashes_of;
using deluge::ui::widgets::grid::favourite_bank_colours;
using deluge::ui::widgets::grid::favourite_row_colours;
using deluge::ui::widgets::grid::FlashColour;
using deluge::ui::widgets::grid::kFavourites;
using deluge::ui::widgets::grid::PadFlash;
namespace uitest = deluge::ui::test;

namespace {

/// Favourites set in slots 0, 3 and 9, the rest empty.
std::array<RGB, kFavourites> some_set() {
	std::array<std::optional<uint8_t>, kFavourites> slots{};
	slots[0] = 2;
	slots[3] = 7;
	slots[9] = 12;
	return favourite_row_colours(slots);
}

/// @p e as the input of @p r's next pass.
void press(uitest::GridRig& r, const deluge::ui::widgets::core::PadEvent& e) {
	r.input.pads = std::span<const deluge::ui::widgets::core::PadEvent>(&e, 1);
}

} // namespace

// clang-format off
describe favourites_v("ui::views::grid::favourites", $ {
	it("draws the favourites over the banks, flashes the current favourite, and matches its golden", _ {
		uitest::GridRig r;
		(void)favourites(r.frame, {.row = 6, .colours = some_set(), .banks = favourite_bank_colours(),
		                           .flash_favourite = 3, .flash_bank = std::nullopt});
		expect(flashes_of(r)).to_equal(std::vector<PadFlash>{PadFlash{.x = 3, .y = 6, .colour = FlashColour::Default}});
		expect(r.grid.at(4, 7)).to_equal(favourite_bank_colours()[4]);
		expect_golden(self, "ui/views/grid/favourites_banks.state", r.state());
	});

	it("flashes the current bank on the row below", _ {
		uitest::GridRig r;
		(void)favourites(r.frame, {.row = 6, .colours = some_set(), .banks = favourite_bank_colours(),
		                           .flash_favourite = std::nullopt, .flash_bank = 2});
		expect(flashes_of(r)).to_equal(std::vector<PadFlash>{PadFlash{.x = 2, .y = 7, .colour = FlashColour::Default}});
	});

	it("returns a press on the favourites row as the favourite's index, and leaves the bank's empty", _ {
		uitest::GridRig r;
		const deluge::ui::widgets::core::PadEvent e{.x = 5, .y = 7, .velocity = 64, .pressed = true};
		press(r, e);
		const FavouriteResponses resp = favourites(r.frame, {.row = 7, .colours = some_set(), .banks = std::nullopt,
		                                                     .flash_favourite = std::nullopt, .flash_bank = std::nullopt});
		expect(resp.favourite.pressed).to_be_true();
		expect(static_cast<int>(resp.favourite.index)).to_equal(5);
		expect(resp.bank.pressed).to_be_false();
	});

	it("returns a press on the bank row as the bank's index", _ {
		uitest::GridRig r;
		const deluge::ui::widgets::core::PadEvent e{.x = 5, .y = 7, .velocity = 64, .pressed = true};
		press(r, e);
		const FavouriteResponses resp = favourites(r.frame, {.row = 6, .colours = some_set(),
		                                                     .banks = favourite_bank_colours(),
		                                                     .flash_favourite = std::nullopt, .flash_bank = std::nullopt});
		expect(resp.bank.pressed).to_be_true();
		expect(static_cast<int>(resp.bank.index)).to_equal(5);
		expect(resp.favourite.pressed).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(views_grid_favourites_spec, favourites_v)
