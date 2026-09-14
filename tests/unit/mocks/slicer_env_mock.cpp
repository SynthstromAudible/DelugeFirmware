/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "slicer_env_mock.h"

#include "definitions_cxx.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "model/clip/clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_drum.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/owner.h"

#include <cstddef>
#include <cstring>
#include <new>
#include <string>

namespace {

/// Raw, zeroed backing for one object of type `T`.
///
/// The firmware types below are reached by member access only -- no constructor of theirs is ever
/// run, because running one would drag in the whole model layer this binary deliberately does not
/// link. Zeroed storage is exactly what the audition path expects to find in every field it reads
/// (empty range lists, null audio file, no active voices).
template <typename T>
struct RawObject {
	alignas(alignof(T)) unsigned char bytes[sizeof(T)];
	T* get() { return reinterpret_cast<T*>(bytes); }
	void zero() { std::memset(bytes, 0, sizeof(bytes)); }
};

RawObject<Kit> g_kit;
RawObject<SoundDrum> g_drum;
RawObject<MultisampleRange> g_range;
RawObject<Sample> g_sample;
RawObject<Clip> g_clip;
RawObject<ParamManager> g_paramManager;
RawObject<AutoParam> g_autoParam;

/// The one `ParamCollection` the audition path asks for an `AutoParam` from. Every other member is
/// a do-nothing override: `ParamCollection`'s interface is 15 pure virtuals wide and the audition
/// touches exactly one of them.
class FakeParamCollection final : public ParamCollection {
public:
	FakeParamCollection() : ParamCollection(sizeof(FakeParamCollection), nullptr) {}

	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation) override {
		// The ModelStack classes are same-memory views of one buffer, which is how the real
		// collections answer this too.
		auto* withAutoParam = reinterpret_cast<ModelStackWithAutoParam*>(modelStack);
		withAutoParam->autoParam = g_autoParam.get();
		return withAutoParam;
	}

	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) override {}
	void tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) override {}
	void tickTicks(int32_t numSamples, ModelStackWithParamCollection* modelStack) override {}
	void playbackHasEnded(ModelStackWithParamCollection* modelStack) override {}
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) override {}
	void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong) override {}
	void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                           ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                           int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) override {}
	void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                  bool maySetupPatching) override {}
	void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount,
	                       int32_t effectiveLength) override {}
	void processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSinceLast, bool reversed,
	                       bool didPingpong, bool mayInterpolate) override {}
	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) override {}
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) override {}
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack) override {}
	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::PATCHED; }
};

FakeParamCollection* g_paramCollection = nullptr;

} // namespace

namespace deluge::test {

bool forceLoadInProgress = false;
void (*duringNextLoadFile)() = nullptr;
int32_t auditionNoteCount = 0;
int32_t lastAuditionOn = -1;

void resetSlicerEnv() {
	forceLoadInProgress = false;
	duringNextLoadFile = nullptr;
	auditionNoteCount = 0;
	lastAuditionOn = -1;

	g_kit.zero();
	g_drum.zero();
	g_range.zero();
	g_sample.zero();
	g_clip.zero();
	g_paramManager.zero();
	g_autoParam.zero();

	static FakeParamCollection collection;
	g_paramCollection = &collection;

	// The std::string members are the only ones that cannot be left as zeroed bytes.
	new (&g_drum.get()->drumName) std::string();
	new (&g_range.get()->sampleHolder.filePath) std::string();
	new (&g_sample.get()->filePath) std::string("SAMPLES/SLICE.WAV");

	g_kit.get()->firstDrum = reinterpret_cast<Drum*>(g_drum.get());

	// AUDIO, so graphicsRoutine()'s voice-position lookup short-circuits before it reaches the
	// voice machinery. It is the deferral re-ask at the top of that function the specs are after.
	const_cast<ClipType&>(g_clip.get()->type) = ClipType::AUDIO;

	// getPatchedParamSetSummary() hands back summaries[1]; the audition asks it for an AutoParam.
	g_paramManager.get()->summaries[1].paramCollection = g_paramCollection;

	waveformBasicNavigator.sample = g_sample.get();
}

} // namespace deluge::test

// ---------------------------------------------------------------------------------------------
// The collaborators the audition path actually calls.
// ---------------------------------------------------------------------------------------------

Kit* getCurrentKit() {
	return g_kit.get();
}

Clip* getCurrentClip() {
	return g_clip.get();
}

ModelStackWithThreeMainThings* SoundEditor::getCurrentModelStack(void* memory) {
	std::memset(memory, 0, MODEL_STACK_MAX_SIZE);
	auto* modelStack = static_cast<ModelStackWithThreeMainThings*>(memory);
	modelStack->paramManager = g_paramManager.get();
	return modelStack;
}

MultiRange* Source::getOrCreateFirstRange() {
	return g_range.get();
}

/// The one seam that matters: a card miss that has not landed yet answers LOAD_IN_PROGRESS, and a
/// hit assigns an audio file, exactly as the real holder does.
Error AudioFileHolder::loadFile(bool reversed, bool manuallySelected, bool mayActuallyReadFile,
                                int32_t clusterLoadInstruction, bool makeWaveTableWorkAtAllCosts, bool mayLoadAsync,
                                deluge::storage::AudioLoadContext* context) {
	if (deluge::test::duringNextLoadFile != nullptr) {
		// Cleared BEFORE the call, not after: the hook re-enters the slicer, which reaches this
		// function again, and a hook that re-armed itself would recurse without bound.
		auto* hook = deluge::test::duringNextLoadFile;
		deluge::test::duringNextLoadFile = nullptr;
		hook();
	}
	if (deluge::test::forceLoadInProgress) {
		return Error::LOAD_IN_PROGRESS;
	}
	audioFile = reinterpret_cast<AudioFile*>(g_sample.get());
	return Error::NONE;
}

void InstrumentClipView::sendAuditionNote(bool on, uint8_t yDisplay, uint8_t velocity, uint32_t sampleSyncLength) {
	deluge::test::auditionNoteCount++;
	deluge::test::lastAuditionOn = on ? 1 : 0;
}

namespace deluge::storage {
/// Owner::run, with the legacy/host semantics `owner.h` documents: "on legacy/host it runs inline on
/// the caller's stack". Not a stub -- running the op is what makes this build's Owner faithful, and it
/// is the same thing `owner.cpp` does via `deluge_worker_run` on a BSP with no worker fiber. The real
/// owner.cpp is not linked here because it would drag `libdeluge/worker.h`'s whole ABI in.
///
/// The queue-full return (false) therefore never happens in these specs -- the same coverage boundary the
/// Embassy-only fiber queue has everywhere else in the tree.
bool Owner::run(void (*fn)(void*), void* ctx) {
	fn(ctx);
	return true;
}
} // namespace deluge::storage

void AutoParam::setCurrentValueWithNoReversionOrRecording(ModelStackWithAutoParam const* modelStack, int32_t newValue) {
}

int32_t getParamFromUserValue(uint8_t p, int8_t userValue) {
	return 0;
}

namespace PadLEDs {
void setTickSquares(const uint8_t* squares, const uint8_t* colours) {
}
} // namespace PadLEDs

// ---------------------------------------------------------------------------------------------
// Everything else `slicer.cpp` references. None of it runs in the audition specs -- these exist so
// the real translation unit links. Value-returning symbols use DELUGE_STUB_RET, so a future spec
// that drives one gets a defined (if inert) answer rather than an undefined return; see that
// macro's comment for the failure mode this avoids.
// ---------------------------------------------------------------------------------------------

/// Define a do-nothing function under an explicit mangled name.
///
/// The asm label means the definition need not restate the real declaration, which for a member
/// would drag another header's worth of model layer in here. A member's `this` is just a leading
/// stack argument the stub ignores, and the i386 SysV ABI is caller-cleanup, so a nullary stub is
/// call-compatible with any non-variadic signature.
///
/// USE ONLY FOR A FUNCTION RETURNING void. A `void` stub standing in for a value-returning function
/// leaves its caller reading an undefined return -- garbage at runtime, NOT a link error, i.e. the
/// one failure mode of this file that is silent. Value-returning symbols go through
/// DELUGE_STUB_RET below, so the return is defined even for a spec that has not been written yet.
#define DELUGE_STUB_FN(id, mangled)                                                                                    \
	void id() asm(mangled);                                                                                            \
	void id() {                                                                                                        \
	}

/// As DELUGE_STUB_FN, but for a function that returns a value: @p type is its real return type and
/// @p value the answer every call gets.
#define DELUGE_STUB_RET(id, mangled, type, value)                                                                      \
	type id() asm(mangled);                                                                                            \
	type id() {                                                                                                        \
		return value;                                                                                                  \
	}

/// Zeroed storage under an explicit mangled name, for a vtable or a global whose type is never
/// touched here. Over-aligned rather than left at `unsigned char[]`'s natural alignment of 1: some
/// of these ARE accessed through their real type (`waveformBasicNavigator`), which UBSan reports as
/// a misaligned access at alignment 1 even where x86 tolerates it.
#define DELUGE_STUB_DATA(id, mangled, bytes) alignas(std::max_align_t) unsigned char id[bytes] asm(mangled) = {};

// `SoundDrum::killAllVoices()` IS entered (Slicer::stopAnyPreviewing calls it) but is stubbed under
// its mangled name rather than written out as a member definition: defining a member of `SoundDrum`
// here makes this the translation unit that emits `SoundDrum`'s vtable, and that vtable names every
// virtual in the Sound/ModControllableAudio hierarchy -- i.e. the whole synth. The vtable is stubbed
// as data below instead; nothing in these specs ever dispatches through it.
DELUGE_STUB_FN(stub_soundDrumKillAllVoices, "_ZN9SoundDrum13killAllVoicesEv")

DELUGE_STUB_RET(stub_multisampleRangeGetAudioFileHolder, "_ZN16MultisampleRange18getAudioFileHolderEv",
                AudioFileHolder*, &g_range.get()->sampleHolder)
DELUGE_STUB_RET(stub_getUIUpOneLevel, "_Z15getUIUpOneLeveli", UI*, nullptr)
DELUGE_STUB_FN(stub_renderUIsForOled, "_Z16renderUIsForOledv")
DELUGE_STUB_FN(stub_uiNeedsRendering, "_Z16uiNeedsRenderingP2UIjj")
DELUGE_STUB_RET(stub_getCurrentInstrument, "_Z20getCurrentInstrumentv", Instrument*, nullptr)
DELUGE_STUB_RET(stub_getCurrentInstrumentClip, "_Z24getCurrentInstrumentClipv", InstrumentClip*, nullptr)
DELUGE_STUB_FN(stub_audioEngineStopAnyPreviewing, "_ZN11AudioEngine17stopAnyPreviewingEv")
DELUGE_STUB_RET(stub_voiceSampleGetPlaySample, "_ZN11VoiceSample13getPlaySampleEP6SampleP19SamplePlaybackGuide",
                int32_t, 0)
DELUGE_STUB_FN(stub_actionLoggerDeleteAllLogs, "_ZN12ActionLogger13deleteAllLogsEv")
DELUGE_STUB_RET(stub_matrixDriverIsPadPressed, "_ZN12MatrixDriver12isPadPressedEii", bool, false)
DELUGE_STUB_RET(stub_paramManagerSetupWithPatching, "_ZN12ParamManager17setupWithPatchingEv", Error, Error::NONE)
DELUGE_STUB_FN(stub_paramManagerDtor, "_ZN12ParamManagerD2Ev")
DELUGE_STUB_FN(stub_sampleHolderSetAudioFile, "_ZN12SampleHolder12setAudioFileEP9AudioFilebbi")
DELUGE_STUB_FN(stub_sampleBrowserExitAndNeverDeleteDrum, "_ZN13SampleBrowser22exitAndNeverDeleteDrumEv")
DELUGE_STUB_RET(stub_sampleBrowserClaimAudioFile, "_ZN13SampleBrowser27claimAudioFileForInstrumentEb", Error,
                Error::NONE)
DELUGE_STUB_RET(stub_sampleBrowserPadAction, "_ZN13SampleBrowser9padActionEiii", ActionResult, ActionResult::DEALT_WITH)
DELUGE_STUB_FN(stub_instrumentClipAssignDrums,
               "_ZN14InstrumentClip21assignDrumsToNoteRowsEP29ModelStackWithTimelineCounterbi")
DELUGE_STUB_RET(stub_waveformRendererRenderFullScreen,
                "_ZN16WaveformRenderer16renderFullScreenEP6SampleyyPA18_3RGBP18WaveformRenderDataP14SampleRecorderSt8"
                "optionalIS2_Ebi",
                bool, false)
DELUGE_STUB_FN(stub_instrumentClipViewRecalculateColours, "_ZN18InstrumentClipView18recalculateColoursEv")
DELUGE_STUB_FN(stub_modControllableAudioDtor, "_ZN20ModControllableAudioD2Ev")
DELUGE_STUB_FN(stub_sampleHolderForVoiceClaimClusterReasons, "_ZN20SampleHolderForVoice19claimClusterReasonsEbi")
DELUGE_STUB_FN(stub_paramManagerForTimelineCtor, "_ZN23ParamManagerForTimelineC1Ev")
DELUGE_STUB_FN(stub_uiModButtonAction, "_ZN2UI15modButtonActionEhb")
DELUGE_STUB_FN(stub_uiModEncoderAction, "_ZN2UI16modEncoderActionEii")
DELUGE_STUB_FN(stub_uiModEncoderButtonAction, "_ZN2UI22modEncoderButtonActionEhb")
DELUGE_STUB_FN(stub_uiDisplayOrLanguageChanged, "_ZN2UI24displayOrLanguageChangedEv")
DELUGE_STUB_FN(stub_uiClose, "_ZN2UI5closeEv")
DELUGE_STUB_FN(stub_uiCtor, "_ZN2UIC2Ev")
DELUGE_STUB_RET(stub_kitGetDrumFromIndex, "_ZN3Kit16getDrumFromIndexEi", Drum*, reinterpret_cast<Drum*>(g_drum.get()))
DELUGE_STUB_FN(stub_kitResetDrumTempValues, "_ZN3Kit19resetDrumTempValuesEv")
DELUGE_STUB_FN(stub_kitAddDrum, "_ZN3Kit7addDrumEP4Drum")
DELUGE_STUB_FN(stub_drumCtor, "_ZN4DrumC2E8DrumType")
DELUGE_STUB_FN(stub_songBackUpParamManager,
               "_ZN4Song18backUpParamManagerEP20ModControllableAudioP4ClipP23ParamManagerForTimelineb")
DELUGE_STUB_FN(stub_soundInitParams, "_ZN5Sound10initParamsEP12ParamManager")
DELUGE_STUB_FN(stub_soundSetupAsSample, "_ZN5Sound13setupAsSampleEP23ParamManagerForTimeline")
DELUGE_STUB_FN(stub_soundCtor, "_ZN5SoundC2Ev")
DELUGE_STUB_FN(
    stub_canvasDrawString,
    "_ZN6deluge3hid7display11oled_canvas6Canvas10drawStringESt17basic_string_viewIcSt11char_traitsIcEEiiiiiib")
DELUGE_STUB_FN(stub_canvasDrawRectangle, "_ZN6deluge3hid7display11oled_canvas6Canvas13drawRectangleEiiii")
DELUGE_STUB_FN(stub_canvasClearAreaExact, "_ZN6deluge3hid7display11oled_canvas6Canvas14clearAreaExactEiiii")
DELUGE_STUB_FN(stub_canvasDrawStringCentred, "_ZN6deluge3hid7display11oled_canvas6Canvas17drawStringCentredEPKciiii")
DELUGE_STUB_FN(stub_canvasDrawHorizontalLine, "_ZN6deluge3hid7display11oled_canvas6Canvas18drawHorizontalLineEiii")
DELUGE_STUB_RET(stub_memoryAllocFast, "_ZN6deluge6memory10alloc_fastEjj", void*, nullptr)
DELUGE_STUB_RET(stub_stringFromInt, "_ZN6deluge6string7fromIntB5cxx11Eij", std::string, std::string{})
DELUGE_STUB_RET(stub_ownerRunOrInline, "_ZN6deluge7storage5Owner13run_or_inlineEPFvPvES2_", bool, true)
DELUGE_STUB_FN(stub_sourceSetOscType, "_ZN6Source10setOscTypeE7OscType")
DELUGE_STUB_FN(stub_sourceDtor, "_ZN6SourceD1Ev")
DELUGE_STUB_RET(stub_buttonsIsShiftButtonPressed, "_ZN7Buttons20isShiftButtonPressedEv", bool, false)
DELUGE_STUB_RET(stub_sampleControlsIsCurrentlyReversed, "_ZNK14SampleControls19isCurrentlyReversedEv", bool, false)
DELUGE_STUB_FN(stub_delugeDealloc, "delugeDealloc")
DELUGE_STUB_FN(stub_intToString, "intToString")
DELUGE_STUB_RET(stub_snprintf, "snprintf_", int, 0)

// Globals. Only `waveformBasicNavigator` is read for its contents (its `sample`); the rest are
// merely addressed.
DELUGE_STUB_DATA(stub_currentUIMode, "currentUIMode", sizeof(uint32_t))
DELUGE_STUB_DATA(stub_waveformBasicNavigator, "waveformBasicNavigator", sizeof(WaveformBasicNavigator))
DELUGE_STUB_DATA(stub_actionLogger, "actionLogger", 1024)
DELUGE_STUB_DATA(stub_matrixDriver, "matrixDriver", 1024)
DELUGE_STUB_DATA(stub_sampleBrowser, "sampleBrowser", 8192)
DELUGE_STUB_DATA(stub_soundEditor, "soundEditor", 8192)
DELUGE_STUB_DATA(stub_instrumentClipView, "instrumentClipView", 8192)
DELUGE_STUB_DATA(stub_waveformRenderer, "waveformRenderer", 1024)
DELUGE_STUB_DATA(stub_audioEngineSounds, "_ZN11AudioEngine6soundsE", 64)
DELUGE_STUB_DATA(stub_padLedsImage, "_ZN7PadLEDs5imageE", 8 * 18 * 4)
DELUGE_STUB_DATA(stub_flashStorageDefaultSliceMode, "_ZN12FlashStorage16defaultSliceModeE", 4)

// Vtables and the UI typeinfo. Reached only by the slice-commit path (`new SoundDrum`), which the
// audition specs never enter, and by `Slicer`'s own RTTI, which nothing queries.
DELUGE_STUB_DATA(stub_vtableSoundDrum, "_ZTV9SoundDrum", 4096)
DELUGE_STUB_DATA(stub_vttSoundDrum, "_ZTT9SoundDrum", 512)
DELUGE_STUB_DATA(stub_vtableArpeggiatorBase, "_ZTV15ArpeggiatorBase", 512)
DELUGE_STUB_DATA(stub_vtableArpeggiatorForDrum, "_ZTV18ArpeggiatorForDrum", 512)
DELUGE_STUB_DATA(stub_vtableParamManagerForTimeline, "_ZTV23ParamManagerForTimeline", 512)
DELUGE_STUB_DATA(stub_vtableUI, "_ZTV2UI", 1024)
DELUGE_STUB_DATA(stub_typeinfoUI, "_ZTI2UI", 64)

// `ParamCollection`'s own out-of-line members, so `FakeParamCollection` above has a base to build on
// without linking the real collection hierarchy.
ParamCollection::ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary) : objectSize(newObjectSize) {
}
ParamCollection::~ParamCollection() = default;
void ParamCollection::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {
}
void ParamCollection::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                   bool automationChanged, bool automatedBefore, bool automatedNow) {
}
bool ParamCollection::mayParamInterpolate(int32_t paramId) {
	return false;
}
int32_t ParamCollection::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	return 0;
}
int32_t ParamCollection::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	return 0;
}
void ParamCollection::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {
}
