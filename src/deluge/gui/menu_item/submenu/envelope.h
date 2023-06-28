#pragma once
#include "gui/menu_item/submenu.h"

namespace menu_item::submenu {
class Envelope final : public SubmenuReferringToOneThing {
public:
	Envelope() {}
	Envelope(char const* newName, MenuItem** newItems, int newSourceIndex)
	    : SubmenuReferringToOneThing(newName, newItems, newSourceIndex) {}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom = NULL) {
		SubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
		setEnvelopeNumberForTitles(thingIndex);
	}
#endif
};
}
