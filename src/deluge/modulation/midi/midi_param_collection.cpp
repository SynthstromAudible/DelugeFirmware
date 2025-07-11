/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "modulation/midi/midi_param_collection.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "gui/views/automation_view.h"
#include "io/midi/midi_engine.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/exceptions.h"

MIDIParamCollection::MIDIParamCollection(ParamCollectionSummary* summary)
    : ParamCollection(sizeof(MIDIParamCollection), summary) {

	// Just to indicate there could be some automation, cos we don't actually use this variable properly.
	// TODO: at least make this go to 0 when no MIDIParams present.
	summary->whichParamsAreAutomated[0] = 1;
	summary->whichParamsAreInterpolating[0] = 0;
}

MIDIParamCollection::~MIDIParamCollection() {
	deleteAllParams(nullptr);
}
void MIDIParamCollection::tickTicks(int32_t numTicks, ModelStackWithParamCollection* modelStack) {
	for (auto& [cc, param] : params) {
		if (param.valueIncrementPerHalfTick != 0) {
			int32_t oldValue = param.getCurrentValue();
			bool shouldNotify = param.tickTicks(numTicks);
			if (shouldNotify) { // Should always actually be true...
				ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(cc, &param);
				notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
			}
		}
	}
}
void MIDIParamCollection::deleteAllParams(Action* action) {
	if (action != nullptr) {
		for (auto& [cc, param] : params) {
			param.deleteAutomationBasicForSetup();
		}
	}

	params.clear();
}

void MIDIParamCollection::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) {
	try {
		params = decltype(params){params}; // Copies construct new Params
	} catch (deluge::exception e) {
		FREEZE_WITH_ERROR("EMPC");
	}

	// And now, copy the memory for the automation data that each member of params references
	for (auto& param : params | std::views::values) {
		param.beenCloned(copyAutomation, reverseDirectionWithLength);
	}
}

void MIDIParamCollection::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {

	// Bend param is the only one which is actually gonna maybe want to set up some interpolation -
	// but for the other ones we still need to initialize them and crucially make sure automation overriding is
	// switched off
	for (auto& [cc, param] : params) {
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(cc, &param);

		param.setPlayPos(pos, modelStackWithAutoParam, reversed);
	}

	ParamCollection::setPlayPos(pos, modelStack, reversed);
}

void MIDIParamCollection::generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength,
                                          uint32_t newLength, bool shouldPingpong) {
	for (AutoParam& param : params | std::views::values) {
		param.generateRepeats(oldLength, newLength, shouldPingpong);
	}
}

void MIDIParamCollection::appendParamCollection(ModelStackWithParamCollection* modelStack,
                                                ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
                                                int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {

	MIDIParamCollection* otherMIDIParamCollection = (MIDIParamCollection*)otherModelStack->paramCollection;
	for (auto& [otherCC, otherParam] : otherMIDIParamCollection->params) {
		// Find param in *this* collection for the same CC. There should be one
		auto it = params.find(otherCC);

		if (it != params.end()) {
			AutoParam& midiParam = it->second;
			midiParam.appendParam(&otherParam, oldLength, reverseThisRepeatWithLength, pingpongingGenerally);
		}
	}

	ticksTilNextEvent = 0;
}

void MIDIParamCollection::trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
                                       bool maySetupPatching) {
	for (auto& [cc, param] : params) {
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(cc, &param);

		param.trimToLength(newLength, action, modelStackWithAutoParam);
	}
	ticksTilNextEvent = 0;
}

void MIDIParamCollection::shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount,
                                            int32_t effectiveLength) {
	for (AutoParam& param : params | std::views::values) {
		param.shiftHorizontally(amount, effectiveLength);
	}
}

void MIDIParamCollection::processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped,
                                            bool reversed, bool didPingpong, bool mayInterpolate) {

	ticksTilNextEvent -= ticksSkipped;
	if (ticksTilNextEvent <= 0) {
		bool interpolating = false;

		ticksTilNextEvent = 2147483647;

		for (auto& [cc, param] : params) {
			ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(cc, &param);

			int32_t ticksTilNextEventThisParam =
			    param.processCurrentPos(modelStackWithAutoParam, reversed, didPingpong, true, true); // No interpolating
			ticksTilNextEvent = std::min(ticksTilNextEvent, ticksTilNextEventThisParam);
			if (param.valueIncrementPerHalfTick) {
				interpolating = true;
			}
		}
		modelStack->summary->whichParamsAreInterpolating[0] = static_cast<uint32_t>(interpolating);
	}
}

void MIDIParamCollection::remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) {

	// Try to find the key
	int32_t cc = modelStack->paramId;
	auto it = params.find(cc);

	// Insert if it doesn't exist
	if (it == params.end()) {
		try {
			it = params.try_emplace(cc).first;
		} catch (deluge::exception e) {
			if (e == deluge::exception::BAD_ALLOC) {
				// TODO(@stellar-aria): Early return due to allocation failure follows the original
				// implementation, but is it a good idea?
				return;
			}
			FREEZE_WITH_ERROR("EXMR");
		}
	}

	AutoParam& param = it->second;
	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(&param);

	param.swapState(state, modelStackWithParam);
}

void MIDIParamCollection::deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) {

	for (auto& [cc, param] : params) {
		if (param.isAutomated()) {
			ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(cc, &param);
			param.deleteAutomation(action, modelStackWithParam, false);
		}
	}
}

ModelStackWithAutoParam* MIDIParamCollection::getAutoParamFromId(ModelStackWithParamId* modelStack,
                                                                 bool allowCreation) {

	// Try to find the key
	int32_t cc = modelStack->paramId;
	auto it = params.find(cc);

	if (it == params.end()) {
		// Didn't find the key
		if (!allowCreation) {
			// Creation not allowed, so add a nullptr
			return modelStack->addAutoParam(nullptr);
		}

		// Try creating a new param
		try {
			it = params.try_emplace(cc).first;
		} catch (deluge::exception e) {
			if (e == deluge::exception::BAD_ALLOC) {
				return modelStack->addAutoParam(nullptr);
			}
			FREEZE_WITH_ERROR("EXMG");
		}
	}

	AutoParam& param = it->second;

	return modelStack->addAutoParam(&param);
}

int32_t MIDIParamCollection::autoparamValueToCC(int32_t newValue) {
	int32_t rShift = 25;
	int32_t roundingAmountToAdd = 1 << (rShift - 1);
	int32_t maxValue = 2147483647 - roundingAmountToAdd;

	if (newValue > maxValue) {
		newValue = maxValue;
	}
	return (newValue + roundingAmountToAdd) >> rShift;
}
void MIDIParamCollection::sendMIDI(MIDISource source, int32_t masterChannel, int32_t cc, int32_t newValue,
                                   int32_t midiOutputFilter) {
	int32_t newValueSmall = autoparamValueToCC(newValue);

	if (cc == CC_NUMBER_PROGRAM_CHANGE) {
		midiEngine.sendPGMChange(source, masterChannel, newValueSmall + 64, midiOutputFilter);
		return;
	}

	midiEngine.sendCC(source, masterChannel, cc, newValueSmall + 64,
	                  midiOutputFilter); // TODO: get master channel
}

// For MIDI CCs, which prior to V2.0 did interpolation
// Returns error code
Error MIDIParamCollection::makeInterpolatedCCsGoodAgain(int32_t clipLength) {

	for (auto& [cc, param] : params) {
		if (cc >= 120) {
			return Error::NONE;
		}
		Error error = param.makeInterpolationGoodAgain(clipLength, 25);
		if (error != Error::NONE) {
			return error;
		}
	}

	return Error::NONE;
}

void MIDIParamCollection::grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) {
	for (auto& [cc, param] : params) {

		// With MIDI, we only want to send these out if the param is actually automated and the value is
		// actually different
		if (param.isAutomated()) {

			int32_t oldValue = param.getCurrentValue();
			ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(cc, &param);
			bool shouldSend = param.grabValueFromPos(pos, modelStackWithAutoParam);

			if (shouldSend) {
				notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
			}
		}
	}
}

void MIDIParamCollection::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop,
                                                          Action* action, ModelStackWithParamCollection* modelStack) {

	for (auto& [cc, param] : params) {
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(cc, &param);

		param.nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action, modelStackWithAutoParam);
	}
}

void MIDIParamCollection::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                       bool automationChanged, bool automatedBefore,
                                                       bool automatedNow) {

	ParamCollection::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                              automatedNow);

	if (modelStack->song->isOutputActiveInArrangement((MIDIInstrument*)modelStack->modControllable)) {
		auto new_v = modelStack->autoParam->getCurrentValue();
		bool current_value_changed = modelStack->modControllable->valueChangedEnoughToMatter(
		    oldValue, new_v, getParamKind(), modelStack->paramId);
		if (current_value_changed) {
			MIDIInstrument* instrument = (MIDIInstrument*)modelStack->modControllable;
			int32_t midiOutputFilter = instrument->getChannel();
			int32_t masterChannel = instrument->getOutputMasterChannel();
			sendMIDI(instrument, masterChannel, modelStack->paramId, modelStack->autoParam->getCurrentValue(),
			         midiOutputFilter);
		}
	}
}

bool MIDIParamCollection::mayParamInterpolate(int32_t paramId) {
	return false;
}

int32_t MIDIParamCollection::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	return ParamCollection::knobPosToParamValue(knobPos, modelStack);
}

void MIDIParamCollection::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {
	ParamCollection::notifyPingpongOccurred(modelStack);

	for (AutoParam& param : params | std::views::values) {
		param.notifyPingpongOccurred();
	}
}

void MIDIParamCollection::writeToFile(Serializer& writer) {
	if (params.size()) {

		writer.writeOpeningTag("midiParams");

		for (auto& [cc, param] : params) {
			writer.writeOpeningTag("param");
			if (cc == CC_NUMBER_NONE) { // Why would I have put this in here?
				writer.writeTag("cc", "none");
			}
			else {
				writer.writeTag("cc", cc);
			}

			writer.writeOpeningTag("value", false);
			param.writeToFile(writer, true);
			writer.writeClosingTag("value", false);

			writer.writeClosingTag("param");
		}

		writer.writeClosingTag("midiParams");
	}
}

std::expected<typename decltype(MIDIParamCollection::params)::iterator, Error>
MIDIParamCollection::getOrCreateParamFromCC(int32_t cc) {
	auto it = params.find(cc);
	if (it != params.end()) {
		return it; // Found it!
	}

	// Didn't find the key
	// Try creating a new param
	try {
		it = params.try_emplace(cc).first;
	} catch (deluge::exception e) {
		if (e == deluge::exception::BAD_ALLOC) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		FREEZE_WITH_ERROR("EXMF");
	}
	return it;
}
