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

#ifndef MODCONTROLLABLE_H_
#define MODCONTROLLABLE_H_
#include "definitions.h"

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

class ModControllable {
public:
	ModControllable();
	virtual bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) {
		return false;
	} // Returns whether Instrument was changed
	virtual void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager){};
	virtual ModelStackWithAutoParam*
	getParamFromModEncoder(int whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                       bool allowCreation = true); // Check that autoParam isn't NULL, after calling this.
	virtual ModelStackWithAutoParam* getParamFromMIDIKnob(
	    MIDIKnob* knob,
	    ModelStackWithThreeMainThings* modelStack); // Check that autoParam isn't NULL, after calling this
	virtual uint8_t* getModKnobMode();              // Return NULL if different modes not supported
	virtual bool isKit() { return false; }
	virtual int getKnobPosForNonExistentParam(
	    int whichModEncoder,
	    ModelStackWithAutoParam* modelStack); // modelStack->autoParam will be NULL in this rare case!!
	virtual int modEncoderActionForNonExistentParam(int offset, int whichModEncoder,
	                                                ModelStackWithAutoParam* modelStack) {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}
	virtual bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false) { return true; }
	virtual void polyphonicExpressionEventOnChannelOrNote(int newValue, int whichExpressionDimension,
	                                                      int channelOrNoteNumber, int whichCharacteristic) {}
	virtual void monophonicExpressionEvent(int newValue, int whichExpressionDimension) {}
};

#endif /* MODCONTROLLABLE_H_ */
