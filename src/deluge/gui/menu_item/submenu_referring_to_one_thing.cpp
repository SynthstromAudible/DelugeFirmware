#include "submenu_referring_to_one_thing.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item {
void SubmenuReferringToOneThing::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentSourceIndex = thingIndex;
	soundEditor.currentSource = &soundEditor.currentSound->sources[thingIndex];
	soundEditor.currentSampleControls = &soundEditor.currentSource->sampleControls;
	Submenu::beginSession(navigatedBackwardFrom);
}

} // namespace menu_item
