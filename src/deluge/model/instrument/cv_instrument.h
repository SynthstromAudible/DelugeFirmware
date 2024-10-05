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

enum class CVMode : uint8_t { off, pitch, mod, aftertouch, velocity };
enum class GateMode : uint8_t { off, gate, trigger };
enum class CVInstrumentMode : uint8_t { one, two, both };

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
	// returns -1 if  no pitch
	int32_t getPitchChannel() {
		auto c = getChannel();
		if (c > 1) {
			return 0;
		}
		return c;
	}
	void setMode(CVInstrumentMode channel) {
		clearModes();
		switch (channel) {
		case CVInstrumentMode::one: {
			gateMode[0] = GateMode::gate;
			cvmode[0] = CVMode::pitch;
		}
		case CVInstrumentMode::two: {
			gateMode[1] = GateMode::gate;
			cvmode[1] = CVMode::pitch;
		}
		case CVInstrumentMode::both: {
			gateMode[0] = GateMode::gate;
			gateMode[1] = GateMode::trigger;
			cvmode[0] = CVMode::pitch;
			cvmode[1] = CVMode::aftertouch;
		}
		}
	}
	void clearModes() {
		gateMode[0] = GateMode::off;
		gateMode[1] = GateMode::off;
		cvmode[0] = CVMode::off;
		cvmode[1] = CVMode::off;
	}
	GateMode gateMode[2]{GateMode::off, GateMode::off};
	CVMode cvmode[2]{CVMode::off, CVMode::off};
};
