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

#include "chrono"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "deluge_resource.h" // deluge_resource_stats (loader instrumentation)
#include "dsp/reverb/reverb.hpp"
#include "dsp/timestretch/time_stretcher.h"
#include "extern.h"
#include "gui/context_menu/sample_browser/kit.h"
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/slicer.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/encoder_input.h"
#include "hid/encoders.h"
#include "hid/led/indicator_leds.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/app.h"
#include "libdeluge/audio_io.h"
#include "libdeluge/signals.h"
#include "libdeluge/storage_owner.h"  // deluge_storage_on_owner
#include "libdeluge/streaming_fill.h" // deluge_streaming_resource_manager()
#include "libdeluge/system.h"
#include "libdeluge/worker.h"
#include "memory/general_memory_allocator.h"
#include "memory/stack_guard.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "modulation/envelope.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/audio_output.h"
#include "processing/engines/cv_engine.h"
#include "processing/live/live_input_buffer.h"
#include "processing/metronome/metronome.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/stem_export/stem_export.h"
#include "scheduler_api.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/owner.h" // deluge::storage::Coalescer (SD-routine dispatch)
#include "storage/storage_manager.h"
#include "sync/sd_access.h"
#include "util/functions.h"
#include "util/misc.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <execution>
#include <iterator>
#include <new>
#include <numeric>
#include <ranges>

namespace params = deluge::modulation::params;

using namespace deluge;

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
StereoFloatSample approxRMSLevel{0};
AbsValueFollower envelopeFollower{};
int32_t timeLastPopup{0};

SoundDrum* sampleForPreview;
ParamManagerForTimeline* paramManagerForSamplePreview;

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

// See audio_engine.h: the app-owned mirror of the received input, and the read
// cursor into it. CPU-only memory (no DMA), so it may live in cached SDRAM.
PLACE_SDRAM_BSS int32_t inputRing[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS];
uintptr_t inputRingPos;

int32_t* inputRingStart() {
	return inputRing;
}
int32_t* inputRingEnd() {
	return inputRing + (SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS);
}

// Append the input block the BSP handed to deluge_app_render() to the app-owned
// input ring at the current cursor. Must cover the full render block before any
// input reader runs; the cursor itself only advances as output is produced, so
// each input sample stays paired with its output sample.
static void fillInputRing(const DelugeStereoSample* in, size_t numSamples) {
	uint32_t ringBytes = SSI_RX_BUFFER_NUM_SAMPLES << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE);
	uint32_t offsetBytes = inputRingPos - (uintptr_t)inputRing;
	uint32_t copyBytes = numSamples << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE);
	uint32_t firstChunkBytes = std::min(copyBytes, ringBytes - offsetBytes);
	memcpy((uint8_t*)inputRing + offsetBytes, in, firstChunkBytes);
	if (copyBytes != firstChunkBytes) {
		memcpy(inputRing, (const uint8_t*)in + firstChunkBytes, copyBytes - firstChunkBytes);
	}
}

// Advance the input-ring cursor by `numSamples`, wrapping. Called as output is
// produced, keeping in/out sample pairing exact.
static void advanceInputRingPos(size_t numSamples) {
	inputRingPos += (numSamples << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
	if (inputRingPos >= (uintptr_t)inputRingEnd()) {
		inputRingPos -= (SSI_RX_BUFFER_NUM_SAMPLES << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
	}
}
volatile int voices_started_this_render = 0;
bool headphonesPluggedIn;
bool micPluggedIn;
bool lineInPluggedIn;
InputMonitoringMode inputMonitoringMode = InputMonitoringMode::SMART;
bool routineBeenCalled;
uint8_t numHopsEndedThisRoutineCall;

LiveInputBuffer* liveInputBuffers[3];

// For debugging
double lastRoutineTime;

alignas(CACHE_LINE_SIZE) std::array<StereoSample, SSI_TX_BUFFER_NUM_SAMPLES> renderingMemory;
alignas(CACHE_LINE_SIZE) std::array<int32_t, 2 * SSI_TX_BUFFER_NUM_SAMPLES> reverbMemory;

StereoSample* renderingBufferOutputPos = renderingMemory.begin();
StereoSample* renderingBufferOutputEnd = renderingMemory.begin();

int32_t masterVolumeAdjustmentL;
int32_t masterVolumeAdjustmentR;

bool doMonitoring;
MonitoringAction monitoringAction;

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

	deluge_audio_start();
	inputRingPos = (uintptr_t)inputRing;

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

double audioLogTimes[AUDIO_LOG_SIZE];
char audioLogStrings[AUDIO_LOG_SIZE][64];
const char* audioLogFiles[AUDIO_LOG_SIZE];
int audioLogLines[AUDIO_LOG_SIZE];

int32_t numAudioLogItems = 0;
#endif

/// Force a voice to stop within this render window. Will click slightly, especially if multiple are stopped in the
/// same render
void killOneVoice(size_t num_samples) {
	// Only include audio if doing a hard cull and not saving the voice
	auto lowest_priority_voices = sounds //<
	                              | std::views::filter(&Sound::hasActiveVoices)
	                              | std::views::transform(&Sound::getLowestPriorityVoice);
	// Compare by Voice priority, not by the ActiveVoice (unique_ptr) pointer value — a bare max_element
	// orders by address, picking a heap-layout-dependent (and semantically arbitrary) voice to cull. See
	// Sound::getLowestPriorityVoice and docs/dev/overread_hunt.md.
	auto it = std::ranges::max_element(lowest_priority_voices, [](const auto& a, const auto& b) { return *a < *b; });
	if (it == lowest_priority_voices.end()) {
		return;
	}

	const Sound::ActiveVoice& voice = *it;

	bool still_rendering = voice->doImmediateRelease();
	if (!still_rendering) {
		voice->sound.freeActiveVoice(voice);
	}

	D_PRINTLN("killed 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", num_samples, getNumVoices(),
	          getNumAudio());
}

/// Force a voice to release very quickly - will be almost instant but not click
void terminateOneVoice(size_t numSamples) {
	auto all_voices = sounds | std::views::transform(&Sound::voices) | std::views::join;

	if (all_voices.empty()) {
		return;
	}

	// The skip filter must apply to the first voice too (see Sound::terminateOneActiveVoice / issue #4721):
	// seeding `best` unfiltered let an already-fast-releasing front voice absorb every cull in the window.
	const Sound::ActiveVoice* best = nullptr;
	for (const auto& voice : all_voices) {
		if (voice->isCullFading()) {
			continue;
		}
		if (best == nullptr || (*best)->getPriorityRating() < voice->getPriorityRating()) {
			best = &voice;
		}
	}

	if (best == nullptr) {
		return;
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

	// Don't spend another soft cull on a voice that's already disappearing at the cull fade rate. FAST_RELEASE has a
	// high priority rating, so without this filter repeated soft culls can chase the same fading voice while long
	// musical-release tails keep stacking up.
	const Sound::ActiveVoice* best = nullptr;
	for (const auto& voice : all_voices) {
		if (voice->isCullFading()) {
			continue;
		}
		if (best == nullptr || (*best)->getPriorityRating() < voice->getPriorityRating()) {
			best = &voice;
		}
	}

	if (best == nullptr) {
		return;
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

void routineWithClusterLoading() {
	logAction("AudioDriver::routineWithClusterLoading");

	routineBeenCalled = false;

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
void cullVoices(size_t numSamples, int32_t numAudio, int32_t numVoice) {

	bool culled = false;
	// at high loads voicesStarted is limited to 2. Since starting voices is a heavy load and we know it's temporary
	// we can be a bit more lenient with the limit
	auto max_num_samples = numSamplesLimit + (20 * voices_started_this_render);
	if (numAudio + numVoice > MIN_VOICES) {
		static int32_t last_num_samples_over = 0;

		int32_t num_samples_over_limit = numSamples - max_num_samples;
		// If it's real dire, do a proper immediate cull
		if (num_samples_over_limit >= 32) {
			int32_t num_to_cull = (num_samples_over_limit >> 4);

			// leave at least 7 - below this point culling won't save us
			// if they can't load their sample in time they'll stop the same way anyway
			num_to_cull = std::min(num_to_cull, numAudio + numVoice - MIN_VOICES);
			D_PRINTLN("Culling %d voices", num_to_cull);

			for (int32_t i = num_to_cull / 2; i < num_to_cull; i++) {
				// cull with fast release
				terminateOneVoice(numSamples);
			}
			for (int32_t i = 1; i < num_to_cull / 2; i++) {
				// cull with immediate release
				killOneVoice(numSamples);
			}
			forceReleaseOneVoice(numSamples);

#if ALPHA_OR_BETA_VERSION
			logAction("hard cull");

#endif
			culled = true;
		}

		// Or if it's just a little bit dire, do a soft cull with fade-out, but only cull for sure if numSamples
		// is increasing
		else if (num_samples_over_limit >= 0) {
			if (last_num_samples_over > 0 && num_samples_over_limit >= last_num_samples_over) {
				forceReleaseOneVoice(numSamples);
				logAction("soft cull");
				if (numRoutines > 0) {
					culled = true;
					D_PRINTLN("culling in second routine");
				}
			}
		}
		last_num_samples_over = num_samples_over_limit;
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
		D_PRINTLN("started %i voices this render cycle", voices_started_this_render);
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

void scheduleMidiGateOutISR(uint32_t offsetInBlock, int32_t timeWithinWindowAtWhichMIDIOrGateOccurs);
void setMonitoringMode();
void renderSongFX(size_t numSamples);
void renderSamplePreview(size_t numSamples);
void renderReverb(size_t numSamples);
int32_t tickSongFinalizeWindows(size_t& numSamples);
void flushMIDIGateBuffers();
void renderAudio(size_t numSamples);
void renderAudioForStemExport(size_t numSamples);
void dumpAudioLog();
bool calledFromScheduler = false;

static uint32_t outputStage(DelugeStereoSample* out, uint32_t produced, uint32_t frames);

/// The application's single audio entry point, called by the BSP render driver
/// (see <libdeluge/app.h> / <libdeluge/audio_io.h>). Renders exactly `frames`
/// stereo samples into the BSP's `out` block, sub-dividing internally so render
/// windows still end exactly on sequencer events; `in` holds the `frames` input
/// samples aligned with `out`. The window-length policy (how `frames` is
/// chosen) is the board's: on RZ/A1L it stays the DMA-polled variable window.
extern "C" void deluge_app_render(const DelugeStereoSample* in, DelugeStereoSample* out, uint32_t frames) {
	static double last_call_time = getSystemTime();
	double current_time = getSystemTime();
	if (current_time - last_call_time > 0.003) {
		// If the audio routine is called at less than a 3ms interval, something is wrong. `frames` is
		// logged alongside because the two failure shapes look identical in the interval alone: a driver
		// handing over one huge late window (frames tracks the gap) versus the render being starved while
		// the window stays small (frames stays nominal).
		//
		// RATE-LIMITED to once a second, reporting how many it swallowed. Unthrottled this fires on EVERY
		// render call once the engine falls behind, which made it three problems at once: formatting and
		// pushing thousands of lines a second costs real time on the audio path, so the warning helped
		// cause the condition it reports; it exhausted a 10000-line RTT capture within ten seconds, so no
		// later event could be observed at all; and it turned every latency figure into a measurement of
		// the logging rather than of the engine. A diagnostic that changes what it measures is worse than
		// none.
		static double lastWarnTime = 0.;
		static uint32_t suppressedWarnings = 0;
		if (current_time - lastWarnTime >= 1.0) {
			D_PRINTLN("Audio routine latency high: %.3fms frames=%d (+%d more in the last second)",
			          (current_time - last_call_time) * 1000., (int)frames, (int)suppressedWarnings);
			lastWarnTime = current_time;
			suppressedWarnings = 0;
		}
		else {
			suppressedWarnings++;
		}
	}
	last_call_time = current_time;

	// At this point, there may be MIDI, including clocks, waiting to be sent.

	checkStack("AudioDriver::routine");

	fillInputRing(in, frames);

	bool offline = stemExport.processStarted && stemExport.renderOffline;

	if (!offline) {
		flushMIDIGateBuffers();
		setDireness(frames);
	}

	uint32_t produced = 0;
	while (produced < frames) {

		// First drain anything already rendered through the output stage (the
		// previous sub-window; during offline export, the export loop's block).
		if (renderingBufferOutputPos != renderingBufferOutputEnd) {
			produced += outputStage(out, produced, frames);
			continue;
		}

		// During offline stem export the export loop in routine() does the
		// rendering; just keep the codec fed (it mutes on sustained zeros) and
		// keep the input cursor pairing intact.
		if (offline) {
			for (uint32_t i = produced; i < frames; i++) {
				out[i].l = (int32_t)((i - produced) % 2);
				out[i].r = (int32_t)((i - produced) % 2);
			}
			advanceInputRingPos(frames - produced);
			produced = frames;
			break;
		}

		// Render the next sub-window, ending exactly at the next sequencer
		// event (tickSongFinalizeWindows may shorten it).
		if (produced != 0) {
			flushMIDIGateBuffers();
		}

		voices_started_this_render = 0;

		size_t numSamples = frames - produced;
		int32_t timeWithinWindowAtWhichMIDIOrGateOccurs = tickSongFinalizeWindows(numSamples);

		numSamplesLastTime = numSamples;

		renderAudio(numSamples);

		scheduleMidiGateOutISR(produced, timeWithinWindowAtWhichMIDIOrGateOccurs);

		sideChainHitPending = 0;
		audioSampleTimer += numSamples;
	}

#if DO_AUDIO_LOG
	dumpAudioLog();
#endif

	bypassCulling = false;
	numRoutines += 1;

	// Periodic loader-instrumentation dump. Every ~2 s of audio time: an integer compare on the
	// render path, and a print two orders of magnitude rarer than the renders themselves.
	//
	// Stats are RESET after each dump so the maxima are PER-INTERVAL. Cumulative maxima proved
	// misleading — a chunk left enqueued across an idle gap reported a 79-second "latency".
	//
	// What these numbers are for (all measured 2026-08-16, see the storage-execution-model design's
	// Appendix C): `wait` vs `fill` splits a streaming chunk's service latency into the fill task's
	// scheduling delay (mean 35 ms) versus the card (mean 5.6 ms) — 35 ms is the number the
	// storage-tier scheduling work has to beat. `depth` is the tripwire for ordering ever mattering
	// (mean 1.19 today: nothing to order). `scans` guards the chunk-table indexes against a
	// regression back to the O(table) walks they replaced.
	{
		static uint32_t lastLoaderDumpTime = 0;
		if (audioSampleTimer - lastLoaderDumpTime >= 44100u * 2u) {
			lastLoaderDumpTime = audioSampleTimer;
			DelugeResource* mgr = deluge_streaming_resource_manager();
			if (mgr != nullptr) {
				DelugeResourceStats st{};
				deluge_resource_stats(mgr, &st);
				if (st.queue_wait_samples != 0u || st.loader_depth_polls != 0u) {
					D_PRINTLN("loader wait: n %d mean %d max %d | fill: n %d mean %d max %d (frames)",
					          (int32_t)st.queue_wait_samples,
					          (int32_t)(st.queue_wait_samples != 0u ? st.queue_wait_total / st.queue_wait_samples : 0u),
					          (int32_t)st.queue_wait_max, (int32_t)st.fill_samples,
					          (int32_t)(st.fill_samples != 0u ? st.fill_total / st.fill_samples : 0u),
					          (int32_t)st.fill_max);
					D_PRINTLN("loader depth: polls %d mean_x100 %d max %d | scans: resident %d/%d ptr %d/%d",
					          (int32_t)st.loader_depth_polls,
					          (int32_t)(st.loader_depth_polls != 0u
					                        ? (st.loader_depth_total * 100u) / st.loader_depth_polls
					                        : 0u),
					          (int32_t)st.loader_depth_max, (int32_t)st.scan_resident_calls,
					          (int32_t)st.scan_resident_slots, (int32_t)st.scan_ptr_calls, (int32_t)st.scan_ptr_slots);
				}
				deluge_resource_stats_reset(mgr); // per-interval, not cumulative (see above)
			}
		}
	}
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

	// Feed the just-rendered block to the offline stem recorder. Two recorder channels
	// reach the offline path: OFFLINE_OUTPUT (a MIXDOWN with song FX — post-FX master)
	// and MIX (per-instrument TRACK / FX-less export — the pre-song-FX mix, which here is
	// just the one un-muted instrument). Both want `renderingBuffer`; only the FX stage
	// above differs. Without feeding the MIX recorder, a TRACK export's recorder never
	// reaches its loop-end, never finalises (recordingSource stays MIX), and the export
	// loops forever rendering silence past the arrangement end.
	SampleRecorder* recorder = audioRecorder.recorder;
	if (recorder && (recorder->mode == AudioInputChannel::OFFLINE_OUTPUT || recorder->mode == AudioInputChannel::MIX)) {
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
	CriticalSectionGuard guard;
	bool anythingInMidiOutputBufferNow = midiEngine.anythingInOutputBuffer();
	bool anythingInGateOutputBufferNow = cvEngine.isAnythingButRunPending();
	if (anythingInMidiOutputBufferNow || anythingInGateOutputBufferNow) {

		// We're only allowed to do this if the timer ISR isn't pending (i.e. we haven't enabled to timer to trigger
		// it)
		// - otherwise this will all get called soon anyway. I thiiiink this is 100% immune to any synchronization
		// problems?
		if (!deluge_midi_gate_timer_pending()) {
			if (anythingInGateOutputBufferNow) {
				cvEngine.updateGateOutputs();
			}
			if (anythingInMidiOutputBufferNow) {
				midiEngine.flushMIDI();
			}
		}
	}
}

int32_t tickSongFinalizeWindows(size_t& numSamples) {
	int32_t timeWithinWindowAtWhichMIDIOrGateOccurs = -1; // -1 means none

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
		if (!stemExport.renderingOffline()) {
			// only send trigger clock out if you've enabled it
			if (cvEngine.isTriggerClockOutputEnabled()) {
				if (playbackHandler.triggerClockOutTickScheduled) {
					int32_t timeTilTriggerClockOutTick = playbackHandler.timeNextTriggerClockOutTick - audioSampleTimer;
					if (std::cmp_less(timeTilTriggerClockOutTick, numSamples)) {
						playbackHandler.doTriggerClockOutTick();
						playbackHandler.scheduleTriggerClockOutTick(); // Schedules another one

						if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
							timeWithinWindowAtWhichMIDIOrGateOccurs = timeTilTriggerClockOutTick;
						}
					}
				}
				else {
					playbackHandler.scheduleTriggerClockOutTick();
				}
			}

			// only send midi clock out if you've enabled it
			if (playbackHandler.currentlySendingMIDIOutputClocks()) {
				if (playbackHandler.midiClockOutTickScheduled) {
					int32_t timeTilMIDIClockOutTick = playbackHandler.timeNextMIDIClockOutTick - audioSampleTimer;
					if (std::cmp_less(timeTilMIDIClockOutTick, numSamples)) {
						playbackHandler.doMIDIClockOutTick();
						playbackHandler.scheduleMIDIClockOutTick(); // Schedules another one

						if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
							timeWithinWindowAtWhichMIDIOrGateOccurs = timeTilMIDIClockOutTick;
						}
					}
				}
				else {
					playbackHandler.scheduleMIDIClockOutTick();
				}
			}
		}
	}
	return timeWithinWindowAtWhichMIDIOrGateOccurs;
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
void renderSamplePreview(size_t numSamples) { // Previewing sample
	std::span renderingBuffer{renderingMemory.data(), numSamples};
	std::span reverbBuffer{reverbMemory.data(), numSamples};

	if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &gui::context_menu::sample_browser::kit
	    || getCurrentUI() == &gui::context_menu::sample_browser::synth || getCurrentUI() == &slicer) {

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
void scheduleMidiGateOutISR(uint32_t offsetInBlock, int32_t timeWithinWindowAtWhichMIDIOrGateOccurs) {
	bool anyGateOutputPending = cvEngine.isAnythingPending();

	CriticalSectionGuard guard;
	// guard against timer firing mid check
	if ((midiEngine.anythingInOutputBuffer() || anyGateOutputPending) && !deluge_midi_gate_timer_pending()) {

		// I don't think this actually could still get left at -1, but just in case...
		if (timeWithinWindowAtWhichMIDIOrGateOccurs == -1) {
			timeWithinWindowAtWhichMIDIOrGateOccurs = 0;
		}

		// The event occurs `offsetInBlock + timeWithinWindow` frames into the
		// render block the BSP is currently flushing; the BSP knows when the
		// DAC will play that frame.
		uint32_t blockElapsed;
		int32_t samplesTilMIDIOrGate = (int32_t)deluge_audio_frames_until_block_offset(
		    offsetInBlock + (uint32_t)timeWithinWindowAtWhichMIDIOrGateOccurs, &blockElapsed);

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

				// Real time elapsed since this sub-window's timeline anchor
				// (audioSampleTimer): the block's played frames minus the
				// sub-window's offset into the block.
				int32_t windowElapsed =
				    (int32_t)(blockElapsed & (SSI_TX_BUFFER_NUM_SAMPLES - 1)) - (int32_t)offsetInBlock;
				if (windowElapsed > 0) {
					samplesTilAllowedToSend -= windowElapsed;
				}

				if (samplesTilMIDIOrGate < samplesTilAllowedToSend) {
					samplesTilMIDIOrGate = samplesTilAllowedToSend;
				}
			}
		}

		deluge_midi_gate_timer_arm((uint32_t)samplesTilMIDIOrGate);
	}
}

void routine_task() {
	// An offline export owns the rendering: it drives routine() itself, as fast as it can, and the
	// block count per call is what decides how rendering interleaves with the cluster loader and the
	// recorder's write-out. A second, scheduler-paced caller rendering into the same recorder changes
	// that interleaving and so changes the exported audio. There is also nothing for this task to do -
	// an offline render is by definition not paced by the real-time clock.
	if (stemExport.renderingOffline()) {
		ignoreForStats();
		return;
	}

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

#if MEM_GUARD
	// Periodically validate the whole heap from a safe point. audioRoutineLocked guards against re-entry, and
	// checkEverythingOk additionally bails if an alloc/dealloc is in flight (lock held), so the boundary tags are
	// stable while we walk.
	static uint32_t memGuardWalkCountdown = MEM_GUARD_WALK_INTERVAL;
	if (--memGuardWalkCountdown == 0) {
		memGuardWalkCountdown = MEM_GUARD_WALK_INTERVAL;
		GeneralMemoryAllocator::get().checkEverythingOk("periodic walk");
	}
#endif

	numRoutines = 0;
	if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
		// The BSP flushes previously-rendered audio toward the DAC and calls
		// deluge_app_render() for as many new frames as its pacing policy asks.
		uint32_t renders = deluge_audio_drive();
		if (renders == 0 && calledFromScheduler) {
			ignoreForStats();
		}
	}
	else {
		if (!isSDRoutineActive()) {
			// An offline render is by definition NOT paced by real time, so the work done per
			// audioRoutine call must never be bounded by the wall clock: the block count per call
			// must not depend on host speed, host load, or how long the work inside each iteration
			// happens to take. That count decides how rendering interleaves with the cluster loader,
			// the SD routine and the recorder's cluster write-out, which decides eviction and
			// underrun behaviour, which changes the rendered audio -- fragile enough that adding a
			// bare fprintf to an unrelated translation unit could tip a call's block count and flip a
			// fixture's rendered payload.
			//
			// So the batch is bounded by a fixed block count. Do not reintroduce any time-based bound
			// here (the real-time branch above is the one that legitimately paces off the clock).
			//
			// The batch is still a batch - the point of looping is to amortise the surrounding
			// machinery (the loader pump and the slow/recorder routines run between audioRoutine
			// calls, see StemExport::renderWait) rather than pay it per 32 frames. The size is one
			// output-buffer quantum, SSI_TX_BUFFER_NUM_SAMPLES frames: the same amount of audio the
			// real-time path produces per driver pass, so the offline cadence matches the one the rest
			// of the engine is built around - and now it is identical on every host and on the device.
			constexpr size_t kOfflineFramesPerBlock = 32;
			constexpr int32_t kOfflineBlocksPerRoutine = SSI_TX_BUFFER_NUM_SAMPLES / kOfflineFramesPerBlock;
			for (int32_t block = 0; block < kOfflineBlocksPerRoutine; ++block) {
				size_t numSamples = kOfflineFramesPerBlock;
				tickSongFinalizeWindows(numSamples);

				numSamplesLastTime = numSamples;
				renderAudioForStemExport(numSamples);
				sideChainHitPending = 0;
				audioSampleTimer += numSamples;
				// Drains the just-rendered block to any OUTPUT-mode recorder
				// and keeps the DAC fed (deluge_app_render's offline path).
				deluge_audio_drive();

				// Write out the recorder's completed clusters so they can't be stolen. There's no recorder at all
				// between stems, and one that has hit a card error or been aborted (RAM exhaustion) will never
				// advance firstUnwrittenClusterIndex again, because cardRoutine() early-returns in both cases -
				// so bail on those, and bound the loop regardless rather than trusting that list to be complete.
				SampleRecorder* recorder = audioRecorder.recorder;
				if (recorder != nullptr) {
					// Each call writes at most one cluster, and we don't render (so can't produce new ones) until
					// the drain finishes. The slack covers calls that return without reaching this recorder at all:
					// doRecorderCardRoutines() abandons its traversal whenever a recorder was recently created.
					int32_t callsRemaining =
					    recorder->currentRecordClusterIndex - recorder->firstUnwrittenClusterIndex + 8;

					while (callsRemaining-- > 0) {
						if (recorder->hadCardError || recorder->status >= RecorderStatus::COMPLETE
						    || recorder->firstUnwrittenClusterIndex >= recorder->currentRecordClusterIndex) {
							break;
						}

						doRecorderCardRoutines();

						// doRecorderCardRoutines() can finish and free recorders - don't trust the pointer after it
						if (audioRecorder.recorder != recorder) {
							break;
						}
					}
				}
			}
		}
	}
	audioRoutineLocked = false;
	routineBeenCalled = true;
}

int32_t getNumSamplesLeftToOutputFromPreviousRender() {
	return renderingBufferOutputEnd - renderingBufferOutputPos;
}

// Independent white-noise PRNG for the master output dither — deliberately separate from the synth-wide
// getNoise()/jcong stream (see the dither call site below). Same LCG recurrence as CONG, so the dither's
// statistics are unchanged; only its state is private, so dithering can't perturb synth randomness.
[[gnu::always_inline]] inline int32_t outputDither() {
	static uint32_t state = 380116160;
	state = 69069 * state + 1234567;
	return (int32_t)state;
}

// Drain up to `frames - produced` already-rendered samples through the output
// stage — master volume + dither (+ input monitoring) — into the BSP's `out`
// block at `produced`, feeding SampleRecorders along the way and advancing the
// input-ring cursor in lockstep. During offline stem export the DAC gets the
// 0/1 keep-alive pattern instead (it freaks out on sustained zeros) while the
// real post-gain samples still reach OUTPUT-mode recorders. Returns the number
// of samples produced.
static uint32_t outputStage(DelugeStereoSample* out, uint32_t produced, uint32_t frames) {

	// Copy to the output block, and apply heaps of gain too, with clipping
	uint32_t numSamplesOutputted = 0;
	uint32_t maxSamples = frames - produced;

	std::span<StereoSample> outputBufferForResampling{reinterpret_cast<StereoSample*>(spareRenderingBuffer), 128 * 2};
	StereoSample* __restrict__ renderingBufferOutputPosNow = renderingBufferOutputPos;
	int32_t* __restrict__ inputReadPos = (int32_t*)inputRingPos;

	bool outputRealAudio = (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline));

	while (renderingBufferOutputPosNow != renderingBufferOutputEnd && numSamplesOutputted < maxSamples) {

		// Here we're going to do something equivalent to a multiply_32x32_rshift32(), but add dithering.
		// Because dithering is good, and also because the annoying codec chip freaks out if it sees the same 0s for
		// too long. The dither draws from its OWN PRNG stream (outputDither), NOT the synth-wide getNoise()/jcong:
		// the output stage runs once per output sample — including a load-/timing-dependent number of keep-alive
		// samples — so sharing the stream let that count perturb every synth random draw, making the offline render
		// depend on heap layout (and coupling musical randomness to output-sample count). See
		// docs/dev/overread_hunt.md.
		int64_t lAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->l * (int64_t)masterVolumeAdjustmentL + (int64_t)outputDither();
		int64_t rAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->r * (int64_t)masterVolumeAdjustmentR + (int64_t)outputDither();

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
			if (inputReadPos >= inputRingEnd()) {
				inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		}

		outputBufferForResampling[numSamplesOutputted].l = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(lAdjusted);
		outputBufferForResampling[numSamplesOutputted].r = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(rAdjusted);
		if (outputRealAudio) {
			out[produced + numSamplesOutputted].l = outputBufferForResampling[numSamplesOutputted].l;
			out[produced + numSamplesOutputted].r = outputBufferForResampling[numSamplesOutputted].r;
		}
		// otherwise output 0 or 1 for silence, the DAC doesn't like all zeroes
		else {
			out[produced + numSamplesOutputted].l = (int32_t)(numSamplesOutputted % 2);
			out[produced + numSamplesOutputted].r = (int32_t)(numSamplesOutputted % 2);
		}

		numSamplesOutputted++;
		++renderingBufferOutputPosNow;
	}

	renderingBufferOutputPos = renderingBufferOutputPosNow; // Write back from __restrict__ pointer to permanent pointer

	if (numSamplesOutputted) {

		advanceInputRingPos(numSamplesOutputted);

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
				uintptr_t stopPos =
				    (inputRingPos < (uintptr_t)recorder->sourcePos) ? (uintptr_t)inputRingEnd() : inputRingPos;

				size_t numSamplesFeedingNow =
				    (stopPos - (uintptr_t)recorder->sourcePos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE);

				// We also enforce a firm limit on how much to feed, to keep things sane. Any remaining will get
				// done next time.
				numSamplesFeedingNow = std::min(numSamplesFeedingNow, size_t{256});

				// this is extremely janky, but essentially because feedAudio only works on an interleaved stream of
				// stereo samples, we can offset it one sample to get it to operate on the right channel
				std::span streamToRecord =
				    (recorder->mode == AudioInputChannel::RIGHT)
				        ? std::span{reinterpret_cast<StereoSample*>(recorder->sourcePos + 1), numSamplesFeedingNow}
				        : std::span{reinterpret_cast<StereoSample*>(recorder->sourcePos), numSamplesFeedingNow};

				recorder->feedAudio(streamToRecord);

				recorder->sourcePos += numSamplesFeedingNow << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
				if (recorder->sourcePos >= inputRingEnd()) {
					recorder->sourcePos -= SSI_RX_BUFFER_NUM_SAMPLES << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
				}
			}
		}
	}

	return numSamplesOutputted;
}

void logAudioAction(char const* string, const char* file, int line) {
#if DO_AUDIO_LOG
	if (numAudioLogItems >= AUDIO_LOG_SIZE)
		return;
	audioLogTimes[numAudioLogItems] = getSystemTime();
	strcpy(audioLogStrings[numAudioLogItems], string);
	audioLogFiles[numAudioLogItems] = file;
	audioLogLines[numAudioLogItems] = line;
	numAudioLogItems++;
#endif
}

void dumpAudioLog() {
#if DO_AUDIO_LOG
	double currentTime = getSystemTime();
	uint32_t timePassedUSA = (uint32_t)((currentTime - lastRoutineTime) * 1e6);
	if (timePassedUSA > (2500)) {

		D_PRINTLN("Audio log dump");
		for (int32_t i = 0; i < numAudioLogItems; i++) {
			uint32_t timePassedUS = (uint32_t)((audioLogTimes[i] - lastRoutineTime) * 1e6);
			logDebug(kDebugPrintModeNewlined, audioLogFiles[i], audioLogLines[i], 256, "\t %d us:  %s", timePassedUS,
			         audioLogStrings[i]);
		}

		D_PRINTLN("%d: end", timePassedUSA);
	}
	lastRoutineTime = getSystemTime();
	numAudioLogItems = 0;
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

Error previewSample(std::string_view path, bool shouldActuallySound) {
	stopAnyPreviewing();
	MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].getOrCreateFirstRange();
	if (!range) {
		return Error::INSUFFICIENT_RAM;
	}
	range->sampleHolder.filePath.assign(path);
	// CLUSTER_ENQUEUE, not CLUSTER_LOAD_IMMEDIATELY: the immediate mode reads the covered clusters from
	// the card SYNCHRONOUSLY, measured at 38.2-40.1 ms on device. That stalls the audio path (a 9.8 ms
	// late render, plus four more in the same second), which inflates the engine's measured render time,
	// which makes it declare an overload and force-cull a voice -- the very preview voice the fill was
	// warming. That is the preview-truncation bug; see the 2026-08-15 async-note-on-residency design
	// doc, section 4b.
	//
	// Enqueueing never blocks. The note-on then sees LOADING and defers (VoiceUnisonPartSource), so the
	// preview starts a few ms late instead of either stalling audio or dropping the voice.
	Error error = range->sampleHolder.loadFile(false, true, true, CLUSTER_ENQUEUE, false, /*mayLoadAsync=*/true);

	if (error == Error::LOAD_IN_PROGRESS) {
		// Not a failure and not an outcome: the async loader has this file and has not finished. There
		// is no sample to note-on (sampleHolder.audioFile is still nullptr, and a note-on against it
		// would start a voice with nothing behind it) and nothing to draw. Hand the deferral back
		// UNREPORTED so the caller's own tick can ask again -- see the declaration's contract. Showing
		// a popup here would tell the user a load failed that is still running; swallowing it here
		// would leave the preview permanently silent with no error at all.
		return error;
	}

	if (error != Error::NONE) {
		display->displayError(error); // Rare, shouldn't cause later problems.
	}

	if (shouldActuallySound) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);
		// Set BEFORE the note-on, not after. Needed - Dec 2021 - because SampleBrowser::selectEncoderAction()
		// may have gone a while without an audio routine call, so the first render after this sees a huge
		// apparent load and culls.
		//
		// The ordering matters for a second reason: previewSample runs on the worker fiber while the audio
		// render preempts it on the InterruptExecutor. With the assignment after the call, the preview voice
		// was cull-eligible for the whole duration of its own note-on -- and cullVoices -> terminateOneVoice
		// -> freeActiveVoice() erases it from Sound::voices_ while Sound::noteOn still holds a reference to
		// that element, which is what made the E199 assertion fire on a voice something else had removed.
		//
		// This narrows the window; it does not close the underlying cross-tier race on voices_ (bypassCulling
		// is cleared every render, and the race is reachable from any note-on, not just this one). See
		// docs/superpowers/specs/2026-08-16-voices-cross-tier-race.md.
		bypassCulling = true;
		sampleForPreview->Sound::noteOn(modelStack, &sampleForPreview->arpeggiator, kNoteForDrum, zeroMPEValues);
	}

	return error;
}

void stopAnyPreviewing() {
	sampleForPreview->killAllVoices();
	if (std::ssize(sampleForPreview->sources[0].ranges)) {
		MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(nullptr);
	}
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
	auto threshold = cpuDireness > 12 ? 1 : 4;
	if (voices_started_this_render < threshold) {
		voices_started_this_render += 1;
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

		void* memory = deluge::memory::alloc_fast(size);
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

namespace {
// Coalesced SD-routine dispatch of the recorder card-write drain onto the storage
// owner (the worker fiber on Embassy). SD-routine-class: while a drain is in flight
// the RESOURCE_SD_ROUTINE gate holds off audioRecorder.slowRoutine (discardRecorder),
// so the recorder can't be freed mid-drain. Only ever touched on the main executor.
deluge::storage::Coalescer g_recorder_coalescer{/*sd_routine=*/true};

void recorder_card_routines_fill(void*) {
	doRecorderCardRoutines();
}
} // namespace

void requestRecorderCardRoutines() {
	// Never from an ISR / the audio interrupt-executor: deluge_storage_on_owner() is false there
	// and we'd race the coalescer's main-executor-only state. The recorder drain is never driven
	// from an ISR.
	if (deluge_in_interrupt()) {
		return;
	}
	// Already on the owner (fiber on Embassy; always on legacy/host) — run inline, so a
	// fiber-context caller doesn't re-dispatch onto the fiber it already runs on.
	if (deluge_storage_on_owner()) {
		doRecorderCardRoutines();
		return;
	}
	g_recorder_coalescer.request(recorder_card_routines_fill, nullptr);
}

void slowRoutine() {
	if (isSDRoutineActive()) {
		// can happen if the SD routine is yielding
		return;
	}

	// Re-align the input stream's latency window if an audio glitch (or
	// start-up) made us miss a whole output-ring cycle. Ring geometry is the
	// board's business; skipped input simply never reaches the input ring.
	deluge_audio_input_resync();

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

	// Go through all SampleRecorders, getting them to write etc — via the storage
	// owner (SD-routine-class; inline on legacy/host, coalesced on the fiber on Embassy).
	requestRecorderCardRoutines();
}

// will need to take in the config argument
SampleRecorder* getNewRecorder(int32_t numChannels, AudioRecordingFolder folderID, AudioInputChannel mode,
                               bool keepFirstReasons, bool writeLoopPoints, int32_t buttonPressLatency,
                               bool shouldNormalize, Output* outputRecordingFrom, RecorderConfig config) {
	Error error;

	void* recorderMemory = deluge::memory::alloc_fast(sizeof(SampleRecorder));
	if (!recorderMemory) {
		return nullptr;
	}

	SampleRecorder* newRecorder = new (recorderMemory) SampleRecorder();
	error = newRecorder->setup(numChannels, mode, keepFirstReasons, writeLoopPoints, folderID, buttonPressLatency,
	                           outputRecordingFrom, config);
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
// ...because the recorder may be suspended part-way through its own cardRoutine(), and freeing it there leaves that
// cardRoutine() operating on freed memory before handing it back to doRecorderCardRoutines() to be freed a second
// time. That's a double free, and it surfaces as M123 from the allocator, a long way from here - so rather than
// leaving the rule as a comment for each caller to honour, enforce it.
//
// Diagnostic only, not a hard invariant (see docs/dev/sd_busy_audit.md, audio_engine.cpp): this used
// to FREEZE_WITH_ERROR("E251") here. On Embassy isSDRoutineActive() is hardwired false
// (scheduler.rs:658), so before sd_busy() was backed for real, this check never fired at all -- it was
// exactly as dead as save_song_ui.cpp's E316, not "half-live". sd_busy() is now real, but it answers "is
// ANY FS operation in flight anywhere", not "is THIS recorder's own cardRoutine() suspended mid-transfer"
// -- the two can differ, since FatFS work is serialized but not per-caller-attributed. The precise
// invariant this comment actually wants is tracked per-op on the BSP (fiber.rs's SD_ROUTINE_HELD /
// sd_routine_held(), consumed by the RESOURCE_SD_ROUTINE scheduler gate) but is not yet exported through
// the libdeluge C-ABI -- exporting it is the correct future fix, not a new export sneaked in here. Until
// then, treat a busy signal at this call site as informative, not as proof this recorder is the one
// suspended -- log it rather than freeze a beta build on what may be an unrelated FS operation.
void discardRecorder(SampleRecorder* recorder) {
	if (ALPHA_OR_BETA_VERSION && (isSDRoutineActive() || deluge::sync::sd_busy())) {
		D_PRINTLN("discardRecorder: isSDRoutineActive()/sd_busy() was true on entry (see sd_busy_audit.md)");
	}

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
