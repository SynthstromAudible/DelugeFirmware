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

#include "modulation/params/param_set.h"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/melodic_instrument.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

namespace params = deluge::modulation::params;

ParamSet::ParamSet(int32_t newObjectSize, ParamCollectionSummary* summary)
    : ParamCollection(newObjectSize, summary), numParams_(0), params(nullptr), topUintToRepParams(1) {
}

void ParamSet::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) {
	int32_t numParams = getNumParams();
	for (int32_t p = 0; p < numParams; p++) {
		params[p].beenCloned(copyAutomation, reverseDirectionWithLength);
	}
}

void ParamSet::copyOverridingFrom(ParamSet* otherParamSet) {

	int32_t numParams = getNumParams();
	for (int32_t p = 0; p < numParams; p++) {
		params[p].copyOverridingFrom(&otherParamSet->params[p]);
	}
}

void ParamSet::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                            bool automationChanged, bool automatedBefore, bool automatedNow) {
	if (automatedBefore != automatedNow) {
		if (automatedNow) {
			paramHasAutomationNow(modelStack->summary, modelStack->paramId);
		}
		else {
			paramHasNoAutomationNow(modelStack, modelStack->paramId);
		}
	}
	ParamCollection::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                              automatedNow);
}

void ParamSet::shiftParamValues(int32_t p, int32_t offset) {
	params[p].shiftValues(offset);
}

void ParamSet::shiftParamVolumeByDB(int32_t p, float offset) {
	params[p].shiftParamVolumeByDB(offset);
}

void ParamSet::paramHasAutomationNow(ParamCollectionSummary* summary, int32_t p) {
	summary->whichParamsAreAutomated[p >> 5] |= ((uint32_t)1 << (p & 31));
}

void ParamSet::paramHasNoAutomationNow(ModelStackWithParamCollection const* modelStack, int32_t p) {

	uint32_t mask = ~((uint32_t)1 << (p & 31));
	modelStack->summary->whichParamsAreAutomated[p >> 5] &= mask;
	modelStack->summary->whichParamsAreInterpolating[p >> 5] &= mask;
}

#define FOR_EACH_FLAGGED_PARAM(whichParams)                                                                            \
	for (int32_t i = topUintToRepParams; i >= 0; i--) {                                                                \
		uint32_t whichParamsHere = whichParams[i];                                                                     \
		while (whichParamsHere) {                                                                                      \
			int32_t whichBit = 31 - clz(whichParamsHere);                                                              \
			whichParamsHere &= ~((uint32_t)1 << whichBit);                                                             \
			int32_t p = whichBit + (i << 5);

#define FOR_EACH_PARAM_END                                                                                             \
	}                                                                                                                  \
	}

inline void ParamSet::checkWhetherParamHasInterpolationNow(ModelStackWithParamCollection const* modelStack, int32_t p) {
	if (params[p].valueIncrementPerHalfTick) {
		modelStack->summary->whichParamsAreInterpolating[p >> 5] |= ((uint32_t)1 << (p & 31));
	}
}

void ParamSet::processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t posIncrement, bool reversed,
                                 bool didPingpong, bool mayInterpolate) {

	ticksTilNextEvent -= posIncrement;

	if (ticksTilNextEvent <= 0) {
		ticksTilNextEvent = 2147483647;

		modelStack->summary->resetInterpolationRecord(topUintToRepParams);

		FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

		AutoParam* param = &params[p];
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);
		int32_t ticksTilNextEventThisParam =
		    param->processCurrentPos(modelStackWithAutoParam, reversed, didPingpong, mayInterpolate);
		ticksTilNextEvent = std::min(ticksTilNextEvent, ticksTilNextEventThisParam);

		checkWhetherParamHasInterpolationNow(modelStack, p);

		FOR_EACH_PARAM_END
	}
}

void ParamSet::tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating);

	AutoParam* param = &params[p];

	int32_t oldValue = param->getCurrentValue();
	bool shouldNotify = param->tickSamples(numSamples);
	if (shouldNotify) { // Should always actually be true...
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);
		notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
	}

	FOR_EACH_PARAM_END
}
void ParamSet::tickTicks(int32_t numTicks, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating);

	AutoParam* param = &params[p];

	int32_t oldValue = param->getCurrentValue();
	bool shouldNotify = param->tickTicks(numTicks);
	if (shouldNotify) { // Should always actually be true...
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);
		notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
	}

	FOR_EACH_PARAM_END
}

void ParamSet::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {
	modelStack->summary->resetInterpolationRecord(topUintToRepParams);

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	AutoParam* param = &params[p];
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);

	int32_t oldValue = param->getCurrentValue();
	param->setPlayPos(pos, modelStackWithAutoParam, reversed); // May change interpolation state

	checkWhetherParamHasInterpolationNow(modelStack, p);

	FOR_EACH_PARAM_END

	ParamCollection::setPlayPos(pos, modelStack, reversed);
}

void ParamSet::writeParamAsAttribute(Serializer& writer, char const* name, int32_t p, bool writeAutomation,
                                     bool onlyIfContainsSomething, int32_t* valuesForOverride) {
	if (onlyIfContainsSomething && !params[p].containsSomething()) {
		return;
	}

	int32_t* valueForOverride = valuesForOverride ? &valuesForOverride[p] : NULL;
	writer.insertCommaIfNeeded();
	writer.write("\n");
	writer.printIndents();
	writer.writeTagNameAndSeperator(name);
	writer.write("\"");
	params[p].writeToFile(writer, writeAutomation, valueForOverride);
	writer.write("\"");
}

void ParamSet::readParam(Deserializer& reader, ParamCollectionSummary* summary, int32_t p,
                         int32_t readAutomationUpToPos) {
	params[p].readFromFile(reader, readAutomationUpToPos);
	if (params[p].isAutomated()) {
		paramHasAutomationNow(summary, p);
	}
}

void ParamSet::playbackHasEnded(ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating);
	params[p].valueIncrementPerHalfTick = 0;
	FOR_EACH_PARAM_END

	modelStack->summary->resetInterpolationRecord(topUintToRepParams);
}

void ParamSet::grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	AutoParam* param = &params[p];

	int32_t oldValue = param->getCurrentValue();
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);
	bool shouldNotify = param->grabValueFromPos(pos, modelStackWithAutoParam);
	if (shouldNotify) {
		notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
	}

	FOR_EACH_PARAM_END
}

void ParamSet::generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
                               bool shouldPingpong) {
	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);
	params[p].generateRepeats(oldLength, newLength, shouldPingpong);
	FOR_EACH_PARAM_END
}

void ParamSet::appendParamCollection(ModelStackWithParamCollection* modelStack,
                                     ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
                                     int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {
	ParamSet* otherParamSet = (ParamSet*)otherModelStack->paramCollection;

	FOR_EACH_FLAGGED_PARAM(
	    otherModelStack->summary->whichParamsAreAutomated); // Iterate through the *other* ParamManager's stuff
	params[p].appendParam(&otherParamSet->params[p], oldLength, reverseThisRepeatWithLength, pingpongingGenerally);
	FOR_EACH_PARAM_END

	ticksTilNextEvent = 0;
}

void ParamSet::trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
                            bool maySetupPatching) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	AutoParam* param = &params[p];
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);

	params[p].trimToLength(newLength, action, modelStackWithAutoParam);
	if (!params[p].isAutomated()) {
		paramHasNoAutomationNow(modelStack, p);
	}

	FOR_EACH_PARAM_END

	ticksTilNextEvent = 0;
}

void ParamSet::deleteAutomationForParamBasicForSetup(ModelStackWithParamCollection* modelStack, int32_t p) {
	params[p].deleteAutomationBasicForSetup();
	paramHasNoAutomationNow(modelStack, p);
}

void ParamSet::shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount, int32_t effectiveLength) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	params[p].shiftHorizontally(amount, effectiveLength);

	FOR_EACH_PARAM_END
}

void ParamSet::remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) {

	AutoParam* param = &params[modelStack->paramId];

	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(param);

	param->swapState(state, modelStackWithParam);
	int32_t oldValue = params[modelStack->paramId].getCurrentValue();
}

void ParamSet::deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(p, &params[p]);
	params[p].deleteAutomation(action, modelStackWithParam, false);

	FOR_EACH_PARAM_END

	for (int32_t i = 0; i <= topUintToRepParams; i++) {
		modelStack->summary->whichParamsAreAutomated[i] = 0;
	}

	modelStack->summary->resetInterpolationRecord(topUintToRepParams);
}

void ParamSet::insertTime(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t lengthToInsert) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	params[p].insertTime(pos, lengthToInsert);

	FOR_EACH_PARAM_END
}

void ParamSet::deleteTime(ModelStackWithParamCollection* modelStack, int32_t startPos, int32_t lengthToDelete) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, &params[p]);
	params[p].deleteTime(startPos, lengthToDelete, modelStackWithAutoParam);
	if (!params[p].isAutomated()) {
		paramHasNoAutomationNow(modelStack, p);
	}

	FOR_EACH_PARAM_END
}

void ParamSet::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
                                               ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	AutoParam* param = &params[p];
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);

	param->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action, modelStackWithAutoParam);

	if (!params[p].isAutomated()) {
		paramHasNoAutomationNow(modelStack, p);
	}

	FOR_EACH_PARAM_END
}

void ParamSet::backUpAllAutomatedParamsToAction(Action* action, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated);

	backUpParamToAction(p, action, modelStack);

	FOR_EACH_PARAM_END
}

void ParamSet::backUpParamToAction(int32_t p, Action* action, ModelStackWithParamCollection* modelStack) {
	AutoParam* param = &params[p];
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);
	action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithAutoParam, false);
}

ModelStackWithAutoParam* ParamSet::getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation) {
	return modelStack->addAutoParam(&params[modelStack->paramId]);
}

void ParamSet::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {

	ParamCollection::notifyPingpongOccurred(modelStack);

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating);

	params[p].notifyPingpongOccurred();

	FOR_EACH_PARAM_END
}

// UnpatchedParamSet --------------------------------------------------------------------------------------------

UnpatchedParamSet::UnpatchedParamSet(ParamCollectionSummary* summary) : ParamSet(sizeof(UnpatchedParamSet), summary) {
	params = params_.data();
	numParams_ = static_cast<int32_t>(params_.size());
	topUintToRepParams = (numParams_ - 1) >> 5;
}

void UnpatchedParamSet::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) {
	params = params_.data();
	numParams_ = static_cast<int32_t>(params_.size());
	topUintToRepParams = (numParams_ - 1) >> 5;

	ParamSet::beenCloned(copyAutomation, reverseDirectionWithLength);
}

bool UnpatchedParamSet::shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) {
	// Shared
	switch (modelStack->paramId) {
	case params::UNPATCHED_STUTTER_RATE:
		return !(((ModControllableAudio*)modelStack->modControllable)->stutterConfig.useSongStutter
		             ? currentSong->globalEffectable.stutterConfig.quantized
		             : ((ModControllableAudio*)modelStack->modControllable)->stutterConfig.quantized)
		       || isUIModeActive(UI_MODE_STUTTERING);
	case params::UNPATCHED_BASS:
	case params::UNPATCHED_TREBLE:
		return true;
	}
	// Global
	if (modelStack->paramCollection->getParamKind() == deluge::modulation::params::Kind::UNPATCHED_GLOBAL) {
		switch (modelStack->paramId) {
		case params::UNPATCHED_DELAY_RATE:
		case params::UNPATCHED_DELAY_AMOUNT:
		case params::UNPATCHED_PAN:
		case params::UNPATCHED_PITCH_ADJUST:
			return true;
		}
	}
	return false;
}
int32_t UnpatchedParamSet::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	if (modelStack && (modelStack->paramId == params::UNPATCHED_COMPRESSOR_THRESHOLD)) {
		return (paramValue >> 24) - 64;
	}
	else {
		return ParamSet::paramValueToKnobPos(paramValue, modelStack);
	}
}

int32_t UnpatchedParamSet::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	if (modelStack && (modelStack->paramId == params::UNPATCHED_COMPRESSOR_THRESHOLD)) {
		int32_t paramValue = 2147483647;
		if (knobPos < 64) {
			paramValue = (knobPos + 64) << 24;
		}
		return paramValue;
	}
	else {
		return ParamSet::knobPosToParamValue(knobPos, modelStack);
	}
}

bool UnpatchedParamSet::doesParamIdAllowAutomation(ModelStackWithParamId const* modelStack) {
	return (modelStack->paramId != params::UNPATCHED_STUTTER_RATE);
}

// PatchedParamSet --------------------------------------------------------------------------------------------

PatchedParamSet::PatchedParamSet(ParamCollectionSummary* summary) : ParamSet(sizeof(PatchedParamSet), summary) {
	params = params_.data();
	numParams_ = static_cast<int32_t>(params_.size());
	topUintToRepParams = (numParams_ - 1) >> 5;
}

void PatchedParamSet::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) {
	params = params_.data();
	numParams_ = static_cast<int32_t>(params_.size());
	topUintToRepParams = (numParams_ - 1) >> 5;

	ParamSet::beenCloned(copyAutomation, reverseDirectionWithLength);
}

void PatchedParamSet::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                   bool automationChanged, bool automatedBefore, bool automatedNow) {
	ParamSet::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore, automatedNow);

	// If the Clip is active (or there isn't one)...
	if (!modelStack->timelineCounterIsSet() || ((Clip*)modelStack->getTimelineCounter())->isActiveOnOutput()) {
		int32_t current_value = modelStack->autoParam->getCurrentValue();
		bool current_value_changed = modelStack->modControllable->valueChangedEnoughToMatter(
		    oldValue, current_value, getParamKind(), modelStack->paramId);
		if (current_value_changed) {
			((Sound*)modelStack->modControllable)
			    ->notifyValueChangeViaLPF(modelStack->paramId, true, modelStack, oldValue, current_value, false);
		}

		if (!automatedNow) {
			if (modelStack->paramId == params::GLOBAL_REVERB_AMOUNT) {
				AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
			}
		}
	}

	// Because some patch cables are marked as "unusable" under certain circumstances, see if those circumstances have
	// changed
	switch (modelStack->paramId) {
	case params::LOCAL_OSC_A_VOLUME:
	case params::LOCAL_OSC_B_VOLUME:
	case params::LOCAL_NOISE_VOLUME:
	case params::LOCAL_MODULATOR_0_VOLUME:
	case params::LOCAL_MODULATOR_1_VOLUME:
	case params::LOCAL_CARRIER_0_FEEDBACK:
	case params::LOCAL_CARRIER_1_FEEDBACK:
	case params::LOCAL_MODULATOR_0_FEEDBACK:
	case params::LOCAL_MODULATOR_1_FEEDBACK:
		bool containsSomethingNow = modelStack->autoParam->containsSomething(-2147483648);
		bool containedSomethingBefore = AutoParam::containedSomethingBefore(automatedBefore, oldValue, -2147483648);
		if (containedSomethingBefore != containsSomethingNow) {

			ParamManager* paramManager = modelStack->paramManager;

			char localModelStackMemory[MODEL_STACK_MAX_SIZE];
			copyModelStack(localModelStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));

			ModelStackWithParamCollection* modelStackWithParamCollection =
			    paramManager->getPatchCableSet((ModelStackWithThreeMainThings*)localModelStackMemory);

			((PatchCableSet*)modelStackWithParamCollection->paramCollection)
			    ->setupPatching(
			        modelStackWithParamCollection); // Only need to setupPatching on this one ParamManager because this
			                                        // is the only one for which the param preset value has just changed
		}
	}
}

int32_t PatchedParamSet::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	if (modelStack
	    && (modelStack->paramId == params::LOCAL_OSC_A_PHASE_WIDTH
	        || modelStack->paramId == params::LOCAL_OSC_B_PHASE_WIDTH)) {
		return (paramValue >> 24) - 64;
	}
	else {
		return ParamSet::paramValueToKnobPos(paramValue, modelStack);
	}
}

int32_t PatchedParamSet::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	if (modelStack
	    && (modelStack->paramId == params::LOCAL_OSC_A_PHASE_WIDTH
	        || modelStack->paramId == params::LOCAL_OSC_B_PHASE_WIDTH)) {
		int32_t paramValue = 2147483647;
		if (knobPos < 64) {
			paramValue = (knobPos + 64) << 24;
		}
		return paramValue;
	}
	else {
		return ParamSet::knobPosToParamValue(knobPos, modelStack);
	}
}

bool PatchedParamSet::shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) {
	switch (modelStack->paramId) {
	case params::LOCAL_PAN:
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
	case params::GLOBAL_DELAY_FEEDBACK:
	case params::GLOBAL_DELAY_RATE:
		return true;
	default:
		return false;
	}
}

// ExpressionParamSet --------------------------------------------------------------------------------------------
ExpressionParamSet::ExpressionParamSet(ParamCollectionSummary* summary, bool forDrum)
    : ParamSet(sizeof(ExpressionParamSet), summary) {
	params = params_.data();
	numParams_ = static_cast<int32_t>(params_.size());
	topUintToRepParams = (numParams_ - 1) >> 5;
	bendRanges[BEND_RANGE_MAIN] = FlashStorage::defaultBendRange[BEND_RANGE_MAIN];

	bendRanges[BEND_RANGE_FINGER_LEVEL] =
	    forDrum ? bendRanges[BEND_RANGE_MAIN] : FlashStorage::defaultBendRange[BEND_RANGE_FINGER_LEVEL];
}

void ExpressionParamSet::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) {
	params = params_.data();
	numParams_ = static_cast<int32_t>(params_.size());
	topUintToRepParams = (numParams_ - 1) >> 5;

	ParamSet::beenCloned(copyAutomation, reverseDirectionWithLength);
}

void ExpressionParamSet::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                      bool automationChanged, bool automatedBefore, bool automatedNow) {
	ParamSet::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore, automatedNow);

	// If the Clip is active (or there isn't one)...
	if (!modelStack->timelineCounterIsSet() || ((Clip*)modelStack->getTimelineCounter())->isActiveOnOutput()) {
		int32_t current_value = modelStack->autoParam->getCurrentValue();
		bool current_value_changed = modelStack->modControllable->valueChangedEnoughToMatter(
		    oldValue, current_value, getParamKind(), modelStack->paramId);
		if (current_value_changed) {
			// TODO: tell it to deal with abrupt change by smoothing

			NoteRow* noteRow = modelStack->getNoteRowAllowNull();

			if (noteRow) {
				modelStack->modControllable->polyphonicExpressionEventOnChannelOrNote(
				    current_value, modelStack->paramId, modelStack->getNoteRow()->y, MIDICharacteristic::NOTE);
			}
			else {
				modelStack->modControllable->monophonicExpressionEvent(current_value, modelStack->paramId);
			}
		}
	}
}

/// mono expression only - used just for knobs in midi clips currently. If mpe expression ever gets modified to call
/// this, the expression set needs to be made MPE aware to treat Y_SLIDE_TIMBRE as bipolar
int32_t ExpressionParamSet::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	// mono pitch bend is still bipolar and gets handled by parent from here
	if (modelStack->paramId == X_PITCH_BEND) {
		return ParamSet::knobPosToParamValue(knobPos, modelStack);
	}

	if (knobPos == 64) {
		return 2147483647;
	}
	return (knobPos + 64) << 24;
}

/// mono expression only - used just for knobs in midi clips currently. If mpe expression ever gets modified to call
/// this, the expression set needs to be made MPE aware to treat Y_SLIDE_TIMBRE as bipolar
int32_t ExpressionParamSet::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	// mono pitch bend is still bipolar and gets handled by parent from here
	if (modelStack->paramId == X_PITCH_BEND) {
		return ParamSet::paramValueToKnobPos(paramValue, modelStack);
	}

	int32_t knobPos = (paramValue >> 24) - 64;
	return knobPos;
}

char const* expressionParamNames[] = {"pitchBend", "yExpression", "pressure"};

bool ExpressionParamSet::writeToFile(Serializer& writer, bool mustWriteOpeningTagEndFirst) {

	bool writtenAnyYet = false;

	for (int32_t p = 0; p < kNumExpressionDimensions; p++) {
		if (params[p].containsSomething()) {
			if (!writtenAnyYet) {
				writtenAnyYet = true;
				if (mustWriteOpeningTagEndFirst) {
					writer.writeOpeningTagEnd();
				}

				writer.writeOpeningTagBeginning("expressionData");
			}

			writeParamAsAttribute(writer, expressionParamNames[p], p, true);
		}
	}

	if (writtenAnyYet) {
		writer.closeTag();
	}

	return writtenAnyYet;
}

void ExpressionParamSet::readFromFile(Deserializer& reader, ParamCollectionSummary* summary,
                                      int32_t readAutomationUpToPos) {

	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		int32_t p;
		for (p = 0; p < kNumExpressionDimensions; p++) {
			if (!strcmp(tagName, expressionParamNames[p])) {
doReadParam:
				readParam(reader, summary, p, readAutomationUpToPos);
				goto finishedTag;
			}
		}

		if (!strcmp(tagName,
		            "channelPressure")) { // Alpha testers had 2 weeks or so to create files like this - not sure if
			                              // anyone even did.
			p = 2;
			goto doReadParam;
		}

finishedTag:
		reader.exitTag();
	}
}

void ExpressionParamSet::moveRegionHorizontally(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t length,
                                                int32_t offset, int32_t lengthBeforeLoop, Action* action) {

	// Because this is just for ExpressionParamSet, which only has 3 params, let's just do it for all of them rather
	// than our other optimization.
	for (int32_t p = 0; p < kNumExpressionDimensions; p++) {
		AutoParam* param = &params[p];
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);
		param->moveRegionHorizontally(modelStackWithAutoParam, pos, length, offset, lengthBeforeLoop, action);
	}
}

void ExpressionParamSet::clearValues(ModelStackWithParamCollection const* modelStack) {
	for (int32_t p = 0; p < kNumExpressionDimensions; p++) {
		AutoParam* param = &params[p];
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(p, param);
		param->setCurrentValueWithNoReversionOrRecording(modelStackWithAutoParam, 0);
	}
}

void ExpressionParamSet::cancelAllOverriding() {
	for (int32_t p = 0; p < kNumExpressionDimensions; p++) {
		AutoParam* param = &params[p];
		param->cancelOverriding();
	}
}

void ExpressionParamSet::deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) {
	ParamSet::deleteAllAutomation(action, modelStack);

	clearValues(modelStack);
}

/*
 *
 *
 * For interpolating params
    if (whichParamHasInterpolationActive == 127) return;

    // If many params interpolating
    else if (whichParamHasInterpolationActive >= 128) {
        int32_t endInterpolatingParams = (whichParamHasInterpolationActive & 127);
        for (int32_t p = 0; p <= endInterpolatingParams; p++) {
        }
    }

    // If just one param interpolating
    else {
    }
 */

/*
 *
 * For all automated params
 *
        if (whichParamHasAutomation != 127) {
            // Many params automated
            if (whichParamHasAutomation >= 128) {
                int32_t endAutomatedParams = (whichParamHasAutomation & 127);
                for (int32_t p = 0; p <= endAutomatedParams; p++) {
                    if (params[p].isAutomated()) {

                        params[p].

                    }
                }
            }

            // One param automated
            else {
                params[whichParamHasAutomation].
            }
        }

 */
