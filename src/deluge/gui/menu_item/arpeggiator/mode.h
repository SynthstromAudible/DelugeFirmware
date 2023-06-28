#pragma once
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace menu_item::arpeggiator {
class Mode final : public Selection {
public:
	Mode(char const* newName = NULL) : Selection(newName) {}
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentArpSettings->mode; }
	void writeCurrentValue() {

		// If was off, or is now becoming off...
		if (soundEditor.currentArpSettings->mode == ARP_MODE_OFF || soundEditor.currentValue == ARP_MODE_OFF) {
			if (currentSong->currentClip->isActiveOnOutput()) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

				if (soundEditor.editingCVOrMIDIClip()) {
					((InstrumentClip*)currentSong->currentClip)
					    ->stopAllNotesForMIDIOrCV(modelStack->toWithTimelineCounter());
				}
				else {
					ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();
					soundEditor.currentSound->allNotesOff(
					    modelStackWithSoundFlags,
					    soundEditor.currentSound->getArp()); // Must switch off all notes when switching arp on / off
					soundEditor.currentSound->reassessRenderSkippingStatus(modelStackWithSoundFlags);
				}
			}
		}
		soundEditor.currentArpSettings->mode = soundEditor.currentValue;

		// Only update the Clip-level arp setting if they hadn't been playing with other synth parameters first (so it's clear that switching the arp on or off was their main intention)
		if (!soundEditor.editingKit()) {
			bool arpNow = (soundEditor.currentValue != ARP_MODE_OFF); // Uh.... this does nothing...
		}
	}
	char const** getOptions() {
		static char const* options[] = {"OFF", "UP", "DOWN", "BOTH", "Random", NULL};
		return options;
	}
	int getNumOptions() { return NUM_ARP_MODES; }
};
} // namespace menu_item::arpeggiator
