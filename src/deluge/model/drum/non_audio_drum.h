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

#include "definitions_cxx.hpp"
#include "model/drum/drum.h"
#include "model/mod_controllable/mod_controllable.h"
#include "modulation/arpeggiator.h"
#include <cstdint>

class NonAudioDrum : public Drum, public ModControllable {
	bool state_ = false;

public:
	NonAudioDrum(DrumType newType) : Drum(newType) {}

	// Voiced overrides
	bool anyNoteIsOn() final { return state_; };
	[[nodiscard]] bool hasActiveVoices() const final { return state_; };
	void killAllVoices() override;
	bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false) final { return true; }

	bool readDrumTagFromFile(Deserializer& reader, char const* tagName);

	virtual int32_t getNumChannels() = 0;

	virtual int8_t modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset, uint8_t whichModEncoder);

	ModControllable* toModControllable() override { return this; }

	uint8_t lastVelocity;

	uint8_t channel;
	int8_t channelEncoderCurrentOffset = 0;

	ArpeggiatorBase* getArp() { return &arpeggiator; }
	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = NULL) { return &arpSettings; }

	virtual void noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) { state_ = true; };
	virtual void noteOffPostArp(int32_t noteCodePostArp) { state_ = false; };

	void writeArpeggiatorToFile(Serializer& writer);

protected:
	void modChange(ModelStackWithThreeMainThings* modelStack, int32_t offset, int8_t* encoderOffset, uint8_t* value,
	               int32_t numValues);
};
