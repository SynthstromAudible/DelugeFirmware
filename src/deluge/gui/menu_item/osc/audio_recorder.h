#pragma once
#include "gui/ui/audio_recorder.h"
#include "gui/menu_item/menu_item.h"
#include "hid/display/numeric_driver.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"

namespace menu_item::osc {
class AudioRecorder final : public MenuItem {
public:
	AudioRecorder(char const* newName = 0) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		soundEditor.shouldGoUpOneLevelOnBegin = true;
		bool success = openUI(&audioRecorder);
		if (!success) {
			if (getCurrentUI() == &soundEditor) {
				soundEditor.goUpOneLevel();
			}
			uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
		}
		else {
			audioRecorder.process();
		}
	}
	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		return (DELUGE_MODEL != DELUGE_MODEL_40_PAD && sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE);
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, ::MultiRange** currentRange) {

		bool can = isRelevant(sound, whichThing);
		if (!can) {
			numericDriver.displayPopup(HAVE_OLED ? "Can't record audio into an FM synth" : "CANT");
			return false;
		}

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, false, currentRange);
	}
};
} // namespace menu_item::osc
