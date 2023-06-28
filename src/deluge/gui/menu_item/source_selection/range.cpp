#include "range.h"
#include "regular.h"
#include "gui/ui/sound_editor.h"
#include "modulation/params/param_descriptor.h"
#include "gui/menu_item/patch_cable_strength/range.h"

namespace menu_item::source_selection {
Range rangeMenu{};

Range::Range() {
#if HAVE_OLED
	basicTitle = "Modulate depth";
#endif
}

ParamDescriptor Range::getDestinationDescriptor() {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamAndSource(soundEditor.patchingParamSelected, regularMenu.s);
	return descriptor;
}

MenuItem* Range::selectButtonPress() {
	return &patch_cable_strength::rangeMenu;
}

MenuItem* Range::patchingSourceShortcutPress(int newS, bool previousPressStillActive) {
	return (MenuItem*)0xFFFFFFFF;
}

} // namespace menu_item::source_selection
