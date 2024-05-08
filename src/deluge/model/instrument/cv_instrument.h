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

#include "model/instrument/cv_instrument.h"
#include "model/instrument/non_audio_instrument.h"

class ParamManagerForTimeline;
class ModelStackWithThreeMainThings;
class ModelStackWithSoundFlags;

class CVInstrument final : public NonAudioInstrument {
public:
	CVInstrument();
	void noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote);
	void noteOffPostArp(int32_t noteCode, int32_t oldMIDIChannel, int32_t velocity);
	void polyphonicExpressionEventPostArpeggiator(int32_t newValue, int32_t noteCodeAfterArpeggiation,
	                                              int32_t whichExpressionDmiension, ArpNote* arpNote);
	bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song);
	void monophonicExpressionEvent(int32_t newValue, int32_t whichExpressionDmiension);
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs);
	void setupWithoutActiveClip(ModelStack* modelStack);

	// It's much easier to store local copies of the most recent of these, so we never have to go doing complex quizzing
	// of the arp, or MPE params, which we otherwise would have to do regularly.
	int32_t monophonicPitchBendValue;
	int32_t polyPitchBendValue;

	char const* getXMLTag() { return "cvChannel"; }

private:
	void updatePitchBendOutput(bool outputToo = true);
};
