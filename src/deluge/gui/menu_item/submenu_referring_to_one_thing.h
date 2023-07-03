#pragma once
#include "submenu.h"

namespace menu_item {

class SubmenuReferringToOneThing : public Submenu {
public:
	SubmenuReferringToOneThing() {}
	SubmenuReferringToOneThing(char const* newName, MenuItem** newItems, int newThingIndex)
	    : Submenu(newName, newItems) {
		thingIndex = newThingIndex;
	}
	void beginSession(MenuItem* navigatedBackwardFrom = NULL);

	uint8_t thingIndex;
};
} // namespace menu_item
