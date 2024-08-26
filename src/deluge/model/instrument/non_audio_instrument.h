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

#pragma once

#include "model/instrument/melodic_instrument.h"
#include "model/mod_controllable/mod_controllable.h"
#include "storage/flash_storage.h"

class ModelStack;
class ModelStackWithSoundFlags;

class NonAudioInstrument : public MelodicInstrument, public ModControllable {
public:
	NonAudioInstrument(OutputType newType) : MelodicInstrument(newType) {
		cachedBendRanges[BEND_RANGE_MAIN] = FlashStorage::defaultBendRange[BEND_RANGE_MAIN];
		cachedBendRanges[BEND_RANGE_FINGER_LEVEL] = FlashStorage::defaultBendRange[BEND_RANGE_FINGER_LEVEL];
	}

	void renderOutput(ModelStack* modelStack, StereoSample* startPos, StereoSample* endPos, int32_t numSamples,
	                  int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
	                  bool shouldLimitDelayFeedback, bool isClipActive);
	void sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCode, int16_t const* mpeValues,
	              int32_t fromMIDIChannel = 16, uint8_t velocity = 64, uint32_t sampleSyncLength = 0,
	              int32_t ticksLate = 0, uint32_t samplesLate = 0);
	int32_t doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) final;
	ParamManager* getParamManager(Song* song) final;

	void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t whichExpressionDimension,
	                                              int32_t channelOrNote, MIDICharacteristic whichCharacteristic) final;

	void beenEdited(bool shouldMoveToEmptySlot) {} // Probably don't need this anymore...

	char const* getSlotXMLTag() { return "channel"; }
	char const* getSubSlotXMLTag() { return NULL; }

	virtual void noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) = 0;
	virtual void noteOffPostArp(int32_t noteCodePostArp, int32_t oldMIDIChannel, int32_t velocity,
	                            int32_t noteIndex) = 0;

	bool readTagFromFile(Deserializer& reader, char const* tagName);

	ModControllable* toModControllable() { return this; }
	virtual void setChannel(int newChannel) { channel = newChannel; }
	inline int32_t getChannel() const { return channel; }
	// Cache these here just in case there's no ParamManager - because CVInstruments don't do backedUpParamManagers.
	uint8_t cachedBendRanges[2];
	bool needsEarlyPlayback() const override;

protected:
	virtual void polyphonicExpressionEventPostArpeggiator(int32_t newValue, int32_t noteCodeAfterArpeggiation,
	                                                      int32_t whichExpressionDimension, ArpNote* arpNote,
	                                                      int32_t noteIndex) = 0;
	// for tracking mono expression output
	int32_t lastMonoExpression[3]{0};
	int32_t lastCombinedPolyExpression[3]{0};
	// could be int8 for aftertouch/Y but Midi 2 will allow those to be 14 bit too
	int16_t lastOutputMonoExpression[3]{0};

private:
	int32_t channel = 0;
};
