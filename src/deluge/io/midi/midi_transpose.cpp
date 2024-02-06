#include "midi_transpose.h"
#include "midi_device.h"
#include "hid/display/display.h"
#include "model/song/song.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/instrument_clip_view.h"

namespace MIDITranspose {

	void doTranspose(MIDIDevice* newDevice, int32_t newChannel, int32_t newNoteOrCC) {

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

		int32_t semitones;
		semitones = (newNoteOrCC + currentSong->transposeOffset) - currentSong->rootNote;

		uint8_t indexInMode = currentSong->getYNoteIndexInMode(newNoteOrCC);

		if (indexInMode < 255) {
			int8_t octaves;
			if (semitones < 0) {
				octaves = ((semitones+1) / 12)-1;
			} else {
				octaves = (semitones / 12);
			}
			int32_t steps = octaves*currentSong->numModeNotes + indexInMode;

			currentSong->transposeAllScaleModeClips(steps, false);

			uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
			uiNeedsRendering(&instrumentClipView);
		}

	}
} // namespace MIDITranspose
