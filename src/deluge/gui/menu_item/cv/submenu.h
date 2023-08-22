#pragma once
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/submenu.h"
#include <string_view>

namespace deluge::gui::menu_item::cv {
class Submenu : public menu_item::Submenu<2>, public FormattedTitle {
public:
	Submenu(l10n::String newName, MenuItem* const (&newItems)[2])
	    : menu_item::Submenu<2>::Submenu(newName, newItems), FormattedTitle(newName) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }
};
} // namespace deluge::gui::menu_item::cv
