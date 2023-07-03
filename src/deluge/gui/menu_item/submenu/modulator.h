#pragma once
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "processing/sound/sound.h"

extern void setModulatorNumberForTitles(int);

namespace menu_item::submenu {
class Modulator final : public SubmenuReferringToOneThing {
public:
	using SubmenuReferringToOneThing::SubmenuReferringToOneThing;
#if HAVE_OLED
	void beginSession(MenuItem* navigatedBackwardFrom) {
		setModulatorNumberForTitles(thingIndex);
		SubmenuReferringToOneThing::beginSession(navigatedBackwardFrom);
	}
#endif
	bool isRelevant(Sound* sound, int whichThing) {
		return (sound->synthMode == SYNTH_MODE_FM);
	}
};

}
