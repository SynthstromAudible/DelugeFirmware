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

#include "model/instrument/cv_instrument.h"
#include "model/model_stack.h"
#include "model/timeline_counter.h"
#include "modulation/params/param_set.h"
#include "processing/engines/cv_engine.h"
#include "storage/storage_manager.h"
#include <cstring>

CVInstrument::CVInstrument() : NonAudioInstrument(OutputType::CV) {
	monophonicPitchBendValue = 0;
	polyPitchBendValue = 0;
}

void CVInstrument::noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote) {
	// First update pitch bend for the new note
	polyPitchBendValue = (int32_t)arpNote->mpeValues[0] << 16;
	updatePitchBendOutput(false);

	cvEngine.sendNote(true, getPitchChannel(), noteCodePostArp);
}

void CVInstrument::noteOffPostArp(int32_t noteCodePostArp, int32_t oldMIDIChannel, int32_t velocity) {
	cvEngine.sendNote(false, getPitchChannel(), noteCodePostArp);
}

void CVInstrument::polyphonicExpressionEventPostArpeggiator(int32_t newValue, int32_t noteCodeAfterArpeggiation,
                                                            int32_t whichExpressionDimension, ArpNote* arpNote) {
	if (!whichExpressionDimension) { // Pitch bend only
		if (cvEngine.isNoteOn(getPitchChannel(), noteCodeAfterArpeggiation)) {
			polyPitchBendValue = newValue;
			updatePitchBendOutput();
		}
	}
}

void CVInstrument::monophonicExpressionEvent(int32_t newValue, int32_t whichExpressionDimension) {
	if (!whichExpressionDimension) { // Pitch bend only
		monophonicPitchBendValue = newValue;
		updatePitchBendOutput();
	}
}

void CVInstrument::updatePitchBendOutput(bool outputToo) {

	ParamManager* paramManager = getParamManager(NULL);
	if (paramManager) {
		ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
		if (expressionParams) {
			cachedBendRanges[BEND_RANGE_MAIN] = expressionParams->bendRanges[BEND_RANGE_MAIN];
			cachedBendRanges[BEND_RANGE_FINGER_LEVEL] = expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL];
		}
	}

	// If couldn't update bend ranges that way, no worries - we'll keep our cached ones, cos the user probably intended
	// that.

	int32_t totalBendAmount = // (1 << 23) represents one semitone. So full 32-bit range can be +-256 semitones. This is
	                          // different to the equivalent calculation in Voice, which needs to get things into a
	                          // number of octaves.
	    (monophonicPitchBendValue >> 8) * cachedBendRanges[BEND_RANGE_MAIN]
	    + (polyPitchBendValue >> 8) * cachedBendRanges[BEND_RANGE_FINGER_LEVEL];

	cvEngine.setCVPitchBend(getPitchChannel(), totalBendAmount, outputToo);
}

bool CVInstrument::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {
	// NonAudioInstrument::writeDataToFile(clipForSavingOutputOnly, song); // Nope, this gets called within the below
	// call
	writeMelodicInstrumentAttributesToFile(writer, clipForSavingOutputOnly, song);

	if (clipForSavingOutputOnly || !midiInput.containsSomething()) {
		return false; // If we don't need to write a "device" tag, opt not to end the opening tag
	}

	writer.writeOpeningTagEnd();
	MelodicInstrument::writeMelodicInstrumentTagsToFile(writer, clipForSavingOutputOnly, song);
	return true;
}

bool CVInstrument::setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) {
	bool clipChanged = NonAudioInstrument::setActiveClip(modelStack, maySendMIDIPGMs);

	if (clipChanged) {
		if (modelStack) {

			ParamManager* paramManager = &modelStack->getTimelineCounter()->paramManager;
			ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
			if (expressionParams) {
				monophonicPitchBendValue = expressionParams->params[0].getCurrentValue();

				cachedBendRanges[BEND_RANGE_MAIN] = expressionParams->bendRanges[BEND_RANGE_MAIN];
				cachedBendRanges[BEND_RANGE_FINGER_LEVEL] = expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL];
			}
			else {
				monophonicPitchBendValue = 0;
			}
		}
		else {
			monophonicPitchBendValue = 0;
		}
		// Don't change the CV output voltage right now (we could, but this Clip-change might come with a
		// note that's going to sound "now" anyway...)
		// - but make it so the next note which sounds will have our new correct bend value / range.
		updatePitchBendOutput(false);
	}

	return clipChanged;
}

void CVInstrument::setupWithoutActiveClip(ModelStack* modelStack) {
	NonAudioInstrument::setupWithoutActiveClip(modelStack);

	monophonicPitchBendValue = 0;
}
