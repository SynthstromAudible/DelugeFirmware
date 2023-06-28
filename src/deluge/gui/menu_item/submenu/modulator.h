#pragma once
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "processing/sound/sound.h"


namespace menu_item::submenu {
class Modulator final : public SubmenuReferringToOneThing {
public:
	Modulator(char const* newName = NULL, MenuItem** newFirstItem = NULL, int newSourceIndex = 0)
	    : SubmenuReferringToOneThing(newName, newFirstItem, newSourceIndex) {}
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		setModulatorNumberForTitles(thingIndex);
		MenuItemSubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
	}
#endif
	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->synthMode == SYNTH_MODE_FM);
	}
};

}
