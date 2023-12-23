#include "midi_transpose.h"
#include "midi_device.h"
#include "hid/display/display.h"
#include "model/song/song.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/instrument_clip_view.h"


/*MIDITranspose midiTranspose{};


MIDITranspose::MIDITranspose() {

}*/

namespace MIDITranspose {

	uint8_t scaleMap[7] = {0,5,1,2,3,4,6};
	uint8_t invScaleMap[7] = {0,2,3,4,5,1,6};

	void doTranspose(MIDIDevice* newDevice, int32_t newChannel, int32_t newNoteOrCC) {
		int32_t offset;
		if (!currentSong->hasBeenTransposed) {
			/* First transpose event in a new song snaps to nearest octave */
			currentSong->transposeOffset = (currentSong->rootNote - newNoteOrCC) / 12;
			if (currentSong->rootNote < newNoteOrCC) {
				if (abs((currentSong->rootNote - newNoteOrCC)%12) > 6) {
					currentSong->transposeOffset -= 1;
				}
			}
			currentSong->transposeOffset *= 12;
			currentSong->hasBeenTransposed = true;
		}

		offset = (newNoteOrCC + currentSong->transposeOffset) - currentSong->rootNote;

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
} // namespace MIDITranspose
