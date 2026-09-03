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

#pragma once

#include "OSLikeStuff/scheduler_api.h"
#include "definitions_cxx.hpp"
#include "dsp/compressor/rms_feedback.h"
#include "dsp/envelope_follower/absolute_value.h"
#include "memory/fast_allocator.h"
#include "memory/object_pool.h"
#include "model/output.h"
#include "util/containers.h"
#include <atomic>
#include <cstdint>
#include <memory>

class Song;
class StereoSample;
class Instrument;
class Sound;
class ParamManagerForTimeline;
class LiveInputBuffer;
class SampleRecorder;
class Voice;
class VoiceSample;
class TimeStretcher;
#include <string>
class SideChain;
class VoiceVector;
class Freeverb;
class Metronome;
class RMSFeedbackCompressor;
class ModelStackWithSoundFlags;
class SoundDrum;
class AbsValueFollower;

namespace deluge::dsp {
class Reverb;
}

/*
 * ================== Audio rendering ==================
 *
 * The Deluge renders its audio in “windows” (or you could more or less say “buffers”),
 * a certain number of samples long. In fact, the Deluge’s audio output buffer is a circular,
 * 128-sample one, whose contents are continuously output to the DAC / codec via I2S
 * (Renesas calls this SSI) at 44,100hz. That buffer is an array called ssiTxBuffer.
 *
 * Each time the audio routine, AudioEngine::routine(), is called, the code checks where the
 * DMA outputting has gotten up to in ssiTxBuffer, considers where its rendering of the previous
 * “window” had ended at, and so determines how many new samples it may now render and write into
 * ssiTxBuffer without “getting ahead of itself”.
 *
 * This scheme is primarily useful because it regulates CPU load somewhat, for a slight tradeoff
 * in audio rendering quality (more on that below) by using longer windows for each render. For instance,
 * if the CPU load is very light (as in, not many sounds playing), rendering a window will happen very fast,
 * so when the audio routine is finished and is then called again the next time, only say one or two
 * samples will have been output via DMA to the DAC. This means the next window length is set at just
 * one or two samples. I.e. lighter CPU load means shorter windows.
 *
 * Often the window length will be rounded to a multiple of 4, because various parts of the rendering code
 * (e.g. oscillator table lookup / interpolation) use Arm NEON optimizations, which are implemented in
 * a way that happens to perform best working on chunks of 4 samples. Also, and taking precedence over that,
 * windows will be shortened to always end at the precise audio sample where an event (e.g. note-on)
 * is to occur on the Deluge’s sequencer.
 *
 * The window length has strictly speaking no effect on the output or quality of oscillators
 * (including sync, FM, ringmod and wavetable), filters, most effects, sample playback, or timings/jitter
 * from the Deluge’s sequencer. Where it primarily does have a (barely audible) effect is on envelope shapes.
 * The current “stage” of the envelope (i.e. A, D, S or R) is only recalculated at the beginning of each window,
 * so the minimum attack, decay or release time is the length of that window, *unless* that parameter
 * is in fact set to 0, in which case that stage is skipped. So, you can get a 0ms (well, 1 sample) attack time,
 * but depending on the window length (which depends on CPU load), your 0.1ms attack time could theoretically
 * get as long as 2.9ms (128 samples), though it would normally end up quite a bit shorter.
 *
 * With envelopes (and LFO position actually) only being recalculated at the start of each “window”,
 * you might be wondering whether you’ll get an audible “zipper” or stepped effect as the output of these
 * changes sporadically. The answer is, usually not, because most parameters for which this would be audible
 * (e.g. definitely anything level/volume related) will linearly interpolate their value change throughout
 * the window, usually using a variable named something to do with “increment”. This is done per parameter,
 * rather than per modulation source, essentially for ease / performance. Wavetable position and FM amounts
 * are other important parameters to do this to.
 *
 * But for many parameters, the stepping is not audible in the first place, so there is no interpolation.
 * This includes filter resonance and oscillator / sample-playback pitch. Especially sample-playback
 * pitch would be very difficult to interpolate this way because the (unchanging) pitch for the window
 * is used at the start to calculate how many raw audio-samples (i.e. how many bytes) will need to be
 * read from memory to render the (repitched) audio-sample window.
 */

/*
 * ====================== Audio / CPU performance ======================
 * A huge number of factors influence the performance of a particular Deluge firmware build.
 * Subpar performance will usually be noticed in the form of sounds dropping out in songs which
 * performed better in other firmware versions. As an open source contributer, ensuring optimal performance of any code
 * modifications you make, and subsequently their builds, will be a big challenge. But don’t sweat it too much - if
 * you’ve added some cool features which are useful to you or others, maybe lowered audio performance is a reasonable
 * tradeoff?
 *
 * The Deluge codebase, since 2021, has been built with GCC 9.2. I (Rohan) compared this with the other 9.x GCC
 * versions, some 10.x ones, and the 6.x version(s) that the Deluge had used earlier. Performance differences were
 * negligible in most cases, and ultimately I settled on GCC 9.2 because it resulted in a built binary which was
 * smaller by a number of kilobytes compared to other versions. GCC 10.x was particularly bad in this regard.
 *
 * The build process includes LTO - this helps performance a fair bit. And normal O2 optimization.
 * These are both disabled in the HardwareDebug build configuration though, for faster build times and
 * ease of code debugging. If you’re using live code uploading via a J-link and want to do some tests for
 * real-world performance, you should enable these for this configuration, at least while doing your tests.
 *
 * A few other of the standard compiler optimizations are enabled, like –gc-sections to remove unused code from the
 * build. Beyond that, I’ve experimented with enabling various of the most advanced GCC optimizations, but haven’t found
 * any that improve overall performance.
 */

/// For advanced debugging printouts.
#define DO_AUDIO_LOG 0

namespace AudioEngine {
#define logAction(string) logAudioAction(string, __FILE__, __LINE__)

using VoicePool = deluge::memory::ObjectPool<Voice, deluge::memory::fast_allocator>;
using VoiceSamplePool = deluge::memory::ObjectPool<VoiceSample, deluge::memory::fast_allocator>;
using TimeStretcherPool = deluge::memory::ObjectPool<TimeStretcher, deluge::memory::fast_allocator>;
void routine();
void routine_task();
void routineWithClusterLoading();
void runRoutine();

void init();
void previewSample(std::string_view path, bool shouldActuallySound);
void stopAnyPreviewing();

void songSwapAboutToHappen();
void killAllVoices(bool deletingSong = false);
void logAudioAction(char const* string, const char* file, int line);

void getReverbParamsFromSong(Song* song);

VoiceSample* solicitVoiceSample();
void voiceSampleUnassigned(VoiceSample* voiceSample);

TimeStretcher* solicitTimeStretcher();
void timeStretcherUnassigned(TimeStretcher* timeStretcher);

LiveInputBuffer* getOrCreateLiveInputBuffer(OscType inputType, bool mayCreate);
void slowRoutine();
void doRecorderCardRoutines();
/// @brief Owner-mediated, coalesced entry point for doRecorderCardRoutines() — the recorder
///        card-write drain, routed through the storage owner (SD-routine-class).
///
/// Runs inline on legacy/host; coalesced onto the worker fiber on Embassy.
void requestRecorderCardRoutines();

int32_t getNumSamplesLeftToOutputFromPreviousRender();

void registerSideChainHit(int32_t strength);

SampleRecorder* getNewRecorder(int32_t numChannels, AudioRecordingFolder folderID, AudioInputChannel mode,
                               bool keepFirstReasons, bool writeLoopPoints, int32_t buttonPressLatency,
                               bool shouldNormalize, Output* outputRecordingFrom, RecorderConfig config = {});
void discardRecorder(SampleRecorder* recorder);
bool isAnyInternalRecordingHappening();

#ifdef FLIGHTDATA
char log[32][64];
uint8_t logNextIndex = 0;
uint32_t logAbsoluteIndex = 0;
char* getEmptyLogEntry();
void printLog();
#endif
int32_t getNumAudio();
int32_t getNumVoices();
void updateReverbParams();

extern bool headphonesPluggedIn;
extern bool micPluggedIn;
extern bool lineInPluggedIn;
extern bool renderInStereo;
extern uint32_t audioSampleTimer;
extern bool mustUpdateReverbParamsBeforeNextRender;
extern bool bypassCulling;
/// @brief Set by the render loop when it culls a voice while the high-CPU indicator is on; cleared
///        by the UI tick, which blinks the Play LED in response.
///
/// A namespace-scope `inline` variable -- one object across all TUs, not `static`, which would
/// give each TU its own copy. [any]
///
/// @note `std::atomic<bool>` because the write happens on the audio render path (or, on the
///       RZ/A1, an audio ISR) while the read-and-clear happens on the UI drain: two threads
///       touching a plain `bool` without synchronisation is a data race even though each side
///       only ever writes a single boolean value. `memory_order_relaxed` suffices -- this carries
///       no other memory with it, it is just a flag -- so the drain clears it with
///       `exchange(false, std::memory_order_relaxed)` rather than a separate load-then-store,
///       which would race the render loop setting it again in between.
inline std::atomic<bool> cull_alert_pending{false};
// The app-owned input ring. A mirror of the received audio input, filled by the
// audio routine ahead of each render window; everything in the application that
// consumes live input (monitoring, INPUT_L/R oscillators, live input buffers,
// sample recorders) reads this, never the hardware DMA ring. Same geometry as
// the board's RX ring: SSI_RX_BUFFER_NUM_SAMPLES frames of
// NUM_MONO_INPUT_CHANNELS interleaved q31 samples.
extern uintptr_t inputRingPos; // read cursor (an address): input aligned with the next output sample
int32_t* inputRingStart();
int32_t* inputRingEnd();
extern int32_t cpuDireness;
extern InputMonitoringMode inputMonitoringMode;
extern bool audioRoutineLocked;
extern bool routineBeenCalled;
extern uint8_t numHopsEndedThisRoutineCall;
extern SideChain reverbSidechain;
extern uint32_t timeThereWasLastSomeReverb;
extern deluge::fast_vector<Sound*> sounds;
extern deluge::dsp::Reverb reverb;
extern uint32_t nextVoiceState;
extern SoundDrum* sampleForPreview;
extern int32_t reverbSidechainVolume;
extern int32_t reverbSidechainShape;
extern int32_t reverbPan;
extern SampleRecorder* firstRecorder;
extern Metronome metronome;
extern RMSFeedbackCompressor mastercompressor;
extern uint32_t timeLastSideChainHit;
extern int32_t sizeLastSideChainHit;
extern StereoFloatSample approxRMSLevel;
extern AbsValueFollower envelopeFollower;
extern TaskID routine_task_id;
void feedReverbBackdoorForGrain(int index, q31_t value);

/// @name The foreground recording
///
/// `AudioRecorder::recordingSource` and `AudioRecorder::recorder` (`gui/ui/audio_recorder.h:40-42`)
/// are not that class's private bookkeeping: **nine subsystems read them** as "a foreground
/// recording is live". `setMonitoringMode()` below enables input monitoring only for the two
/// channels the recorder UI arms; `hid/buttons.cpp:147,180` gate shift+RECORD on them;
/// `processing/stem_export/stem_export.cpp:195,412,460,594,716`,
/// `playback/playback_handler.cpp:2503`, `playback/mode/arrangement.cpp:328`,
/// `gui/ui/keyboard/keyboard_screen.cpp:305`, `gui/views/instrument_clip_view.cpp:353,5435` and
/// `gui/ui/browser/sample_browser.cpp:456` all test one of them.
///
/// These two functions are that pair's write and read seam, for a UI that is **not**
/// `AudioRecorder` -- `deluge::scenes::recorder::RecorderScene`, which may not name a `gui/` global
/// (`scripts/check_layer_rules.py`, `scenes-no-root-ui`) and would otherwise arm a recording nine
/// subsystems cannot see. They live here rather than in a new `processing/` file because
/// `audio_engine.cpp` **already** names `audioRecorder` (`setMonitoringMode` does), so this adds no
/// layer edge that was not there; a new file would add a `model-includes-ui` baseline row.
///
/// @note Not a replacement for `AudioRecorder`'s own writes: `setupRecordingToFile` and
///       `finishRecording` still assign the members directly, and this is an additional writer of
///       the same storage.
/// @{
/// @brief Publish (or clear) the foreground recording.
///
/// The assignment order is legacy's own, in both directions: `setupRecordingToFile` assigns
/// `recorder` before `recordingSource` under the comment "This sets recording to begin happening
/// even as the file is created" (`gui/ui/audio_recorder.cpp:135-138`), and `finishRecording` clears
/// them in the same order (`:250-251`). The audio routine tests `recordingSource` and then
/// dereferences `recorder`, so the pointer must become visible first and stop being visible last.
///
/// @param recorder The armed recorder, or null to clear.
/// @param source   The channel it captures from, or `AudioInputChannel::NONE` to clear.
void setForegroundRecording(SampleRecorder* recorder, AudioInputChannel source);
/// @brief The foreground recording's channel -- `AudioRecorder::recordingSource`.
/// @return `AudioInputChannel::NONE` when nothing is recording.
AudioInputChannel foregroundRecordingSource();
/// @}

/// @brief Re-derive `doMonitoring` and `monitoringAction` from the foreground recording.
///
/// Called from `routine()` on every render window, so a UI that arms a recording never has to call
/// it; declared here (it was a file-local forward declaration) so a spec can pin the *behaviour*
/// the seam above exists to preserve rather than only the two values it writes.
void setMonitoringMode();
/// @brief Is live input being monitored through to the output? Set by `setMonitoringMode()`.
extern bool doMonitoring;

/// returns whether a voice is allowed to start right now - otherwise it should be deferred to the next tick
bool allowedToStartVoice();
} // namespace AudioEngine
