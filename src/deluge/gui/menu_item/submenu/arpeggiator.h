#pragma once
#include "gui/menu_item/submenu.h"

namespace menu_item::submenu {

class Arpeggiator final : public Submenu {
public:
	Arpeggiator() {}
	Arpeggiator(char const* newName, MenuItem** newItems) : Submenu(newName, newItems) {}
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);
};
}
