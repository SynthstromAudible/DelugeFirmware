/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "dsp/delay/delay_buffer.h"
#include "dsp/scatter.hpp"
#include <cstdint>
#include <span>

class ParamManagerForTimeline;
class ParamManager;

/// Scatter mode determines how the stutter buffer is manipulated during playback
enum class ScatterMode : uint8_t {
	Classic = 0, ///< Original stutter behavior (passthrough)
	Burst,       ///< Gated stutter: play grain (rate/2), silence until next trigger
	Repeat,      ///< Beat repeat with count control
	Time,        ///< Zone A=combine (grain length), Zone B=repeat (hold same slice)
	Shuffle,     ///< Phi-based segment reordering
	Leaky,       ///< Shuffle with probabilistic write-back to buffer (exclusive ownership)
	// FUTURE: Tweaky - Leaky without ownership check, allows cross-track chaos
	// Both tracks can write to recordBuffer simultaneously, creating unpredictable
	// contamination where A's processed output bleeds into B's recording and vice versa.
	// Implementation: remove `recordSource == playSource` check from leaky write logic.
	Pattern, ///< Zone A selects slice pattern + phi offset: seq/weave/skip/mirror/pairs
	Pitch,   ///< Pitch manipulation
	NUM_MODES
};

struct StutterConfig {
	bool useSongStutter = true;
	bool quantized = true;
	bool reversed = false;
	bool pingPong = false;
	bool latch = false; ///< Scatter mode: latch (stays on after release) vs normal (release to stop)
	ScatterMode scatterMode = ScatterMode::Classic;

	// Secret knob phase offsets (push+twist encoder on zone knobs)
	float zoneAPhaseOffset{0};       ///< Zone A structural phase offset (push Zone A encoder)
	float zoneBPhaseOffset{0};       ///< Zone B timbral phase offset (push Zone B encoder)
	float macroConfigPhaseOffset{0}; ///< Macro config phase offset (push Macro Config encoder)
	float gammaPhase{0};             ///< Gamma multiplier for macro (push Macro encoder)

	// Leaky mode: probability of writing processed output back to buffer
	// 0 = never write (no leak), 1 = always write (max leak)
	float leakyWriteProb{0.2f}; ///< pWrite: write probability [0,1], default 20%

	// Pitch mode: scale selection
	// 0=Chromatic, 1=Major, 2=Minor, 3=MajPent, 4=MinPent, 5=Blues, 6=Dorian, 7=Mixolyd
	uint8_t pitchScale{0};
};

class Stutterer {
public:
	Stutterer() = default;
	static void initParams(ParamManager* paramManager);

	/// Buffer size for non-Classic looper modes (4 seconds at 44.1kHz for ring buffer)
	static constexpr size_t kLooperBufferSize = 44100 * 4;
	/// Check if source is actively playing from the scatter buffer
	inline bool isStuttering(void* source) {
		return playSource == source && (status == Status::RECORDING || status == Status::PLAYING);
	}
	/// Check if scatter is actively playing (regardless of ownership)
	inline bool isScatterPlaying() const {
		return stutterConfig.scatterMode != ScatterMode::Classic
		       && (status == Status::RECORDING || status == Status::PLAYING);
	}
	/// Check if this source owns either play or record buffer
	inline bool ownsStutter(void* source) { return playSource == source || recordSource == source; }
	/// Check if scatter is playing but has no owner (released during patch change)
	inline bool isOrphaned() const {
		return playSource == nullptr && stutterConfig.scatterMode != ScatterMode::Classic
		       && (status == Status::RECORDING || status == Status::PLAYING);
	}
	/// Check if scatter is currently owned by a different source (not us, not orphaned)
	/// Use this to distinguish "owned by someone else" from "not owned by us" (which includes orphaned)
	inline bool isOwnedByOther(void* source) const { return playSource != nullptr && playSource != source; }
	/// Release ownership while keeping scatter playing (for patch changes with latched scatter)
	/// The scatter continues playing from its buffer; a new source can adopt it
	inline void releaseOwnership() {
		if (isLatched() && (status == Status::RECORDING || status == Status::PLAYING)) {
			playSource = nullptr;
			recordSource = nullptr;
		}
	}
	/// Adopt orphaned scatter (new source takes over processing after patch change)
	/// Returns true if adoption occurred
	/// Global scatter state: ownership is just about who processes audio, config is global
	inline bool adoptOrphanedScatter(void* source) {
		if (isOrphaned()) {
			playSource = source;
			recordSource = source;
			return true;
		}
		return false;
	}
	/// Get the current scatter mode (valid while stuttering or armed)
	inline ScatterMode getScatterMode() const {
		if (status == Status::ARMED) {
			return armedConfig.scatterMode;
		}
		return stutterConfig.scatterMode;
	}
	/// Get the active stutter config (read-only)
	inline const StutterConfig& getStutterConfig() const { return stutterConfig; }
	/// Get mutable stutter config (for direct menu writes when scatter is playing)
	inline StutterConfig& getStutterConfigMutable() { return stutterConfig; }
	/// Check if standby recording is active
	inline bool isInStandby() const { return status == Status::STANDBY; }
	/// Check if scatter is latched (should keep playing when switching views/tracks)
	inline bool isLatched() const { return stutterConfig.latch && stutterConfig.scatterMode != ScatterMode::Classic; }
	/// Check if armed and waiting for beat quantize
	/// Takeover = PLAYING with a different source recording (preparing to take over)
	inline bool isArmed() const {
		return status == Status::ARMED || (status == Status::PLAYING && recordSource != playSource);
	}
	/// Check if source is armed for takeover (recording while someone else plays)
	inline bool isArmedForTakeover(void* source) const {
		return status == Status::PLAYING && recordSource == source && playSource != source;
	}
	/// Check if source is recording in standby mode (buffer filling but not yet playing)
	inline bool isRecordingInStandby(void* source) const { return status == Status::STANDBY && recordSource == source; }
	// These calls are slightly awkward with the magniture & timePerTickInverse, but that's the price for not depending
	// on currentSong and playbackhandler...
	// loopLengthSamples: for scatter modes, the length of the loop region in samples (one bar or 2 beats)
	// halfBar: if true, buffer contains 2 beats that should be virtually doubled for scatter processing
	[[nodiscard]] Error beginStutter(void* source, ParamManagerForTimeline* paramManager, StutterConfig stutterConfig,
	                                 int32_t magnitude, uint32_t timePerTickInverse, size_t loopLengthSamples = 0,
	                                 bool halfBar = false);
	/// Optional modulatedValues array indexed as [SCATTER_ZONE_A, SCATTER_ZONE_B, SCATTER_MACRO_CONFIG, SCATTER_MACRO]
	/// If nullptr, preset values are used. If provided, these modulated values override the param preset.
	void processStutter(std::span<StereoSample> audio, ParamManager* paramManager, int32_t magnitude,
	                    uint32_t timePerTickInverse, int64_t currentTick = 0, uint64_t timePerTickBig = 0,
	                    uint32_t barLengthInTicks = 0, const q31_t* modulatedValues = nullptr);
	void endStutter(ParamManagerForTimeline* paramManager = nullptr);

	/// Update live-adjustable params from source's current config (call before processStutter)
	/// This allows real-time adjustment of phase offsets and leaky write prob while scatter is playing
	/// Also allows seamless mode switching between looper modes without stopping/clearing
	inline void updateLiveParams(const StutterConfig& sourceConfig) {
		stutterConfig.zoneAPhaseOffset = sourceConfig.zoneAPhaseOffset;
		stutterConfig.zoneBPhaseOffset = sourceConfig.zoneBPhaseOffset;
		stutterConfig.macroConfigPhaseOffset = sourceConfig.macroConfigPhaseOffset;
		stutterConfig.gammaPhase = sourceConfig.gammaPhase;
		stutterConfig.leakyWriteProb = sourceConfig.leakyWriteProb;
		stutterConfig.pitchScale = sourceConfig.pitchScale;
		stutterConfig.latch = sourceConfig.latch;
		// Allow mode switching between all looper-based modes (all except Classic)
		// Classic uses different buffer system, can't switch to/from it during playback
		// Changes take effect on next slice boundary
		if (stutterConfig.scatterMode != ScatterMode::Classic && sourceConfig.scatterMode != ScatterMode::Classic) {
			stutterConfig.scatterMode = sourceConfig.scatterMode;
		}
	}

	/// Direct setters for live params (for menu access)
	inline void setLivePitchScale(uint8_t scale) { stutterConfig.pitchScale = scale; }
	inline void setLiveLeakyWriteProb(float prob) { stutterConfig.leakyWriteProb = prob; }
	inline void setLiveLatch(bool latch) { stutterConfig.latch = latch; }
	/// Mark that encoder was released during STANDBY (for momentary mode)
	inline void markReleasedDuringStandby() { releasedDuringStandby = true; }

	/// Arm stutter for quantized trigger (starts on next beat)
	/// Returns Error::NONE if armed successfully
	/// loopLengthSamples: for scatter modes, the length of the loop region in samples
	/// halfBar: if true, buffer contains 2 beats that should be virtually doubled for scatter processing
	[[nodiscard]] Error armStutter(void* source, ParamManagerForTimeline* paramManager, StutterConfig stutterConfig,
	                               int32_t magnitude, uint32_t timePerTickInverse, int64_t targetTick,
	                               size_t loopLengthSamples = 0, bool halfBar = false);

	/// Check if armed trigger should fire (call from audio processing)
	/// Returns true if stutter was started
	bool checkArmedTrigger(int64_t currentTick, ParamManager* paramManager, int32_t magnitude,
	                       uint32_t timePerTickInverse);

	/// Cancel armed stutter without starting
	void cancelArmed();

	/// Enable/disable standby recording for instant scatter engagement
	/// When enabled, buffer continuously records audio in ring-buffer fashion
	Error enableStandby(void* source, int32_t magnitude, uint32_t timePerTickInverse);
	void disableStandby();

	/// Feed audio to standby buffer (call during audio processing when standby is active)
	/// Only records if source matches the one that enabled standby
	/// Uses tick-boundary detection for sample-accurate beat-quantized recording start
	void recordStandby(void* source, std::span<StereoSample> audio, int64_t lastSwungTick, uint32_t syncLength);

	/// Check if pending play trigger should fire (call from audio processing)
	/// Uses tick-boundary detection for sample-accurate beat-quantized trigger
	/// Returns true if playback was started
	bool checkPendingTrigger(void* source, int64_t lastSwungTick, uint32_t syncLength, ParamManager* paramManager,
	                         int32_t magnitude, uint32_t timePerTickInverse);

	/// Check if source has a pending trigger waiting
	inline bool hasPendingTrigger(void* source) const { return pendingPlayTrigger && recordSource == source; }
	/// Clear pending trigger without canceling standby recording
	/// Use this in momentary mode when button is released during STANDBY
	inline void clearPendingTrigger(void* source) {
		if (recordSource == source) {
			pendingPlayTrigger = false;
		}
	}

private:
	enum class Status {
		OFF,
		STANDBY, ///< Buffer allocated and recording, but not affecting output
		ARMED,   ///< Waiting for beat-quantized trigger
		RECORDING,
		PLAYING,
	};
	int32_t getStutterRate(ParamManager* paramManager, int32_t magnitude, uint32_t timePerTickInverse);

	/// Trigger playback immediately (used by Repeat mode which bypasses beat quantization)
	/// Transitions from STANDBY to PLAYING, swaps buffers, resets playback state
	void triggerPlaybackNow(void* source);
	bool currentReverse;
	DelayBuffer buffer;
	Status status = Status::OFF;
	// TODO: This is currently unused! It's set to 7 initially, and never modified. Either we should set it depending
	// on sync, or get rid of it entirely.
	uint8_t sync = 7;
	StutterConfig stutterConfig;
	int32_t sizeLeftUntilRecordFinished = 0;
	int32_t valueBeforeStuttering = 0;
	int32_t lastQuantizedKnobDiff = 0;
	/// === CLEAN BUFFER OWNERSHIP MODEL ===
	/// Two independent ownership slots - can be same or different sources:
	/// - playSource: who is playing from playBuffer (gets scatter output)
	/// - recordSource: who is recording to recordBuffer (capturing audio)
	///
	/// When different, we have a takeover in progress:
	///   A is playSource (still playing), B is recordSource (preparing to take over)
	/// When B triggers, swap buffers and B becomes both playSource and recordSource.
	void* playSource = nullptr;   ///< Who owns playBuffer (or nullptr)
	void* recordSource = nullptr; ///< Who owns recordBuffer (or nullptr)

	/// Track if we started from standby mode (to return to it after stutter ends)
	bool startedFromStandby = false;

	/// Cached param values - preserved during scatter session
	/// These capture the original Sound's settings including any cable modulation
	/// Includes zone params and rate (page 1 settings like mode/pWrite are in stutterConfig)
	struct {
		q31_t zoneA{0};
		q31_t zoneB{0};
		q31_t macroConfig{0};
		q31_t macro{0};
		int32_t rate{0}; ///< Stutter rate param value
		bool valid{false};
	} cachedParams;

	/// Standby timeout: release after N bars of idle standby (no playback)
	static constexpr size_t kStandbyTimeoutBars = 32; ///< ~64 seconds at 120 BPM
	size_t standbyIdleSamples = 0;                    ///< Samples since last playback (reset on trigger)

	/// Double buffer system - swap instead of copy on trigger
	StereoSample* bufferA = nullptr;
	StereoSample* bufferB = nullptr;
	StereoSample* recordBuffer = nullptr; ///< Points to buffer being recorded
	StereoSample* playBuffer = nullptr;   ///< Points to buffer being played

	size_t recordWritePos = 0;     ///< Current write position in record buffer (ring buffer style)
	bool recordBufferFull = false; ///< True once ring buffer has wrapped (full loop available)
	size_t playbackStartPos = 0;   ///< Where captured bar starts in play buffer (ring buffer offset)
	size_t playbackLength = 0;     ///< Full captured bar length in samples

	/// Beat quantization for recording and playback
	bool waitingForRecordBeat = false; ///< In standby, waiting for beat before recording starts
	int64_t recordStartTick = 0;       ///< Tick to start recording at
	bool pendingPlayTrigger = false;   ///< User requested trigger, waiting for beat
	int64_t playTriggerTick = 0;       ///< Tick to trigger playback at

	/// Slice playback system - flexible enough for complex patterns
	/// A "slice" is a region within the captured bar defined by offset and length.
	/// Future scatter modes can play arbitrary patterns like:
	///   - Reordered beats: [beat3, beat1, beat2, beat4]
	///   - Fractional positions: [1.0, 3.5, 2.0, 4.5]
	///   - Variable lengths: different slice sizes in sequence
	///
	/// For Repeat mode: single slice from end of bar, length controlled by rate knob
	/// For future modes: sliceStartOffset/currentSliceLength set by pattern sequencer
	size_t playbackPos = 0;         ///< Current read offset within current slice
	size_t sliceStartOffset = 0;    ///< Offset from bar start to current slice (in samples)
	size_t currentSliceLength = 0;  ///< Length of current slice (in samples)
	size_t scatterLinearBarPos = 0; ///< Linear position in bar for leaky writes (grid-aligned)

	/// Gated stutter (Reverse mode): fixed grain length captured at trigger time
	/// Rate knob changes trigger spacing, not grain size (no pitch change)
	size_t gatedGrainLength = 0;   ///< Fixed grain size in samples (set at trigger)
	size_t gatedGrainReadPos = 0;  ///< Position within grain being read
	size_t gatedCyclePos = 0;      ///< Position within current gated cycle
	uint32_t gatedInitialRate = 0; ///< Rate at trigger time (for cycle length scaling)
	size_t gatedInitialCycle = 0;  ///< Initial cycle length (buffer.size() at trigger)

	/// Convert beat position to sample offset within captured bar
	/// Supports fractional beats (e.g., 2.5 = halfway through beat 3)
	/// @param beat Beat number (0-based, can be fractional)
	/// @param beatsPerBar Number of beats in the bar (typically 4)
	/// @return Sample offset from start of captured bar
	size_t beatToSamples(float beat, size_t beatsPerBar = 4) const {
		if (playbackLength == 0 || beatsPerBar == 0) {
			return 0;
		}
		size_t samplesPerBeat = playbackLength / beatsPerBar;
		return static_cast<size_t>(beat * samplesPerBeat);
	}

	/// Set current slice by beat position and length
	/// @param startBeat Start beat (0-based, can be fractional)
	/// @param lengthBeats Length in beats (can be fractional)
	/// @param beatsPerBar Number of beats in the bar
	void setSliceByBeat(float startBeat, float lengthBeats, size_t beatsPerBar = 4) {
		sliceStartOffset = beatToSamples(startBeat, beatsPerBar);
		currentSliceLength = beatToSamples(lengthBeats, beatsPerBar);
		if (currentSliceLength < 256) {
			currentSliceLength = 256; // Minimum to avoid clicks
		}
	}

	/// Half-bar mode: when bar is too long for buffer, we capture 2 beats and virtually double them
	bool halfBarMode = false;

	/// Scatter/Shuffle state
	int32_t scatterSliceIndex{0};      ///< Current sequential slice (0 to numSlices-1)
	int32_t scatterRepeatLoopIndex{0}; ///< Repeat mode: loop iteration counter (wraps at slices-per-bar)
	int32_t scatterNumSlices{8};       ///< Number of slices to divide bar into
	bool scatterReversed{false};       ///< Whether current slice is playing reversed
	float scatterDryMix{0};            ///< Per-grain dry value [0,1] compared against threshold
	float scatterDryThreshold{1};      ///< Threshold for dry cut [0,1]: higher = more grains, lower = more dry
	float scatterEnvDepth{0};          ///< Envelope depth [0,1]: 0=hard cut, 1=full envelope
	float scatterEnvShape{0.5f};       ///< Envelope shape [0,1]: 0=fade-out, 0.5=symmetric, 1=fade-in
	float scatterEnvWidth{1.0f};       ///< Envelope region [0,1]: 1=full slice, smaller=edges only
	float scatterGateRatio{1.0f};      ///< Gate duty cycle [0,1]: 1=full slice, smaller=truncated with silence
	float scatterPan{0};               ///< Crossfeed pan [-1,1]: 0=center, +1=L→R, -1=R→L
	int32_t scatterPanCounter{0};      ///< Ever-incrementing counter for decorrelated pan (not tied to slice content)

	/// Precomputed pan coefficients (Q31 fixed-point, computed once per slice)
	int32_t scatterPanFadeQ31{0};          ///< Fading side multiplier: (1 - |pan|)
	int32_t scatterPanCrossQ31{0};         ///< Crossfeed amount: |pan|/2
	bool scatterPanRight{false};           ///< Pan direction: true = pan right (L fades), false = pan left (R fades)
	bool scatterPanActive{false};          ///< Precomputed: pan != 0, skip per-sample check
	bool scatterEnvActive{false};          ///< Precomputed: depth > 0, envelope applies
	bool scatterGateActive{false};         ///< Precomputed: gate < 1, truncation applies
	int32_t scatterSubdivisions{1};        ///< Current subdivision count (1,2,3,4,6,8,12) - ratchet
	int32_t scatterSubdivIndex{0};         ///< Current subdivision within slice [0, subdivisions-1]
	size_t scatterSubSliceLength{256};     ///< Precomputed: currentSliceLength / subdivisions (avoid per-sample div)
	size_t scatterLastSubSliceLength{256}; ///< Last subdivision gets remainder to prevent timing drift
	bool needsSliceSetup{true};            ///< Dirty flag: set when slice completes, cleared after setup
	bool scatterPitchUp{false};            ///< Pitch up via sample decimation (2x = octave up)
	bool scatterConsecutive{false};        ///< Slices are sequential (skip ZC when env=0)
	int32_t scatterPitchUpLoopCount{0};    ///< Which loop of pitch-up grain (0=first, 1=second)
	uint32_t scatterPitchRatioFP{65536};   ///< Pitch ratio (16.16 fixed-point), 65536 = 1.0 (unison)
	uint32_t scatterPitchPosFP{0};         ///< Fixed-point position accumulator for pitch shifting
	int32_t scatterParamThrottle{0};       ///< Buffers since last param update (throttle to 1 per 10 buffers)

	/// Repeat grain state (inverse of ratchet - hold same grain for N slices)
	int32_t scatterRepeatCounter{0};                        ///< Countdown for repeat mode (0 = compute new grain)
	deluge::dsp::scatter::GrainParams scatterCachedGrain{}; ///< Cached grain during repeat (skip computeGrainParams)

	/// Bar counter for multi-bar patterns (0 to kBarIndexWrap-1)
	/// Individual bits used as offsets with Zone B-derived weights to shift Zone A
	static constexpr int32_t kBarIndexWrap = 16; ///< Bar counter wraps at 16 (supports phrases up to 16 bars)
	int32_t scatterBarIndex{0};

	/// Tick-based bar boundary detection for grid sync
	/// When currentTick/barLength crosses to a new bar, we force sync to bar start
	int64_t lastTickBarIndex{-1}; ///< Last bar index from tick clock (-1 = not initialized)

	/// Precomputed envelope parameters (Q31 fixed-point, computed once per slice, used per-sample)
	deluge::dsp::scatter::GrainEnvPrecomputedQ31 scatterEnvPrecomputed{};

	/// Anti-click: mute at zero crossings for attack/release (per-channel)
	bool waitingForZeroCrossL{true};                 ///< Attack L: mute until zero crossing detected
	bool waitingForZeroCrossR{true};                 ///< Attack R: mute until zero crossing detected
	bool releaseMutedL{false};                       ///< Release L: mute after zero crossing found
	bool releaseMutedR{false};                       ///< Release R: mute after zero crossing found
	q31_t prevOutputL{0};                            ///< Previous L output for zero crossing detection
	q31_t prevOutputR{0};                            ///< Previous R output for zero crossing detection
	static constexpr size_t kMinGrainSize = 256;     ///< Minimum grain size in samples (~5.8ms)
	static constexpr size_t kGrainReleaseZone = 662; ///< ~15ms release window before grain end (ZC search, 33Hz min)
	static constexpr size_t kBarEndZone = 2205;      ///< ~50ms silent window before bar/phrase end (slop)
	static constexpr int32_t kTimePhraseLength = 4;  ///< Time mode phrase length in bars (reset every N bars)

	/// Buffer wrap fade: destructive fade at ring buffer boundary (position 0)
	/// Applied once when buffer is captured, not per-sample during playback
	static constexpr size_t kBufferWrapFadeLen = 220; ///< ~5ms fade at buffer boundary

	/// === STATIC vs DYNAMIC PARAM SEPARATION ===
	/// STATIC params: Only depend on zone knob positions (zoneA, zoneB, macroConfig, macro)
	///   - Recompute only when knob values change (checked at slice boundary)
	///   - Includes: macro influence, subdiv influence, base envDepth/pan (standard mode)
	/// DYNAMIC params: Depend on sliceIndex/slicePhase (changes every slice)
	///   - Must recompute every slice boundary
	///   - Includes: sliceOffset, skipProb, reverseProb, filter, delayFeed (in evolution mode)

	/// Static phi triangles - only recompute when knob inputs change
	/// Saves ~500 cycles/slice when params are static
	struct StaticTriangles {
		// Cached input values for change detection
		q31_t lastZoneBParam{0};
		q31_t lastMacroConfigParam{0};
		q31_t lastMacroParam{0};

		// Outputs: depend only on macroConfig (not slicePhase)
		float subdivInfluence{0};     ///< triangleSimpleUnipolar(macroConfig * kPhi225, 0.5f)
		float zoneAMacroInfluence{0}; ///< triangleSimpleUnipolar(macroConfig * kPhi050, 0.5f)
		float zoneBMacroInfluence{0}; ///< triangleSimpleUnipolar(macroConfig * kPhi075, 0.5f)

		// Threshold scales for reverse/pitch/delay probability (bipolar, macro-scaled)
		float reverseScale{0}; ///< triangleFloat(macroConfig * kPhi125, 0.6f) [-1,1]
		float pitchScale{0};   ///< triangleFloat(macroConfig * kPhi200, 0.6f) [-1,1]
		float delayScale{0};   ///< triangleFloat(macroConfig * kPhi075, 0.6f) [-1,1]

		// Outputs: depend only on zoneB (standard mode, not evolution mode)
		float envDepthBase{0};  ///< triangleSimpleUnipolar(zoneBPos * kPhi050, 0.6f)
		float panAmountBase{0}; ///< triangleSimpleUnipolar(zoneBPos * kPhi125, 0.25f)

		// Delay params: independent phase, not tied to slice index
		float delayTimeMod{1.0f}; ///< Multiplier around slice length [0.5, 2.0]
		// Note: feedback is fixed at 50% via bit shift (no variable feedback)

		bool valid{false}; ///< Force recompute on scatter start
	} staticTriangles;

	/// Delay send/return state (slice-synced delay, fully integer)
	static constexpr size_t kDelayBufferSize = 32768; ///< ~0.74s at 44.1kHz (256KB) - quarter bar down to ~80 BPM
	StereoSample* delayBuffer{nullptr};
	size_t delayWritePos{0};
	size_t delayTime{0};       ///< Delay time in samples (= slice length, capped)
	uint8_t delaySendShift{0}; ///< Send via bit shift: 2=25%, 1=50%, 0=100%
	bool delayActive{false};   ///< Skip processing when send=0

	/// Stored config for takeover (when recordSource triggers playback)
	StutterConfig armedConfig{};
	size_t armedLoopLengthSamples{0};
	bool armedHalfBarMode{false};
	/// Track if encoder was released during STANDBY (for momentary mode: don't start playback)
	bool releasedDuringStandby{false};
};

// There's only one stutter effect active at a time, so we have a global stutterer to save memory.
// NOTE: Classic mode uses DelayBuffer, scatter modes use double buffers (bufferA/bufferB).
// These are separate memory, so in theory classic + scatter could run simultaneously on
// different tracks. Would require separating the state (status, playSource, recordSource) per mode.
extern Stutterer stutterer;
