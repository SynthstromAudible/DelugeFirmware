#pragma once
#include "gui/menu_item/patch_cable_strength.h"

namespace menu_item::patch_cable_strength {

class Regular : public PatchCableStrength {
public:
	using PatchCableStrength::PatchCableStrength;

	ParamDescriptor getDestinationDescriptor() final;
	uint8_t getS() final;
	ParamDescriptor getLearningThing() final;
	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange);
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour);
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive);
	MenuItem* selectButtonPress() final;
};


extern Regular regularMenu;
}
