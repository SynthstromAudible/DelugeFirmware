#include "midi_transpose.h"
#include "midi_device.h"
#include "hid/display/display.h"
#include "model/song/song.h"

uint8_t scaleMap[7] = {0,5,1,2,3,4,6};
uint8_t invScaleMap[7] = {0,2,3,4,5,1,6};

MIDITranspose midiTranspose{};


MIDITranspose::MIDITranspose() {

}

void MIDITranspose::doTranspose(MIDIDevice* newDevice, int32_t newChannel, int32_t newNoteOrCC) {

	display->displayPopup("trn");
	int32_t offset = (newNoteOrCC - currentSong->rootNote)-60;

	uint8_t indexInMode = currentSong->getYNoteIndexInMode(newNoteOrCC);

	currentSong->transposeAllScaleModeClips(offset);

	uint8_t currentMode = currentSong->getCurrentPresetScale();
	if (currentMode < 7) {
		currentMode = scaleMap[currentMode];
		currentMode += indexInMode;
		currentMode = invScaleMap[currentMode] % 7;
		currentSong->setCurrentPresetScale(currentMode);
	}

}
