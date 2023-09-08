#include "midi_transpose.h"
#include "midi_device.h"
#include "hid/display/display.h"
#include "model/song/song.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/instrument_clip_view.h"

uint8_t scaleMap[7] = {0,5,1,2,3,4,6};
uint8_t invScaleMap[7] = {0,2,3,4,5,1,6};

MIDITranspose midiTranspose{};


MIDITranspose::MIDITranspose() {

}

void MIDITranspose::doTranspose(MIDIDevice* newDevice, int32_t newChannel, int32_t newNoteOrCC) {

	display->displayPopup("trn");
	int32_t offset = (newNoteOrCC - currentSong->rootNote)-60;

	uint8_t indexInMode = currentSong->getYNoteIndexInMode(newNoteOrCC);

	uint8_t currentMode = currentSong->getCurrentPresetScale();
	if (indexInMode < 7 && currentMode < 7) {
		currentSong->transposeAllScaleModeClips(offset);
		currentMode = scaleMap[currentMode];
		currentMode += indexInMode;
		currentMode = invScaleMap[currentMode%7];
		currentSong->setCurrentPresetScale(currentMode);
		uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
		uiNeedsRendering(&instrumentClipView);
	}

}
