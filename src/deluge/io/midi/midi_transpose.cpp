#include "midi_transpose.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "midi_device.h"
#include "model/song/song.h"

namespace MIDITranspose {

MIDITransposeControlMethod controlMethod;

void doTranspose(bool on, int32_t newNoteOrCC) {

	if (on) {
		if (!currentSong->hasBeenTransposed) {
			/* First transpose event in a new song snaps to nearest octave */
			currentSong->transposeOffset = (currentSong->rootNote - newNoteOrCC) / 12;
			if (currentSong->rootNote < newNoteOrCC) {
				if (abs((currentSong->rootNote - newNoteOrCC) % 12) > 6) {
					currentSong->transposeOffset -= 1;
				}
			}
			currentSong->transposeOffset *= 12;
			currentSong->hasBeenTransposed = true;
		}

		int32_t semitones;
		semitones = (newNoteOrCC + currentSong->transposeOffset) - currentSong->rootNote;

		if (controlMethod == MIDITransposeControlMethod::INKEY) {

			uint8_t indexInMode = currentSong->getYNoteIndexInMode(newNoteOrCC);
			if (indexInMode < 255) {
				int8_t octaves;
				if (semitones < 0) {
					octaves = ((semitones + 1) / 12) - 1;
				}
				else {
					octaves = (semitones / 12);
				}
				int32_t steps = octaves * currentSong->numModeNotes + indexInMode;

				currentSong->transposeAllScaleModeClips(steps, false);

				uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
				uiNeedsRendering(&instrumentClipView);
			}
		}
		else {
			currentSong->transposeAllScaleModeClips(semitones, true);

			uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
			uiNeedsRendering(&instrumentClipView);
		}
	}
	else {
		// off events tracked, in future can track
		// held notes in a chord.
	}
}
} // namespace MIDITranspose
