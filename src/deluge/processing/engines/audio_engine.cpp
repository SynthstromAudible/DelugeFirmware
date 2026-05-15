/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "processing/engines/audio_engine.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "dsp/reverb/reverb.hpp"
#include "dsp/timestretch/time_stretcher.h"
#include "extern.h"
#include "gui/context_menu/sample_browser/kit.h"
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/slicer.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "modulation/automation/auto_param.h"
#include "modulation/envelope.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/audio_output.h"
#include "processing/engines/cv_engine.h"
#include "processing/live/live_input_buffer.h"
#include "processing/metronome/metronome.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "processing/stem_export/stem_export.h"
#include "scheduler_api.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table.h"
#include "timers_interrupts/timers_interrupts.h"
#include "util/functions.h"
#include "util/misc.h"
#include <algorithm>
#include <bits/ranges_algo.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <execution>
#include <new>
#include <numeric>
#include <ranges>

namespace params = deluge::modulation::params;

#if AUTOMATED_TESTER_ENABLED
#include "testing/automated_tester.h"
#endif

extern "C" {
#include "RZA1/mtu/mtu.h"
#include "drivers/ssi/ssi.h"

#include "RZA1/intc/devdrv_intc.h"
// void *__dso_handle = NULL; // This fixes an insane error.
}

#define DISABLE_INTERRUPTS_COUNT (sizeof(disableInterrupts) / sizeof(uint32_t))
uint32_t disableInterrupts[] = {INTC_ID_SPRI0,
                                INTC_ID_DMAINT0 + PIC_TX_DMA_CHANNEL,
                                IRQ_INTERRUPT_0 + 6,
                                INTC_ID_USBI0,
                                INTC_ID_SDHI1_0,
                                INTC_ID_SDHI1_3,
                                INTC_ID_DMAINT0 + OLED_SPI_DMA_CHANNEL,
                                INTC_ID_DMAINT0 + MIDI_TX_DMA_CHANNEL,
                                INTC_ID_SDHI1_1};

using namespace deluge;

extern bool inSpamMode;
extern bool anythingProbablyPressed;
extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

// #define REPORT_CPU_USAGE 1

#define NUM_SAMPLES_FOR_CPU_USAGE_REPORT 32

#define AUDIO_OUTPUT_GAIN_DOUBLINGS 8

#ifdef REPORT_CPU_USAGE
#define REPORT_AVERAGE_NUM 10
int32_t usageTimes[REPORT_AVERAGE_NUM];
#endif

extern "C" uint32_t getAudioSampleTimerMS() {
	return AudioEngine::audioSampleTimer / 44.1;
}
// Pitch, Y, Pressure
int16_t zeroMPEValues[kNumExpressionDimensions] = {0, 0, 0};

namespace AudioEngine {

// used for culling. This can be high now since it's decoupled from the time between renders, and
// will spill into a second render and output if needed so as long as we always render 128 samples in under 128/44100
// seconds it will work maybe this should be user configurable? Values from 60-100 all seem justifiable. 100
// occasionally crackles but means more voices. 60 keeps the modulation updating at about 800hz and LFO2 goes up to
// 100ish
constexpr int32_t numSamplesLimit = 80;
// used for decisions in rendering engine
constexpr int32_t direnessThreshold = numSamplesLimit - 30;

// 7 can overwhelm SD bandwidth if we schedule the loads badly. It could be improved by starting future loads
// earlier for now we provide an outlet in culling a single voice if we're under MIN_VOICES and still getting close
// to the limit
constexpr int MIN_VOICES = 7;

dsp::Reverb reverb{};
PLACE_INTERNAL_FRUNK SideChain reverbSidechain{};
int32_t reverbSidechainVolume;
int32_t reverbSidechainShape;
int32_t reverbPan = 0;

int32_t reverbSidechainVolumeInEffect; // Active right now - possibly overridden by the sound with the most reverb
int32_t reverbSidechainShapeInEffect;

bool mustUpdateReverbParamsBeforeNextRender = false;

int32_t sideChainHitPending = false;

uint32_t timeLastSideChainHit = 2147483648;
int32_t sizeLastSideChainHit;

Metronome metronome{};
deluge::dsp::StereoSample<float> approxRMSLevel{0};
deluge::dsp::AbsValueFollower envelopeFollower{};
int32_t timeLastPopup{0};

SoundDrum* sampleForPreview;
ParamManagerForTimeline* paramManagerForSamplePreview;

// Wavetable browse-preview ping-pong state. Active while sampleForPreview is in OscType::WAVETABLE.
constexpr uint32_t kPreviewWavetableHalfPeriodSamples = kSampleRate * 2; // 2 s per direction
constexpr float kPreviewWavetableDwellFraction = 0.4f;                   // hold each integer slice for 40% of segment
bool previewWavetableActive = false;
uint32_t previewWavetablePhaseSamples = 0;
// Manual scrub: when active, renderSamplePreview() uses previewWavetableManualPos as the slice index instead of
// running the ping-pong. Set by SampleBrowser's vertical encoder; cleared on stopAnyPreviewing (file change).
bool previewWavetableManualActive = false;
float previewWavetableManualPos = 0.0f;
// Read by the LED preview renderer in SampleBrowser. Written by renderSamplePreview() on the audio thread; the
// reader uses these values opportunistically — slight tearing is harmless because the LED render only needs a
// "near-current" position to look animated.
WaveTable* previewWavetableHandle = nullptr;
float previewWavetableSlicePos = 0.0f;

PLACE_SDRAM_BSS char paramManagerForSamplePreviewMemory[sizeof(ParamManagerForTimeline)];
PLACE_SDRAM_BSS char sampleForPreviewMemory[sizeof(SoundDrum)];

SampleRecorder* firstRecorder = nullptr;

// Let's keep these grouped - the stuff we're gonna access regularly during audio rendering
int32_t cpuDireness = 0;
uint32_t timeDirenessChanged;
uint32_t timeThereWasLastSomeReverb = 0x8FFFFFFF;
int32_t numSamplesLastTime = 0;
uint32_t nextVoiceState = 1;
bool renderInStereo = true;
bool bypassCulling = false;
bool audioRoutineLocked = false;
uint32_t audioSampleTimer = 0;
uint32_t i2sTXBufferPos;
uint32_t i2sRXBufferPos;
int voicesStartedThisRender = 0;
bool headphonesPluggedIn;
bool micPluggedIn;
bool lineInPluggedIn;
InputMonitoringMode inputMonitoringMode = InputMonitoringMode::SMART;
bool routineBeenCalled;
uint8_t numHopsEndedThisRoutineCall;

LiveInputBuffer* liveInputBuffers[3];

// For debugging
uint16_t lastRoutineTime;

alignas(CACHE_LINE_SIZE) std::array<deluge::dsp::StereoSample<q31_t>, SSI_TX_BUFFER_NUM_SAMPLES> renderingMemory;
alignas(CACHE_LINE_SIZE) std::array<int32_t, 2 * SSI_TX_BUFFER_NUM_SAMPLES> reverbMemory;

deluge::dsp::StereoSample<q31_t>* renderingBufferOutputPos = renderingMemory.begin();
deluge::dsp::StereoSample<q31_t>* renderingBufferOutputEnd = renderingMemory.begin();

int32_t masterVolumeAdjustmentL;
int32_t masterVolumeAdjustmentR;

bool doMonitoring;
MonitoringAction monitoringAction;

uint32_t saddr;

deluge::fast_vector<Sound*> sounds;
TaskID routine_task_id = -1;

// You must set up dynamic memory allocation before calling this, because of its call to setupWithPatching()
void init() {
	paramManagerForSamplePreview = new ((void*)paramManagerForSamplePreviewMemory) ParamManagerForTimeline();
	paramManagerForSamplePreview->setupWithPatching(); // Shouldn't be an error at init time...
	Sound::initParams(paramManagerForSamplePreview);
	sampleForPreview = new ((void*)sampleForPreviewMemory) SoundDrum();
	sampleForPreview->setupAsSample(paramManagerForSamplePreview);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addTimelineCounter(nullptr)
	        ->addOtherTwoThingsButNoNoteRow(sampleForPreview, paramManagerForSamplePreview)
	        ->addParamCollectionSummary(paramManagerForSamplePreview->getPatchCableSetSummary());

	((PatchCableSet*)modelStackWithParamCollection->paramCollection)->setupPatching(modelStackWithParamCollection);
	sampleForPreview->patcher.performInitialPatching(*sampleForPreview, *paramManagerForSamplePreview);

	sampleForPreview->sideChainSendLevel = 2147483647;

	i2sTXBufferPos = (uint32_t)getTxBufferStart();

	i2sRXBufferPos = (uint32_t)getRxBufferStart()
	                 + ((SSI_RX_BUFFER_NUM_SAMPLES - SSI_TX_BUFFER_NUM_SAMPLES - 16)
	                    << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE)); // Subtracting 5 or more seems fine

	VoicePool::get().repopulate();
	VoiceSamplePool::get().repopulate();
	TimeStretcherPool::get().repopulate();
}

void killAllVoices(bool deletingSong) {
	for (Sound* sound : sounds) {
		sound->killAllVoices();
	}

	// Because we unfortunately don't have a master list of VoiceSamples or actively sounding AudioClips,
	// we have to unassign all of those by going through all AudioOutputs.
	// But if there's no currentSong, that's fine - it's already been deleted, and this has already been called for it
	// before then.
	if (currentSong != nullptr) {
		for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
			if (output->type == OutputType::AUDIO) {
				static_cast<AudioOutput*>(output)->cutAllSound();
			}
		}
	}
}

void songSwapAboutToHappen() {
	// Otherwise, a timer might get called and try to access Clips that we may have deleted below
	uiTimerManager.unsetTimer(TimerName::PLAY_ENABLE_FLASH);
	logAction("a1");
	currentSong->deleteSoundsWhichWontSound();
	logAction("a2");
	playbackHandler.stopAnyRecording();
}

enum CullType { HARD, FORCE, SOFT_ALWAYS, SOFT };

#define AUDIO_LOG_SIZE 64
bool definitelyLog = false;
#if DO_AUDIO_LOG

uint16_t audioLogTimes[AUDIO_LOG_SIZE];
char audioLogStrings[AUDIO_LOG_SIZE][64];
int32_t numAudioLogItems = 0;
#endif

/// Force a voice to stop within this render window. Will click slightly, especially if multiple are stopped in the
/// same render
void killOneVoice(size_t num_samples) {
	// Only include audio if doing a hard cull and not saving the voice
	auto lowest_priority_voices = sounds //<
	                              | std::views::filter(&Sound::hasActiveVoices)
	                              | std::views::transform(&Sound::getLowestPriorityVoice);
	auto it = std::ranges::max_element(lowest_priority_voices);
	if (it == lowest_priority_voices.end()) {
		return;
	}

	const Sound::ActiveVoice& voice = *it;

	bool still_rendering = voice->doImmediateRelease();
	if (!still_rendering) {
		voice->sound.freeActiveVoice(voice);
	}

	D_PRINTLN("force-culled 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", num_samples,
	          getNumVoices(), getNumAudio());
}

/// Force a voice to release very quickly - will be almost instant but not click
void terminateOneVoice(size_t numSamples) {
	auto all_voices = sounds | std::views::transform(&Sound::voices) | std::views::join;

	if (all_voices.empty()) {
		return;
	}

	const Sound::ActiveVoice* best = &all_voices.front();
	for (const auto& voice : all_voices | std::views::drop(1)) {
		// if we're not skipping releasing voices, or if we are and this one isn't in fast release
		if (voice->envelopes[0].state >= EnvelopeStage::FAST_RELEASE
		    && voice->envelopes[0].fastReleaseIncrement >= SOFT_CULL_INCREMENT) {
			continue;
		}
		best = (*best)->getPriorityRating() < voice->getPriorityRating() ? &voice : best;
	}

	const Sound::ActiveVoice& voice = *best;
	bool still_rendering = voice->doFastRelease(SOFT_CULL_INCREMENT);
	if (!still_rendering) {
		voice->sound.freeActiveVoice(voice);
	}

	D_PRINTLN("force-culled 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", numSamples,
	          getNumVoices(), getNumAudio());
}

/// Force a voice to release, or speed up its release if the oldest voice is already releasing
void forceReleaseOneVoice(size_t num_samples) {
	auto all_voices = sounds | std::views::transform(&Sound::voices) | std::views::join;
	if (all_voices.empty()) {
		return;
	}

	const Sound::ActiveVoice* best = &all_voices.front();
	for (const auto& voice : all_voices | std::views::drop(1)) {
		// if the voice is already releasing faste than this we'd rather release another voice
		if (voice->envelopes[0].state >= EnvelopeStage::FAST_RELEASE
		    && voice->envelopes[0].fastReleaseIncrement >= 4096) {
			continue;
		}
		best = (*best)->getPriorityRating() < voice->getPriorityRating() ? &voice : best;
	}

	const Sound::ActiveVoice& voice = *best;

	auto stage = voice->envelopes[0].state;
	if (stage < EnvelopeStage::FAST_RELEASE) {
		D_PRINTLN("soft-culled 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", num_samples,
		          getNumVoices(), getNumAudio());
	}

	bool still_rendering = voice->speedUpRelease();
	if (!still_rendering) {
		voice->sound.freeActiveVoice(voice);
	}
}

int32_t getNumAudio() {
	return currentSong != nullptr ? currentSong->countAudioClips() : 0;
}

int32_t getNumVoices() {
	return std::transform_reduce(sounds.cbegin(), sounds.cend(), 0, std::plus{},
	                             [](auto sound) { return sound->voices().size(); });
}

void routineWithClusterLoading(bool mayProcessUserActionsBetween) {
	logAction("AudioDriver::routineWithClusterLoading");

	routineBeenCalled = false;

	audioFileManager.loadAnyEnqueuedClusters(128, mayProcessUserActionsBetween);

	if (!routineBeenCalled) {
		// bypassCulling = true; // yolo? Sean: not sure if this is necessary

		logAction("call runRoutine() from routineWithClusterLoading()");
		runRoutine();
	}
}

void runRoutine() {
	// check if we've setup the audio routine task
	if (routine_task_id != -1) [[likely]] {
		// run AudioEngine::routine() task so that scheduler is aware
		runTask(AudioEngine::routine_task_id);
	}
	// otherwise call audio routine directly (necessary otherwise Deluge freezes on boot)
	else {
		ignoreForStats();
		routine();
	}
}

#define TICK_TYPE_SWUNG 1
#define TICK_TYPE_TIMER 2

extern uint16_t g_usb_usbmode;

uint8_t numRoutines = 0;

// not in header (private to audio engine)
/// determines how many voices to cull based on num audio samples, current voices and numSamplesLimit
inline void cullVoices(size_t numSamples, int32_t numAudio, int32_t numVoice) {
	bool culled = false;
	int32_t numToCull = 0;
	if (numAudio + numVoice > MIN_VOICES) {

		int32_t numSamplesOverLimit = numSamples - numSamplesLimit;
		// If it's real dire, do a proper immediate cull
		if (numSamplesOverLimit >= 20) {

			numToCull = (numSamplesOverLimit >> 3);

			// leave at least 7 - below this point culling won't save us
			// if they can't load their sample in time they'll stop the same way anyway
			numToCull = std::min(numToCull, numAudio + numVoice - MIN_VOICES);
			for (int32_t i = numToCull / 2; i < numToCull; i++) {
				// cull with fast release
				terminateOneVoice(numSamples);
			}
			for (int32_t i = 1; i < numToCull / 2; i++) {
				// cull with immediate release
				killOneVoice(numSamples);
			}
			forceReleaseOneVoice(numSamples);

#if ALPHA_OR_BETA_VERSION

			definitelyLog = true;
			logAction("hard cull");

#endif
			culled = true;
		}

		// Or if it's just a little bit dire, do a soft cull with fade-out, but only cull for sure if numSamples
		// is increasing
		else if (numSamplesOverLimit >= 0) {

			// If not in first routine call this is inaccurate, so just release another voice since things are
			// probably bad
			forceReleaseOneVoice(numSamples);
			logAction("soft cull");
			if (numRoutines > 0) {
				culled = true;
				D_PRINTLN("culling in second routine");
			}
		}
	}
	else {
		int32_t numSamplesOverLimit = numSamples - numSamplesLimit;
		// Cull anyway if things are bad
		if (numSamplesOverLimit >= 40) {
			D_PRINTLN("under min voices but culling anyway");
			terminateOneVoice(numSamples);
			culled = true;
		}
	}
	// blink LED to alert the user
	if (culled && FlashStorage::highCPUUsageIndicator) {
		if (indicator_leds::getLedBlinkerIndex(IndicatorLED::PLAY) == 255) {
			indicator_leds::indicateAlertOnLed(IndicatorLED::PLAY);
		}
	}
}

// not in header (private to audio engine)
/// set the direness level and cull any voices
inline void setDireness(size_t numSamples) { // Consider direness and culling - before increasing the number of samples
	// number of samples it took to do the last render
	auto dspTime = (int32_t)(getAverageRunTimeForTask(routine_task_id) * 44100.);
	size_t nonDSP = numSamples - dspTime;
	// we don't care about the number that were rendered in the last go, only the ones taken by the first routine call
	numSamples = std::max<int32_t>(dspTime - (int32_t)(numRoutines * numSamples), 0);

	// don't smooth this - used for other decisions as well
	if (numSamples >= direnessThreshold) {

		int32_t newDireness = std::min<int32_t>(numSamples - (direnessThreshold - 1), 14);

		if (newDireness >= cpuDireness) {
			cpuDireness = newDireness;
			timeDirenessChanged = audioSampleTimer;
		}
		auto numAudio = currentSong ? currentSong->countAudioClips() : 0;
		auto numVoice = getNumVoices();
		if (!bypassCulling) {
			cullVoices(numSamples, numAudio, numVoice);
		}
		else {

			int32_t num_samples_over_limit = (int32_t)numSamples - numSamplesLimit;
			if (num_samples_over_limit >= 0) {
#if DO_AUDIO_LOG
				definitelyLog = true;
#endif
				D_PRINTLN("numSamples %d, numVoice %d, numAudio %d", numSamples, numVoice, numAudio);
				logAction("skipped cull");
			}
		}
	}
	// otherwise lower it (with some hysteresis to avoid jittering
	else if (numSamples < direnessThreshold - 3) {

		if ((int32_t)(audioSampleTimer - timeDirenessChanged) >= (kSampleRate >> 3)) { // Only if it's been long enough
			timeDirenessChanged = audioSampleTimer;
			cpuDireness--;
			if (cpuDireness < 0) {
				cpuDireness = 0;
			}
			else {
				D_PRINTLN("direness:  %d", cpuDireness);
			}
		}
	}
}

void scheduleMidiGateOutISR(uint32_t saddrPosAtStart, int32_t unadjustedNumSamplesBeforeLappingPlayHead,
                            int32_t timeWithinWindowAtWhichMIDIOrGateOccurs);
void setMonitoringMode();
void renderSongFX(size_t numSamples);
void renderSamplePreview(size_t numSamples);
void renderReverb(size_t numSamples);
void tickSongFinalizeWindows(size_t& numSamples, int32_t& timeWithinWindowAtWhichMIDIOrGateOccurs);
void flushMIDIGateBuffers();
void renderAudio(size_t numSamples);
void renderAudioForStemExport(size_t numSamples);
void dumpAudioLog();
bool calledFromScheduler = false;

/// inner loop of audio rendering, deliberately not in header
[[gnu::hot]] void routine_() {
	static double last_call_time = getSystemTime();
	double current_time = getSystemTime();
	if (current_time - last_call_time > 0.003) {
		// If the audio routine is called at less than a 3ms interval, something is wrong
		D_PRINTLN("Audio routine latency high: %.3fms", (current_time - last_call_time) * 1000.);
	}
	last_call_time = current_time;
#ifndef USE_TASK_MANAGER
	playbackHandler.routine();
#endif
	// At this point, there may be MIDI, including clocks, waiting to be sent.

	GeneralMemoryAllocator::get().checkStack("AudioDriver::routine");

	saddr = (uint32_t)(getTxBufferCurrentPlace());
	uint32_t saddrPosAtStart = saddr >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE);
	size_t numSamples = ((uint32_t)(saddr - i2sTXBufferPos) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
	                    & (SSI_TX_BUFFER_NUM_SAMPLES - 1);

	if (numSamples <= (10 * numRoutines)) {
		if (!numRoutines && calledFromScheduler) {
			ignoreForStats();
		}
		return;
	}
#if AUTOMATED_TESTER_ENABLED
	AutomatedTester::possiblyDoSomething();
#endif
	flushMIDIGateBuffers();

	setDireness(numSamples);

	// Double the number of samples we're going to do - within some constraints
	int32_t sampleThreshold = 6; // If too low, it'll lead to bigger audio windows and stuff
	constexpr size_t maxAdjustedNumSamples = SSI_TX_BUFFER_NUM_SAMPLES;

	int32_t unadjustedNumSamplesBeforeLappingPlayHead = numSamples;

	if (numSamples < maxAdjustedNumSamples) {
		int32_t samplesOverThreshold = numSamples - sampleThreshold;
		if (samplesOverThreshold > 0) {
			samplesOverThreshold = samplesOverThreshold << 1;
			numSamples = sampleThreshold + samplesOverThreshold;
			numSamples = std::min(numSamples, maxAdjustedNumSamples);
		}
	}

	// Want to round to be doing a multiple of 4 samples, so the NEON functions can be utilized most efficiently.
	// Note - this can take numSamples up as high as SSI_TX_BUFFER_NUM_SAMPLES (currently 128).
	if (numSamples % 4 != 0) {
		numSamples = (numSamples + 3) & ~3;
	}

	int32_t timeWithinWindowAtWhichMIDIOrGateOccurs;
	tickSongFinalizeWindows(numSamples, timeWithinWindowAtWhichMIDIOrGateOccurs);

	numSamplesLastTime = numSamples;
	renderAudio(numSamples);

	scheduleMidiGateOutISR(saddrPosAtStart, unadjustedNumSamplesBeforeLappingPlayHead,
	                       timeWithinWindowAtWhichMIDIOrGateOccurs);

#if DO_AUDIO_LOG
	dumpAudioLog();
#endif

	sideChainHitPending = 0;
	audioSampleTimer += numSamples;

	bypassCulling = false;
}
void renderAudio(size_t numSamples) {
	std::span renderingBuffer{renderingMemory.data(), numSamples};
	std::span reverbBuffer{reverbMemory.data(), numSamples};

	memset(&renderingMemory, 0, renderingBuffer.size_bytes());
	memset(&reverbMemory, 0, reverbBuffer.size_bytes());

	if (sideChainHitPending != 0) {
		timeLastSideChainHit = audioSampleTimer;
		sizeLastSideChainHit = sideChainHitPending;
	}

	numHopsEndedThisRoutineCall = 0;

	// Render audio for song
	if (currentSong != nullptr) {
		currentSong->renderAudio(renderingBuffer, reverbBuffer.data(), sideChainHitPending);
	}

	renderReverb(numSamples);

	renderSamplePreview(numSamples);

	renderSongFX(numSamples);

	metronome.render(renderingBuffer);

	approxRMSLevel = envelopeFollower.calcApproxRMS(renderingBuffer);

	setMonitoringMode();

	renderingBufferOutputPos = renderingMemory.begin();
	renderingBufferOutputEnd = renderingMemory.begin() + numSamples;
}

void renderAudioForStemExport(size_t numSamples) {
	std::span renderingBuffer{renderingMemory.data(), numSamples};
	std::span reverbBuffer{reverbMemory.data(), numSamples};

	memset(&renderingMemory, 0, renderingBuffer.size_bytes());
	memset(&reverbMemory, 0, reverbBuffer.size_bytes());

	if (sideChainHitPending) {
		timeLastSideChainHit = audioSampleTimer;
		sizeLastSideChainHit = sideChainHitPending;
	}

	numHopsEndedThisRoutineCall = 0;

	// Render audio for song
	if (currentSong != nullptr) {
		currentSong->renderAudio(renderingBuffer, reverbBuffer.data(), sideChainHitPending);
	}

	if (stemExport.includeSongFX) {
		renderReverb(numSamples);
		renderSongFX(numSamples);
	}

	// If we're recording final output for offline stem export with song FX
	// Check if we have a recorder
	SampleRecorder* recorder = audioRecorder.recorder;
	if (recorder && recorder->mode == AudioInputChannel::OFFLINE_OUTPUT) {
		// continue feeding audio if we're not finished recording
		if (recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
			recorder->feedAudio(renderingBuffer, true);
		}
	}

	approxRMSLevel = envelopeFollower.calcApproxRMS(renderingBuffer);

	doMonitoring = false;
	monitoringAction = MonitoringAction::NONE;

	renderingBufferOutputPos = renderingMemory.begin();
	renderingBufferOutputEnd = renderingMemory.begin() + numSamples;
}

void flushMIDIGateBuffers() { // Flush everything out of the MIDI buffer now. At this stage, it would only really have
	                          // live user-triggered
	// output and MIDI THRU in it. We want any messages like "start" to go out before we send any clocks below, and
	// also want to give them a head-start being sent and out of the way so the clock messages can be sent on-time
	bool anythingInMidiOutputBufferNow = midiEngine.anythingInOutputBuffer();
	bool anythingInGateOutputBufferNow = cvEngine.isAnythingButRunPending();
	if (anythingInMidiOutputBufferNow || anythingInGateOutputBufferNow) {

		// We're only allowed to do this if the timer ISR isn't pending (i.e. we haven't enabled to timer to trigger
		// it)
		// - otherwise this will all get called soon anyway. I thiiiink this is 100% immune to any synchronization
		// problems?
		if (!isTimerEnabled(TIMER_MIDI_GATE_OUTPUT)) {
			if (anythingInGateOutputBufferNow) {
				cvEngine.updateGateOutputs();
			}
			if (anythingInMidiOutputBufferNow) {
				midiEngine.flushMIDI();
			}
		}
	}
}
void tickSongFinalizeWindows(size_t& numSamples, int32_t& timeWithinWindowAtWhichMIDIOrGateOccurs) {
	timeWithinWindowAtWhichMIDIOrGateOccurs = -1; // -1 means none

	// If a timer-tick is due during or directly after this window of audio samples...
	if (playbackHandler.isEitherClockActive()) {

startAgain:
		int32_t nextTickType = 0;
		uint32_t timeNextTick = audioSampleTimer + 9999;

		if (playbackHandler.isInternalClockActive()) {
			timeNextTick = playbackHandler.timeNextTimerTickBig >> 32;
			nextTickType = TICK_TYPE_TIMER;
		}

		// Or if a swung tick is scheduled sooner...
		if (playbackHandler.swungTickScheduled
		    && (int32_t)(playbackHandler.scheduledSwungTickTime - timeNextTick) < 0) {
			timeNextTick = playbackHandler.scheduledSwungTickTime;
			nextTickType = TICK_TYPE_SWUNG;
		}

		int32_t timeTilNextTick = timeNextTick - audioSampleTimer;

		// If tick occurs right now at the start of this window - which is fairly likely, cos we would have cut the
		// previous window short if it had such a tick during it...
		if (timeTilNextTick <= 0) {
			if (nextTickType == TICK_TYPE_TIMER) {
				playbackHandler.actionTimerTick();
			}
			else if (nextTickType == TICK_TYPE_SWUNG) {
				playbackHandler.actionSwungTick();
				playbackHandler.scheduleSwungTick(); // If that swung tick action just did a song swap, this will have
				                                     // already been called, but fortunately this doesn't break anything
			}

			// Those could have outputted clock or other MIDI / gate
			if (midiEngine.anythingInOutputBuffer() || cvEngine.isAnythingButRunPending()) {
				timeWithinWindowAtWhichMIDIOrGateOccurs = 0;
			}

			goto startAgain;
		}

		// If the tick is during this window, shorten the window so we stop right at the tick
		if (timeTilNextTick < numSamples) {
			numSamples = timeTilNextTick;
		}

		// And now we know how long the window's definitely going to be, see if we want to do any trigger clock or
		// MIDI clock out ticks during it
		if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
			if (playbackHandler.triggerClockOutTickScheduled) {
				int32_t timeTilTriggerClockOutTick = playbackHandler.timeNextTriggerClockOutTick - audioSampleTimer;
				if (timeTilTriggerClockOutTick < numSamples) {
					playbackHandler.doTriggerClockOutTick();
					playbackHandler.scheduleTriggerClockOutTick(); // Schedules another one

					if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
						timeWithinWindowAtWhichMIDIOrGateOccurs = timeTilTriggerClockOutTick;
					}
				}
			}

			if (playbackHandler.midiClockOutTickScheduled) {
				int32_t timeTilMIDIClockOutTick = playbackHandler.timeNextMIDIClockOutTick - audioSampleTimer;
				if (timeTilMIDIClockOutTick < numSamples) {
					playbackHandler.doMIDIClockOutTick();
					playbackHandler.scheduleMIDIClockOutTick(); // Schedules another one

					if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
						timeWithinWindowAtWhichMIDIOrGateOccurs = timeTilMIDIClockOutTick;
					}
				}
			}
		}
	}
}

void feedReverbBackdoorForGrain(int index, q31_t value) {
	reverbMemory[index] += value;
}
void renderReverb(size_t numSamples) {
	std::span renderingBuffer{renderingMemory.data(), numSamples};
	std::span reverbBuffer{reverbMemory.data(), numSamples};

	if (currentSong && mustUpdateReverbParamsBeforeNextRender) {
		updateReverbParams();
		mustUpdateReverbParamsBeforeNextRender = false;
	}

	// Render the reverb sidechain
	int32_t sidechainOutput = 0;
	if (reverbSidechainVolumeInEffect != 0) {
		if (sideChainHitPending != 0) {
			reverbSidechain.registerHit(sideChainHitPending);
		}

		sidechainOutput = reverbSidechain.render(numSamples, reverbSidechainShapeInEffect);
	}

	int32_t reverbAmplitudeL;
	int32_t reverbAmplitudeR;

	// Stop reverb after 12 seconds of inactivity
	bool reverbOn = ((uint32_t)(audioSampleTimer - timeThereWasLastSomeReverb) < kSampleRate * 12);
	// around the mutable noise floor, ~70dB from peak
	reverbOn |= (std::max(approxRMSLevel.l, approxRMSLevel.r) > 9);

	if (reverbOn) {
		// Patch that to reverb volume
		int32_t positivePatchedValue =
		    multiply_32x32_rshift32(sidechainOutput, reverbSidechainVolumeInEffect) + 0x20000000;
		int32_t reverbOutputVolume = (positivePatchedValue >> 15) * (positivePatchedValue >> 14);

		// Reverb panning
		bool thisDoPanning = (renderInStereo && shouldDoPanning(reverbPan, &reverbAmplitudeL, &reverbAmplitudeR));

		if (thisDoPanning) {
			reverbAmplitudeL = multiply_32x32_rshift32(reverbAmplitudeL, reverbOutputVolume) << 2;
			reverbAmplitudeR = multiply_32x32_rshift32(reverbAmplitudeR, reverbOutputVolume) << 2;
		}
		else {
			reverbAmplitudeL = reverbAmplitudeR = reverbOutputVolume;
		}

		// Mix reverb into main render
		reverb.setPanLevels(reverbAmplitudeL, reverbAmplitudeR);
		reverb.process(reverbBuffer, renderingBuffer);
		logAction("Reverb complete");
	}
}
// Smooth ping-pong with per-slice dwell. tri ∈ [0, 1] sweeps linearly up and down; this pulls it toward integer
// slice positions for the first kPreviewWavetableDwellFraction of each unit segment, then smoothsteps from one
// slice to the next over the remainder. Returns a position in [0, numCycles - 1].
static float shapedPingPongPosition(uint32_t phase, int32_t numCycles) {
	if (numCycles <= 1) {
		return 0.0f;
	}
	uint32_t fullPeriod = 2u * kPreviewWavetableHalfPeriodSamples;
	uint32_t inPeriod = phase % fullPeriod;
	float tri;
	if (inPeriod < kPreviewWavetableHalfPeriodSamples) {
		tri = (float)inPeriod / (float)kPreviewWavetableHalfPeriodSamples;
	}
	else {
		tri = 2.0f - (float)inPeriod / (float)kPreviewWavetableHalfPeriodSamples;
	}
	float continuousPos = tri * (float)(numCycles - 1);
	int32_t segInt = (int32_t)continuousPos; // continuousPos ≥ 0, so truncation == floor
	float seg = (float)segInt;
	float frac = continuousPos - seg;
	float shaped;
	if (frac < kPreviewWavetableDwellFraction) {
		shaped = 0.0f;
	}
	else {
		float t = (frac - kPreviewWavetableDwellFraction) / (1.0f - kPreviewWavetableDwellFraction);
		shaped = t * t * (3.0f - 2.0f * t); // smoothstep
	}
	float pos = seg + shaped;
	if (pos > (float)(numCycles - 1)) {
		pos = (float)(numCycles - 1);
	}
	return pos;
}

void renderSamplePreview(size_t numSamples) { // Previewing sample
	std::span renderingBuffer{renderingMemory.data(), numSamples};
	std::span reverbBuffer{reverbMemory.data(), numSamples};

	if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &gui::context_menu::sample_browser::kit
	    || getCurrentUI() == &gui::context_menu::sample_browser::synth || getCurrentUI() == &slicer) {

		// Drive the wave-index parameter for ping-pong wavetable preview.
		if (previewWavetableActive && sampleForPreview->sources[0].oscType == OscType::WAVETABLE) {
			MultiWaveTableRange* range = nullptr;
			if (sampleForPreview->sources[0].ranges.getNumElements() > 0) {
				range = (MultiWaveTableRange*)sampleForPreview->sources[0].ranges.getElement(0);
			}
			WaveTable* waveTable = (range != nullptr) ? (WaveTable*)range->waveTableHolder.audioFile : nullptr;
			int32_t numCycles = (waveTable != nullptr) ? waveTable->numCycles : 0;
			if (numCycles > 1) {
				float pos;
				if (previewWavetableManualActive) {
					pos = previewWavetableManualPos;
					if (pos < 0.0f) {
						pos = 0.0f;
					}
					if (pos > (float)(numCycles - 1)) {
						pos = (float)(numCycles - 1);
					}
				}
				else {
					pos = shapedPingPongPosition(previewWavetablePhaseSamples, numCycles);
				}
				// Map pos ∈ [0, numCycles-1] to the patched-param value such that Voice's waveIndex
				// (sourceWaveIndexLastTime + 2^30) sweeps the table from cycle 0 to cycle numCycles-1.
				// Derivation: WaveTable::render expects waveIndex ∈ [0, 2^31] to span the cycle range.
				float normalised = pos / (float)(numCycles - 1); // [0, 1]
				int64_t paramValue = (int64_t)(normalised * 2147483647.0f) - 1073741824;
				// setCurrentValueBasicForSetup skips the param LPF and change-notify path; that's deliberate —
				// shapedPingPongPosition is already a continuous curve and our update granularity (one render
				// window, ~3ms) is below audible zipper threshold for this param.
				paramManagerForSamplePreview->getPatchedParamSet()
				    ->params[params::LOCAL_OSC_A_WAVE_INDEX]
				    .setCurrentValueBasicForSetup((int32_t)paramValue);
				// Force paramFinalValues to be re-derived. Without this, the per-window performPatching()
				// would skip this param since nothing in the patcher's "sources changed" mask flips.
				sampleForPreview->recalculatePatchingToParam(params::LOCAL_OSC_A_WAVE_INDEX,
				                                             paramManagerForSamplePreview);
				// Publish for the LED preview renderer.
				previewWavetableHandle = waveTable;
				previewWavetableSlicePos = pos;
			}
			previewWavetablePhaseSamples += (uint32_t)numSamples;
		}

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);

		sampleForPreview->render(modelStack, renderingBuffer, reverbBuffer.data(), sideChainHitPending);
	}
}
void renderSongFX(size_t numSamples) { // LPF and stutter for song (must happen after reverb mixed in, which is why it's
	                                   // happening all the way out here
	std::span renderingBuffer{renderingMemory.data(), numSamples};
	std::span reverbBuffer{reverbMemory.data(), numSamples};

	masterVolumeAdjustmentL = 167763968; // getParamNeutralValue(params::GLOBAL_VOLUME_POST_FX);
	masterVolumeAdjustmentR = 167763968; // getParamNeutralValue(params::GLOBAL_VOLUME_POST_FX);
	// 167763968 is 134217728 made a bit bigger so that default filter resonance doesn't reduce volume overall

	if (currentSong) {
		currentSong->globalEffectable.setupFilterSetConfig(&masterVolumeAdjustmentL, &currentSong->paramManager);
		currentSong->globalEffectable.processFilters(renderingBuffer);
		currentSong->globalEffectable.processSRRAndBitcrushing(renderingBuffer, &masterVolumeAdjustmentL,
		                                                       &currentSong->paramManager);

		masterVolumeAdjustmentR = masterVolumeAdjustmentL; // This might have changed in the above function calls

		currentSong->globalEffectable.processStutter(renderingBuffer, &currentSong->paramManager);

		// And we do panning for song here too - must be post reverb, and we had to do a volume adjustment below
		// anyway
		int32_t pan = currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_PAN) >> 1;

		if (pan != 0) {
			// Set up panning
			int32_t amplitudeL;
			int32_t amplitudeR;
			bool doPanning = (renderInStereo && shouldDoPanning(pan, &amplitudeL, &amplitudeR));

			if (doPanning) {
				masterVolumeAdjustmentL = multiply_32x32_rshift32(masterVolumeAdjustmentL, amplitudeL) << 2;
				masterVolumeAdjustmentR = multiply_32x32_rshift32(masterVolumeAdjustmentR, amplitudeR) << 2;
			}
		}
		logAction("mastercomp start");

		int32_t songVolume =
		    getFinalParameterValueVolume(
		        134217728, cableToLinearParamShortcut(
		                       currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME)))
		    >> 1;
		// there used to be a static subtraction of 2 nepers (natural log based dB), this is the multiplicative
		// equivalent
		currentSong->globalEffectable.compressor.render(renderingBuffer, masterVolumeAdjustmentL >> 1,
		                                                masterVolumeAdjustmentR >> 1, songVolume >> 3);
		masterVolumeAdjustmentL = ONE_Q31;
		masterVolumeAdjustmentR = ONE_Q31;
		logAction("mastercomp end");
	}
}
void setMonitoringMode() { // Monitoring setup
	doMonitoring = false;
	if (audioRecorder.recordingSource == AudioInputChannel::STEREO
	    || audioRecorder.recordingSource == AudioInputChannel::LEFT) {
		if (inputMonitoringMode == InputMonitoringMode::SMART) {
			doMonitoring = (lineInPluggedIn || headphonesPluggedIn);
		}
		else {
			doMonitoring = (inputMonitoringMode == InputMonitoringMode::ON);
		}
	}

	monitoringAction = MonitoringAction::NONE;
	if (doMonitoring && audioRecorder.recorder) { // Double-check
		if (lineInPluggedIn) {                    // Line input
			if (audioRecorder.recorder->inputLooksDifferential()) {
				monitoringAction = MonitoringAction::SUBTRACT_RIGHT_CHANNEL;
			}
			else if (audioRecorder.recorder->inputHasNoRightChannel()) {
				monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
			}
		}

		else if (micPluggedIn) { // External mic
			if (audioRecorder.recorder->inputHasNoRightChannel()) {
				monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
			}
		}

		else { // Internal mic
			monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
		}
	}
}
void scheduleMidiGateOutISR(uint32_t saddrPosAtStart, int32_t unadjustedNumSamplesBeforeLappingPlayHead,
                            int32_t timeWithinWindowAtWhichMIDIOrGateOccurs) {
	bool anyGateOutputPending = cvEngine.isAnythingPending();

	if ((midiEngine.anythingInOutputBuffer() || anyGateOutputPending) && !isTimerEnabled(TIMER_MIDI_GATE_OUTPUT)) {

		// I don't think this actually could still get left at -1, but just in case...
		if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
			timeWithinWindowAtWhichMIDIOrGateOccurs = 0;
		}

		uint32_t saddrAtEnd = (uint32_t)(getTxBufferCurrentPlace());
		uint32_t saddrPosAtEnd = saddrAtEnd >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE);
		uint32_t saddrMovementSinceStart =
		    saddrPosAtEnd - saddrPosAtStart; // You'll need to &(SSI_TX_BUFFER_NUM_SAMPLES - 1) this anytime it's used

		int32_t samplesTilMIDIOrGate = (timeWithinWindowAtWhichMIDIOrGateOccurs - saddrMovementSinceStart
		                                - unadjustedNumSamplesBeforeLappingPlayHead)
		                               & (SSI_TX_BUFFER_NUM_SAMPLES - 1);

		if (!samplesTilMIDIOrGate) {
			samplesTilMIDIOrGate = SSI_TX_BUFFER_NUM_SAMPLES;
		}

		// samplesTilMIDI += 10; This gets the start of stuff perfectly lined up. About 10 for MIDI, 12 for gate

		if (anyGateOutputPending) {
			// If a gate note-on was processed at the same time as a gate note-off, the note-off will have already
			// been sent, but we need to make sure that the note-on now doesn't happen until a set amount of time
			// after the note-off.
			int32_t gateMinDelayInSamples =
			    ((uint32_t)cvEngine.minGateOffTime * 289014) >> 16; // That's *4.41 (derived from 44100)
			int32_t samplesTilAllowedToSend =
			    cvEngine.mostRecentSwitchOffTimeOfPendingNoteOn + gateMinDelayInSamples - audioSampleTimer;

			if (samplesTilAllowedToSend > 0) {

				samplesTilAllowedToSend -= (saddrMovementSinceStart & (SSI_TX_BUFFER_NUM_SAMPLES - 1));

				if (samplesTilMIDIOrGate < samplesTilAllowedToSend) {
					samplesTilMIDIOrGate = samplesTilAllowedToSend;
				}
			}
		}

		R_INTC_Enable(INTC_ID_TGIA[TIMER_MIDI_GATE_OUTPUT]);

		// Set delay time. This is samplesTilMIDIOrGate * 515616 / kSampleRate.
		*TGRA[TIMER_MIDI_GATE_OUTPUT] = ((uint32_t)samplesTilMIDIOrGate * 766245) >> 16;
		enableTimer(TIMER_MIDI_GATE_OUTPUT);
	}
}

void routine_task() {
	if (audioRoutineLocked) {
		logAction("AudioDriver::routine locked");
		ignoreForStats();
		return; // Prevents this from being called again from inside any e.g. memory allocation routines that get
		        // called from within this!
	}
	calledFromScheduler = true;
	routine();
	calledFromScheduler = false;
}
void routine() {

	logAction("AudioDriver::routine");

	if (audioRoutineLocked) {
		logAction("AudioDriver::routine locked");
		return; // Prevents this from being called again from inside any e.g. memory allocation routines that get
		        // called from within this!
	}

	audioRoutineLocked = true;

	numRoutines = 0;
	voicesStartedThisRender = std::max<int>(
	    cpuDireness - 12,
	    0); // if we're at high direness then we pretend a couple have already been started to limit note ons
	if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
		while (doSomeOutputting() && numRoutines < 2) {

#ifndef USE_TASK_MANAGER
			if (numRoutines > 0) {
				deluge::hid::encoders::readEncoders();
				deluge::hid::encoders::interpretEncoders(true);
			}
#endif
			routine_();
			numRoutines += 1;
		}
	}
	else {
		if (!sdRoutineLock) {
			auto timeNow = getSystemTime();
			while (getSystemTime() < timeNow + 32 / 44100.) {
				size_t numSamples = 32;
				int32_t timeWithinWindowAtWhichMIDIOrGateOccurs;
				tickSongFinalizeWindows(numSamples, timeWithinWindowAtWhichMIDIOrGateOccurs);

				numSamplesLastTime = numSamples;
				renderAudioForStemExport(numSamples);
				audioSampleTimer += numSamples;
				doSomeOutputting();
				// gross and hacky way to make sure the audio recorder writes all of its data so it can't be stolen
				while (audioRecorder.recorder->firstUnwrittenClusterIndex
				       < audioRecorder.recorder->currentRecordClusterIndex) {
					doRecorderCardRoutines();
				}

				audioFileManager.loadAnyEnqueuedClusters(128, false);
			}
		}
	}
	audioRoutineLocked = false;
	routineBeenCalled = true;
}

int32_t getNumSamplesLeftToOutputFromPreviousRender() {
	return ((uint32_t)renderingBufferOutputEnd - (uint32_t)renderingBufferOutputPos) >> 3;
}

// Returns whether we got to the end
bool doSomeOutputting() {

	// Copy to actual output buffer, and apply heaps of gain too, with clipping
	int32_t numSamplesOutputted = 0;

	deluge::dsp::StereoBuffer<q31_t> outputBufferForResampling{
	    reinterpret_cast<deluge::dsp::StereoSample<q31_t>*>(spareRenderingBuffer), 128 * 2};
	deluge::dsp::StereoSample<q31_t>* __restrict__ renderingBufferOutputPosNow = renderingBufferOutputPos;
	int32_t* __restrict__ i2sTXBufferPosNow = (int32_t*)i2sTXBufferPos;
	int32_t* __restrict__ inputReadPos = (int32_t*)i2sRXBufferPos;

	while (renderingBufferOutputPosNow != renderingBufferOutputEnd) {

		// If we've reached the end of the known space in the output buffer...
		if (!(((uint32_t)((uint32_t)i2sTXBufferPosNow - saddr) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
		      & (SSI_TX_BUFFER_NUM_SAMPLES - 1))) {

			// See if there's now some more space.
			saddr = (uint32_t)getTxBufferCurrentPlace();

			// If there wasn't, stop for now
			if (!(((uint32_t)((uint32_t)i2sTXBufferPosNow - saddr) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
			      & (SSI_TX_BUFFER_NUM_SAMPLES - 1))) {
				break;
			}
		}

		// Here we're going to do something equivalent to a multiply_32x32_rshift32(), but add dithering.
		// Because dithering is good, and also because the annoying codec chip freaks out if it sees the same 0s for
		// too long.
		int64_t lAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->l * (int64_t)masterVolumeAdjustmentL + (int64_t)getNoise();
		int64_t rAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->r * (int64_t)masterVolumeAdjustmentR + (int64_t)getNoise();

		int32_t lAdjusted = lAdjustedBig >> 32;
		int32_t rAdjusted = rAdjustedBig >> 32;

		if (doMonitoring) {

			if (monitoringAction == MonitoringAction::SUBTRACT_RIGHT_CHANNEL) {
				int32_t value = (inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS + 1))
				                - (inputReadPos[1] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS));
				lAdjusted += value;
				rAdjusted += value;
			}

			else {
				lAdjusted += inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);

				if (monitoringAction == MonitoringAction::NONE) {
					rAdjusted += inputReadPos[1] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);
				}
				else { // Remove right channel
					rAdjusted += inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);
				}
			}

			inputReadPos += NUM_MONO_INPUT_CHANNELS;
			if (inputReadPos >= getRxBufferEnd()) {
				inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		}

#if HARDWARE_TEST_MODE
		// Send a square wave if anything pressed
		if (anythingProbablyPressed) {
			int32_t outputSample = 1 << 29;
			if ((audioSampleTimer >> 6) & 1) {
				outputSample = -outputSample;
			}

			i2sTXBufferPos->l = outputSample;
			i2sTXBufferPos->r = outputSample;
		}

		// Otherwise, echo input
		else {
			i2sTXBufferPos->l = i2sRXBufferPos->l;
			i2sTXBufferPos->r = i2sRXBufferPos->r;
		}

#else
		outputBufferForResampling[numSamplesOutputted].l = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(lAdjusted);
		outputBufferForResampling[numSamplesOutputted].r = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(rAdjusted);
		if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
			i2sTXBufferPosNow[0] = outputBufferForResampling[numSamplesOutputted].l;
			i2sTXBufferPosNow[1] = outputBufferForResampling[numSamplesOutputted].r;
		}
		// otherwise output 0 or 1 for silence, the DAC doesn't like all zeroes
		else {
			i2sTXBufferPosNow[0] = numSamplesOutputted % 2;
			i2sTXBufferPosNow[1] = numSamplesOutputted % 2;
		}
#endif

#if ALLOW_SPAM_MODE
		if (inSpamMode) {
			i2sTXBufferPosNow[0] = getNoise() >> 4;
			i2sTXBufferPosNow[1] = getNoise() >> 4;
		}
#endif

		i2sTXBufferPosNow += NUM_MONO_OUTPUT_CHANNELS;
		if (i2sTXBufferPosNow == getTxBufferEnd()) {
			i2sTXBufferPosNow = getTxBufferStart();
		}

		numSamplesOutputted++;
		++renderingBufferOutputPosNow;
	}

	renderingBufferOutputPos = renderingBufferOutputPosNow; // Write back from __restrict__ pointer to permanent pointer
	i2sTXBufferPos = (uint32_t)i2sTXBufferPosNow;

	if (numSamplesOutputted) {

		i2sRXBufferPos += (numSamplesOutputted << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
		if (i2sRXBufferPos >= (uint32_t)getRxBufferEnd()) {
			i2sRXBufferPos -= (SSI_RX_BUFFER_NUM_SAMPLES << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
		}

		// Go through each SampleRecorder, feeding them audio
		for (SampleRecorder* recorder = firstRecorder; recorder != nullptr; recorder = recorder->next) {

			if (recorder->status >= RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
				continue;
			}

			// Recording final output
			if (recorder->mode == AudioInputChannel::OUTPUT) {
				recorder->feedAudio(outputBufferForResampling.first(numSamplesOutputted));
			}

			// Recording from an input source
			else if (recorder->mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {

				// We'll feed a bunch of samples to the SampleRecorder - normally all of what we just advanced, but
				// if the buffer wrapped around, we'll just go to the end of the buffer, and not worry about the
				// extra bit at the start - that'll get done next time.
				uint32_t stopPos =
				    (i2sRXBufferPos < (uint32_t)recorder->sourcePos) ? (uint32_t)getRxBufferEnd() : i2sRXBufferPos;

				size_t numSamplesFeedingNow =
				    (stopPos - (uint32_t)recorder->sourcePos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE);

				// We also enforce a firm limit on how much to feed, to keep things sane. Any remaining will get
				// done next time.
				numSamplesFeedingNow = std::min(numSamplesFeedingNow, 256u);

				// this is extremely janky, but essentially because feedAudio only works on an interleaved stream of
				// stereo samples, we can offset it one sample to get it to operate on the right channel
				std::span streamToRecord =
				    (recorder->mode == AudioInputChannel::RIGHT)
				        ? std::span{reinterpret_cast<deluge::dsp::StereoSample<q31_t>*>(recorder->sourcePos + 1),
				                    numSamplesFeedingNow}
				        : std::span{reinterpret_cast<deluge::dsp::StereoSample<q31_t>*>(recorder->sourcePos),
				                    numSamplesFeedingNow};

				recorder->feedAudio(streamToRecord);

				recorder->sourcePos += numSamplesFeedingNow << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
				if (recorder->sourcePos >= getRxBufferEnd()) {
					recorder->sourcePos -= SSI_RX_BUFFER_NUM_SAMPLES << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
				}
			}
		}
	}

	return (renderingBufferOutputPos == renderingBufferOutputEnd);
}

void logAction(char const* string) {
#if DO_AUDIO_LOG
	if (numAudioLogItems >= AUDIO_LOG_SIZE)
		return;
	audioLogTimes[numAudioLogItems] = *TCNT[TIMER_SYSTEM_FAST];
	strcpy(audioLogStrings[numAudioLogItems], string);
	numAudioLogItems++;
#endif
}

void logAction(int32_t number) {
#if DO_AUDIO_LOG
	char buffer[12];
	intToString(number, buffer);
	logAction(buffer);
#endif
}

void dumpAudioLog() {
#if DO_AUDIO_LOG
	uint16_t currentTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t timePassedA = (uint16_t)currentTime - lastRoutineTime;
	uint32_t timePassedUSA = fastTimerCountToUS(timePassedA);
	if (definitelyLog || timePassedUSA > (1000)) {

		D_PRINTLN("");
		for (int32_t i = 0; i < numAudioLogItems; i++) {
			uint16_t timePassed = (uint16_t)audioLogTimes[i] - lastRoutineTime;
			uint32_t timePassedUS = fastTimerCountToUS(timePassed);
			D_PRINTLN("%d:  %s", timePassedUS, audioLogStrings[i]);
		}

		D_PRINTLN("%d: end", timePassedUSA);
	}
	definitelyLog = false;
	lastRoutineTime = *TCNT[TIMER_SYSTEM_FAST];
	numAudioLogItems = 0;
	memset(audioLogStrings, 0, sizeof(audioLogStrings));
#endif
}

void updateReverbParams() {

	// If reverb sidechain on "auto" settings...
	if (reverbSidechainVolume < 0) {

		// Just leave everything as is if parts deleted cos loading new song
		if (loadSongUI.isLoadingSong() && loadSongUI.deletedPartsOfOldSong) {
			return;
		}

		Sound* soundWithMostReverb = nullptr;
		ParamManager* paramManagerWithMostReverb = nullptr;
		GlobalEffectableForClip* globalEffectableWithMostReverb = nullptr;

		// Set the initial "highest amount found" to that of the song itself, which can't be affected by sidechain.
		// If nothing found with more reverb, then we don't want the reverb affected by sidechain
		int32_t highestReverbAmountFound =
		    currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT);

		for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
			thisOutput->getThingWithMostReverb(&soundWithMostReverb, &paramManagerWithMostReverb,
			                                   &globalEffectableWithMostReverb, &highestReverbAmountFound);
		}

		ModControllableAudio* modControllable;

		if (soundWithMostReverb) {
			modControllable = soundWithMostReverb;

			ParamDescriptor paramDescriptor;
			paramDescriptor.setToHaveParamOnly(params::GLOBAL_VOLUME_POST_REVERB_SEND);

			PatchCableSet* patchCableSet = paramManagerWithMostReverb->getPatchCableSet();

			int32_t whichCable = patchCableSet->getPatchCableIndex(PatchSource::SIDECHAIN, paramDescriptor);
			if (whichCable != 255) {
				reverbSidechainVolumeInEffect =
				    patchCableSet->getModifiedPatchCableAmount(whichCable, params::GLOBAL_VOLUME_POST_REVERB_SEND);
			}
			else {
				reverbSidechainVolumeInEffect = 0;
			}
			goto sidechainFound;
		}
		else if (globalEffectableWithMostReverb) {
			modControllable = globalEffectableWithMostReverb;

			reverbSidechainVolumeInEffect =
			    globalEffectableWithMostReverb->getSidechainVolumeAmountAsPatchCableDepth(paramManagerWithMostReverb);

sidechainFound:
			reverbSidechainShapeInEffect =
			    paramManagerWithMostReverb->getUnpatchedParamSet()->getValue(params::UNPATCHED_SIDECHAIN_SHAPE);
			reverbSidechain.attack = modControllable->sidechain.attack;
			reverbSidechain.release = modControllable->sidechain.release;
			reverbSidechain.syncLevel = modControllable->sidechain.syncLevel;
			return;
		}

		// Or if no thing has more reverb than the Song itself...
		else {
			reverbSidechainVolumeInEffect = 0;
			return;
		}
	}

	// Otherwise, use manually set params for reverb sidechain
	reverbSidechainVolumeInEffect = reverbSidechainVolume;
	reverbSidechainShapeInEffect = reverbSidechainShape;
}

void registerSideChainHit(int32_t strength) {
	sideChainHitPending = combineHitStrengths(strength, sideChainHitPending);
}

// Configure paramManagerForSamplePreview's envelope and oscillator state for either sample or wavetable preview.
// Switching adjusts oscType, repeatMode, envelope sustain/release, and resets the wave-index parameter so the
// ping-pong driver in renderSamplePreview() starts at slice 0.
static void configurePreviewSoundForMode(bool asWavetable) {
	PatchedParamSet* patchedParams = paramManagerForSamplePreview->getPatchedParamSet();

	if (asWavetable) {
		if (sampleForPreview->sources[0].oscType != OscType::WAVETABLE) {
			sampleForPreview->sources[0].setOscType(OscType::WAVETABLE);
		}
		sampleForPreview->sources[0].repeatMode = SampleRepeatMode::LOOP;
		// Long sustain so the voice keeps playing across the ping-pong sweep.
		patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(2147483647);
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 30));
		// Centre the wave-index param; renderSamplePreview() will overwrite this each block while ping-pong is active.
		patchedParams->params[params::LOCAL_OSC_A_WAVE_INDEX].setCurrentValueBasicForSetup(0);
	}
	else {
		if (sampleForPreview->sources[0].oscType != OscType::SAMPLE) {
			sampleForPreview->sources[0].setOscType(OscType::SAMPLE);
		}
		// Restore the sample-preview envelope defaults from setupAsSample().
		patchedParams->params[params::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_ENV_0_SUSTAIN, 50));
		patchedParams->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 0));
		// Restore default pitch — the wavetable path may have shifted it down.
		sampleForPreview->sources[0].transpose = 0;
		sampleForPreview->sources[0].setCents(0);
	}
}

// Pitch the wavetable preview so one stored cycle plays in exactly cycleSize output samples (i.e. the file would
// sound the same as if it were played as raw PCM at the engine's native sample rate). At kNoteForDrum (60) the
// wavetable would otherwise render at C4 = 261.63 Hz regardless of cycle length, which is many octaves above
// the file's "intended" pitch.
static void applyWavetablePitchForNativeRate(WaveTable* waveTable) {
	if (waveTable == nullptr || waveTable->bands.getNumElements() == 0) {
		return;
	}
	auto* band = (WaveTableBand*)waveTable->bands.getElementAddress(0);
	int32_t cycleSize = band->cycleSizeNoDuplicates;
	if (cycleSize <= 0) {
		return;
	}
	// freq at note 60 = 440 * 2^((60 - 69) / 12) ≈ 261.6256 Hz. Target freq = kSampleRate / cycleSize.
	constexpr float kFreqAtNoteForDrum = 261.6255653005986f;
	float totalCents = 1200.0f * std::log2((float)kSampleRate / ((float)cycleSize * kFreqAtNoteForDrum));
	int32_t totalCentsRounded = (int32_t)std::lround(totalCents);
	int32_t transpose =
	    totalCentsRounded / 100; // truncates toward zero, which is what we want for cents in (-100, 100)
	int32_t cents = totalCentsRounded - transpose * 100;
	sampleForPreview->sources[0].transpose = (int16_t)transpose;
	sampleForPreview->sources[0].setCents(cents);
}

// Build a preview-only WaveTable: single-band, no FFT mipmap, NOT inserted into wavetableFiles. Reuses any cached
// Sample (so re-previewing the same file is cheap). Caller owns lifetime — stopAnyPreviewing destroys it.
static WaveTable* loadPreviewWavetable(String* path, FilePointer* filePointer, Error* errorOut) {
	*errorOut = Error::NONE;
	auto sampleResult =
	    audioFileManager.getAudioFileFromFilename(*path, true, filePointer, AudioFileType::SAMPLE, false);
	if (!sampleResult.has_value()) {
		*errorOut = sampleResult.error();
		return nullptr;
	}
	auto* sample = (Sample*)sampleResult.value();
	if (sample == nullptr) {
		*errorOut = Error::FILE_UNREADABLE;
		return nullptr;
	}
	if (sample->numChannels != 1) {
		*errorOut = Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO;
		return nullptr;
	}
	void* mem = GeneralMemoryAllocator::get().allocStealable(sizeof(WaveTable));
	if (!mem) {
		*errorOut = Error::INSUFFICIENT_RAM;
		return nullptr;
	}
	auto* wt = new (mem) WaveTable;
	wt->addReason(); // Keep alive while setup() runs and against mid-setup steal.
	Error err = wt->setup(sample, 0, 0, 0, 0, RawDataFormat::NATIVE, nullptr, /*fastPreview=*/true);
	if (err != Error::NONE) {
		wt->~WaveTable();
		delugeDealloc(mem);
		*errorOut = err;
		return nullptr;
	}
	wt->filePath.set(path);
	wt->finalizeAfterLoad(sample->audioDataLengthBytes); // mirrors AudioFileManager's normal load path
	wt->removeReason("E_PV1");                           // back to 0; holder will addReason on setAudioFile.
	// Deliberately NOT inserted into audioFileManager.wavetableFiles — preview-only wavetables don't pollute
	// the song-wide cache, and stopAnyPreviewing eagerly destroys them when the holder releases its reason.
	return wt;
}

void previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound, bool asWavetable) {
	stopAnyPreviewing();

	configurePreviewSoundForMode(asWavetable);

	Error error = Error::NONE;
	if (asWavetable) {
		MultiWaveTableRange* range = (MultiWaveTableRange*)sampleForPreview->sources[0].getOrCreateFirstRange();
		if (!range) {
			return;
		}
		AudioFileHolder* holder = &range->waveTableHolder;
		holder->filePath.set(path);
		WaveTable* wt = loadPreviewWavetable(path, filePointer, &error);
		if (wt != nullptr) {
			holder->setAudioFile(wt); // addReason for the holder.
		}
		else if (error == Error::FILE_NOT_LOADABLE_AS_WAVETABLE
		         || error == Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO) {
			// Fall back to sample preview — file isn't wavetable-shaped.
			configurePreviewSoundForMode(false);
			MultisampleRange* sampleRange = (MultisampleRange*)sampleForPreview->sources[0].getOrCreateFirstRange();
			if (!sampleRange) {
				return;
			}
			sampleRange->sampleHolder.filePath.set(path);
			error = sampleRange->sampleHolder.loadFile(false, true, true, CLUSTER_LOAD_IMMEDIATELY, filePointer);
			asWavetable = false;
		}
	}
	else {
		MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].getOrCreateFirstRange();
		if (!range) {
			return;
		}
		range->sampleHolder.filePath.set(path);
		error = range->sampleHolder.loadFile(false, true, true, CLUSTER_LOAD_IMMEDIATELY, filePointer);
	}

	if (error != Error::NONE) {
		display->displayError(error); // Rare, shouldn't cause later problems.
	}

	previewWavetableActive = asWavetable && error == Error::NONE;
	previewWavetablePhaseSamples = 0;

	if (previewWavetableActive) {
		auto* range = (MultiWaveTableRange*)sampleForPreview->sources[0].ranges.getElement(0);
		auto* wt = (WaveTable*)range->waveTableHolder.audioFile;
		applyWavetablePitchForNativeRate(wt);
		// Publish the handle synchronously. renderSamplePreview() also writes this on each audio tick, but the UI
		// thread queries getPreviewWavetableState() the moment previewSample() returns — if we wait for the audio
		// thread to publish, that race leaves previewWavetableHandle == nullptr just long enough for the caller's
		// "is this a wavetable preview?" check to read false (skipping the SHORTCUT_BLINK unset + scroll setup).
		previewWavetableHandle = wt;
		previewWavetableSlicePos = 0.0f;
	}

	if (shouldActuallySound) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);
		sampleForPreview->Sound::noteOn(modelStack, &sampleForPreview->arpeggiator, kNoteForDrum, zeroMPEValues);
		bypassCulling = true; // Needed - Dec 2021. I think it's during SampleBrowser::selectEncoderAction() that we
		                      // may have gone a while without an audio routine call.
	}
}

void stopAnyPreviewing() {
	sampleForPreview->killAllVoices();
	AudioFile* prevFile = nullptr;
	if (sampleForPreview->sources[0].ranges.getNumElements()) {
		MultiRange* range = (MultiRange*)sampleForPreview->sources[0].ranges.getElement(0);
		prevFile = range->getAudioFileHolder()->audioFile;
		range->getAudioFileHolder()->setAudioFile(nullptr);
	}
	// Preview WaveTables are allocated outside the audioFileManager.wavetableFiles cache; once the holder
	// drops its reason no other reference exists, so they'd otherwise linger in stealable memory and fragment
	// it after dozens of files browsed. Destroy + free now. Samples stay in the sample cache (snappy
	// back-scroll, sample data is small and cluster-reclaimable under pressure).
	if (prevFile != nullptr && prevFile->type == AudioFileType::WAVETABLE && prevFile->numReasonsToBeLoaded <= 0
	    && !audioFileManager.wavetableFiles.contains(&prevFile->filePath)) {
		prevFile->~AudioFile();
		delugeDealloc(prevFile);
	}
	previewWavetableActive = false;
	previewWavetablePhaseSamples = 0;
	previewWavetableHandle = nullptr;
	previewWavetableSlicePos = 0.0f;
	previewWavetableManualActive = false;
	previewWavetableManualPos = 0.0f;
	// Reset any pitch offset applied by applyWavetablePitchForNativeRate so the next sample preview is not detuned.
	sampleForPreview->sources[0].transpose = 0;
	sampleForPreview->sources[0].setCents(0);
}

void setPreviewWavetableManualSlicePos(float pos) {
	previewWavetableManualActive = true;
	previewWavetableManualPos = pos;
	// Mirror into the public slicePos so the LED graphicsRoutine renders the chosen slice immediately on the next
	// graphics tick, without having to wait a full audio render to sync.
	previewWavetableSlicePos = pos;
}

float getPreviewWavetableManualSlicePos() {
	return previewWavetableManualPos;
}

bool getPreviewWavetableManualActive() {
	return previewWavetableManualActive;
}

WaveTable* getPreviewWavetableState(float* slicePos) {
	if (!previewWavetableActive) {
		return nullptr;
	}
	if (slicePos != nullptr) {
		*slicePos = previewWavetableSlicePos;
	}
	return previewWavetableHandle;
}

void getReverbParamsFromSong(Song* song) {
	reverb.setModel(song->model);
	reverb.setRoomSize(song->reverbRoomSize);
	reverb.setLPF(song->reverbLPF);
	reverb.setDamping(song->reverbDamp);
	reverb.setWidth(song->reverbWidth);
	reverbPan = song->reverbPan;
	reverbSidechainVolume = song->reverbSidechainVolume;
	reverbSidechainShape = song->reverbSidechainShape;
	reverbSidechain.attack = song->reverbSidechainAttack;
	reverbSidechain.release = song->reverbSidechainRelease;
	reverbSidechain.syncLevel = song->reverbSidechainSync;
}

bool allowedToStartVoice() {
	if (voicesStartedThisRender < 4) {
		voicesStartedThisRender += 1;
		return true;
	}
	return false;
}

VoiceSample* solicitVoiceSample() {
	try {
		return VoiceSamplePool::get().acquire().release();
	} catch (deluge::exception e) {
		return nullptr;
	}
}

void voiceSampleUnassigned(VoiceSample* voiceSample) {
	VoiceSamplePool::recycle(voiceSample);
}

TimeStretcher* solicitTimeStretcher() {
	try {
		return TimeStretcherPool::get().acquire().release();
	} catch (deluge::exception e) {
		return nullptr;
	}
}

// There are no destructors. You gotta clean it up before you call this
void timeStretcherUnassigned(TimeStretcher* timeStretcher) {
	TimeStretcherPool::recycle(timeStretcher);
}

// TODO: delete unused ones
LiveInputBuffer* getOrCreateLiveInputBuffer(OscType inputType, bool mayCreate) {
	const auto idx = util::to_underlying(inputType) - util::to_underlying(OscType::INPUT_L);
	if (!liveInputBuffers[idx]) {
		if (!mayCreate) {
			return nullptr;
		}

		int32_t size = sizeof(LiveInputBuffer);
		if (inputType == OscType::INPUT_STEREO) {
			size += kInputRawBufferSize * sizeof(int32_t);
		}

		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(size);
		if (!memory) {
			return nullptr;
		}

		liveInputBuffers[idx] = new (memory) LiveInputBuffer();
	}

	return liveInputBuffers[idx];
}

bool createdNewRecorder;

void doRecorderCardRoutines() {

	SampleRecorder** prevPointer = &firstRecorder;
	int32_t count = 0;
	while (true) {
		count++;

		SampleRecorder* recorder = *prevPointer;
		if (!recorder) {
			break;
		}

		Error error = recorder->cardRoutine();
		if (error != Error::NONE) {
			display->displayError(error);
		}

		// If, while in the card routine, a new Recorder was added, then our linked list traversal state thing will
		// be out of wack, so let's just get out and come back later
		if (createdNewRecorder) {
			break;
		}

		// If complete, discard it
		if (recorder->status == RecorderStatus::AWAITING_DELETION) {
			D_PRINTLN("deleting recorder");
			*prevPointer = recorder->next;
			recorder->~SampleRecorder();
			delugeDealloc(recorder);
		}

		// Otherwise, move on
		else {
			prevPointer = &recorder->next;
		}
	}

	if (ALPHA_OR_BETA_VERSION && ENABLE_CLIP_CUTTING_DIAGNOSTICS && count >= 10 && !display->hasPopup()) {
		display->displayPopup("MORE");
	}
}

void slowRoutine() {
	if (sdRoutineLock) {
		// can happen if the SD routine is yielding
		return;
	}
	// The RX buffer is much bigger than the TX, and we get our timing from the TX buffer's sending.
	// However, if there's an audio glitch, and also at start-up, it's possible we might have missed an entire cycle
	// of the TX buffer. That would cause the RX buffer's latency to increase. So here, we check for that and
	// correct it. The correct latency for the RX buffer is between (SSI_TX_BUFFER_NUM_SAMPLES) and (2 *
	// SSI_TX_BUFFER_NUM_SAMPLES).

	uint32_t rxBufferWriteAddr = (uint32_t)getRxBufferCurrentPlace();
	uint32_t latencyWithinAppropriateWindow =
	    (((rxBufferWriteAddr - (uint32_t)i2sRXBufferPos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE))
	     - SSI_TX_BUFFER_NUM_SAMPLES)
	    & (SSI_RX_BUFFER_NUM_SAMPLES - 1);

	while (latencyWithinAppropriateWindow >= SSI_TX_BUFFER_NUM_SAMPLES) {
		i2sRXBufferPos += (SSI_TX_BUFFER_NUM_SAMPLES << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE));
		if (i2sRXBufferPos >= (uint32_t)getRxBufferEnd()) {
			i2sRXBufferPos -= (SSI_RX_BUFFER_NUM_SAMPLES << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE));
		}
		latencyWithinAppropriateWindow =
		    (((rxBufferWriteAddr - (uint32_t)i2sRXBufferPos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE))
		     - SSI_TX_BUFFER_NUM_SAMPLES)
		    & (SSI_RX_BUFFER_NUM_SAMPLES - 1);
	}

	// Discard any LiveInputBuffers which aren't in use
	for (int32_t i = 0; i < 3; i++) {
		if (liveInputBuffers[i]) {
			if (liveInputBuffers[i]->upToTime != audioSampleTimer) {
				liveInputBuffers[i]->~LiveInputBuffer();
				delugeDealloc(liveInputBuffers[i]);
				liveInputBuffers[i] = nullptr;
			}
		}
	}

	createdNewRecorder = false;

	// Go through all SampleRecorders, getting them to write etc
	doRecorderCardRoutines();
}

SampleRecorder* getNewRecorder(int32_t numChannels, AudioRecordingFolder folderID, AudioInputChannel mode,
                               bool keepFirstReasons, bool writeLoopPoints, int32_t buttonPressLatency,
                               bool shouldNormalize, Output* outputRecordingFrom) {
	Error error;

	void* recorderMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SampleRecorder));
	if (!recorderMemory) {
		return nullptr;
	}

	SampleRecorder* newRecorder = new (recorderMemory) SampleRecorder();

	error = newRecorder->setup(numChannels, mode, keepFirstReasons, writeLoopPoints, folderID, buttonPressLatency,
	                           outputRecordingFrom);
	if (error != Error::NONE) {
errorAfterAllocation:
		newRecorder->~SampleRecorder();
		delugeDealloc(recorderMemory);
		return nullptr;
	}

	if (mode == AudioInputChannel::SPECIFIC_OUTPUT) {
		if (!outputRecordingFrom) {
			D_PRINTLN("Specific output recorder with no output provided");
			goto errorAfterAllocation;
		}
		bool success = outputRecordingFrom->addRecorder(newRecorder);
		if (!success) {
			D_PRINTLN("Tried to attach to an occupied output");
			goto errorAfterAllocation;
		}
	}

	newRecorder->next = firstRecorder;
	firstRecorder = newRecorder;

	createdNewRecorder = true;

	if (mode >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		renderInStereo = true; // Quickly set it to true. It'll keep getting reset to true now every time
		                       // inputRoutine() is called
	}

	return newRecorder;
}

// PLEASE don't call this if there's any chance you might be in the SD card routine...
void discardRecorder(SampleRecorder* recorder) {
	int32_t count = 0;
	SampleRecorder** prevPointer = &firstRecorder;
	while (*prevPointer) {

		count++;
		if (ALPHA_OR_BETA_VERSION && !*prevPointer) {
			FREEZE_WITH_ERROR("E264");
		}
		if (*prevPointer == recorder) {
			*prevPointer = recorder->next;
			break;
		}

		prevPointer = &(*prevPointer)->next;
	}

	recorder->~SampleRecorder();
	delugeDealloc(recorder);
}

bool isAnyInternalRecordingHappening() {
	for (SampleRecorder* recorder = firstRecorder; recorder; recorder = recorder->next) {
		if (recorder->mode >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
			return true;
		}
	}

	return false;
}

} // namespace AudioEngine
