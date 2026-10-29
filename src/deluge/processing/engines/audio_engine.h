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

#include "definitions_cxx.hpp"
#include "dsp/envelope_follower/absolute_value.h"
#include "engines/effects/state.h"
#include "memory/fast_allocator.h"
#include "memory/object_pool.h"
#include "model/output.h"
#include "runtime/app_loop.h"
#include "util/audio_owned_list.h"
#include "util/containers.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <libdeluge/scheduler.h>
#include <memory>

class Song;
namespace deluge::dsp {
struct StereoSample;
} // namespace deluge::dsp
using StereoSample = deluge::dsp::StereoSample;
class Instrument;
class Sound;
class ParamManagerForTimeline;
namespace deluge::synth {
class LiveInputBuffer;
}
class SampleRecorder;
class Voice;
class VoiceSample;
class TimeStretcher;
#include <string>
class VoiceVector;
class Metronome;
enum class Polarity : uint8_t;
struct CriticalSectionGuard;
class ModelStackWithSoundFlags;
class SoundDrum;
class Clip;
class AbsValueFollower;

namespace deluge::effects {
class Sidechain;
class Reverb;
enum class ReverbModel : uint8_t;
} // namespace deluge::effects
namespace deluge::project {
struct ReverbSettings;
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
uint32_t routine();
bool routine_task();
void routineWithClusterLoading();
void runRoutine();

void init();
/// The volume context for a sample preview: the params the preview drum should adopt so that it sounds like the
/// instrument or kit-row the sample will be loaded into (issue #1591). Values are already fully resolved - any kit
/// output volume has been folded in by the caller.
struct PreviewVolume {
	int32_t volumePostFX;           ///< params::GLOBAL_VOLUME_POST_FX
	int32_t localVolume;            ///< params::LOCAL_VOLUME
	int32_t velocityCableDepth;     ///< depth of the VELOCITY -> LOCAL_VOLUME patch cable
	Polarity velocityCablePolarity; ///< polarity of the VELOCITY -> LOCAL_VOLUME patch cable

	/// The defaults the preview drum is built with, from Sound::initParams() and Sound::setupAsSample().
	static PreviewVolume neutral();
};

/// @brief Sound the preview drum, with whatever sample its first range holds, at @p volume.
///
/// The note-on half of a preview: the sample browser loads the sample through its own tick, then calls this.
void soundPreview(PreviewVolume const& volume);
void stopAnyPreviewing();

void songSwapAboutToHappen();
void killAllVoices(bool deletingSong = false);
void logAudioAction(char const* string, const char* file, int line);

void getReverbParamsFromSong(Song* song);

VoiceSample* solicitVoiceSample();
void voiceSampleUnassigned(VoiceSample* voiceSample);

TimeStretcher* solicitTimeStretcher();
void timeStretcherUnassigned(TimeStretcher* timeStretcher);

deluge::synth::LiveInputBuffer* getOrCreateLiveInputBuffer(OscType inputType, bool mayCreate);
void slowRoutine();
void doRecorderCardRoutines();
/// @brief Run the recorder card-write drain (doRecorderCardRoutines()) inline. Counts the drains after boot:
///        the gates' evidence that the drain runs off the storage tier.
void requestRecorderCardRoutines();

/// @brief How many recorder drains ran after boot, when their card writes go through the stream channel.
///
/// The evidence that the drain itself ran on that path. A served count alone says writes were served,
/// not which drain issued them.
[[nodiscard]] int32_t recorderDrainTicks();

/// @brief How many finalize ticks have run.
///
/// The evidence that finishing a recording ran through the finalize machine at all. Distinct from
/// @ref recorderDrainTicks, which only says the drain did.
[[nodiscard]] int32_t finalizeTicks();

/// @brief How many create ticks have run.
///
/// The evidence that creating a recording's file ran through the create machine at all.
[[nodiscard]] int32_t createTicks();

/// @brief How many document-staging attempts ran after boot, through the byte channel.
///
/// The evidence that the song load's card read goes through the channels. Reading zero after a song
/// load means the load took its bytes some other way, which is the whole thing this is here to disprove.
[[nodiscard]] int32_t songStageTicks();

/// @brief Record one staging attempt made after boot.
void noteSongStageTick();

/// @brief Record one create tick.
void noteCreateTick();

/// @brief Record one finalize tick.
void noteFinalizeTick();

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
// Latches true once enough samples have elapsed since power-on for the codec ADC to settle and input monitoring to
// fade in. Until then, input monitoring (Looper/Sampler) is suppressed and then faded in, so the codec's power-on
// transient isn't passed through to the output. See getInputMonitoringGain().
extern bool inputMonitoringWarmedUp;
// Set during a render if any audio track is monitoring the USB input. USB direct monitoring stands aside for that
// window, or the host's audio would be heard twice. Cleared at the start of each render
extern bool usbInputMonitoredByTrack;
extern bool mustUpdateReverbParamsBeforeNextRender;
extern bool bypassCulling;
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
extern deluge::effects::Sidechain reverbSidechain;
/// The reverb sidechain's settings: the song's (project.reverb), or, on auto, those of the owner with the most
/// reverb. Its sync type is set only from the menu.
extern deluge::effects::SidechainState reverbSidechainSettings;
extern uint32_t timeThereWasLastSomeReverb;
/// Every live Sound. Changed only by Sound's constructor and destructor, walked by the render: see AudioOwnedList.
extern deluge::AudioOwnedList<Sound, deluge::fast_vector<Sound*>> sounds;
extern deluge::effects::Reverb reverb;
/// @brief Rebuilds the song reverb at @p model (memory zeroed), as a song load or the model menu sets it; the render
/// adds no reverb while it rebuilds. From the interaction tier, or from the render itself in a song swap at a launch;
/// a render-tier call during an interaction-tier rebuild is taken up by that rebuild before it commits.
void setReverbModel(deluge::effects::ReverbModel model);
/// @brief Publishes the song's reverb knobs (project.reverb), which the render takes up at its next block. From the
/// interaction tier, or from the render itself in a song swap at a launch.
void publishReverbSettings(const deluge::project::ReverbSettings& knobs);
extern uint32_t nextVoiceState;
extern SoundDrum* sampleForPreview;
extern int32_t reverbSidechainVolume;
extern int32_t reverbSidechainShape;
extern int32_t reverbPan;
extern SampleRecorder* firstRecorder;
extern Metronome metronome;
extern uint32_t timeLastSideChainHit;
extern int32_t sizeLastSideChainHit;
extern StereoFloatSample approxRMSLevel;
extern AbsValueFollower envelopeFollower;
/// Set once the BSP's audio tier drives the render (at the end of deluge_app_init). From then on
/// runRoutine() leaves rendering to the tier, which preempts its caller.
extern std::atomic<bool> renderDrivenByTier;
/// Remove these gate channels from any output waiting to be sent by the MIDI/gate output timer, because they've been
/// superseded by a newer state which is being output now
void cancelScheduledGates(uint8_t gateMask, const CriticalSectionGuard&);
/// Called from the MIDI/gate output timer's interrupt. Sends any MIDI/gate output which is due and arms the timer for
/// the rest
void midiGateOutputTimerFired();
/// Gain to apply to monitored codec input, `offset` samples into the buffer currently being rendered. 0 until the
/// codec has settled after power-on, then ramps up to full scale.
q31_t getInputMonitoringGain(uint32_t offset);

/// Each live input source has a ring buffer laid out like the input ring - same length, same stereo interleaving -
/// and kept in step with it, so the same offset into any of them is the same moment. These find the one for a source:
/// the USB input's for AudioInputChannel::USB, the input ring for the others.
int32_t* getInputBufferStart(AudioInputChannel channel);
int32_t* getInputBufferEnd(AudioInputChannel channel);
/// Where the audio about to be rendered starts, in the source's input buffer - inputRingPos, translated to it
int32_t* getInputReadPos(AudioInputChannel channel);

/// Whether a source is the codec's line/mic input - as opposed to USB audio, which is also external (below
/// AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) but needs none of the codec's normalising or jack detection
constexpr bool isCodecInput(AudioInputChannel channel) {
	return channel < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION && channel != AudioInputChannel::USB;
}

/// returns whether a voice is allowed to start right now - otherwise it should be deferred to the next tick
bool allowedToStartVoice();

/// @brief [interaction] Hand a drum the caller has already taken out of its kit over to the render, which ends its
///        voices and deregisters it at the top of a following block. It is destroyed and freed at the first
///        retire() or reclaim-task tick after the render has let go of it and two blocks have begun since the
///        handover -- so no earlier than two blocks later, and as late as the interaction tier's next such call.
///        Replaces `drum->~SoundDrum(); delugeDealloc(...)`: the caller must not touch @p drum afterwards.
void retire(SoundDrum* drum);
/// @brief [interaction] As above, for an output the caller has already unlinked from the song's lists. A synth's Sound
///        and every SoundDrum of a kit are let go by the render; every output kind is destroyed as above.
void retire(Output* output);
/// @brief [interaction] As above, for a clip the caller has already prepared for destruction
///        (Clip::prepareForDestruction()) and taken out of its song's arrays and clip instances. The references the
///        interaction tier itself reads -- the current song's currentClip and syncScalingClip, and the view's active
///        ModControllable model stack -- are cleared here, synchronously, before the handover. At the top of a
///        following block the render then nulls whatever it wrote itself in between (currentClip, syncScalingClip,
///        any of the current song's clips' beingRecordedFromClip, the view's stack), and the clip is destroyed as
///        above. An AudioClip must no longer be recording (i001 on alpha/beta builds).
void retire(Clip* clip);
/// @brief [interaction] As above, for a whole song the caller has already replaced as currentSong (E458 if it is still
///        current). The view's active model stack is dropped at once if it names the song or anything it owns. At the
///        top of a following block the render lets go of every Sound of every output the song owns (voices ended,
///        Sounds deregistered), and the song is then destroyed as above: ~Song() destroys its clips and outputs
///        directly, since nothing can reach them any more.
void retire(Song* song);
/// @brief [interaction] Block until every retired object has been destroyed (retiredPending() == 0), waiting on the
///        render with deluge_audio_await_block() between reclaims. Interaction tier only (E455 otherwise).
///        Bounded: freezes with E459 after 250 ms or 1000 waits for a block, whichever comes first. For a
///        stopped-clock song load or clear, which must free the old song before it builds the new one, and for a
///        swap's finish, whose sample-load retry needs the old song's memory back.
void awaitRetired();
/// @brief [interaction] Destroy every retired object the render has let go of, once two blocks have begun since it
///        was handed over. Called by @ref reclaimRoutine and, before each handover, by retire().
void reclaimRetired();
/// @brief How often @ref reclaimRoutine looks again while retired objects are still waiting, in µs: about five render
///        blocks, so an object is freed soon after the two blocks it waits for.
inline constexpr deluge::runtime::Micros kReclaimRecheckUs = 15'000;
/// @brief The job that runs @ref reclaimRoutine. Requested by every handover to the render; it asks for its own next
///        run while anything is still waiting.
extern deluge::runtime::JobId reclaimJob;
/// @brief [interaction] reclaimRetired(), then a run @ref kReclaimRecheckUs from now while retiredPending() > 0.
void reclaimRoutine();
/// @brief [interaction] How many retired objects are still waiting to be destroyed. Test/diagnostic hook.
std::size_t retiredPending();
/// @brief [interaction] How many retired objects were destroyed while a Sound of theirs was still in the Sound
///        registry -- before the render let go of it. Always 0 unless the retirement's ordering is broken.
std::size_t retireViolations();
#ifdef DELUGE_HOST
/// @brief [interaction] Host-only measurement: the longest block-top let-go of retired objects since the last reset, in
///        host wall-clock microseconds. @p reset zeroes it and returns the value it replaced.
uint32_t retireDrainMicrosMax(bool reset);
#endif
} // namespace AudioEngine
