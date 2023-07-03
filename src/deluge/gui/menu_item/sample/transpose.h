#pragma once
#include "storage/multi_range/multisample_range.h"
#include "gui/menu_item/source/transpose.h"
#include "processing/sound/sound.h"

namespace menu_item::sample {
class Transpose final : public source::Transpose {
public:
	Transpose(char const* newName = NULL, int newP = 0) : source::Transpose(newName, newP) {}
	void readCurrentValue() {
		int transpose;
		int cents;
		if (soundEditor.currentMultiRange && soundEditor.currentSound->getSynthMode() != SYNTH_MODE_FM
		    && soundEditor.currentSource->oscType == OSC_TYPE_SAMPLE) {
			transpose = ((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.transpose;
			cents = ((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.cents;
		}
		else {
			transpose = soundEditor.currentSource->transpose;
			cents = soundEditor.currentSource->cents;
		}
		soundEditor.currentValue = transpose * 100 + cents;
	}
	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;

		int transpose = semitones - 256;
		if (soundEditor.currentMultiRange && soundEditor.currentSound->getSynthMode() != SYNTH_MODE_FM
		    && soundEditor.currentSource->oscType == OSC_TYPE_SAMPLE) {
			((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.transpose = transpose;
			((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.setCents(cents);
		}
		else {
			soundEditor.currentSource->transpose = transpose;
			soundEditor.currentSource->setCents(cents);
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->recalculateAllVoicePhaseIncrements(modelStack);
	}
	int checkPermissionToBeginSession(Sound* sound, int whichThing, ::MultiRange** currentRange) {

		if (!isRelevant(sound, whichThing)) {
			return MENU_PERMISSION_NO;
		}

		Source* source = &sound->sources[whichThing];

		if (sound->getSynthMode() == SYNTH_MODE_FM
		    || (source->oscType != OSC_TYPE_SAMPLE && source->oscType != OSC_TYPE_WAVETABLE))
			return MENU_PERMISSION_YES;

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, true, currentRange);
	}
	bool isRangeDependent() { return true; }
};
} // namespace menu_item::sample
