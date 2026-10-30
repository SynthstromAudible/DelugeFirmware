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
#include "dsp-next/fixed/vca/pan.h"
#include "model/fx/chain_inputs.h"

#include "chrono"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "deluge_resource.h" // deluge_resource_stats (loader instrumentation)
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/dynamics/hit_strength.h" // combine_hit_strengths
#include "dsp-next/fixed/mix/output_fold.h"
#include "dsp/timestretch/time_stretcher.h"
#include "effects/reverb.h"
#include "effects/sidechain.h"
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
#include "io/midi/midi_queue.h"
#include "io/usb/usb_audio.h"
#include "libdeluge/app.h"
#include "libdeluge/audio_io.h"
#include "libdeluge/clock.h"
#include "libdeluge/file_stream.h"
#include "libdeluge/sample_stream.h" // deluge_streaming_resource_manager()
#include "libdeluge/signals.h"
#include "libdeluge/system.h"
#include "libdeluge/tier.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "memory/stack_guard.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_slot.h"
#include "modulation/patch/patch_cable_set.h"
#include "modulation/sidechain/sidechain.h"
#include "playback/render_requests.h"
#include "processing/audio_output.h"
#include "processing/engines/cv_engine.h"
#include "processing/metronome/metronome.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/load_trace.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "sync/sd_access.h"
#include "synth/modulation/cable_depth.h"
#include "synth/modulation/envelope.h"
#include "synth/sources/input/live/live_input_buffer.h"
#include "util/boot.h"
#include "util/functions.h"
#include "util/misc.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <etl/vector.h>
#include <execution>
#include <iterator>
#include <libdeluge/scheduler.h>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <ranges>

// Last, after every other header: these declare namespace deluge::util, which would shadow the global ::util for
// the headers above that name util:: from inside namespace deluge.
#include "deluge/util/retire_list.h"
#include "deluge/util/spsc_ring.h"
#include "processing/engines/render_domain.h"

namespace params = deluge::modulation::params;

using namespace deluge;
using deluge::synth::LiveInputBuffer;

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

// Retirement: how the interaction tier hands the render an object it has stopped using (a deleted output, drum, clip or
// whole song), so that the render lets go of it before its memory is freed. See AudioEngine::retire().
namespace {
deluge::util::SnapshotDomain renderDomain_;
deluge::RetireList retireList{renderDomain_};

/// What the render must let go of before a retired object may be destroyed.
struct RetireRequest {
	Output* output = nullptr;  ///< a retired output (a SoundInstrument is a Sound; a Kit owns SoundDrums)
	SoundDrum* drum = nullptr; ///< a retired drum
	Clip* clip = nullptr;      ///< a retired clip
	Song* song = nullptr;      ///< a retired song, no longer currentSong
	uint64_t sequence = 0;     ///< its place in the order requests reach the render (see retireSequenceLetGo)
};
deluge::util::SpscRing<RetireRequest, 64> retireRing;
std::vector<RetireRequest> retireOverflow; ///< [interaction] requests the ring had no room for, re-pushed at reclaim
/// [interaction] The sequence of the last request handed over. Requests reach the render in this order (the overflow
/// list is only ever appended to while it is non-empty, and re-pushed from its front), so "every request up to N has
/// been let go of" is one number.
uint64_t retireSequencePushed = 0;
/// [render] -> [interaction] The highest sequence the render has let go of. Release-stored by the render after its
/// drain, acquire-loaded by reclaim: an entry whose sequence is at or below it has been let go of, and every effect of
/// that let-go is visible. An entry is freed once this covers it AND two blocks have begun since its stamp.
std::atomic<uint64_t> retireSequenceLetGo{0};
/// How long AudioEngine::awaitRetired() waits, in getSystemTime() seconds, before it freezes (E459). A song of many
/// outputs is let go of in one block and freed two blocks later -- a few milliseconds; this is far past that.
#if defined(__SANITIZE_THREAD__)
#define DELUGE_UNDER_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define DELUGE_UNDER_TSAN 1
#endif
#endif
#if defined(DELUGE_UNDER_TSAN)
// Under ThreadSanitizer a render block runs many times slower, and 250 ms was measured to run out on a genuine,
// completing wait (2 of 40 preemptive-race-tsan runs). The block bound below still catches a render that stops.
constexpr double kAwaitRetiredTimeoutSeconds = 5.0;
#else
constexpr double kAwaitRetiredTimeoutSeconds = 0.25;
#endif
/// How many render blocks AudioEngine::awaitRetired() waits for before it freezes (E459), whatever the clock says.
/// A retired object is let go of at the next block and freed two blocks after its stamp, so 2-3 blocks is normal.
/// Needed where getSystemTime() does not move inside a synchronous wait (the virtual-time host harnesses, which run
/// the render inline). 1000 is over 300x the normal count; on the device, where a block is ~2.9 ms, it is ~2.9 s, so
/// the 250 ms time bound always fires first there.
constexpr int32_t kAwaitRetiredMaxBlocks = 1000;

/// [render] End a Sound's voices and take it out of the registry: after this no walk can reach it.
void letGoOfSound(Sound& sound) {
	sound.killAllVoices();
	AudioEngine::sounds.remove(&sound);
}

/// [interaction] Stop the view's active ModControllable model stack naming @p clip. Dereferences nothing.
void clearViewIfClip(Clip const* clip) {
	if (view.activeModControllableModelStack.getTimelineCounterAllowNull() == clip) {
		view.releaseActiveModControllable();
	}
}

/// [render] Drop every reference the render keeps from block to block to a retired clip. The caller already unlinked
/// it from its song's arrays, clip instances and output (Clip::prepareForDestruction()), and AudioEngine::retire(Clip*)
/// already cleared the ones the interaction tier writes (currentClip, syncScalingClip, the view's active stack). This
/// is the catch for what the render itself may have written between the retire and this let-go: another clip's
/// beingRecordedFromClip and syncScalingClip (the recording ticks), and currentClip (session.cpp's new-recording
/// hand-over). Only the current song is looked at: the render reaches clips through currentSong alone, and a song that
/// is no longer current takes its references with it. The view's active stack needs no catch: only the interaction
/// tier writes it, and retire() applied the render's requests for it before clearing it.
void letGoOfClip(Clip* clip) {
	if (currentSong != nullptr) {
		if (currentSong->getCurrentClip() == clip) {
			currentSong->setCurrentClip(nullptr);
		}
		if (currentSong->live.sync_scaling_clip == clip) {
			currentSong->live.sync_scaling_clip = nullptr;
		}
		for (ClipArray* array :
		     {&currentSong->live.clips.session_clips, &currentSong->live.clips.arrangement_only_clips}) {
			for (Clip* other : *array) {
				if (other->beingRecordedFromClip == clip) {
					other->beingRecordedFromClip = nullptr;
				}
			}
		}
	}
}

/// [render] Let go of a retired song: end the voices of, and deregister, every Sound of every output it owns (its
/// main and hibernating lists, as Song::deleteAllOutputs() walks them). AudioEngine::retire(Song*) already dropped the
/// view's reference to it on the interaction tier, the only tier that writes it. The song is no longer current, so
/// the render reaches none of its clips through currentSong; its own currentClip, syncScalingClip and
/// beingRecordedFromClip fields are left alone -- they may name clips already freed.
void letGoOfSong(Song* song) {
	// A song can hold hundreds of Sounds (every drum of every kit), and this runs inside one block. Taking each out of
	// the registry on its own would scan the registry once per Sound; mark them instead, then take every marked one
	// out in a single pass.
	bool anyMarked = false;
	auto mark = [&anyMarked](Sound& sound) {
		sound.killAllVoices();
		sound.letGoWithSong = true;
		anyMarked = true;
	};
	for (Output* first :
	     {song->live.media.first_output, static_cast<Output*>(song->live.media.first_hibernating_instrument)}) {
		for (Output* output = first; output != nullptr; output = output->next) {
			if (output->type == OutputType::SYNTH) {
				mark(*static_cast<SoundInstrument*>(output));
			}
			else if (output->type == OutputType::KIT) {
				auto* kit = static_cast<Kit*>(output);
				for (Drum* d = kit->firstDrum; d != nullptr; d = d->next) {
					if (d->type == DrumType::SOUND) {
						mark(*static_cast<SoundDrum*>(d));
						// Out of the kit's rendering list too: ending the voices does not take it out (a drum leaves
						// the list only when its render finds it silent), and ~Kit destroys it at reclaim.
						kit->renderingDrums.remove(static_cast<SoundDrum*>(d));
					}
				}
			}
		}
	}
	if (anyMarked) {
		AudioEngine::sounds.remove_if([](Sound const* sound) { return sound->letGoWithSong; });
	}
}

/// [render] Let go of everything a retired object owns that the render can reach.
void letGo(const RetireRequest& r) {
	if (r.song != nullptr) {
		letGoOfSong(r.song);
	}
	if (r.clip != nullptr) {
		letGoOfClip(r.clip);
	}
	if (r.drum != nullptr) {
		letGoOfSound(*r.drum);
	}
	if (r.output != nullptr) {
		if (r.output->type == OutputType::SYNTH) {
			letGoOfSound(*static_cast<SoundInstrument*>(r.output));
		}
		else if (r.output->type == OutputType::KIT) {
			auto* kit = static_cast<Kit*>(r.output);
			for (Drum* d = kit->firstDrum; d != nullptr; d = d->next) {
				if (d->type == DrumType::SOUND) {
					letGoOfSound(*static_cast<SoundDrum*>(d));
					// As for a song's kits in letGoOfSong(): out of the rendering list before ~Kit destroys it.
					kit->renderingDrums.remove(static_cast<SoundDrum*>(d));
				}
			}
		}
	}
}

/// [interaction] Retired objects destroyed while the render could still reach them: a Sound still in the registry
/// when its destroy ran, i.e. before the render let go of it. Must stay 0; see AudioEngine::retireViolations().
std::size_t retireViolations_ = 0;

/// [interaction] Whether the render can still reach @p sound through the Sound registry.
bool stillRegistered(Sound* sound) {
	return AudioEngine::sounds.walk([sound](const auto& all) { return std::ranges::find(all, sound) != all.end(); });
}

/// [interaction] Count a destroy that runs before the render let go of what it destroys.
void checkLetGoBeforeDestroy(Output* output, SoundDrum* drum) {
	bool reachable = false;
	if (drum != nullptr) {
		reachable = stillRegistered(drum);
	}
	else if (output->type == OutputType::SYNTH) {
		reachable = stillRegistered(static_cast<SoundInstrument*>(output));
	}
	else if (output->type == OutputType::KIT) {
		for (Drum* d = static_cast<Kit*>(output)->firstDrum; d != nullptr && !reachable; d = d->next) {
			reachable = d->type == DrumType::SOUND && stillRegistered(static_cast<SoundDrum*>(d));
		}
	}
	if (reachable) {
		retireViolations_++;
	}
}

void pushOrOverflow(const RetireRequest& r) {
	// Behind anything already waiting in the overflow, so requests reach the render in sequence order.
	if (!retireOverflow.empty() || !retireRing.try_push(r)) {
		retireOverflow.push_back(r);
	}
}

/// Hand a retired object over: the render lets go of it (ends voices, deregisters Sounds), then it is destroyed by the
/// first reclaim after that let-go once two blocks have begun since the handover. Two cases, decided once from a single
/// epoch read:
///  - the render has never run (epoch 0, boot): nothing can reach it; let go inline and destroy now;
///  - otherwise: the render lets go at the top of its next block (the ring); destroy later.
///
/// Only the interaction tier may retire (E455 otherwise). The ring is single-producer, and the retire list, the
/// overflow list and reclaimRetired() are single-threaded on the interaction side. A retire from the render itself
/// (the song swap, if it ever runs there) would need its own render-to-interaction handover of the destroy; that is
/// left to the Song/Clip deletion work, and until then it freezes rather than corrupt the list.
void handOver(RetireRequest r, void* object, void (*destroy)(void*)) {
	if (deluge_tier_current() != DELUGE_TIER_INTERACTION) {
		FREEZE_WITH_ERROR("E455");
	}
	const bool renderHasRun = renderDomain_.audio_epoch() != 0;
	if (!renderHasRun) {
		letGo(r);
		destroy(object);
		return;
	}
	r.sequence = ++retireSequencePushed;
	pushOrOverflow(r);
	retireList.retire(object, destroy, r.sequence);
	deluge::runtime::appLoop.request(AudioEngine::reclaimJob);
}

/// [render] The render's block-start lifetime step: advance the epoch, then let go of everything retired since the
/// last block -- before any walk of this block can reach it, and publish how far it got. The epoch's and the let-go
/// sequence's release stores are what let reclaim free an object. Run by every block the render starts, and by a block
/// a host harness skips rendering (deluge_app_render_skipped), so retirement completes whether or not audio is
/// produced.
#ifdef DELUGE_HOST
/// [render] -> [interaction] The longest block-top let-go (the drain below) since the last reset, in host wall-clock
/// microseconds. A host-only measurement; see AudioEngine::retireDrainMicrosMax().
std::atomic<uint32_t> retireDrainMicrosMax_{0};
#endif

void renderRetirementStep() {
	renderDomain_.advance_epoch();
#ifdef DELUGE_HOST
	const auto drainStart = std::chrono::steady_clock::now();
#endif
	uint64_t lastLetGo = 0;
	const std::size_t drained = retireRing.drain(
	    [&lastLetGo](RetireRequest&& r) {
		    letGo(r);
		    lastLetGo = r.sequence;
	    },
	    16);
	if (drained != 0) {
		retireSequenceLetGo.store(lastLetGo, std::memory_order_release);
	}
#ifdef DELUGE_HOST
	const auto micros = static_cast<uint32_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - drainStart).count());
	if (micros > retireDrainMicrosMax_.load(std::memory_order_relaxed)) {
		retireDrainMicrosMax_.store(micros, std::memory_order_relaxed);
	}
#endif
}
} // namespace

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

deluge::effects::Reverb reverb{};
deluge::effects::SidechainState reverbSidechainSettings = deluge::modulation::default_sidechain_state();
int32_t reverbSidechainVolume;
int32_t reverbSidechainShape;
int32_t reverbPan = 0;

bool mustUpdateReverbParamsBeforeNextRender = false;

int32_t sideChainHitPending = false;

uint32_t timeLastSideChainHit = 2147483648;
int32_t sizeLastSideChainHit;

Metronome metronome{};
deluge::dsp::fp::StereoLevel approxRMSLevel{};
deluge::dsp::fp::LevelDetector envelopeFollower{};
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

deluge::audio::InputMonitor inputMonitor{};
bool usbInputMonitoredByTrack = false;
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

deluge::AudioOwnedList<Sound, deluge::fast_vector<Sound*>> sounds;
std::atomic<bool> renderDrivenByTier{false};

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

	((PatchCableSet*)modelStackWithParamCollection->paramCollection)
	    ->setupPatching(*static_cast<Sound*>(modelStackWithParamCollection->modControllable),
	                    *modelStackWithParamCollection->paramManager);
	sampleForPreview->patcher.performInitialPatching(
	    sampleForPreview->patchControlBlock(*paramManagerForSamplePreview, Sound::PresetGather::ALL));

	sampleForPreview->setSideChainSendLevel(2147483647); // DOM-owned (an unbound preview sets the cache only)

	deluge_audio_start();
	inputRingPos = (uintptr_t)inputRing;

	VoicePool::get().repopulate();
	VoiceSamplePool::get().repopulate();
	TimeStretcherPool::get().repopulate();
}

void killAllVoices(bool deletingSong) {
	sounds.walk([](const auto& all) {
		for (Sound* sound : all) {
			sound->killAllVoices();
		}
	});

	// Because we unfortunately don't have a master list of VoiceSamples or actively sounding AudioClips,
	// we have to unassign all of those by going through all AudioOutputs.
	// But if there's no currentSong, that's fine - it's already been deleted, and this has already been called for it
	// before then.
	if (currentSong != nullptr) {
		for (Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
			if (output->type == OutputType::AUDIO) {
				static_cast<AudioOutput*>(output)->cutAllSound();
			}
		}
	}
}

void songSwapAboutToHappen() {
	// Otherwise, a timer might get called and try to access Clips that we may have deleted below
	uiTimerManager.unsetTimer(TimerName::PLAY_ENABLE_FLASH);
	// Before the deletion, so the render's reverb update never walks the outputs while they go.
	currentSong->soundsDeletedForSwap.store(true, std::memory_order_release);
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
/// same render. Will stop an audio clip if there are no voices to kill
void killOneVoice(size_t num_samples) {
	logAction("kill");
	sounds.walk([&](const auto& sounds) {
		auto lowest_priority_voices = sounds //<
		                              | std::views::filter(&Sound::hasActiveVoices)
		                              | std::views::transform(&Sound::getLowestPriorityVoice);
		auto it = std::ranges::max_element(lowest_priority_voices, [](const auto& a, const auto& b) {
			return a->priority_rating() < b->priority_rating();
		});
		// are there any active voices to kill?
		if (it == lowest_priority_voices.end()) {
			// no active voices found, try to cull an audio clip instead
			if (currentSong) {
				currentSong->cullAudioClipVoice();
			}
			// return early since there are no active voices to kill
			return;
		}

		const Sound::ActiveVoice& voice = *it;

		bool still_rendering = voice->do_immediate_release();
		if (!still_rendering) {
			voice->sound.freeActiveVoice(voice);
		}

		D_PRINTLN("killed 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", num_samples,
		          getNumVoices(), getNumAudio());
	});
}

/// Force a voice to release very quickly - will be almost instant but not click
void terminateOneVoice(size_t numSamples) {
	logAction("terminate");
	sounds.walk([&](const auto& sounds) {
		auto all_voices = sounds | std::views::transform(&Sound::voices) | std::views::join;

		if (all_voices.empty()) {
			return;
		}

		// The skip filter must apply to the first voice too (see Sound::terminateOneActiveVoice / issue #4721):
		// seeding `best` unfiltered let an already-fast-releasing front voice absorb every cull in the window.
		const Sound::ActiveVoice* best = nullptr;
		for (const auto& voice : all_voices) {
			if (voice->kernel.isCullFading()) {
				continue;
			}
			if (best == nullptr || (*best)->priority_rating() < voice->priority_rating()) {
				best = &voice;
			}
		}

		if (best == nullptr) {
			return;
		}

		const Sound::ActiveVoice& voice = *best;
		bool still_rendering = voice->do_fast_release(2 * SOFT_CULL_INCREMENT);
		if (!still_rendering) {
			voice->sound.freeActiveVoice(voice);
		}

		D_PRINTLN("terminated 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", numSamples,
		          getNumVoices(), getNumAudio());
	});
}

/// Force a voice to release, or speed up its release if the oldest voice is already releasing
void forceReleaseOneVoice(size_t num_samples) {
	logAction("force release");
	sounds.walk([&](const auto& sounds) {
		auto all_voices = sounds | std::views::transform(&Sound::voices) | std::views::join;
		if (all_voices.empty()) {
			return;
		}

		// Don't spend another soft cull on a voice that's already disappearing at the cull fade rate. FAST_RELEASE has
		// a high priority rating, so without this filter repeated soft culls can chase the same fading voice while long
		// musical-release tails keep stacking up.
		const Sound::ActiveVoice* best = nullptr;
		for (const auto& voice : all_voices) {
			if (voice->kernel.isCullFading()) {
				continue;
			}
			if (best == nullptr || (*best)->priority_rating() < voice->priority_rating()) {
				best = &voice;
			}
		}

		if (best == nullptr) {
			return;
		}

		const Sound::ActiveVoice& voice = *best;

		auto stage = voice->kernel.envelopes[0].stage();
		if (stage < Envelope::Stage::FAST_RELEASE) {
			D_PRINTLN("force released 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d", num_samples,
			          getNumVoices(), getNumAudio());
		}
		else {
			D_PRINTLN("sped up release for 1 voice.  numSamples:  %d. Voices left: %d. Audio clips left: %d",
			          num_samples, getNumVoices(), getNumAudio());
		}

		bool still_rendering = voice->speed_up_release();
		if (!still_rendering) {
			voice->sound.freeActiveVoice(voice);
		}
	});
}

int32_t getNumAudio() {
	return currentSong != nullptr ? currentSong->countAudioVoices() : 0;
}

int32_t getNumVoices() {
	return sounds.walk([](const auto& all) {
		return std::transform_reduce(all.cbegin(), all.cend(), 0, std::plus{},
		                             [](auto sound) { return sound->voices().size(); });
	});
}

void routineWithClusterLoading() {
	logAction("AudioDriver::routineWithClusterLoading");
	DELUGE_LOAD_TRACE("routine");

	// In-memory deserialization (FileReader::callRoutines == false) opts out of the cooperative
	// audio yield: there is no SD cluster stream to service and no engine to pump. File-backed
	// reads keep callRoutines == true. This also lets a headless host harness load a Song without
	// standing up the audio engine.
	if (activeReader != nullptr && !activeReader->shouldCallRoutines()) {
		return;
	}

	routineBeenCalled = false;

	if (!routineBeenCalled) {
		// bypassCulling = true; // yolo? Sean: not sure if this is necessary

		logAction("call runRoutine() from routineWithClusterLoading()");
		runRoutine();
	}
}

void runRoutine() {
	// Once the audio tier drives the render it runs on its own cadence and preempts this caller, and
	// rendering here too would run two renders at once. Before that, during boot, render here.
	if (!renderDrivenByTier.load(std::memory_order_acquire)) {
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
	static int32_t last_num_samples_over = 0;
	int32_t num_samples_over_limit = numSamples - max_num_samples;

	if (numAudio + numVoice > MIN_VOICES) {

		// If it's real dire, do a proper immediate cull
		if (num_samples_over_limit >= 32) {

			int32_t num_to_cull = (num_samples_over_limit >> 4);
			// leave at least 7 - below this point culling won't save us
			// if they can't load their sample in time they'll stop the same way anyway
			num_to_cull = std::min(num_to_cull, numAudio + numVoice - MIN_VOICES);

			// soft cull one voice, then terminate half the remainder and kill the other half
			auto num_to_hard_cull = num_to_cull - 1;
			auto num_to_kill = num_to_hard_cull / 2;
			auto num_to_terminate = num_to_hard_cull - num_to_kill;

			D_PRINTLN("Culling %d voices - 1 force, %d  terminate, %d kill", num_to_cull, num_to_terminate,
			          num_to_kill);
			for (int32_t i = 0; i < num_to_kill; i++) {
				// cull with immediate release
				killOneVoice(numSamples);
			}
			for (int32_t i = 0; i < num_to_terminate; i++) {
				// cull with fast release
				terminateOneVoice(numSamples);
			}
			// soft cull
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
				if (numRoutines > 0) {
					culled = true;
					D_PRINTLN("culling in second routine");
				}
			}
		}
	}
	else {
		// Cull anyway if things are bad
		if (num_samples_over_limit >= 40) {
			D_PRINTLN("under min voices but culling anyway");
			forceReleaseOneVoice(numSamples);
			culled = true;
		}
	}
	last_num_samples_over = num_samples_over_limit;

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
	auto dspTime = (int32_t)(deluge_audio_render_time_average() * 44100.);
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
		auto numAudio = currentSong ? currentSong->countAudioVoices() : 0;
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

// MIDI / gate output scheduling. Gates changed while finalizing a render window are captured along with their sample
// offset within the window, then given an output time once rendering is done. MIDI is queued by the MidiEngine with
// its due time already worked out from where the playhead was at the start of the window (see
// setMIDIDueAtWindowOffset()). Output times are on the deluge_clock_now() clock: the BSP says how many samples until
// the playhead plays the start of each window, and the MIDI/gate output timer sends only what's due each time it
// fires, then arms itself for anything else.
namespace {
struct WindowOutput {
	int32_t offset; ///< samples from the start of the render window
	GateOutputs gates;
};

struct ScheduledOutput {
	OutputTime due;
	GateOutputs gates;
};

constexpr size_t kMaxWindowOutputs = 8;
constexpr size_t kMaxScheduledOutputs = 16;

etl::vector<WindowOutput, kMaxWindowOutputs> windowOutputs;

// Sorted by due time. Shared with the timer's interrupt, so only touched under a CriticalSectionGuard - the functions
// using it take one to enforce that
etl::vector<ScheduledOutput, kMaxScheduledOutputs> scheduledOutputs;
// Something due couldn't be output yet - DIN's FIFO had no room for the next MIDI message, or gates are being held
// until their CV reaches the DAC - so the timer's been armed to try again. Same access rules as scheduledOutputs
bool outputIncomplete = false;

// Anything due within a sample of now gets sent rather than arming the timer for it
constexpr int32_t kOutputTimeTolerance = kMicrosPerSampleQ8 >> 8;
// If something due couldn't be output, try again after 0.1ms
constexpr int32_t kOutputRetryMicros = 100;

// The time the current render window began rendering, and how many samples from then until the playhead plays its
// first sample - together these give the time each sample in the window will be played
OutputTime windowStart = 0;
int32_t windowSamplesUntilPlays = 0;

/// MIDI sent from now on is due when the playhead reaches this offset in the current render window
void setMIDIDueAtWindowOffset(int32_t offset) {
	midiEngine.setMIDIDue(outputTimeAtWindowOffset(windowStart, windowSamplesUntilPlays, offset));
}

/// A render window begins `offsetInBlock` samples into the BSP's block: note when its first sample will play
void startWindow(uint32_t offsetInBlock) {
	windowStart = outputTimeNow();
	windowSamplesUntilPlays = static_cast<int32_t>(deluge_audio_frames_until_block_offset(offsetInBlock, nullptr));
	// From here on, MIDI is due when the playhead reaches the start of this window, until a MIDI tick in the window
	// moves that along
	setMIDIDueAtWindowOffset(0);
}

void mergeGateOutputs(GateOutputs& into, const GateOutputs& from) {
	into.levels = (into.levels & ~from.mask) | from.levels;
	into.cvWaitMask = (into.cvWaitMask & ~from.mask) | from.cvWaitMask;
	into.mask |= from.mask;
	into.needsMinOffTime |= from.needsMinOffTime;
}

void removeGateOutputs(GateOutputs& gates, uint8_t gateMask) {
	gates.mask &= ~gateMask;
	gates.levels &= ~gateMask;
	gates.cvWaitMask &= ~gateMask;
}

/// Take any gates changed since the last call, to be output at this offset in the window
void captureWindowOutputs(int32_t offset) {
	if (offset < 0) {
		D_PRINTLN("negative offset");
	}
	GateOutputs gates = cvEngine.takePendingGateOutputs(true);
	if (gates.mask == 0) {
		return;
	}

	auto output = std::find_if(windowOutputs.begin(), windowOutputs.end(),
	                           [offset](const WindowOutput& o) { return o.offset == offset; });
	if (output == windowOutputs.end()) {
		if (!windowOutputs.full()) {
			windowOutputs.push_back(WindowOutput{.offset = offset, .gates = {}});
		}
		// Otherwise (shouldn't happen) merge into the latest one rather than losing it
		output = &windowOutputs.back();
	}
	mergeGateOutputs(output->gates, gates);
}

/// Send everything which is due
void sendDueOutputs(OutputTime now, const CriticalSectionGuard& guard) {
	cvEngine.pollCVSent(guard);

	auto firstNotDue = scheduledOutputs.begin();
	while (firstNotDue != scheduledOutputs.end()
	       && static_cast<int32_t>(firstNotDue->due - now) <= kOutputTimeTolerance) {
		cvEngine.outputGates(firstNotDue->gates, guard);
		firstNotDue++;
	}
	scheduledOutputs.erase(scheduledOutputs.begin(), firstNotDue);

	bool midiBlocked = midiEngine.flushDueMIDI(now + kOutputTimeTolerance, guard);
	outputIncomplete = midiBlocked || cvEngine.anyGatesAwaitingCV();
}

/// Arm the timer for the earliest scheduled output, queued MIDI or retry, or stop it if there's nothing left
void armMidiGateOutputTimer(OutputTime now, const CriticalSectionGuard& guard) {
	OutputTime midiDue = 0;
	bool midiQueued = midiEngine.nextMIDIDue(midiDue, guard);
	if (scheduledOutputs.empty() && !midiQueued && !outputIncomplete) {
		deluge_midi_gate_timer_cancel();
		return;
	}
	int32_t microsTilDue = outputIncomplete ? kOutputRetryMicros : INT32_MAX;
	if (!scheduledOutputs.empty()) {
		microsTilDue = std::min(microsTilDue, static_cast<int32_t>(scheduledOutputs[0].due - now));
	}
	if (midiQueued) {
		microsTilDue = std::min(microsTilDue, static_cast<int32_t>(midiDue - now));
	}
	// A deadline further away than the timer can count just fires early, sends nothing and arms again
	deluge_midi_gate_timer_arm_at(deluge_clock_now() + std::max<int32_t>(microsTilDue, 0));
}

void insertScheduledOutput(const ScheduledOutput& output, OutputTime now, const CriticalSectionGuard& guard) {
	if (scheduledOutputs.full()) [[unlikely]] {
		// Shouldn't happen - but if it does, send the earliest one now to make room
		scheduledOutputs.front().due = now;
		sendDueOutputs(now, guard);
	}
	// After any already scheduled for the same time, so they keep their order
	auto position = std::upper_bound(
	    scheduledOutputs.begin(), scheduledOutputs.end(), output,
	    [](const ScheduledOutput& a, const ScheduledOutput& b) { return static_cast<int32_t>(a.due - b.due) < 0; });
	scheduledOutputs.insert(position, output);
}

/// When not rendering in real time there's no playhead to schedule against, so just send everything now
void sendWindowOutputsNow() {
	CriticalSectionGuard guard;
	cvEngine.pollCVSent(guard);
	for (const WindowOutput& output : windowOutputs) {
		cvEngine.outputGates(output.gates, guard);
	}
	windowOutputs.clear();
	midiEngine.flushAllQueuedMIDI(guard);
}
} // namespace

void cancelScheduledGates(uint8_t gateMask, const CriticalSectionGuard&) {
	for (WindowOutput& output : windowOutputs) {
		removeGateOutputs(output.gates, gateMask);
	}
	for (ScheduledOutput& output : scheduledOutputs) {
		removeGateOutputs(output.gates, gateMask);
	}
}

void midiGateOutputTimerFired() {
	// The timer's interrupt can itself be preempted, so this takes a real guard rather than assuming one
	CriticalSectionGuard guard;
	OutputTime now = outputTimeNow();
	sendDueOutputs(now, guard);
	armMidiGateOutputTimer(now, guard);
}

void scheduleMidiGateOutISR(size_t numSamples);
void setMonitoringMode();
/// The output stage's volume while no song is current: 134217728 made a bit bigger, as a song master's filters start.
constexpr int32_t kMasterVolumeWithoutSong = 167763968;

/// @brief The current song's master over @p renderingBuffer: its half before the reverb with @p master (noting a send
/// to the reverb), then @p between (which renders the reverb, and the sample preview, and returns the half after's
/// inputs, or nothing to skip that half), then that half. The output stage's volume is unity after it; without it, it
/// is left as it was.
template <typename Between>
void renderMaster(std::span<StereoSample> renderingBuffer, const deluge::effects::MasterChainInputs& master,
                  Between&& between) {
	bool post = false;
	auto tracked = [&](bool sent) -> std::optional<deluge::effects::MasterPostInputs> {
		if (sent) {
			timeThereWasLastSomeReverb = audioSampleTimer; // the reverb renders only within a while of a send
		}
		std::optional<deluge::effects::MasterPostInputs> inputs = between();
		post = inputs.has_value();
		return inputs;
	};
	currentSong->live.global_effectable.chain().render(renderingBuffer, master,
	                                                   {reverbMemory.data(), renderingBuffer.size()}, tracked);
	if (post) {
		masterVolumeAdjustmentL = kOneQ31;
		masterVolumeAdjustmentR = kOneQ31;
	}
}
void renderSamplePreview(size_t numSamples);
void renderReverb(size_t numSamples);
void tickSongFinalizeWindows(size_t& numSamples);
void flushMIDIGateBuffers();
void renderAudio(size_t numSamples);
void renderAudioForStemExport(size_t numSamples);
void dumpAudioLog();

static uint32_t outputStage(DelugeStereoSample* out, uint32_t produced, uint32_t frames);

/// The application's single audio entry point, called by the BSP render driver
/// (see <libdeluge/app.h> / <libdeluge/audio_io.h>). Renders exactly `frames`
/// stereo samples into the BSP's `out` block, sub-dividing internally so render
/// windows still end exactly on sequencer events; `in` holds the `frames` input
/// samples aligned with `out`. The window-length policy (how `frames` is
/// chosen) is the board's: on RZ/A1L it stays the DMA-polled variable window.
/// See <libdeluge/app.h>: a block the board chose not to render still runs the render's block-start lifetime step, so
/// objects handed to the render are let go of and freed on schedule.
extern "C" void deluge_app_render_skipped(void) noexcept {
	try {
		// The render's own work, excluded like a rendered block (see deluge_app_render()).
		AudioExcludedSection render_block;
#if defined(DELUGE_HOST)
		View::RenderBlockStackWatch stackWatch;
		deluge::playback::RenderBlockInteractionWatch interactionWatch;
#endif
		renderRetirementStep();
	} catch (...) {
		// Nothing to report to: whatever this step did not let go of stays retired, and the next block's step takes it.
	}
}

/// The body of deluge_app_render(), which catches whatever it throws.
static void renderAppBlock(const DelugeStereoSample* in, DelugeStereoSample* out, uint32_t frames) {
	// The whole block is one AudioExcludedSection. On the device this is free: the render runs on the audio tier, which
	// nothing preempts, so it never masks. On host the render is a thread like any other, and this is what gives it
	// the device's guarantee: an interaction-tier structural edit to a container the render walks (each runs in its
	// own section) and a render block never overlap.
	AudioExcludedSection render_block;
#if defined(DELUGE_HOST)
	// For the harness: whether this block changed the view's active model stack, or other state only the interaction
	// tier may write.
	View::RenderBlockStackWatch stackWatch;
	deluge::playback::RenderBlockInteractionWatch interactionWatch;
#endif
	// Marks this thread as rendering, for the host's count of the render's heap allocations (it must make none).
	struct RenderBlockMark {
		RenderBlockMark() { deluge::memory::noteRenderBlock(true); }
		~RenderBlockMark() { deluge::memory::noteRenderBlock(false); }
	} renderBlockMark;
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
	// The USB input, at the same place in its own ring
	usb_audio::fillInputRing(getInputReadPos(AudioInputChannel::USB), frames);

	// A new block: advance the lifetime epoch, then let go of everything retired since the last one -- before any
	// walk below can reach it.
	renderRetirementStep();

	bool offline = stemExport.processStarted && stemExport.renderOffline;

	if (!offline) {
		startWindow(0);
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
			startWindow(produced);
			flushMIDIGateBuffers();
		}

		voices_started_this_render = 0;

		size_t numSamples = frames - produced;
		tickSongFinalizeWindows(numSamples);

		numSamplesLastTime = numSamples;

		renderAudio(numSamples);

		scheduleMidiGateOutISR(numSamples);

		sideChainHitPending = 0;
		audioSampleTimer += numSamples;

		// Once the codec has had time to settle after power-on and monitoring has faded in, allow full input
		// monitoring. Latches true and stays true, so audioSampleTimer wrapping around can't mute monitoring again.
		inputMonitor.advance_time(audioSampleTimer);
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
#if !DELUGE_STREAM_BENCH // The benchmark zeroes and reads these itself, per phase.
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
#endif
}

extern "C" void deluge_app_render(const DelugeStereoSample* in, DelugeStereoSample* out, uint32_t frames) noexcept {
	try {
		renderAppBlock(in, out, frames);
	} catch (...) {
		// The render must not throw (it must not even allocate); if it does, this block is silence rather than an
		// unwind out of the BSP's render driver. `out` may be NULL (app.h), and then there is no block to silence.
		if (out != nullptr) {
			memset(out, 0, frames * sizeof(DelugeStereoSample));
		}
	}
}

extern "C" bool deluge_app_audio_tick() noexcept {
	try {
		return AudioEngine::routine_task();
	} catch (...) {
		// routine() may have thrown after setting audioRoutineLocked and before clearing it; left set,
		// that reentrancy guard would make every future tick return false and silence audio for good.
		// Clear it here so the next tick renders. Nothing else to report to: this tick counts as one
		// that rendered nothing.
		AudioEngine::audioRoutineLocked = false;
		return false;
	}
}

int32_t* getInputBufferStart(AudioInputChannel channel) {
	return (channel == AudioInputChannel::USB) ? usb_audio::getInputRingStart() : inputRingStart();
}

int32_t* getInputBufferEnd(AudioInputChannel channel) {
	return (channel == AudioInputChannel::USB) ? usb_audio::getInputRingEnd() : inputRingEnd();
}

int32_t* getInputReadPos(AudioInputChannel channel) {
	auto* codecPos = reinterpret_cast<int32_t*>(inputRingPos);
	return (channel == AudioInputChannel::USB) ? usb_audio::getInputRingStart() + (codecPos - inputRingStart())
	                                           : codecPos;
}

namespace {

/// The song's post-FX volume as the metronome takes it: the song volume param's final value, halved; kOneQ31 with no
/// song.
int32_t metronomeVolumePostFx() {
	if (currentSong == nullptr) {
		return kOneQ31;
	}
	return getFinalParameterValueVolume(
	           134217728, cableToLinearParamShortcut(currentSong->paramManager.getUnpatchedParamSet()->getValue(
	                          deluge::modulation::params::UNPATCHED_VOLUME)))
	       >> 1;
}

} // namespace

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
	usbInputMonitoredByTrack = false;

	// Render audio for song: its outputs, then its master around the reverb and the sample preview.
	if (currentSong != nullptr) {
		const deluge::effects::MasterChainInputs master =
		    currentSong->renderAudio(renderingBuffer, reverbBuffer.data(), sideChainHitPending);
		renderMaster(renderingBuffer, master, [&]() -> std::optional<deluge::effects::MasterPostInputs> {
			renderReverb(numSamples);
			renderSamplePreview(numSamples);
			return deluge::model::masterPostInputs(*currentSong);
		});
	}
	else {
		renderReverb(numSamples);
		renderSamplePreview(numSamples);
		masterVolumeAdjustmentL = kMasterVolumeWithoutSong;
		masterVolumeAdjustmentR = kMasterVolumeWithoutSong;
	}

	metronome.render(renderingBuffer, metronomeVolumePostFx());

	approxRMSLevel = envelopeFollower.measure(renderingBuffer);

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
	usbInputMonitoredByTrack = false;

	// Render audio for song: its outputs, then its master, around the reverb when the export takes the song's effects;
	// without them, only the master's half before the reverb.
	if (currentSong != nullptr) {
		const deluge::effects::MasterChainInputs master =
		    currentSong->renderAudio(renderingBuffer, reverbBuffer.data(), sideChainHitPending);
		renderMaster(renderingBuffer, master, [&]() -> std::optional<deluge::effects::MasterPostInputs> {
			if (!stemExport.includeSongFX) {
				return std::nullopt;
			}
			renderReverb(numSamples);
			return deluge::model::masterPostInputs(*currentSong);
		});
	}
	else if (stemExport.includeSongFX) {
		renderReverb(numSamples);
		masterVolumeAdjustmentL = kMasterVolumeWithoutSong;
		masterVolumeAdjustmentR = kMasterVolumeWithoutSong;
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

	approxRMSLevel = envelopeFollower.measure(renderingBuffer);

	inputMonitor.decide({});

	renderingBufferOutputPos = renderingMemory.begin();
	renderingBufferOutputEnd = renderingMemory.begin() + numSamples;
}

void flushMIDIGateBuffers() {
	CriticalSectionGuard guard;
	cvEngine.pollCVSent(guard);
	// Gates (other than run, which has to respect the minimum gate off time) are newer than anything already scheduled
	// for those channels, so they supersede it
	GateOutputs gates = cvEngine.takePendingGateOutputs(false);
	if (gates.mask != 0) {
		cancelScheduledGates(gates.mask, guard);
		cvEngine.outputGates(gates, guard);
	}

	captureWindowOutputs(0);

	// The outputs only hold MIDI which is already due (or sysex, which isn't queued), so they can go now
	midiEngine.flushMIDI();
}

void tickSongFinalizeWindows(size_t& numSamples) {
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

			// Those could have outputted clock or gates, which need to go at the start of the window. MIDI's already
			// due then
			captureWindowOutputs(0);

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

						captureWindowOutputs(timeTilTriggerClockOutTick);
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
						// The clock, and any MIDI after it (it's sent in order), is due at this point in the window
						setMIDIDueAtWindowOffset(timeTilMIDIClockOutTick);
						playbackHandler.doMIDIClockOutTick();
						playbackHandler.scheduleMIDIClockOutTick(); // Schedules another one
					}
				}
				else {
					playbackHandler.scheduleMIDIClockOutTick();
				}
			}
		}
	}
}

void renderReverb(size_t numSamples) {
	std::span renderingBuffer{renderingMemory.data(), numSamples};
	std::span reverbBuffer{reverbMemory.data(), numSamples};

	if (currentSong && mustUpdateReverbParamsBeforeNextRender) {
		updateReverbParams();
		mustUpdateReverbParamsBeforeNextRender = false;
	}

	// The return's duck advances whether or not the reverb renders this block. The tick inverse is read only while it
	// ducks: between songs there is no tempo to read.
	const uint32_t tickInverse = reverb.return_ducks() ? playbackHandler.getTimePerInternalTickInverse() : 0;
	const int32_t reverbOutputVolume =
	    reverb.return_level(numSamples, sideChainHitPending, reverbSidechainSettings, tickInverse);

	int32_t reverbAmplitudeL;
	int32_t reverbAmplitudeR;

	// Stop reverb after 12 seconds of inactivity
	bool reverbOn = ((uint32_t)(audioSampleTimer - timeThereWasLastSomeReverb) < kSampleRate * 12);
	// around the mutable noise floor, ~70dB from peak
	reverbOn |= (std::max(approxRMSLevel.l, approxRMSLevel.r) > 9);

	if (reverbOn) {
		// Reverb panning
		bool thisDoPanning = false;
		if (renderInStereo) {
			const deluge::dsp::fixed::PanAmplitudes amplitudes = deluge::dsp::fixed::pan_amplitudes(reverbPan);
			reverbAmplitudeL = amplitudes.left;
			reverbAmplitudeR = amplitudes.right;
			thisDoPanning = reverbPan != 0;
		}

		if (thisDoPanning) {
			reverbAmplitudeL = multiply_32x32_rshift32(reverbAmplitudeL, reverbOutputVolume) << 2;
			reverbAmplitudeR = multiply_32x32_rshift32(reverbAmplitudeR, reverbOutputVolume) << 2;
		}
		else {
			reverbAmplitudeL = reverbAmplitudeR = reverbOutputVolume;
		}

		// Mix reverb into main render
		reverb.render(renderingBuffer, reverbBuffer, reverbAmplitudeL, reverbAmplitudeR);
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
void setMonitoringMode() {
	SampleRecorder* const recorder = audioRecorder.recorder;
	inputMonitor.decide({
	    .source = audioRecorder.recordingSource,
	    .mode = inputMonitoringMode,
	    .usb_direct_monitoring = usb_audio::directMonitoring,
	    .line_in = lineInPluggedIn,
	    .headphones = headphonesPluggedIn,
	    .mic = micPluggedIn,
	    .recorder = recorder != nullptr,
	    .looks_differential = recorder != nullptr && recorder->inputLooksDifferential(),
	    .no_right_channel = recorder != nullptr && recorder->inputHasNoRightChannel(),
	});
}
void scheduleMidiGateOutISR(size_t numSamples) {
	CriticalSectionGuard guard;

	// Gates output since the window was finalized (e.g. during rendering) go at the start of the window
	captureWindowOutputs(0);

	// Set the due time for any midi that comes in while this window is being output (e.g. live playing, external
	// sequencer)
	setMIDIDueAtWindowOffset(static_cast<int32_t>(numSamples));

	// If a gate note-on was processed at the same time as a gate note-off, the note-off will have already
	// been sent, but we need to make sure that the note-on now doesn't happen until a set amount of time
	// after the note-off.
	int32_t gateMinDelayInSamples =
	    ((uint32_t)cvEngine.minGateOffTime * 289014) >> 16; // That's *4.41 (derived from 44100)
	int32_t samplesTilAllowedToSend =
	    cvEngine.mostRecentSwitchOffTimeOfPendingNoteOn + gateMinDelayInSamples - audioSampleTimer;

	OutputTime now = outputTimeNow();

	// Gates are scheduled the same way as MIDI (see setMIDIDueAtWindowOffset()), so the two stay lined up
	for (const WindowOutput& windowOutput : windowOutputs) {
		int32_t samplesTilGate = windowSamplesUntilPlays + windowOutput.offset;

		// samplesTilMIDI += 10; This gets the start of stuff perfectly lined up. About 10 for MIDI, 12 for gate

		if (windowOutput.gates.needsMinOffTime) {
			samplesTilGate = std::max(samplesTilGate, samplesTilAllowedToSend);
		}

		OutputTime due = outputTimeAtWindowOffset(windowStart, samplesTilGate, 0);
		insertScheduledOutput(ScheduledOutput{.due = due, .gates = windowOutput.gates}, now, guard);
	}
	windowOutputs.clear();

	armMidiGateOutputTimer(now, guard);
}

bool routine_task() {
	// An offline export owns the rendering: it drives routine() itself, as fast as it can, and the
	// block count per call is what decides how rendering interleaves with the cluster loader and the
	// recorder's write-out. A second, tier-paced caller rendering into the same recorder changes that
	// interleaving and so changes the exported audio.
	if (stemExport.renderingOffline()) {
		return false;
	}
	if (audioRoutineLocked) {
		logAction("AudioDriver::routine locked");
		return false;
	}
	return routine() > 0;
}
uint32_t routine() {

	logAction("AudioDriver::routine");

	if (audioRoutineLocked) {
		logAction("AudioDriver::routine locked");
		return 0; // Prevents this from being called again from inside any e.g. memory allocation routines that get
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
	uint32_t renders = 0;
	if (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline)) {
		// The BSP flushes previously-rendered audio toward the DAC and calls
		// deluge_app_render() for as many new frames as its pacing policy asks.
		renders = deluge_audio_drive();
	}
	else {
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
			sendWindowOutputsNow();

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
				int32_t callsRemaining = recorder->currentRecordClusterIndex - recorder->firstUnwrittenClusterIndex + 8;

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
	audioRoutineLocked = false;
	routineBeenCalled = true;
	return renders;
}

int32_t getNumSamplesLeftToOutputFromPreviousRender() {
	return renderingBufferOutputEnd - renderingBufferOutputPos;
}

// Independent white-noise PRNG for the master output dither — deliberately separate from the synth-wide
// getNoise()/synth_noise stream (see the dither call site below). Same LCG recurrence as the synth's Cong, so the
// dither's statistics are unchanged; only its state is private, so dithering can't perturb synth randomness.
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

	// Monitored input - normally the codec's, but the USB input if that's what we're sampling
	AudioInputChannel const monitoringSource =
	    (audioRecorder.recordingSource == AudioInputChannel::USB) ? AudioInputChannel::USB : AudioInputChannel::STEREO;
	int32_t* __restrict__ inputReadPos = getInputReadPos(monitoringSource);
	int32_t const* const inputBufferEnd = getInputBufferEnd(monitoringSource);

	// Audio from a USB host, mixed straight into the outputs if the user wants - unless it's already coming through
	// an audio track's monitoring. The sampler only monitors it when this is off (setMonitoringMode()). It's added
	// after toUSB is taken, so it isn't sent back to the host, which would echo it
	bool const doUSBDirectMonitoring =
	    usb_audio::directMonitoring && !usbInputMonitoredByTrack && usb_audio::receivingFromHost();
	int32_t const* __restrict__ usbReadPos = getInputReadPos(AudioInputChannel::USB);
	int32_t const* const usbInputEnd = usb_audio::getInputRingEnd();
	bool const doSendToUSB = usb_audio::sendingToHost();
	std::array<StereoSample, SSI_TX_BUFFER_NUM_SAMPLES> toUSB;

	// Or the host might want the line input instead of our output. The internal mic only has a left channel
	bool const sendLineInToUSB = (usb_audio::outputSource == usb_audio::OutputSource::LINE_IN);
	bool const lineInIsMono = !lineInPluggedIn && !micPluggedIn;
	int32_t const* __restrict__ lineInReadPos = getInputReadPos(AudioInputChannel::STEREO);
	int32_t const* const lineInEnd = inputRingEnd();

	bool outputRealAudio = (!stemExport.processStarted || (stemExport.processStarted && !stemExport.renderOffline));
	bool const monitorInput = inputMonitor.on();
	deluge::dsp::fixed::OutputFold const monitorFold = inputMonitor.fold();

	while (renderingBufferOutputPosNow != renderingBufferOutputEnd && numSamplesOutputted < maxSamples) {

		// Here we're going to do something equivalent to a multiply_32x32_rshift32(), but add dithering.
		// Because dithering is good, and also because the annoying codec chip freaks out if it sees the same 0s for
		// too long. The dither draws from its OWN PRNG stream (outputDither), NOT the synth-wide
		// getNoise()/synth_noise: the output stage runs once per output sample — including a load-/timing-dependent
		// number of keep-alive samples — so sharing the stream let that count perturb every synth random draw, making
		// the offline render depend on heap layout (and coupling musical randomness to output-sample count). See
		// docs/dev/overread_hunt.md.
		int64_t lAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->l * (int64_t)masterVolumeAdjustmentL + (int64_t)outputDither();
		int64_t rAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->r * (int64_t)masterVolumeAdjustmentR + (int64_t)outputDither();

		int32_t lAdjusted = lAdjustedBig >> 32;
		int32_t rAdjusted = rAdjustedBig >> 32;

		if (monitorInput) {
			const StereoSample monitored = deluge::dsp::fixed::output_monitor_fold(
			    monitorFold, {.l = inputReadPos[0], .r = inputReadPos[1]}, AUDIO_OUTPUT_GAIN_DOUBLINGS);
			lAdjusted = deluge::dsp::wrapping_add(lAdjusted, monitored.l);
			rAdjusted = deluge::dsp::wrapping_add(rAdjusted, monitored.r);

			inputReadPos += NUM_MONO_INPUT_CHANNELS;
			if (inputReadPos >= inputBufferEnd) {
				inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		}

		if (doSendToUSB) {
			if (sendLineInToUSB) {
				toUSB[numSamplesOutputted].l = lineInReadPos[0];
				toUSB[numSamplesOutputted].r = lineInReadPos[lineInIsMono ? 0 : 1];
				lineInReadPos += NUM_MONO_INPUT_CHANNELS;
				if (lineInReadPos >= lineInEnd) {
					lineInReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
				}
			}
			else {
				toUSB[numSamplesOutputted].l = lshift_and_saturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(lAdjusted);
				toUSB[numSamplesOutputted].r = lshift_and_saturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(rAdjusted);
			}
		}

		if (doUSBDirectMonitoring) {
			lAdjusted = add_saturate(lAdjusted, usbReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS));
			rAdjusted = add_saturate(rAdjusted, usbReadPos[1] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS));
			usbReadPos += NUM_MONO_INPUT_CHANNELS;
			if (usbReadPos >= usbInputEnd) {
				usbReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		}

		outputBufferForResampling[numSamplesOutputted].l = lshift_and_saturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(lAdjusted);
		outputBufferForResampling[numSamplesOutputted].r = lshift_and_saturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(rAdjusted);
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

		if (doSendToUSB) {
			usb_audio::sendToHost(std::span{toUSB}.first(numSamplesOutputted));
		}

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
				auto const readPos = (uintptr_t)getInputReadPos(recorder->mode);
				uintptr_t stopPos =
				    (readPos < (uintptr_t)recorder->sourcePos) ? (uintptr_t)getInputBufferEnd(recorder->mode) : readPos;

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
				if (recorder->sourcePos >= getInputBufferEnd(recorder->mode)) {
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
	deluge::effects::ReturnDuck duck{};

	// If reverb sidechain on "auto" settings...
	if (reverbSidechainVolume < 0) {

		// Just leave everything as is while the current song's parts are deleted ahead of a swap to a new one
		if (currentSong->soundsDeletedForSwap.load(std::memory_order_acquire)) {
			return;
		}

		Sound* soundWithMostReverb = nullptr;
		ParamManager* paramManagerWithMostReverb = nullptr;
		GlobalEffectableForClip* globalEffectableWithMostReverb = nullptr;

		// Set the initial "highest amount found" to that of the song itself, which can't be affected by sidechain.
		// If nothing found with more reverb, then we don't want the reverb affected by sidechain
		int32_t highestReverbAmountFound =
		    currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT);

		for (Output* thisOutput = currentSong->live.media.first_output; thisOutput; thisOutput = thisOutput->next) {
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
				duck.depth =
				    deluge::synth::modified_depth(patchCableSet->patchCables[whichCable].param.getCurrentValue(),
				                                  PatchSource::SIDECHAIN, params::GLOBAL_VOLUME_POST_REVERB_SEND);
			}
			else {
				duck.depth = 0;
			}
			goto sidechainFound;
		}
		else if (globalEffectableWithMostReverb) {
			modControllable = globalEffectableWithMostReverb;

			duck.depth =
			    globalEffectableWithMostReverb->getSidechainVolumeAmountAsPatchCableDepth(paramManagerWithMostReverb);

sidechainFound:
			duck.shape =
			    paramManagerWithMostReverb->getUnpatchedParamSet()->getValue(params::UNPATCHED_SIDECHAIN_SHAPE);
			const deluge::effects::SidechainState& from =
			    modControllable->modControllableSnapshot().effects.sidechain; // the render's copy
			reverbSidechainSettings.attack = from.attack;
			reverbSidechainSettings.release = from.release;
			reverbSidechainSettings.sync_level = from.sync_level;
			reverb.set_return_duck(duck);
			return;
		}

		// Or if no thing has more reverb than the Song itself...
		else {
			reverb.set_return_duck(duck);
			return;
		}
	}

	// Otherwise, use manually set params for reverb sidechain
	reverb.set_return_duck({.depth = reverbSidechainVolume, .shape = reverbSidechainShape});
}

void registerSideChainHit(int32_t strength) {
	sideChainHitPending = deluge::dsp::fixed::combine_hit_strengths(strength, sideChainHitPending);
}

PreviewVolume PreviewVolume::neutral() {
	return {
	    .volumePostFX = getParamFromUserValue(params::GLOBAL_VOLUME_POST_FX, kDefaultSoundVolumeUserValue),
	    .localVolume = 0,
	    .velocityCableDepth = getParamFromUserValue(params::PATCH_CABLE, kSampleDrumVelocityCableUserValue),
	    .velocityCablePolarity = PatchCable::getDefaultPolarity(PatchSource::VELOCITY),
	};
}

void soundPreview(PreviewVolume const& volume) {
	// Adopt the volume of the instrument / kit-row the sample will be loaded into, so the preview sounds the same
	// before and after selecting it (issue #1591). The song master volume is applied downstream in renderSongFX().
	PatchedParamSet* previewParams = paramManagerForSamplePreview->getPatchedParamSet();
	previewParams->params[params::GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(volume.volumePostFX);
	previewParams->params[params::LOCAL_VOLUME].setCurrentValueBasicForSetup(volume.localVolume);

	// setupAsSample() gives the preview drum one patch cable, VELOCITY -> LOCAL_VOLUME, unless it ran out of memory.
	PatchCableSet* previewCables = paramManagerForSamplePreview->getPatchCableSet();
	if (previewCables->numPatchCables != 0) {
		previewCables->patchCables[0].param.setCurrentValueBasicForSetup(volume.velocityCableDepth);
		previewCables->setPolarity(0, volume.velocityCablePolarity);
	}

	// GLOBAL_VOLUME_POST_FX is a Sound-level param with no patch cables on the preview drum, so its final value
	// won't update on its own - recompute it before we note-on. LOCAL_VOLUME is per-voice and is picked up when
	// the new voice is patched during noteOn().
	sampleForPreview->recalculatePatchingToParam(params::GLOBAL_VOLUME_POST_FX, paramManagerForSamplePreview);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
	    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);
	// Set BEFORE the note-on, not after. Needed - Dec 2021 - because SampleBrowser::selectEncoderAction()
	// may have gone a while without an audio routine call, so the first render after this sees a huge
	// apparent load and culls.
	//
	// The ordering matters for a second reason: this runs off the audio tier, and the audio render preempts it on
	// the InterruptExecutor. With the assignment after the call, the preview voice was cull-eligible for the
	// whole duration of its own note-on -- and cullVoices -> terminateOneVoice -> freeActiveVoice() erases it
	// from Sound::voices_ while Sound::noteOn still holds a reference to that element, which is what made the
	// E199 assertion fire on a voice something else had removed.
	//
	// This narrows the window; it does not close the underlying cross-tier race on voices_ (bypassCulling
	// is cleared every render, and the race is reachable from any note-on, not just this one); see the
	// containment note in Sound::Sound().
	bypassCulling = true;
	sampleForPreview->Sound::noteOn(modelStack, &sampleForPreview->arpeggiator, kNoteForDrum, zeroMPEValues);
}

void stopAnyPreviewing() {
	sampleForPreview->killAllVoices();
	if (std::ssize(sampleForPreview->sources[0].ranges)) {
		MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(nullptr);
	}
}

void setReverbModel(deluge::effects::ReverbModel model) {
	if (deluge_in_audio_context() || deluge::memory::inRenderBlock()) {
		// A song swap at a launch: the render itself rebuilds, so it is one tier and needs no section. If it has
		// interrupted the interaction tier's change, that change still owns the buffer: it rebuilds at this model next.
		if (reverb.changing()) {
			reverb.request(model);
			return;
		}
		reverb.begin_change();
		reverb.prepare(model);
		reverb.commit();
		return;
	}
	{
		AudioExcludedSection section; // the render stops reading the buffer before it is zeroed
		reverb.begin_change();
	}
	while (true) {
		reverb.prepare(model);
		AudioExcludedSection section; // the render takes up the rebuilt model at its next block
		if (const std::optional<deluge::effects::ReverbModel> requested = reverb.take_request()) {
			model = *requested; // the render's, asked for during the prepare: rebuild at it
			continue;
		}
		reverb.commit();
		return;
	}
}

void publishReverbSettings(const deluge::project::ReverbSettings& knobs) {
	AudioExcludedSection section; // the render reads the settings at its next block
	reverb.publish({.room_size = knobs.room_size,
	                .damping = knobs.damping,
	                .width = knobs.width,
	                .hpf = knobs.hpf,
	                .lpf = knobs.lpf});
}

void getReverbParamsFromSong(Song* song) {
	setReverbModel(song->model);
	publishReverbSettings(song->project.reverb);
	reverbPan = song->project.reverb.pan;
	reverbSidechainVolume = song->project.reverb.sidechain_volume;
	reverbSidechainShape = song->project.reverb.sidechain_shape;
	reverbSidechainSettings.attack = song->project.reverb.sidechain_attack;
	reverbSidechainSettings.release = song->project.reverb.sidechain_release;
	reverbSidechainSettings.sync_level = song->project.reverb.sidechain_sync;
}

bool allowedToStartVoice() {
	// The budget spreads voice starts across render windows so a burst of note-ons cannot blow a real-time deadline. An
	// offline render has no deadline, and its loop has no window to reset the budget in: every note starts when it is
	// played.
	if (stemExport.renderingOffline()) {
		return true;
	}
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
deluge::synth::LiveInputBuffer* getOrCreateLiveInputBuffer(OscType inputType, bool mayCreate) {
	const auto idx = ::util::to_underlying(inputType) - ::util::to_underlying(OscType::INPUT_L);
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
/// How many recorder drains ran after boot, when the drain's card writes go through the write
/// channel. A served count says writes were served; only this one says the drain itself ran on that
/// path.
int32_t g_recorderDrainTicks = 0;

/// Finalize ticks. Separate from the drain counter above because they answer different questions:
/// that one says the DRAIN ran, this one says the FINALIZE did. A served count establishes neither.
int32_t g_finalizeTicks = 0;

/// Create ticks. The third of these counters, and they answer three different questions: the drain
/// ran, the finalize ran, the CREATE ran. A served count answers none of them.
int32_t g_createTicks = 0;
int32_t g_songStageTicks = 0;
} // namespace

int32_t recorderDrainTicks() {
	return g_recorderDrainTicks;
}

int32_t finalizeTicks() {
	return g_finalizeTicks;
}

void noteFinalizeTick() {
	g_finalizeTicks++;
}

int32_t createTicks() {
	return g_createTicks;
}

int32_t songStageTicks() {
	return g_songStageTicks;
}

void noteSongStageTick() {
	g_songStageTicks++;
}

void noteCreateTick() {
	g_createTicks++;
}

void requestRecorderCardRoutines() {
	// Never from an ISR / the audio interrupt-executor: the recorder drain is never driven from an ISR.
	if (deluge_in_interrupt()) {
		return;
	}
	// Inline, always: cardRoutine()'s card operations go through the stream channel after boot, and before boot
	// blocking is legal (and no recorder exists yet). Counted after boot only -- the gates' evidence that the drain
	// runs off the storage tier.
	if (deluge::boot::finished()) {
		g_recorderDrainTicks++;
	}
	doRecorderCardRoutines();
}

void slowRoutine() {
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

	// Go through all SampleRecorders, getting them to write etc (inline, on every BSP).
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
// Diagnostic only, not a hard invariant: sd_busy() answers "is ANY filesystem operation in flight anywhere", not "is
// THIS recorder's own cardRoutine() suspended mid-transfer" -- the two differ, since filesystem work is serialised but
// not attributed to a caller, and nothing on the BSP tracks the second. A busy signal here is informative, not proof
// this recorder is the one suspended, so log it rather than freeze a beta build on what may be an unrelated
// operation.
void discardRecorder(SampleRecorder* recorder) {
	if (ALPHA_OR_BETA_VERSION && deluge::sync::sd_busy()) {
		D_PRINTLN("discardRecorder: sd_busy() was true on entry");
	}

	int32_t count = 0;
	SampleRecorder** prevPointer = &firstRecorder;
	while (*prevPointer) {

		count++;
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

deluge::util::SnapshotDomain& renderDomain() {
	return renderDomain_;
}

void retire(SoundDrum* drum) {
	// First, so a stream of retires frees what earlier ones handed over.
	reclaimRetired();
	handOver(RetireRequest{.drum = drum}, drum, [](void* p) {
		auto* d = static_cast<SoundDrum*>(p);
		checkLetGoBeforeDestroy(nullptr, d);
		void* memory = dynamic_cast<void*>(d);
		d->~SoundDrum();
		delugeDealloc(memory);
	});
}

void retire(Output* output) {
	// First, so a stream of retires frees what earlier ones handed over.
	reclaimRetired();
	handOver(RetireRequest{.output = output}, output, [](void* p) {
		auto* o = static_cast<Output*>(p);
		checkLetGoBeforeDestroy(o, nullptr);
		void* memory = dynamic_cast<void*>(o);
		o->~Output();
		delugeDealloc(memory);
	});
}

void retire(Clip* clip) {
	// First, so a stream of retires frees what earlier ones handed over.
	reclaimRetired();
#if ALPHA_OR_BETA_VERSION
	if (clip->type == ClipType::AUDIO && static_cast<AudioClip*>(clip)->recorder != nullptr) {
		FREEZE_WITH_ERROR("i001"); // Trying to diversify Qui's E278
	}
#endif
	// The references the interaction tier itself reads and writes are cleared here, at once, as ~Clip() used to: no
	// handler that runs before the render's next block may see the retired (detached, possibly output-less) clip as
	// the current clip or the view's active one, nor copy it somewhere nothing clears. letGoOfClip() is the catch for
	// what the render writes itself before it lets go. The render's requests are applied first, so none is left naming
	// this clip (a recording moved on to it, or from it) once it is freed.
	deluge::playback::applyRenderRequests();
	if (currentSong != nullptr) {
		if (currentSong->getCurrentClip() == clip) {
			currentSong->setCurrentClip(nullptr);
		}
		if (currentSong->live.sync_scaling_clip == clip) {
			currentSong->live.sync_scaling_clip = nullptr;
		}
	}
	clearViewIfClip(clip);
	handOver(RetireRequest{.clip = clip}, clip, [](void* p) {
		auto* c = static_cast<Clip*>(p);
		void* memory = dynamic_cast<void*>(c);
		c->~Clip();
		delugeDealloc(memory);
	});
}

void retire(Song* song) {
	if (song == currentSong) {
		FREEZE_WITH_ERROR("E458");
	}
	// First, so a stream of retires frees what earlier ones handed over.
	reclaimRetired();
	// As retire(Clip*): the view's active model stack is the one reference the interaction tier's own handlers read
	// that may still name the old song or something it owns (an armed swap on the render only asks for it to be
	// dropped). Apply what the render asked for, then drop it, so no handler reaches the old song through it and no
	// request is left naming the song once it is freed.
	deluge::playback::applyRenderRequests();
	if (view.activeModControllableNamesSong(song)) {
		view.releaseActiveModControllable();
	}
	handOver(RetireRequest{.song = song}, song, [](void* p) {
		auto* s = static_cast<Song*>(p);
		for (Output* first :
		     {s->live.media.first_output, static_cast<Output*>(s->live.media.first_hibernating_instrument)}) {
			for (Output* output = first; output != nullptr; output = output->next) {
				checkLetGoBeforeDestroy(output, nullptr);
			}
		}
		void* memory = dynamic_cast<void*>(s);
		s->~Song();
		delugeDealloc(memory);
	});
}

void awaitRetired() {
	// Only the interaction tier may wait here: from the render, a single-executor host's inline drive would re-enter
	// the render. The same code as a retire from the wrong tier.
	if (deluge_tier_current() != DELUGE_TIER_INTERACTION) {
		FREEZE_WITH_ERROR("E455");
	}
	// Inside an AudioExcludedSection the render cannot run a block (on host it waits for the section's lock; on the
	// device the section masks its interrupt), so waiting for it would never end.
	if (deluge_audio_excluded_depth() != 0) {
		FREEZE_WITH_ERROR("E460");
	}
	// The render is the higher tier: waiting on it here is allowed, and runs no other interaction task, so whatever
	// the caller left in between (currentSong null during a stopped-clock load) is never seen by one.
	const double start = getSystemTime();
	const uint64_t epochAtStart = renderDomain_.audio_epoch();
	int32_t blocks = 0;
	reclaimRetired();
	while (retiredPending() != 0) {
		if (getSystemTime() - start > kAwaitRetiredTimeoutSeconds || ++blocks > kAwaitRetiredMaxBlocks) {
#ifdef DELUGE_HOST
			// What the wait saw, so a harness freeze says whether the render stopped (the epoch never moved), the
			// render ran but never let go (sequence stuck), or it was only slow.
			std::fprintf(stderr,
			             "awaitRetired: gave up after %.1f ms, %d blocks awaited, epochs advanced %llu, pending %zu, "
			             "sequence pushed %llu let go %llu, overflow %zu\n",
			             (getSystemTime() - start) * 1000.0, blocks,
			             static_cast<unsigned long long>(renderDomain_.audio_epoch() - epochAtStart), retiredPending(),
			             static_cast<unsigned long long>(retireSequencePushed),
			             static_cast<unsigned long long>(retireSequenceLetGo.load(std::memory_order_acquire)),
			             retireOverflow.size());
			std::fflush(stderr);
#endif
			FREEZE_WITH_ERROR("E459");
		}
		deluge_audio_await_block();
		reclaimRetired();
	}
}

void reclaimRetired() {
	while (!retireOverflow.empty() && retireRing.try_push(retireOverflow.front())) {
		retireOverflow.erase(retireOverflow.begin());
	}
	// A request still in the ring or the overflow names an object the render has not let go of yet: it is still in
	// the Sound registry, so a walk can still reach it, whatever its stamp says. Free an entry only once the render
	// has let go of its request (its sequence is covered) and two blocks have begun since its stamp. One undrained
	// request holds back only itself and later ones, never an older entry.
	const uint64_t letGoUpTo = retireSequenceLetGo.load(std::memory_order_acquire);
	retireList.reclaim([letGoUpTo](uint64_t sequence) { return sequence <= letGoUpTo; });
}

std::size_t retireViolations() {
	return retireViolations_;
}

#ifdef DELUGE_HOST
uint32_t retireDrainMicrosMax(bool reset) {
	return reset ? retireDrainMicrosMax_.exchange(0, std::memory_order_relaxed)
	             : retireDrainMicrosMax_.load(std::memory_order_relaxed);
}
#endif

deluge::runtime::JobId reclaimJob = deluge::runtime::kNoJob;

void reclaimRoutine() {
	reclaimRetired();
	if (retiredPending() > 0) {
		deluge::runtime::appLoop.requestAt(reclaimJob, deluge::runtime::nowUs() + kReclaimRecheckUs);
	}
}

std::size_t retiredPending() {
	// Every retired object not yet destroyed is in the list, whether or not its request has reached the render.
	return retireList.size();
}

} // namespace AudioEngine
