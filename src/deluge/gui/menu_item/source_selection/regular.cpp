#include "regular.h"
#include "modulation/params/param_descriptor.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::source_selection {
Regular regularMenu{};

Regular::Regular() {
#if HAVE_OLED
	basicTitle = "Modulate with";
#endif
}

ParamDescriptor Regular::getDestinationDescriptor() {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(soundEditor.patchingParamSelected);
	return descriptor;
}

MenuItem* Regular::selectButtonPress() {
	return &regularMenu;
}

void Regular::beginSession(MenuItem* navigatedBackwardFrom) {

	if (navigatedBackwardFrom) {
		if (soundEditor.patchingParamSelected == PARAM_GLOBAL_VOLUME_POST_REVERB_SEND
		    || soundEditor.patchingParamSelected == PARAM_LOCAL_VOLUME) {
			soundEditor.patchingParamSelected = PARAM_GLOBAL_VOLUME_POST_FX;
		}
	}

	SourceSelection::beginSession(navigatedBackwardFrom);
}

MenuItem* Regular::patchingSourceShortcutPress(int newS, bool previousPressStillActive) {
	s = newS;
	return &regularMenu;
}

} // namespace menu_item::source_selection
