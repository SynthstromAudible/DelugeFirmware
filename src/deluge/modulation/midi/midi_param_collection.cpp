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
#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "gui/views/automation_view.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/exceptions.h"
#include <new>

MIDIParamCollection::MIDIParamCollection(ParamCollectionSummary* summary)
    : ParamCollection(sizeof(MIDIParamCollection), summary) {

	// Just to indicate there could be some automation, cos we don't actually use this variable properly.
	// TODO: at least make this go to 0 when no MIDIParams present.
	summary->whichParamsAreAutomated[0] = 1;
	summary->whichParamsAreInterpolating[0] = 0;
}

// domStore_ intentionally left null: a cloned collection starts UNBOUND (owns its nodes) until separately bound.
MIDIParamCollection::MIDIParamCollection(MIDIParamCollection const& other)
    : ParamCollection(other), params(other.params) {
}

MIDIParamCollection::~MIDIParamCollection() {
	deleteAllParams(NULL, false);
}

void MIDIParamCollection::bindDomStore() {
	if (domStore_) {
		return; // already bound
	}
	// Size the store to the CC count present now; each CC's AutoParam borrows its slot. CCs learned later stay
	// unbound (their AutoParams self-own) — the store is never grown, so these borrow pointers never dangle even
	// as the MIDIParamVector relocates on a subsequent insert (the borrow travels with the AutoParam's move).
	domStore_ = std::make_unique<deluge::project::DocParamSet>();
	domStore_->params.resize(params.size());
	for (int32_t i = 0; i < std::ssize(params); i++) {
		params[i].param.borrowNodeStorage(&domStore_->params[i]);
	}
}

deluge::project::DocParamSet* MIDIParamCollection::domStore() {
	return domStore_.get();
}

deluge::project::DocAutoParam* MIDIParamCollection::docForParam(int32_t i) {
	return (domStore_ && i < std::ssize(domStore_->params)) ? &domStore_->params[i]
	                                                        : params[i].param.ownDocForFallback();
}

deluge::project::DocAutoParam* MIDIParamCollection::docParamForId(int32_t paramId) {
	int32_t i = params.firstAtOrAfter(paramId); // paramId == CC; the vector is ordered by CC
	if (i < std::ssize(params) && params[i].cc == paramId) {
		return docForParam(i);
	}
	return nullptr;
}

void MIDIParamCollection::deleteAllParams(Action* action, bool deleteStorageToo) {
	if (action) {
		for (MIDIParam& midiParam : params) {
			midiParam.param.deleteAutomationBasicForSetup(*midiParam.param.docForThreading());
		}
	}
	params.clear(); // Destructs the params (the deleteStorageToo distinction is moot now this does both)
}

void MIDIParamCollection::tickTicks(int32_t numTicks, ModelStackWithParamCollection* modelStack) {
	for (MIDIParam& midiParam : params) {
		AutoParam* param = &midiParam.param;

		if (param->hasInterpolationIncrement()) {
			int32_t oldValue = param->getCurrentValue();
			bool shouldNotify = param->tickTicks(numTicks);
			if (shouldNotify) { // Should always actually be true...
				ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam.cc, param);
				notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
			}
		}
	}
}

Error MIDIParamCollection::clone(ParamCollection*& result, bool copyAutomation,
                                 int32_t reverseDirectionWithLength) const {
	// params is a value member (MIDIParamVector), so the implicit copy ctor deep-copies it (and each
	// embedded AutoParam) — no pointer re-pointing needed.
	void* mem = deluge::memory::alloc_fast(sizeof(MIDIParamCollection));
	if (mem == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	MIDIParamCollection* clone;
	try {
		clone = new (mem) MIDIParamCollection(*this);
	} catch (deluge::exception&) {
		delugeDealloc(mem);
		return Error::INSUFFICIENT_RAM;
	}
	if (!copyAutomation) {
		clone->stripAutomation();
	}
	else if (reverseDirectionWithLength != 0) {
		try {
			clone->flattenReverseAutomation(reverseDirectionWithLength);
		} catch (deluge::exception&) {
			clone->~MIDIParamCollection();
			delugeDealloc(mem);
			return Error::INSUFFICIENT_RAM;
		}
	}
	result = clone;
	return Error::NONE;
}

void MIDIParamCollection::stripAutomation() {
	for (MIDIParam& midiParam : params) {
		midiParam.param.docForThreading()->nodes.clear();
	}
}

void MIDIParamCollection::flattenReverseAutomation(int32_t reverseDirectionWithLength) {
	for (MIDIParam& midiParam : params) {
		midiParam.param.flattenReverse(*midiParam.param.docForThreading(), reverseDirectionWithLength);
	}
}

void MIDIParamCollection::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {

	// Bend param is the only one which is actually gonna maybe want to set up some interpolation -
	// but for the other ones we still need to initialize them and crucially make sure automation overriding is
	// switched off
	for (MIDIParam& midiParam : params) {
		AutoParam* param = &midiParam.param;
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam.cc, param);

		param->setPlayPos(pos, modelStackWithAutoParam, reversed);
	}

	ParamCollection::setPlayPos(pos, modelStack, reversed);
}

void MIDIParamCollection::generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength,
                                          uint32_t newLength, bool shouldPingpong) {
	for (MIDIParam& midiParam : params) {
		midiParam.param.generateRepeats(*midiParam.param.docForThreading(), oldLength, newLength, shouldPingpong);
	}
}

void MIDIParamCollection::appendParamCollection(ModelStackWithParamCollection* modelStack,
                                                ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
                                                int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {

	MIDIParamCollection* otherMIDIParamCollection = (MIDIParamCollection*)otherModelStack->paramCollection;
	for (MIDIParam& otherMidiParam : otherMIDIParamCollection->params) {
		// Find param in *this* collection for the same CC. There should be one
		MIDIParam* midiParam = params.getParamFromCC(otherMidiParam.cc);
		if (midiParam != nullptr) {
			midiParam->param.appendParam(*midiParam->param.docForThreading(), *otherMidiParam.param.docForThreading(),
			                             oldLength, reverseThisRepeatWithLength, pingpongingGenerally);
		}
	}

	ticksTilNextEvent = 0;
}

void MIDIParamCollection::trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
                                       bool maySetupPatching) {
	for (MIDIParam& midiParam : params) {
		AutoParam* param = &midiParam.param;
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam.cc, param);

		param->trimToLength(newLength, action, modelStackWithAutoParam);
	}
	ticksTilNextEvent = 0;
}

void MIDIParamCollection::shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount,
                                            int32_t effectiveLength) {
	for (MIDIParam& midiParam : params) {
		midiParam.param.shiftHorizontally(*midiParam.param.docForThreading(), amount, effectiveLength);
	}
}

void MIDIParamCollection::processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped,
                                            bool reversed, bool didPingpong, bool mayInterpolate) {

	ticksTilNextEvent -= ticksSkipped;
	if (ticksTilNextEvent <= 0) {
		bool interpolating = false;

		ticksTilNextEvent = 2147483647;

		for (MIDIParam& midiParam : params) {
			AutoParam* param = &midiParam.param;
			ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam.cc, param);

			int32_t ticksTilNextEventThisParam = param->processCurrentPos(modelStackWithAutoParam, reversed,
			                                                              didPingpong, false, true); // No interpolating
			ticksTilNextEvent = std::min(ticksTilNextEvent, ticksTilNextEventThisParam);
			if (param->hasInterpolationIncrement()) {
				interpolating = true;
			}
		}
		modelStack->summary->whichParamsAreInterpolating[0] = static_cast<uint32_t>(interpolating);
	}
}

void MIDIParamCollection::remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) {

	MIDIParam* midiParam = params.getOrCreateParamFromCC(modelStack->paramId);
	if (!midiParam) {
		return;
	}

	AutoParam* param = &midiParam->param;

	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(param);

	param->swapState(state, modelStackWithParam);
}

void MIDIParamCollection::deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) {

	for (MIDIParam& midiParam : params) {

		if (midiParam.param.isAutomated(*midiParam.param.docForThreading())) {
			ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(midiParam.cc, &midiParam.param);
			midiParam.param.deleteAutomation(action, modelStackWithParam, false);
		}
	}
}

ModelStackWithAutoParam* MIDIParamCollection::getAutoParamFromId(ModelStackWithParamId* modelStack,
                                                                 bool allowCreation) {
	int32_t defaultValue = 0;
	MIDIParam* midiParam = params.getOrCreateParamFromCC(modelStack->paramId, defaultValue, allowCreation);
	AutoParam* param = NULL;
	if (midiParam) {
		param = &midiParam->param;
	}
	return modelStack->addAutoParam(param);
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

	midiEngine.sendCC(source, masterChannel, cc, newValueSmall + 64,
	                  midiOutputFilter); // TODO: get master channel
}

// For MIDI CCs, which prior to V2.0 did interpolation
// Returns error code
Error MIDIParamCollection::makeInterpolatedCCsGoodAgain(int32_t clipLength) {

	for (MIDIParam& midiParam : params) {

		if (midiParam.cc >= 120) {
			return Error::NONE;
		}
		Error error = midiParam.param.makeInterpolationGoodAgain(*midiParam.param.docForThreading(), clipLength, 25);
		if (error != Error::NONE) {
			return error;
		}
	}

	return Error::NONE;
}

void MIDIParamCollection::grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) {
	for (MIDIParam& midiParam : params) {

		AutoParam* param = &midiParam.param;

		// With MIDI, we only want to send these out if the param is actually automated and the value is
		// actually different
		if (param->isAutomated(*param->docForThreading())) {

			int32_t oldValue = param->getCurrentValue();
			ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam.cc, param);
			bool shouldSend = param->grabValueFromPos(pos, modelStackWithAutoParam);

			if (shouldSend) {
				notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
			}
		}
	}
}

void MIDIParamCollection::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop,
                                                          Action* action, ModelStackWithParamCollection* modelStack) {

	for (MIDIParam& midiParam : params) {
		AutoParam* param = &midiParam.param;
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(midiParam.cc, param);

		param->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action, modelStackWithAutoParam);
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

	for (MIDIParam& midiParam : params) {
		midiParam.param.notifyPingpongOccurred();
	}
}

void MIDIParamCollection::writeToFile(Serializer& writer) {
	if (!params.empty()) {

		writer.writeOpeningTag("midiParams");

		for (MIDIParam& midiParam : params) {
			int32_t cc = midiParam.cc;

			writer.writeOpeningTag("param");
			if (cc == CC_NUMBER_NONE) { // Why would I have put this in here?
				writer.writeTag("cc", "none");
			}
			else {
				writer.writeTag("cc", cc);
			}

			writer.writeOpeningTag("value", false);
			midiParam.param.writeToFile(*midiParam.param.docForThreading(), writer, true);
			writer.writeClosingTag("value", false);

			writer.writeClosingTag("param");
		}

		writer.writeClosingTag("midiParams");
	}
}

/*
    for (int32_t i = 0; i < params.getNumElements(); i++) {
        MIDIParam* midiParam = params.getElement(i);

    }
*/
