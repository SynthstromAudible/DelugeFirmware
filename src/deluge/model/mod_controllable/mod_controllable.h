/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include "controls/knob_focus.h"
#include "definitions_cxx.hpp"
#include "modulation/params/param.h"

class ParamManagerForTimeline;
class ParamManagerForTimeline;
class InstrumentClip;
class Action;
class ParamManagerForTimeline;
class AutoParam;
class ParamCollection;
class Sound;
class TimelineCounter;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class MIDIKnob;
class ModelStackWithSoundFlags;

/// Base class for things that the internal modulation engine knows how to control.
///
/// Responsible for reacting to the mod (gold) encoders and MIDI data (CC+MPE) as well as providing a consistent
/// interface to the rest of the system when displaying information about the underlying modulation state.
class ModControllable {
public:
	ModControllable() = default;
	virtual ~ModControllable() = default;
	/// Check that autoParam isn't NULL, after calling this
	virtual ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob& knob, ModelStackWithThreeMainThings* modelStack);

	virtual bool isSong() { return false; }
	virtual bool valueChangedEnoughToMatter(int32_t old_value, int32_t new_value, deluge::modulation::params::Kind kind,
	                                        uint32_t paramID) {
		return old_value != new_value;
	}
	virtual bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false) { return true; }
	virtual void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
	                                                      int32_t channelOrNoteNumber,
	                                                      MIDICharacteristic whichCharacteristic) {}
	virtual void monophonicExpressionEvent(int32_t newValue, int32_t expressionDimension) {}
};
