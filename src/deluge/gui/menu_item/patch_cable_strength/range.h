#pragma once
#include "gui/menu_item/patch_cable_strength.h"

namespace menu_item::patch_cable_strength {
class Range final : public PatchCableStrength {
public:
	Range(char const* newName = NULL) : PatchCableStrength(newName) {}
	ParamDescriptor getDestinationDescriptor();
	uint8_t getS();
	ParamDescriptor getLearningThing();
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour);
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive);
};
extern Range rangeMenu;
} // namespace menu_item::patch_cable_strength
