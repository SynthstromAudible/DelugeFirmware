#include "midi_transpose.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "midi_device.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/song/song.h"

namespace MIDITranspose {

MIDITransposeControlMethod controlMethod;

void doTranspose(bool on, int32_t newNoteOrCC) {
	bool doRender = false;

	if (on) {
		if (!currentSong->hasBeenTransposed) {
			/* First transpose event in a new song snaps to nearest octave */
			currentSong->transposeOffset = (currentSong->key.rootNote - newNoteOrCC) / 12;
			if (currentSong->key.rootNote < newNoteOrCC) {
				if (abs((currentSong->key.rootNote - newNoteOrCC) % 12) > 6) {
					currentSong->transposeOffset -= 1;
				}
			}
			currentSong->transposeOffset *= 12;
			currentSong->hasBeenTransposed = true;
		}

		int32_t semitones;
		semitones = (newNoteOrCC + currentSong->transposeOffset) - currentSong->key.rootNote;

		if (controlMethod == MIDITransposeControlMethod::INKEY) {

			int8_t degree = currentSong->key.degreeOf(newNoteOrCC);
			if (degree >= 0) {
				int8_t octaves;
				if (semitones < 0) {
					octaves = ((semitones + 1) / 12) - 1;
				}
				else {
					octaves = (semitones / 12);
				}
				int32_t steps = octaves * currentSong->key.modeNotes.count() + degree;

				currentSong->transposeAllScaleModeClips(steps, false);

				doRender = true;
			}
		}
		else {
			currentSong->transposeAllScaleModeClips(semitones, true);

			doRender = true;
		}
		if (doRender) {
			RootUI* rootUI = getRootUI();
			if (rootUI->getUIContextType() == UIType::INSTRUMENT_CLIP) {
				switch (rootUI->getUIType()) {
				case UIType::KEYBOARD_SCREEN:
					uiNeedsRendering(rootUI, 0xFFFFFFFF, 0);
					break;
				case UIType::INSTRUMENT_CLIP:
					uiNeedsRendering(rootUI);
					break;
				case UIType::AUTOMATION:
					uiNeedsRendering(rootUI, 0, 0xFFFFFFFF);
					break;
				default:
				    // fallthrough for everything else -- to many UIs to list explicitly
				    ;
				}
			}
		}
	}
	else {
		// off events tracked, in future can track
		// held notes in a chord.
	}

	UI* currentUI = getCurrentUI();
	bool isOLEDSessionView = display->haveOLED() && (currentUI == &sessionView || currentUI == &arrangerView);
	if (isOLEDSessionView) {
		if (currentSong->key.rootNote != sessionView.lastDisplayedRootNote) {
			currentSong->displayCurrentRootNoteAndScaleName();
			sessionView.lastDisplayedRootNote = currentSong->key.rootNote;
		}
	}
}

void exitScaleModeForMIDITransposeClips() {
	if (currentUIMode == UI_MODE_NONE && getRootUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) {
		InstrumentClip* clip = getCurrentInstrumentClip();

		if (clip != nullptr) {
			if (clip->output->type == OutputType::MIDI_OUT
			    && MIDITranspose::controlMethod == MIDITransposeControlMethod::CHROMATIC
			    && ((NonAudioInstrument*)clip->output)->getChannel() == MIDI_CHANNEL_TRANSPOSE) {
				instrumentClipView.exitScaleMode();
				clip->inScaleMode = false;
			}
		}
	}
}

} // namespace MIDITranspose
