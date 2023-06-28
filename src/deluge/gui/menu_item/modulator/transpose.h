#pragma once
#include "gui/menu_item/source/transpose.h"
#include "processing/sound/sound.h"

namespace menu_item::modulator {

class Transpose final : public source::Transpose {
public:
	using source::Transpose::Transpose;

	void readCurrentValue() {
		soundEditor.currentValue =
		    (int32_t)soundEditor.currentSound->modulatorTranspose[soundEditor.currentSourceIndex] * 100
		    + soundEditor.currentSound->modulatorCents[soundEditor.currentSourceIndex];
	}

	void writeCurrentValue() {
		int currentValue = soundEditor.currentValue + 25600;

		int semitones = (currentValue + 50) / 100;
		int cents = currentValue - semitones * 100;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();

		soundEditor.currentSound->setModulatorTranspose(soundEditor.currentSourceIndex, semitones - 256, modelStack);
		soundEditor.currentSound->setModulatorCents(soundEditor.currentSourceIndex, cents, modelStack);
	}

	bool isRelevant(Sound* sound, int whichThing) { return (sound->getSynthMode() == SYNTH_MODE_FM); }
};

} // namespace menu_item::modulator
