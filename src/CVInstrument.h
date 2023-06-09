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

#ifndef CVINSTRUMENT_H_
#define CVINSTRUMENT_H_

#include "CVInstrument.h"
#include "NonAudioInstrument.h"

class ParamManagerForTimeline;
class ModelStackWithThreeMainThings;
class ModelStackWithSoundFlags;

class CVInstrument final : public NonAudioInstrument {
public:
	CVInstrument();
	void noteOnPostArp(int noteCodePostArp, ArpNote* arpNote);
	void noteOffPostArp(int noteCode, int oldMIDIChannel, int velocity);
	void polyphonicExpressionEventPostArpeggiator(int newValue, int noteCodeAfterArpeggiation,
	                                              int whichExpressionDmiension, ArpNote* arpNote);
	bool writeDataToFile(Clip* clipForSavingOutputOnly, Song* song);
	void monophonicExpressionEvent(int newValue, int whichExpressionDmiension);
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, int maySendMIDIPGMs);
	void setupWithoutActiveClip(ModelStack* modelStack);

	// It's much easier to store local copies of the most recent of these, so we never have to go doing complex quizzing of the arp, or MPE params, which we otherwise would have to do regularly.
	int32_t monophonicPitchBendValue;
	int32_t polyPitchBendValue;

	uint8_t cachedBendRanges
	    [2]; // Cache these here just in case there's no ParamManager - because CVInstruments don't do backedUpParamManagers.

	char const* getXMLTag() { return "cvChannel"; }

private:
	void updatePitchBendOutput(bool outputToo = true);
};

#endif /* CVINSTRUMENT_H_ */
