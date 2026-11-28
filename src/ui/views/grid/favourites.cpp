#include "ui/views/grid/favourites.h"

#include "ui/widgets/grid/rows.h"

namespace deluge::ui::views::grid {

FavouriteResponses favourites(widgets::grid::GridFrame& f, const FavouritePads& d) {
	FavouriteResponses r{};
	r.favourite = widgets::grid::row(f, d.row, d.colours);
	if (d.banks.has_value()) {
		r.bank = widgets::grid::row(f, d.row + 1, *d.banks);
	}
	if (d.flash_favourite.has_value()) {
		f.flash(*d.flash_favourite, d.row, widgets::grid::FlashColour::Default);
	}
	if (d.flash_bank.has_value()) {
		f.flash(*d.flash_bank, d.row + 1, widgets::grid::FlashColour::Default);
	}
	return r;
}

} // namespace deluge::ui::views::grid
