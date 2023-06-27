/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#ifndef GATEDRUM_H_
#define GATEDRUM_H_

#include "NonAudioDrum.h"
#include "r_typedefs.h"

class Kit;
class ParamManagerForTimeline;
class Clip;
class ParamManager;

class GateDrum final : public NonAudioDrum {
public:
	GateDrum();
	void noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
	            int fromMIDIChannel = MIDI_CHANNEL_NONE, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	            uint32_t samplesLate = 0);
	void noteOff(ModelStackWithThreeMainThings* modelStack, int velocity);
	void writeToFile(bool savingSong, ParamManager* paramManager);
	int readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos);
	void getName(char* buffer);
	int getNumChannels();
};

#endif /* GATEDRUM_H_ */
