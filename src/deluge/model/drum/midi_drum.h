/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/drum/non_audio_drum.h"

class MIDIDrum final : public NonAudioDrum {
public:
	MIDIDrum();

	void noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
	            int32_t fromMIDIChannel = MIDI_CHANNEL_NONE, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	            uint32_t samplesLate = 0);
	void noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity = kDefaultLiftValue);
	void writeToFile(bool savingSong, ParamManager* paramManager);
	int32_t readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos);
	void getName(char* buffer);
	int32_t getNumChannels() { return 16; }
	void unassignAllVoices();

	int8_t modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset, uint8_t whichModEncoder);

	void expressionEvent(int32_t newValue, int32_t whichExpressionDimension);

	void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t whichExpressionDimension, int32_t channelOrNoteNumber,
	                                              MIDICharacteristic whichCharacteristic);

	uint8_t note;
	int8_t noteEncoderCurrentOffset;
};
