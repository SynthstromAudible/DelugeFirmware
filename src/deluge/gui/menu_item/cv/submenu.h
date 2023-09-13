#pragma once
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/submenu.h"
#include <initializer_list>
#include <string_view>

namespace deluge::gui::menu_item::cv {
class Submenu : public menu_item::Submenu, public FormattedTitle {
public:
	Submenu(l10n::String newName, std::initializer_list<MenuItem*> newItems)
	    : menu_item::Submenu::Submenu(newName, newItems), FormattedTitle(newName) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }
};
} // namespace deluge::gui::menu_item::cv
