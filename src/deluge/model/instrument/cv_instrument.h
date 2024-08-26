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
enum CVInstrumentMode { one, two, both };
constexpr int32_t kNumCVInstrumentChannels = both + 1;

class CVInstrument final : public NonAudioInstrument {
public:
	CVInstrument();
	void noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) override;
	void noteOffPostArp(int32_t noteCode, int32_t oldMIDIChannel, int32_t velocity, int32_t noteIndex) override;
	void polyphonicExpressionEventPostArpeggiator(int32_t newValue, int32_t noteCodeAfterArpeggiation,
	                                              int32_t whichExpressionDmiension, ArpNote* arpNote,
	                                              int32_t noteIndex) override;
	bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) override;
	bool readTagFromFile(Deserializer& reader, const char* tagName) override;
	void monophonicExpressionEvent(int32_t newValue, int32_t whichExpressionDmiension);
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs);
	void setupWithoutActiveClip(ModelStack* modelStack);
	static int32_t navigateChannels(int oldChannel, int offset) {
		auto newChannel = (oldChannel + offset) % kNumCVInstrumentChannels;
		if (newChannel == -1) {
			newChannel = kNumCVInstrumentChannels - 1;
		}
		return newChannel;
	}
	bool matchesPreset(OutputType otherType, int32_t otherChannel, int32_t channelSuffix, char const* otherName,
	                   char const* otherPath) override {
		bool match{false};
		if (type == otherType) {
			auto ourChannel = getChannel();
			match = ourChannel == otherChannel || ourChannel == both || otherChannel == both; // 3 means both
		}
		return match;
	}

	// It's much easier to store local copies of the most recent of these, so we never have to go doing complex quizzing
	// of the arp, or MPE params, which we otherwise would have to do regularly.
	int32_t monophonicPitchBendValue;
	int32_t polyPitchBendValue;

	char const* getXMLTag() { return "cvChannel"; }
	void setChannel(int channel) override {
		if (channel <= both) {
			NonAudioInstrument::setChannel(channel);
			setMode(static_cast<CVInstrumentMode>(channel));
		}
		else {
			NonAudioInstrument::setChannel(0);
			setMode(static_cast<CVInstrumentMode>(0));
		}
	}
	CVMode getCV2Mode() { return cvmode[1]; }

	void setCV2Mode(CVMode mode);

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
			if (cvmode[1] == CVMode::off) {
				cvmode[1] = CVMode::aftertouch;
			}
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
	void sendMonophonicExpressionEvent(int32_t dimension);
};
