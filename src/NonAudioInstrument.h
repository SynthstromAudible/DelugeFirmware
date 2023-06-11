/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef NONAUDIOINSTRUMENT_H_
#define NONAUDIOINSTRUMENT_H_

#include "MelodicInstrument.h"
#include "Arpeggiator.h"
#include "ModControllable.h"

class ModelStack;
class ModelStackWithSoundFlags;

class NonAudioInstrument : public MelodicInstrument, public ModControllable {
public:
	NonAudioInstrument(int newType);

	void renderOutput(ModelStack* modelStack, StereoSample* startPos, StereoSample* endPos, int numSamples,
	                  int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
	                  bool shouldLimitDelayFeedback, bool isClipActive);
	void sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int noteCode, int16_t const* mpeValues,
	              int fromMIDIChannel = 16, uint8_t velocity = 64, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	              uint32_t samplesLate = 0);
	int32_t doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) final;
	ParamManager* getParamManager(Song* song) final;

	void polyphonicExpressionEventOnChannelOrNote(int newValue, int whichExpressionDimension, int channelOrNote,
	                                              int whichCharacteristic) final;

	void beenEdited(bool shouldMoveToEmptySlot) {} // Probably don't need this anymore...

	char const* getSlotXMLTag() { return "channel"; }
	char const* getSubSlotXMLTag() { return NULL; }

	virtual void noteOnPostArp(int noteCodePostArp, ArpNote* arpNote) = 0;
	virtual void noteOffPostArp(int noteCodePostArp, int oldMIDIChannel, int velocity) = 0;

	bool readTagFromFile(char const* tagName);

	ModControllable* toModControllable() { return this; }

	int channel;

protected:
	virtual void polyphonicExpressionEventPostArpeggiator(int newValue, int noteCodeAfterArpeggiation,
	                                                      int whichExpressionDimension, ArpNote* arpNote) = 0;
};

#endif /* NONAUDIOINSTRUMENT_H_ */
