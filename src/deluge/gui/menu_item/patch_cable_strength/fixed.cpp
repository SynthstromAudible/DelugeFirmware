#include "fixed.h"
#include "modulation/patch/patch_cable_set.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"
#include "processing/sound/sound.h"
#include "storage/multi_range/multi_range.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::patch_cable_strength {
int Fixed::checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {
	soundEditor.patchingParamSelected = p;
	source_selection::regularMenu.s = s;
	return PatchCableStrength::checkPermissionToBeginSession(sound, whichThing, currentRange);
}

uint8_t Fixed::shouldBlinkPatchingSourceShortcut(int s, uint8_t* sourceShortcutBlinkColours) {

	PatchCableSet* patchCableSet = soundEditor.currentParamManager->getPatchCableSet();

	// If it's the source controlling the range of the source we're editing for...
	if (patchCableSet->getPatchCableIndex(s, getLearningThing()) != 255) {
		return 3;
	}
	else {
		return 255;
	}
}

MenuItem* Fixed::patchingSourceShortcutPress(int s, bool previousPressStillActive) {
	source_selection::rangeMenu.s = s;
	return &patch_cable_strength::rangeMenu;
}

} // namespace menu_item::patch_cable_strength
