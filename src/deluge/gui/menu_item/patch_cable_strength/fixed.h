#pragma once
#include "regular.h"

namespace menu_item::patch_cable_strength {

class Fixed : public Regular {
public:
	Fixed(char const* newName = NULL, int newP = 0, int newS = 0) : Regular(newName) {
		p = newP;
		s = newS;
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) final;
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final;
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive) final;

protected:
	uint8_t p;
	uint8_t s;
};
} // namespace menu_item::patch_cable_strength
