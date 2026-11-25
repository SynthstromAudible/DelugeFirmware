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
#include "fatfs/ff.h"
#include "modulation/automation/auto_param.h"

// These views grant only a single role. They never expose mutable parameter storage.
class String;
// Existing engine namespace; keep this declaration consistent with its definition.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace AudioEngine {
struct PreviewVolume;
// A narrow friend declaration avoids including the complete audio engine here.
// NOLINTNEXTLINE(readability-redundant-declaration)
void previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound, const PreviewVolume& volume);
} // namespace AudioEngine

class AutoParamSetup {
	friend class MIDIParam;
	friend class ParamSet;
	friend class ExpressionParamSet;
	friend class PatchCable;
	friend class PatchCableSet;
	friend class MIDIParamVector;
	friend class MIDIParamCollection;
	friend class InstrumentClip;
	friend class MIDIInstrument;
	friend class ModControllableAudio;
	friend class GlobalEffectable;
	friend class Sound;
	friend class Song;
	friend class SampleBrowser;
	friend class Slicer;
	friend class Stutterer;
	friend void AudioEngine::previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound,
	                                       const AudioEngine::PreviewVolume& volume);
	explicit AutoParamSetup(AutoParam& param) : param_(&param) {}

public:
	[[gnu::always_inline]] Error readFromFile(Deserializer& reader, int32_t readAutomationUpToPos) {
		return param_->readFromFile(reader, readAutomationUpToPos);
	}
	[[gnu::always_inline]] void shiftValues(int32_t offset) { param_->shiftValues(offset); }
	[[gnu::always_inline]] void shiftParamVolumeByDB(float offset) { param_->shiftParamVolumeByDB(offset); }
	[[gnu::always_inline]] Error makeInterpolationGoodAgain(int32_t clipLength, int32_t quantizationRShift) {
		return param_->makeInterpolationGoodAgain(clipLength, quantizationRShift);
	}
	[[gnu::always_inline]] void transposeCCValuesToChannelPressureValues() {
		param_->transposeCCValuesToChannelPressureValues();
	}
	[[gnu::always_inline]] void setCurrentValueWithNoReversionOrRecording(ModelStackWithAutoParam const* modelStack,
	                                                                      int32_t value) {
		param_->setCurrentValueWithNoReversionOrRecording(modelStack, value);
	}
	[[gnu::always_inline]] void restoreClipValueFromFile(int32_t value) { param_->restoreClipValueFromFile(value); }
	[[gnu::always_inline]] bool readRelativeFromFile(Deserializer& reader) {
		return param_->readRelativeFromFile(reader);
	}
	[[gnu::always_inline]] void setCurrentValueBasicForSetup(int32_t value) {
		param_->setCurrentValueBasicForSetup(value);
	}

private:
	AutoParam* param_;
};

class AutoParamOwnership {
	friend class ParamSet;
	friend class PatchCable;
	friend class PatchCableSet;
	friend class MIDIParam;
	friend class MIDIParamVector;
	friend class MIDIParamCollection;
	friend class ConsequenceParamChange;
	friend class Action;
	friend class MIDIInstrument;
	explicit AutoParamOwnership(AutoParam& param) : param_(&param) {}

public:
	[[gnu::always_inline]] void init() { param_->init(); }
	[[gnu::always_inline]] void cloneFrom(AutoParam* otherParam, bool copyAutomation) {
		param_->cloneFrom(otherParam, copyAutomation);
	}
	[[gnu::always_inline]] Error beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) {
		return param_->beenCloned(copyAutomation, reverseDirectionWithLength);
	}

	[[gnu::always_inline]] void deleteAutomationBasicForSetup() { param_->deleteAutomationBasicForSetup(); }
	[[gnu::always_inline]] void swapState(AutoParamState* state, ModelStackWithAutoParam const* modelStack) {
		param_->swapState(state, modelStack);
	}
	[[gnu::always_inline]] void bindClipValue(ClipParamValue* clip) { param_->bindClipValue(clip); }
	[[gnu::always_inline]] void captureState(AutoParamState& state, bool stealNodes) {
		param_->captureState(state, stealNodes);
	}

	[[gnu::always_inline]] void storeHeldRamp() { param_->storeHeldRamp(); }
	[[gnu::always_inline]] void rebindClipValueAfterMove(ClipParamValue* clip) {
		param_->rebindClipValueAfterMove(clip);
	}
	[[gnu::always_inline]] void releaseNodes() { param_->nodes.empty(); }
	[[gnu::always_inline]] void forgetNodes() { param_->nodes.init(); }

private:
	AutoParam* param_;
};

class AutoParamPlayback {
	friend class ExpressionParamSet;
	friend class ParamCollection;
	friend class MIDIParam;
	friend class ParamSet;
	friend class PatchCableSet;
	friend class MIDIParamCollection;
	friend class InstrumentClip;
	friend class Song;
	explicit AutoParamPlayback(AutoParam& param) : param_(&param) {}

public:
	[[gnu::always_inline]] int32_t processCurrentPos(ModelStackWithAutoParam const* modelStack, bool reversed,
	                                                 bool didPinpong, bool mayInterpolate = true,
	                                                 bool mustUpdateValueAtEveryNode = false) {
		return param_->processCurrentPos(modelStack, reversed, didPinpong, mayInterpolate, mustUpdateValueAtEveryNode);
	}
	[[gnu::always_inline]] bool grabValueFromPos(uint32_t pos, int32_t loopLength) {
		return param_->grabValueFromPos(pos, loopLength);
	}
	[[gnu::always_inline]] bool tickSamples(int32_t numSamples, uint32_t inverse, bool use_float_interpolation,
	                                        bool baselines_unchanged, AutomationStep& step, int32_t* slot,
	                                        int32_t* ramp_slot) {
		return param_->tickSamples(numSamples, inverse, use_float_interpolation, baselines_unchanged, step, slot,
		                           ramp_slot);
	}
	[[gnu::always_inline]] bool tickTicks(int32_t numTicks, bool use_float_interpolation, AutomationStep& step,
	                                      int32_t* slot) {
		return param_->tickTicks(numTicks, use_float_interpolation, step, slot);
	}
	[[gnu::always_inline]] void setPlayPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed) {
		param_->setPlayPos(pos, modelStack, reversed);
	}
	[[gnu::always_inline]] bool grabValueFromPos(uint32_t pos, ModelStackWithAutoParam const* modelStack) {
		return param_->grabValueFromPos(pos, modelStack);
	}
	[[gnu::always_inline]] void copyOverridingFrom(AutoParam* otherParam) { param_->copyOverridingFrom(otherParam); }
	[[gnu::always_inline]] void clearAutomationOffset() const { param_->clearAutomationOffset(); }
	[[gnu::always_inline]] void notifyPingpongOccurred(bool use_float_interpolation) {
		param_->notifyPingpongOccurred(use_float_interpolation);
	}
	[[gnu::always_inline]] void cancelOverriding() { param_->cancelOverriding(); }
	[[gnu::always_inline]] void resetInterpolationIncrement() { param_->resetInterpolationIncrement(); }

private:
	AutoParam* param_;
};

class AutoParamAutomation {
public:
	explicit AutoParamAutomation(AutoParam& param) : param_(&param) {}
	[[gnu::always_inline]] void setValueForRegion(uint32_t pos, uint32_t length, int32_t value,
	                                              ModelStackWithAutoParam const* modelStack,
	                                              ActionType actionType = ActionType::NOTE_EDIT) {
		param_->setValueForRegion(pos, length, value, modelStack, actionType);
	}
	[[gnu::always_inline]] void generateRepeats(uint32_t oldLength, uint32_t newLength, bool shouldPingpong) {
		param_->generateRepeats(oldLength, newLength, shouldPingpong);
	}
	[[gnu::always_inline]] void trimToLength(uint32_t newLength, Action* action,
	                                         ModelStackWithAutoParam const* modelStack) {
		param_->trimToLength(newLength, action, modelStack);
	}
	[[gnu::always_inline]] void erase(Action* action, ModelStackWithAutoParam const* modelStack,
	                                  bool shouldNotify = true) {
		param_->deleteAutomation(action, modelStack, shouldNotify);
	}
	[[gnu::always_inline]] void shiftHorizontally(int32_t amount, int32_t effectiveLength) {
		param_->shiftHorizontally(amount, effectiveLength);
	}
	[[gnu::always_inline]] void copy(int32_t startPos, int32_t endPos, CopiedParamAutomation* copiedParamAutomation,
	                                 bool isPatchCable, ModelStackWithAutoParam const* modelStack) {
		param_->copy(startPos, endPos, copiedParamAutomation, isPatchCable, modelStack);
	}
	[[gnu::always_inline]] void paste(int32_t startPos, int32_t endPos, float scaleFactor,
	                                  ModelStackWithAutoParam const* modelStack,
	                                  CopiedParamAutomation* copiedParamAutomation, bool isPatchCable) {
		param_->paste(startPos, endPos, scaleFactor, modelStack, copiedParamAutomation, isPatchCable);
	}
	[[gnu::always_inline]] void deleteTime(int32_t startPos, int32_t lengthToDelete,
	                                       ModelStackWithAutoParam* modelStack) {
		param_->deleteTime(startPos, lengthToDelete, modelStack);
	}
	[[gnu::always_inline]] void insertTime(int32_t pos, int32_t lengthToInsert) {
		param_->insertTime(pos, lengthToInsert);
	}
	[[gnu::always_inline]] void appendParam(AutoParam* otherParam, int32_t oldLength,
	                                        int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {
		param_->appendParam(otherParam, oldLength, reverseThisRepeatWithLength, pingpongingGenerally);
	}
	[[gnu::always_inline]] void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop,
	                                                            Action* action,
	                                                            ModelStackWithAutoParam const* modelStack) {
		param_->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action, modelStack);
	}
	[[gnu::always_inline]] void stealNodes(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t regionLength,
	                                       int32_t loopLength, Action* action,
	                                       StolenParamNodes* stolenNodeRecord = nullptr) {
		param_->stealNodes(modelStack, pos, regionLength, loopLength, action, stolenNodeRecord);
	}
	[[gnu::always_inline]] void insertStolenNodes(ModelStackWithAutoParam const* modelStack, int32_t pos,
	                                              int32_t regionLength, int32_t loopLength, Action* action,
	                                              StolenParamNodes* stolenNodeRecord) {
		param_->insertStolenNodes(modelStack, pos, regionLength, loopLength, action, stolenNodeRecord);
	}
	[[gnu::always_inline]] void moveRegionHorizontally(ModelStackWithAutoParam const* modelStack, int32_t pos,
	                                                   int32_t length, int32_t offset, int32_t lengthBeforeLoop,
	                                                   Action* action) {
		param_->moveRegionHorizontally(modelStack, pos, length, offset, lengthBeforeLoop, action);
	}
	[[gnu::always_inline]] void deleteNodesWithinRegion(ModelStackWithAutoParam const* modelStack, int32_t pos,
	                                                    int32_t length) {
		param_->deleteNodesWithinRegion(modelStack, pos, length);
	}
	[[gnu::always_inline]] int32_t setNodeAtPos(int32_t pos, int32_t value, bool shouldInterpolate) {
		return param_->setNodeAtPos(pos, value, shouldInterpolate);
	}
	[[gnu::always_inline]] int32_t homogenizeRegion(ModelStackWithAutoParam const* modelStack, int32_t startPos,
	                                                int32_t length, int32_t startValue, bool interpolateLeftNode,
	                                                bool interpolateRightNode, int32_t effectiveLength, bool reversed,
	                                                int32_t posAtWhichClipWillCut = 2147483647) {
		return param_->homogenizeRegion(modelStack, startPos, length, startValue, interpolateLeftNode,
		                                interpolateRightNode, effectiveLength, reversed, posAtWhichClipWillCut);
	}

	[[gnu::always_inline]] void testSequentiality(const char* code) { param_->nodes.testSequentiality(code); }

private:
	AutoParam* param_;
};

class AutoParamLiveInput {
	friend class ParamCollection;
	friend class NoteRow;
	friend class PerformanceView;
	friend class MelodicInstrument;
	explicit AutoParamLiveInput(AutoParam& param) : param_(&param) {}

public:
	[[gnu::always_inline]] void setValueForLiveInput(int32_t value, ModelStackWithAutoParam const* modelStack,
	                                                 int32_t livePos = -1, bool mayDeleteNodesInLinearRun = true,
	                                                 bool doMPEMode = false) {
		param_->setValueForLiveInput(value, modelStack, livePos, mayDeleteNodesInLinearRun, doMPEMode);
	}
	[[gnu::always_inline]] void setLiveValuePossiblyForRegion(int32_t value, ModelStackWithAutoParam const* modelStack,
	                                                          int32_t pos, int32_t length,
	                                                          bool mayDeleteNodesInLinearRun = true) {
		param_->setLiveValuePossiblyForRegion(value, modelStack, pos, length, mayDeleteNodesInLinearRun);
	}

private:
	AutoParam* param_;
};

[[gnu::always_inline]] inline AutoParamAutomation AutoParam::automation() {
	return AutoParamAutomation(*this);
}
