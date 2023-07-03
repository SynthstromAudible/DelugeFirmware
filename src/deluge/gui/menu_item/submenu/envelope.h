#pragma once
#include "gui/menu_item/submenu.h"

extern void setEnvelopeNumberForTitles(int);

namespace menu_item::submenu {
class Envelope final : public SubmenuReferringToOneThing {
public:
	using SubmenuReferringToOneThing::SubmenuReferringToOneThing;
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom = NULL) {
		SubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
		setEnvelopeNumberForTitles(thingIndex);
	}
#endif
};
}
