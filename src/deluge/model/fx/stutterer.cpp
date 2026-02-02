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

#include "model/fx/stutterer.h"
#include "dsp/scatter.hpp"
#include "dsp/stereo_sample.h"
#include "io/debug/fx_benchmark.h"
#include "memory/memory_allocator_interface.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>

namespace params = deluge::modulation::params;
namespace hash = deluge::dsp::hash;

// Pitch mode: semitone offsets with tonic bias (duplicates increase probability)
// Each scale/triad biases toward tonic (0) and important chord tones
// Scales: Chromatic, Major, Minor, MajPent, MinPent, Blues, Dorian, Mixolydian
// Triads: MajTri, MinTri, Sus4, Dim
// Fixed semitones: +1 through +13 (single interval repeated)
static constexpr int8_t kScaleSemitones[25][8] = {
    {0, 0, 0, 3, 5, 7, 7, 12},        // 0: Chromatic: tonic-heavy with 5th and octave
    {0, 0, 4, 4, 7, 7, 0, 12},        // 1: Major: tonic (3x), 3rd (2x), 5th (2x), octave
    {0, 0, 3, 3, 7, 7, 0, 12},        // 2: Minor: tonic (3x), m3rd (2x), 5th (2x), octave
    {0, 0, 4, 7, 7, 0, 12, 12},       // 3: MajPent: tonic (3x), 3rd, 5th (2x), octave (2x)
    {0, 0, 3, 7, 7, 0, 10, 12},       // 4: MinPent: tonic (3x), m3rd, 5th (2x), b7, octave
    {0, 0, 3, 6, 7, 7, 0, 12},        // 5: Blues: tonic (3x), m3rd, b5, 5th (2x), octave
    {0, 0, 3, 5, 7, 7, 9, 12},        // 6: Dorian: tonic (2x), m3rd, 4th, 5th (2x), 6th, octave
    {0, 0, 4, 5, 7, 7, 10, 12},       // 7: Mixolydian: tonic (2x), 3rd, 4th, 5th (2x), b7, octave
    {0, 0, 0, 4, 4, 7, 7, 12},        // 8: MajTri: tonic (3x), 3rd (2x), 5th (2x), octave
    {0, 0, 0, 3, 3, 7, 7, 12},        // 9: MinTri: tonic (3x), m3rd (2x), 5th (2x), octave
    {0, 0, 0, 5, 5, 7, 7, 12},        // 10: Sus4: tonic (3x), 4th (2x), 5th (2x), octave
    {0, 0, 0, 3, 3, 6, 6, 12},        // 11: Dim: tonic (3x), m3rd (2x), b5 (2x), octave
    {1, 1, 1, 1, 1, 1, 1, 1},         // 12: +1 semitone (minor 2nd)
    {2, 2, 2, 2, 2, 2, 2, 2},         // 13: +2 semitones (major 2nd)
    {3, 3, 3, 3, 3, 3, 3, 3},         // 14: +3 semitones (minor 3rd)
    {4, 4, 4, 4, 4, 4, 4, 4},         // 15: +4 semitones (major 3rd)
    {5, 5, 5, 5, 5, 5, 5, 5},         // 16: +5 semitones (perfect 4th)
    {6, 6, 6, 6, 6, 6, 6, 6},         // 17: +6 semitones (tritone)
    {7, 7, 7, 7, 7, 7, 7, 7},         // 18: +7 semitones (perfect 5th)
    {8, 8, 8, 8, 8, 8, 8, 8},         // 19: +8 semitones (minor 6th)
    {9, 9, 9, 9, 9, 9, 9, 9},         // 20: +9 semitones (major 6th)
    {10, 10, 10, 10, 10, 10, 10, 10}, // 21: +10 semitones (minor 7th)
    {11, 11, 11, 11, 11, 11, 11, 11}, // 22: +11 semitones (major 7th)
    {12, 12, 12, 12, 12, 12, 12, 12}, // 23: +12 semitones (octave)
    {13, 13, 13, 13, 13, 13, 13, 13}, // 24: +13 semitones (octave + minor 2nd)
};

// Pitch ratios as 16.16 fixed-point for semitone offsets 0-17
// ratio = 2^(semitones/12) * 65536
static constexpr uint32_t kPitchRatioFP[18] = {
    65536,  // 0: 1.0000
    69433,  // 1: 1.0595
    73562,  // 2: 1.1225
    77936,  // 3: 1.1892
    82570,  // 4: 1.2599
    87480,  // 5: 1.3348
    92682,  // 6: 1.4142
    98193,  // 7: 1.4983
    104032, // 8: 1.5874
    110218, // 9: 1.6818
    116772, // 10: 1.7818
    123715, // 11: 1.8877
    131072, // 12: 2.0000 (octave)
    138866, // 13: 2.1189
    147123, // 14: 2.2449
    155872, // 15: 2.3784
    165140, // 16: 2.5198
    174959, // 17: 2.6697
};

Stutterer stutterer{};

void Stutterer::initParams(ParamManager* paramManager) {
	paramManager->getUnpatchedParamSet()->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);
}

int32_t Stutterer::getStutterRate(ParamManager* paramManager, int32_t magnitude, uint32_t timePerTickInverse) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	int32_t paramValue = unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE);

	// Quantized Stutter diff
	int32_t knobPos = unpatchedParams->paramValueToKnobPos(paramValue, nullptr);
	knobPos = knobPos + lastQuantizedKnobDiff;
	if (knobPos < -64) {
		knobPos = -64;
	}
	else if (knobPos > 64) {
		knobPos = 64;
	}
	paramValue = unpatchedParams->knobPosToParamValue(knobPos, nullptr);

	int32_t rate =
	    getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_DELAY_RATE], cableToExpParamShortcut(paramValue));

	if (sync != 0) {
		rate = multiply_32x32_rshift32(rate, timePerTickInverse);
		int32_t lShiftAmount = sync + 6 - magnitude;
		int32_t limit = 2147483647 >> lShiftAmount;
		rate = std::min(rate, limit);
		rate <<= lShiftAmount;
	}
	return rate;
}

Error Stutterer::beginStutter(void* source, ParamManagerForTimeline* paramManager, StutterConfig sc, int32_t magnitude,
                              uint32_t timePerTickInverse, size_t loopLengthSamples, bool halfBar) {
	stutterConfig = sc;
	currentReverse = stutterConfig.reversed;
	halfBarMode = halfBar;

	// Non-Classic/Burst modes: single shared buffer (pWrite controls content evolution)
	// Classic and Burst use the simple DelayBuffer, others use the looper system
	bool useLooper =
	    (stutterConfig.scatterMode != ScatterMode::Classic && stutterConfig.scatterMode != ScatterMode::Burst);
	if (useLooper) {
		// Check if this is a takeover trigger (pendingSource is armed to take over)
		bool isTakeoverTrigger = (pendingSource == source && activeSource != source && status == Status::PLAYING);
		if (isTakeoverTrigger) {
			// Takeover: inherit buffer content instantly, start playing
			// Preserve playbackStartPos - triggerPlaybackNow would recalculate from looperWritePos (which is 0)
			size_t inheritedStartPos = playbackStartPos;
			stutterConfig = armedConfig;
			currentReverse = stutterConfig.reversed;
			if (loopLengthSamples == 0) {
				loopLengthSamples = armedLoopLengthSamples;
			}
			halfBarMode = armedHalfBarMode;
			playbackLength = std::min(loopLengthSamples, kLooperBufferSize);
			triggerPlaybackNow(source);
			playbackStartPos = inheritedStartPos; // Restore inherited position
			return Error::NONE;
		}

		// If source owns the buffer, request playback trigger
		if (looperBuffer != nullptr && activeSource == source && loopLengthSamples > 0) {
			// Update armedConfig for retrigger (source's current config)
			armedConfig = sc;
			releasedDuringStandby = false; // Fresh trigger, encoder is held

			// Use full loop length for correct timing
			playbackLength = std::min(loopLengthSamples, kLooperBufferSize);

			// Allow trigger if we have enough samples or buffer has stale audio (tape loop style)
			bool hasEnoughSamples = looperBufferFull || looperWritePos >= playbackLength;
			if (!hasEnoughSamples && !waitingForRecordBeat) {
				// No fresh samples - wait for more
				return Error::NONE;
			}

			// Repeat mode triggers immediately (no beat quantization)
			if (stutterConfig.scatterMode == ScatterMode::Repeat && hasEnoughSamples) {
				triggerPlaybackNow(source);
				return Error::NONE;
			}

			// Set pending trigger - actual transition happens in checkPendingTrigger on next beat
			pendingPlayTrigger = true;
			playTriggerTick = 0; // Will be computed in checkPendingTrigger
			return Error::NONE;
		}

		// Buffer exists but someone else is PLAYING - immediate takeover
		// Only takeover when activeSource is PLAYING, not just STANDBY
		// This allows track B to start fresh recording if track A is idle
		if (looperBuffer != nullptr && activeSource != nullptr && activeSource != source && status == Status::PLAYING) {
			// Takeover: inherit buffer content instantly, new source starts playing
			// Preserve playbackStartPos - triggerPlaybackNow would recalculate from looperWritePos (which is 0)
			size_t inheritedStartPos = playbackStartPos;
			playbackLength = std::min(loopLengthSamples, kLooperBufferSize);
			if (playbackLength == 0) {
				playbackLength = looperBufferFull ? kLooperBufferSize : looperWritePos;
			}
			triggerPlaybackNow(source);
			playbackStartPos = inheritedStartPos; // Restore inherited position
			return Error::NONE;
		}

		// Buffer exists but owner is idle (STANDBY/OFF) - take ownership, start fresh recording
		if (looperBuffer != nullptr && activeSource != nullptr && activeSource != source) {
			// Clear previous owner, new source takes over buffer
			activeSource = source;
			pendingSource = nullptr;
			looperWritePos = 0;
			looperBufferFull = false;
			waitingForRecordBeat = (stutterConfig.scatterMode != ScatterMode::Repeat);
			recordStartTick = 0;
			pendingPlayTrigger = false;
			standbyIdleSamples = 0;
			releasedDuringStandby = false;
			armedConfig = sc;
			armedLoopLengthSamples = loopLengthSamples;
			armedHalfBarMode = halfBar;
			status = Status::STANDBY;
			return Error::NONE;
		}

		// No buffer yet - allocate and start standby recording
		// Use allocLowSpeed() to go directly to SDRAM - buffer is too large for external region
		if (looperBuffer == nullptr) {
			looperBuffer = static_cast<StereoSample*>(allocLowSpeed(kLooperBufferSize * sizeof(StereoSample)));
			if (looperBuffer == nullptr) {
				status = Status::OFF;
				return Error::INSUFFICIENT_RAM;
			}
			memset(looperBuffer, 0, kLooperBufferSize * sizeof(StereoSample));
		}
		// Allocate delay send buffer (small, for slice-synced echo)
		if (delayBuffer == nullptr) {
			delayBuffer = static_cast<StereoSample*>(allocLowSpeed(kDelayBufferSize * sizeof(StereoSample)));
			if (delayBuffer != nullptr) {
				memset(delayBuffer, 0, kDelayBufferSize * sizeof(StereoSample));
			}
			// Not fatal if allocation fails - delay just won't work
		}
		delayWritePos = 0;
		delayActive = false;
		looperWritePos = 0;
		looperBufferFull = false; // Fresh buffer, not full yet
		// Repeat mode records immediately; other modes wait for beat
		waitingForRecordBeat = (stutterConfig.scatterMode != ScatterMode::Repeat);
		recordStartTick = 0;        // Will be computed in recordStandby
		pendingPlayTrigger = false; // No pending trigger yet

		// Source claims buffer, starts recording in STANDBY
		activeSource = source;
		pendingSource = nullptr;
		standbyIdleSamples = 0;        // Reset timeout for new owner
		releasedDuringStandby = false; // Fresh start, encoder is held
		// Store config for retrigger/takeover
		armedConfig = sc;
		armedLoopLengthSamples = loopLengthSamples;
		armedHalfBarMode = halfBar;
		if (status != Status::PLAYING) {
			status = Status::STANDBY;
		}
		return Error::NONE;
	}

	// Classic mode: original community behavior
	// Quantized snapping
	if (stutterConfig.quantized) {
		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
		int32_t paramValue = unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE);
		int32_t knobPos = unpatchedParams->paramValueToKnobPos(paramValue, nullptr);
		if (knobPos < -39) {
			knobPos = -16; // 4ths
		}
		else if (knobPos < -14) {
			knobPos = -8; // 8ths
		}
		else if (knobPos < 14) {
			knobPos = 0; // 16ths
		}
		else if (knobPos < 39) {
			knobPos = 8; // 32nds
		}
		else {
			knobPos = 16; // 64ths
		}
		valueBeforeStuttering = paramValue;
		lastQuantizedKnobDiff = knobPos;
		unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);
	}

	startedFromStandby = false;
	Error error = buffer.init(getStutterRate(paramManager, magnitude, timePerTickInverse), 0, true);
	if (error == Error::NONE) {
		status = Status::RECORDING;
		sizeLeftUntilRecordFinished = buffer.size();
		activeSource = source;
		pendingSource = nullptr;
	}
	return error;
}

/// Mode name tags for benchmarking
static constexpr const char* kScatterModeNames[] = {
    "classic", "repeat", "burst", "time", "shuffle", "leaky", "pitch", "pattern",
};

// === SCATTER PERFORMANCE BENCHMARKS (128-sample buffer, 44.1kHz) ===
// Measured on Deluge hardware, Shuffle mode:
//   total:  ~2,100 cycles/buffer typical, ~3,500 worst case (32nds + ratchet)
//   env:     ~78-103 cycles/sample (only in fade regions)
//   pan:       ~60 cycles/sample  (only when pan active)
//   record:    ~44 cycles/sample  (continuous)
//   params: ~2,200 cycles/slice   (computeGrainParams, once per slice)
//   slice:  ~4,500 cycles/slice   (full slice boundary setup)
// Reference: chorus ~2,300 cycles, flanger ~2,300 cycles
// Worst case (32nds + x3 subdiv) still under 2x chorus budget
// Note: envDepth blend disabled (~30% overhead), envShape still works

void Stutterer::processStutter(std::span<StereoSample> audio, ParamManager* paramManager, int32_t magnitude,
                               uint32_t timePerTickInverse, int64_t currentTick, uint64_t timePerTickBig,
                               uint32_t barLengthInTicks, const q31_t* modulatedValues) {

	// Non-Classic/Burst modes: single buffer - play and write to looperBuffer
	// pWrite controls content evolution (0=freeze, 50=always overwrite)
	constexpr bool kEnableDelay = true;
	bool useLooper =
	    (stutterConfig.scatterMode != ScatterMode::Classic && stutterConfig.scatterMode != ScatterMode::Burst);
	if (useLooper) {
		if (status == Status::PLAYING && looperBuffer != nullptr && playbackLength > 0) {
			// Benchmark: granular scatter processing with dynamic tags
			// Tag layout: [0]=type, [1]=mode, [2]=extra (slices/subdiv for slice benchmark)
			FX_BENCH_DECLARE(benchTotal, "scatter", "total");
			FX_BENCH_DECLARE(benchSlice, "scatter", "slice");
			FX_BENCH_DECLARE(benchParams, "scatter", "params");
			FX_BENCH_DECLARE(benchParamRead, "scatter", "paramread");
			FX_BENCH_DECLARE(benchStatic, "scatter", "static");
			FX_BENCH_DECLARE(benchEnvPrep, "scatter", "envprep");
			FX_BENCH_DECLARE(benchEnv, "scatter", "env");
			FX_BENCH_DECLARE(benchPan, "scatter", "pan");
			FX_BENCH_DECLARE(benchDelay, "scatter", "delay");
			FX_BENCH_DECLARE(benchRecord, "scatter", "record");
			FX_BENCH_DECLARE(benchRead, "scatter", "read");
			FX_BENCH_DECLARE(benchAdvance, "scatter", "advance");
			const char* modeName = kScatterModeNames[static_cast<int>(stutterConfig.scatterMode)];
			FX_BENCH_SET_TAG(benchTotal, 1, modeName);
			FX_BENCH_SET_TAG(benchSlice, 1, modeName);
			FX_BENCH_SET_TAG(benchParams, 1, modeName);
			FX_BENCH_SET_TAG(benchParamRead, 1, modeName);
			FX_BENCH_SET_TAG(benchStatic, 1, modeName);
			FX_BENCH_SET_TAG(benchEnvPrep, 1, modeName);
			FX_BENCH_SET_TAG(benchEnv, 1, modeName);
			FX_BENCH_SET_TAG(benchPan, 1, modeName);
			FX_BENCH_SET_TAG(benchDelay, 1, modeName);
			FX_BENCH_SET_TAG(benchRecord, 1, modeName);
			FX_BENCH_SET_TAG(benchRead, 1, modeName);
			FX_BENCH_SET_TAG(benchAdvance, 1, modeName);
			FX_BENCH_START(benchTotal);

			// Sample counter for benchmarking (only first sample per buffer)
			int32_t sampleIdx = 0;

			// Flag for Repeat mode: bar boundary triggers grain param update without position reset
			bool repeatBarBoundaryUpdate = false;

			// === TICK-BASED BAR SYNC: Lock to grid at every bar boundary ===
			// When the tick clock shows we've entered a new bar, force reset to bar start.
			// This corrects accumulated drift and keeps slices aligned with the beat grid.
			// Repeat mode skips position reset (loops continuously) but still tracks bar for hash evolution
			if (barLengthInTicks > 0 && currentTick >= 0) {
				int64_t tickBarIndex = currentTick / static_cast<int64_t>(barLengthInTicks);

				// First buffer after trigger: sync linear position to current bar position
				// Trigger happens at beat boundary, not bar boundary - compensate for offset
				// Only sync scatterLinearBarPos (for Leaky writes), not scatterSliceIndex
				// Slice index is computed fresh in slice setup based on rate knob
				if (lastTickBarIndex < 0 && playbackLength > 0) {
					int64_t ticksIntoBar = currentTick % static_cast<int64_t>(barLengthInTicks);
					size_t samplesIntoBar = (ticksIntoBar * playbackLength) / barLengthInTicks;
					scatterLinearBarPos = samplesIntoBar % playbackLength;
				}

				if (lastTickBarIndex >= 0 && tickBarIndex != lastTickBarIndex) {
					// Bar boundary crossed - increment bar index for hash evolution
					scatterBarIndex = (scatterBarIndex + 1) % kBarIndexWrap;

					// Repeat mode: continuous loop, never reset
					if (stutterConfig.scatterMode == ScatterMode::Repeat) {
						needsSliceSetup = true;         // Recompute grain params with new bar index
						repeatBarBoundaryUpdate = true; // Flag to skip playbackPos reset
					}
					// Time mode: full sync every N bars (kTimePhraseLength), continue pattern within phrase
					else if (stutterConfig.scatterMode == ScatterMode::Time) {
						if ((scatterBarIndex % kTimePhraseLength) == 0) {
							// Phrase boundary: full transport sync reset
							scatterSliceIndex = 0;
							playbackPos = 0;
							waitingForZeroCrossL = waitingForZeroCrossR = true;
							releaseMutedL = releaseMutedR = false;
							scatterSubdivIndex = 0;
							scatterPitchUpLoopCount = 0;
							scatterLinearBarPos = 0;
							scatterRepeatCounter = 0;
							needsSliceSetup = true;
						}
						else {
							needsSliceSetup = true;
							repeatBarBoundaryUpdate = true; // Continue within 4-bar phrase
						}
					}
					else {
						// Force sync to bar start (bar-level ZC mute already happened)
						scatterSliceIndex = 0;
						playbackPos = 0;
						waitingForZeroCrossL = waitingForZeroCrossR = true;
						releaseMutedL = releaseMutedR = false;
						// Keep prevOutput to detect ZC at the cut point (don't reset to 0)
						scatterSubdivIndex = 0;
						scatterPitchUpLoopCount = 0; // Reset pitch up loop state
						scatterLinearBarPos = 0;     // Reset linear position for leaky writes
						needsSliceSetup = true;
						scatterRepeatCounter = 0; // Fresh params for new bar
						// Also resync playbackLength
						if (timePerTickBig != 0) {
							size_t newLoopLength = ((uint64_t)barLengthInTicks * timePerTickBig) >> 32;
							newLoopLength = std::min(newLoopLength, kLooperBufferSize);
							playbackLength = newLoopLength;
						}
						// All looper modes: single buffer, pWrite controls writes
						// Content evolves via pWrite probability
					}
				}
				lastTickBarIndex = tickBarIndex;
			}

			// === SLICE SETUP: runs immediately when slice boundary was hit ===
			// Throttle controls expensive param reads, not the slice state updates
			scatterParamThrottle++;
			bool shouldRecalcParams = (scatterParamThrottle >= 10 || currentSliceLength == 0);
			if (needsSliceSetup) {
				needsSliceSetup = false;
				if (shouldRecalcParams) {
					scatterParamThrottle = 0;
				}
				// Reset playbackPos unless this is a Repeat bar-boundary update (continuous loop)
				if (!repeatBarBoundaryUpdate) {
					playbackPos = 0; // Snap to slice start, accept jitter
				}
				// ZC protect when params change, UNLESS grain is consecutive (audio flows naturally)
				// scatterNextConsecutive was computed by previous grain to predict this grain
				if (!scatterNextConsecutive) {
					waitingForZeroCrossL = waitingForZeroCrossR = true;
					prevOutputL = prevOutputR = 0;
				}
				releaseMutedL = releaseMutedR = false;
				repeatBarBoundaryUpdate = false; // Clear flag after use
				FX_BENCH_START(benchSlice);
				switch (stutterConfig.scatterMode) {
				case ScatterMode::Repeat:  // Falls through to Shuffle with isRepeat flag
				case ScatterMode::Time:    // Time uses Shuffle but overrides stretch/sparse from zones
				case ScatterMode::Grain:   // Grain mode: dual-voice crossfade (falls through to Shuffle for now)
				case ScatterMode::Pattern: // Pattern mode: Zone A selects slice reordering pattern
				case ScatterMode::Pitch:   // Pitch mode: Zone A selects scale degree for transposition
				case ScatterMode::Shuffle: {
					bool isRepeat = (stutterConfig.scatterMode == ScatterMode::Repeat);
					bool isTime = (stutterConfig.scatterMode == ScatterMode::Time);
					bool isPattern = (stutterConfig.scatterMode == ScatterMode::Pattern);
					bool isPitch = (stutterConfig.scatterMode == ScatterMode::Pitch);
					FX_BENCH_START(benchParamRead);
					// Rate knob controls number of slices - match UI note division labels
					// UI optionValues: {2, 6, 13, 19, 25, 31, 38, 47} for 0-50 range
					// Maps to: 1 BAR, 2nds, 4ths, 8ths, 16ths, 32nds, 64ths, 128ths
					UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
					int32_t rateParam = unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE);
					int32_t knobPos = unpatchedParams->paramValueToKnobPos(rateParam, nullptr);

					if (isRepeat) {
						// Repeat: continuous exponential curve from full bar to minimum
						// knobPos -64 to +64 → normalized 128 to 0
						int32_t normalized = 64 - knobPos;
						if (normalized < 0) {
							normalized = 0;
						}
						if (normalized > 128) {
							normalized = 128;
						}
						constexpr size_t kMinSlice = 256; // ~6ms minimum
						currentSliceLength = (playbackLength * normalized * normalized) / (128 * 128);
						if (currentSliceLength < kMinSlice) {
							currentSliceLength = kMinSlice;
						}
						if (currentSliceLength > playbackLength) {
							currentSliceLength = playbackLength;
						}
						scatterNumSlices = 1; // Not used for Repeat but keep consistent
						// Loop counter: increment and wrap at 8 for bounded hash variation
						scatterRepeatLoopIndex = (scatterRepeatLoopIndex + 1) & 0x7;
					}
					else {
						// Shuffle: discrete note divisions from rate knob
						// Convert knobPos (-64..+64) to UI value (0..50) range
						int32_t uiValue = ((knobPos + 64) * 50) / 128;
						// Map UI value to note divisions (thresholds at midpoints)
						if (uiValue < 4) {
							scatterNumSlices = 1; // 1 BAR
						}
						else if (uiValue < 9) {
							scatterNumSlices = 2; // 2nds (half notes)
						}
						else if (uiValue < 16) {
							scatterNumSlices = 4; // 4ths (quarter notes)
						}
						else if (uiValue < 22) {
							scatterNumSlices = 8; // 8ths
						}
						else if (uiValue < 28) {
							scatterNumSlices = 16; // 16ths
						}
						else {
							scatterNumSlices = 32; // 32nds (max)
						}
						// Pitch mode: halve slices for longer grains (pitch needs time to be heard)
						if (isPitch && scatterNumSlices > 1) {
							scatterNumSlices /= 2;
						}
					}

					// Zone params: use cached values, refresh if throttle allows
					q31_t zoneAParam = cachedZoneAParam;
					q31_t zoneBParam = cachedZoneBParam;
					q31_t macroConfigParam = cachedMacroConfigParam;
					q31_t macroParam = cachedMacroParam;

					if (shouldRecalcParams) {
						// Helper: convert hybrid param output to unipolar Q31
						// Hybrid output for positive presets: [0, +1073741824]
						// Scale x2 to get full unipolar range: [0, 2147483647]
						auto hybridToUnipolar = [](int32_t hybrid) -> q31_t {
							return static_cast<q31_t>(
							    std::clamp<int64_t>(static_cast<int64_t>(hybrid) * 2, 0, 2147483647));
						};

						// Helper: convert bipolar storage to unipolar Q31
						// Bipolar range: INT32_MIN to INT32_MAX → Unipolar: 0 to INT32_MAX
						auto bipolarToUnipolar = [](int32_t bipolar) -> q31_t {
							return static_cast<q31_t>((static_cast<int64_t>(bipolar) + 2147483648LL) >> 1);
						};

						// Read fresh zone params from param set
						if (modulatedValues && paramManager->hasPatchedParamSet()) {
							constexpr int32_t kCableScale = 4;
							PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
							zoneAParam = patchedParams->getValue(params::GLOBAL_SCATTER_ZONE_A)
							             + modulatedValues[0] / kCableScale;
							zoneBParam = patchedParams->getValue(params::GLOBAL_SCATTER_ZONE_B)
							             + modulatedValues[1] / kCableScale;
							macroConfigParam = patchedParams->getValue(params::GLOBAL_SCATTER_MACRO_CONFIG)
							                   + modulatedValues[2] / kCableScale;
							// Hybrid params: convert bipolar patcher output to unipolar
							macroParam = hybridToUnipolar(modulatedValues[3]);
						}
						else if (paramManager->hasPatchedParamSet()) {
							PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
							zoneAParam = patchedParams->getValue(params::GLOBAL_SCATTER_ZONE_A);
							zoneBParam = patchedParams->getValue(params::GLOBAL_SCATTER_ZONE_B);
							macroConfigParam = patchedParams->getValue(params::GLOBAL_SCATTER_MACRO_CONFIG);
							// Macro uses bipolar storage - convert to unipolar
							macroParam = bipolarToUnipolar(patchedParams->getValue(params::GLOBAL_SCATTER_MACRO));
						}
						else {
							zoneAParam = unpatchedParams->getValue(params::UNPATCHED_SCATTER_ZONE_A);
							zoneBParam = unpatchedParams->getValue(params::UNPATCHED_SCATTER_ZONE_B);
							macroConfigParam = unpatchedParams->getValue(params::UNPATCHED_SCATTER_MACRO_CONFIG);
							// Macro uses bipolar storage - convert to unipolar
							macroParam = bipolarToUnipolar(unpatchedParams->getValue(params::UNPATCHED_SCATTER_MACRO));
						}
						// Cache for next slice
						cachedZoneAParam = zoneAParam;
						cachedZoneBParam = zoneBParam;
						cachedMacroConfigParam = macroConfigParam;
						cachedMacroParam = macroParam;

						// Read pWrite and density directly from param set (not patcher output)
						// Bipolar storage: INT32_MIN=0%, 0=50%, INT32_MAX=100% (like TableShaperMix)
						q31_t pWriteQ31;
						q31_t densityQ31;
						if (paramManager->hasPatchedParamSet()) {
							PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
							pWriteQ31 = patchedParams->getValue(params::GLOBAL_SCATTER_PWRITE);
							densityQ31 = patchedParams->getValue(params::GLOBAL_SCATTER_DENSITY);
							// Add cable modulation if available (patcher outputs cables only for zone-like handling)
							if (modulatedValues != nullptr) {
								constexpr int32_t kCableScale = 4;
								constexpr int64_t kBipolarMin = INT32_MIN;
								constexpr int64_t kBipolarMax = INT32_MAX;
								pWriteQ31 = std::clamp<int64_t>(static_cast<int64_t>(pWriteQ31)
								                                    + modulatedValues[4] / kCableScale,
								                                kBipolarMin, kBipolarMax);
								densityQ31 = std::clamp<int64_t>(static_cast<int64_t>(densityQ31)
								                                     + modulatedValues[5] / kCableScale,
								                                 kBipolarMin, kBipolarMax);
							}
						}
						else {
							pWriteQ31 = unpatchedParams->getValue(params::UNPATCHED_SCATTER_PWRITE);
							densityQ31 = unpatchedParams->getValue(params::UNPATCHED_SCATTER_DENSITY);
						}
						// Convert bipolar to 0-1: INT32_MIN→0.0, 0→0.5, INT32_MAX→1.0
						constexpr float kBipolarToUnipolar = 1.0f / 4294967296.0f;
						cachedPWriteProb = (static_cast<int64_t>(pWriteQ31) + 2147483648LL) * kBipolarToUnipolar;
						cachedDensityProb = (static_cast<int64_t>(densityQ31) + 2147483648LL) * kBipolarToUnipolar;
					}
					FX_BENCH_STOP(benchParamRead);

					// === STATIC TRIANGLE UPDATE (lazy - only when inputs change) ===
					float macroConfigNorm = static_cast<float>(macroConfigParam) * deluge::dsp::scatter::kQ31ToFloat;
					float macroNorm = static_cast<float>(macroParam) * deluge::dsp::scatter::kQ31ToFloat;
					float zoneBNorm = static_cast<float>(zoneBParam) * deluge::dsp::scatter::kQ31ToFloat;

					// Check if static params need recompute
					bool needStaticUpdate =
					    !staticTriangles.valid || staticTriangles.lastMacroConfigParam != macroConfigParam
					    || staticTriangles.lastMacroParam != macroParam || staticTriangles.lastZoneBParam != zoneBParam;

					if (needStaticUpdate) {
						FX_BENCH_START(benchStatic);
						// Recompute static triangles (depend only on knob positions, not slicePhase)
						staticTriangles.subdivInfluence =
						    deluge::dsp::triangleSimpleUnipolar(macroConfigNorm * deluge::dsp::phi::kPhi225, 0.5f);
						staticTriangles.zoneAMacroInfluence =
						    deluge::dsp::triangleSimpleUnipolar(macroConfigNorm * deluge::dsp::phi::kPhi050, 0.5f);
						staticTriangles.zoneBMacroInfluence =
						    deluge::dsp::triangleSimpleUnipolar(macroConfigNorm * deluge::dsp::phi::kPhi075, 0.5f);

						// Threshold scales for reverse/pitch/delay (bipolar, macro uses these)
						staticTriangles.reverseScale =
						    deluge::dsp::triangleFloat(macroConfigNorm * deluge::dsp::phi::kPhi125, 0.6f);
						staticTriangles.pitchScale =
						    deluge::dsp::triangleFloat(macroConfigNorm * deluge::dsp::phi::kPhi200, 0.6f);
						staticTriangles.delayScale =
						    deluge::dsp::triangleFloat(macroConfigNorm * deluge::dsp::phi::kPhi075, 0.6f);

						// Zone B standard mode triangles (used when phRawB == 0)
						staticTriangles.envDepthBase =
						    deluge::dsp::triangleSimpleUnipolar(zoneBNorm * deluge::dsp::phi::kPhi050, 0.6f);
						staticTriangles.panAmountBase =
						    deluge::dsp::triangleSimpleUnipolar(zoneBNorm * deluge::dsp::phi::kPhi125, 0.25f);

						// Delay modulation - phi triangle on macro, independent of slice
						staticTriangles.delayTimeMod =
						    0.5f
						    + deluge::dsp::triangleSimpleUnipolar(macroNorm * deluge::dsp::phi::kPhi150, 0.5f)
						          * 1.5f; // [0.5, 2.0]

						// Update cache keys
						staticTriangles.lastMacroConfigParam = macroConfigParam;
						staticTriangles.lastMacroParam = macroParam;
						staticTriangles.lastZoneBParam = zoneBParam;
						staticTriangles.valid = true;
						FX_BENCH_STOP(benchStatic);
					}

					// Use cached static values for macro influence
					constexpr float kMacroPhaseMax = 0.3f;
					float macroZoneAPhase = macroNorm * staticTriangles.zoneAMacroInfluence * kMacroPhaseMax;
					float macroZoneBPhase = macroNorm * staticTriangles.zoneBMacroInfluence * kMacroPhaseMax;

					// Phase offsets from secret encoder menus (push+twist) + macro contribution
					// Only include threshold scales - evolution mode values computed in computeGrainParams
					deluge::dsp::scatter::ScatterPhaseOffsets offsets{
					    stutterConfig.zoneAPhaseOffset + macroZoneAPhase,
					    stutterConfig.zoneBPhaseOffset + macroZoneBPhase,
					    stutterConfig.macroConfigPhaseOffset,
					    stutterConfig.gammaPhase,
					    staticTriangles.reverseScale,
					    staticTriangles.pitchScale,
					    isRepeat ? 0.0f : staticTriangles.delayScale, // No delay for Repeat
					    scatterBarIndex,
					};
					// Cache offsets for slice boundary computation (used inline in sample loop)
					cachedOffsets = offsets;

					// Compute grain params - Repeat uses loop index for evolution, Shuffle uses slice index
					deluge::dsp::scatter::GrainParams grain;
					if (!isRepeat && scatterRepeatCounter > 0) {
						// Shuffle: repeating, reuse cached grain (skip ~2200 cycles)
						grain = scatterCachedGrain;
						scatterRepeatCounter--;
					}
					else {
						// Fresh slice: compute new grain
						FX_BENCH_START(benchParams);
						int32_t grainIndex = isRepeat ? scatterRepeatLoopIndex : scatterSliceIndex;
						grain = deluge::dsp::scatter::computeGrainParams(zoneAParam, zoneBParam, macroConfigParam,
						                                                 macroParam, grainIndex, &offsets);
						FX_BENCH_STOP(benchParams);
						// Time mode: Zone A = grainLength (combine), Zone B = repeatSlices (repeat)
						if (isTime) {
							// Zone A [0,1] → grainLength 1→numSlices (combine consecutive slices)
							// Menu params are unsigned Q31 (0 to ~2^31), kQ31ToFloat maps to 0..1
							float zoneANorm = static_cast<float>(zoneAParam) * deluge::dsp::scatter::kQ31ToFloat;
							int32_t combine = 1 + static_cast<int32_t>(zoneANorm * (scatterNumSlices - 1));
							grain.grainLength = std::clamp(combine, int32_t{1}, scatterNumSlices);
							// Zone B [0,1] → repeatSlices 1→numSlices (repeat same position)
							float zoneBNorm = static_cast<float>(zoneBParam) * deluge::dsp::scatter::kQ31ToFloat;
							int32_t repeat = 1 + static_cast<int32_t>(zoneBNorm * (scatterNumSlices - 1));
							grain.repeatSlices = std::clamp(repeat, int32_t{1}, scatterNumSlices);
						}
						// Grain mode: Rate = grain size, Zone A = position spread
						if (stutterConfig.scatterMode == ScatterMode::Grain && playbackLength > 0) {
							// Rate controls grain size (exponential curve like Repeat)
							int32_t grainNorm = 64 - knobPos; // CCW=large, CW=small
							grainNorm = std::clamp(grainNorm, int32_t{0}, int32_t{128});
							constexpr size_t kMinGrain = 1024; // ~23ms minimum (avoid harsh micro-grains)
							grainLength = (playbackLength * grainNorm * grainNorm) / (128 * 128);
							grainLength = std::clamp(grainLength, kMinGrain, playbackLength);
							// Zone A [0,1] → spread range (pattern control: 0=sequential, 1=random)
							float zoneANorm = static_cast<float>(zoneAParam) * deluge::dsp::scatter::kQ31ToFloat;
							grainSpread = static_cast<size_t>(zoneANorm * playbackLength);
							// Initialize voices if not already running
							if (grainPhaseB == 0 && grainPhaseA == 0) {
								grainPhaseB = 0x80000000u; // Voice B at 50% phase offset
								grainRngState ^= static_cast<uint32_t>(currentTick);
								grainPosA = grainRngState % playbackLength;
								grainRngState ^= grainRngState << 13;
								grainRngState ^= grainRngState >> 17;
								grainPosB = grainRngState % playbackLength;
								grainOffsetA = 0;
								grainOffsetB = grainLength / 2; // 50% through grain
							}
						}
						if (!isRepeat) {
							// Cache for repeat and set counter (Shuffle/Time)
							scatterCachedGrain = grain;
							scatterRepeatCounter = grain.repeatSlices - 1;
						}
					}

					// Slice offset computation: Repeat=continuous, Shuffle=discrete
					int32_t effectiveGrainLength = 1;
					if (isRepeat) {
						// Continuous offset: shift start position within available buffer range
						size_t availableRange = playbackLength - currentSliceLength;
						size_t offsetAmount = (grain.sliceOffset * availableRange) >> 4;
						if (grain.shouldSkip) {
							offsetAmount = (grain.skipTarget * availableRange) >> 4;
						}
						// Start from end of buffer, offset moves earlier
						sliceStartOffset = playbackLength - currentSliceLength - offsetAmount;
					}
					else {
						// Discrete slice offset: calculate target slice from sequential index
						// Time mode: stretch by dividing slice index by repeatSlices (1111,2222,3333)
						int32_t baseSliceIdx = isTime && grain.repeatSlices > 1 ? scatterSliceIndex / grain.repeatSlices
						                                                        : scatterSliceIndex;
						// Pattern/Pitch mode: Zone A selects pattern (8 zones), phi offset still applies on top
						// 0:Seq, 1:Weave, 2:Skip, 3:Mirror, 4:Pairs, 5:Reverse, 6:Thirds, 7:Spiral
						if ((isPattern || isPitch) && scatterNumSlices > 1) {
							float zoneANorm = static_cast<float>(zoneAParam) * deluge::dsp::scatter::kQ31ToFloat;
							int32_t patternIdx = static_cast<int32_t>(zoneANorm * 8.0f); // 0-7
							if (patternIdx < 0)
								patternIdx = 0;
							if (patternIdx > 7)
								patternIdx = 7;
							int32_t half = scatterNumSlices / 2;
							int32_t n = scatterNumSlices;
							switch (patternIdx) {
							case 1: // Weave: 0,N-1,1,N-2,2,N-3...
								baseSliceIdx = (baseSliceIdx & 1) ? (n - 1 - baseSliceIdx / 2) : (baseSliceIdx / 2);
								break;
							case 2: // Skip: evens then odds (0,2,4,6,1,3,5,7)
								baseSliceIdx =
								    (baseSliceIdx < half) ? (baseSliceIdx * 2) : ((baseSliceIdx - half) * 2 + 1);
								break;
							case 3: // Mirror: forward then backward (0,1,2,3,3,2,1,0)
								baseSliceIdx = (baseSliceIdx >= half) ? (n - 1 - baseSliceIdx) : baseSliceIdx;
								break;
							case 4: // Pairs: swap adjacent (1,0,3,2,5,4,7,6)
								baseSliceIdx ^= 1;
								break;
							case 5: // Reverse: N-1,N-2,N-3...0
								baseSliceIdx = n - 1 - baseSliceIdx;
								break;
							case 6: // Thirds: interleave by 3 (0,3,6,1,4,7,2,5,8)
							{
								int32_t third = (n + 2) / 3;
								baseSliceIdx = (baseSliceIdx % third) * 3 + (baseSliceIdx / third);
								if (baseSliceIdx >= n)
									baseSliceIdx = n - 1;
							} break;
							case 7: // Spiral: middle outward (3,4,2,5,1,6,0,7)
							{
								int32_t mid = half;
								int32_t offset = (baseSliceIdx + 1) / 2;
								int32_t spiralIdx = (baseSliceIdx & 1) ? (mid + offset) : (mid - offset);
								if (spiralIdx < 0)
									spiralIdx = 0;
								if (spiralIdx >= n)
									spiralIdx = n - 1;
								baseSliceIdx = spiralIdx;
							} break;
							default: // Sequential: no remapping
								break;
							}
						}
						// Pitch mode: Zone A provides deterministic random offset for degree selection
						if (isPitch) {
							// Hash slice index with Zone A to get deterministic pseudo-random degree
							uint32_t zoneASeed = static_cast<uint32_t>(zoneAParam >> 16); // Use upper bits
							uint32_t hashInput = (zoneASeed ^ (scatterSliceIndex * 2654435761u));
							uint32_t hashVal = hash::mix(hashInput);
							int32_t degreeIdx = static_cast<int32_t>(hashVal & 0x7); // 0-7
							if (degreeIdx < 0)
								degreeIdx = 0;
							if (degreeIdx > 7)
								degreeIdx = 7;

							// Get semitone offset from scale table
							uint8_t scaleIdx = stutterConfig.getPitchScale();
							if (scaleIdx > 24)
								scaleIdx = 0;
							int8_t semitones = kScaleSemitones[scaleIdx][degreeIdx];
							if (semitones < 0)
								semitones = 0;
							if (semitones > 17)
								semitones = 17;
							scatterPitchRatioFP = kPitchRatioFP[semitones];
							scatterPitchPosFP = 0; // Reset position accumulator for new slice
						}
						else {
							scatterPitchRatioFP = 65536; // 1.0 = no pitch shift
						}
						int32_t targetSlice = baseSliceIdx;
						int32_t offsetSlices = (grain.sliceOffset * scatterNumSlices) >> 4;
						targetSlice = (targetSlice + offsetSlices) % scatterNumSlices;
						if (grain.shouldSkip) {
							targetSlice = (grain.skipTarget * scatterNumSlices) >> 4;
							targetSlice = targetSlice % scatterNumSlices;
						}
						// Long grain: combine consecutive slices into one continuous chunk
						int32_t remainingTimeSlices = scatterNumSlices - scatterSliceIndex;
						int32_t remainingBufferSlices = scatterNumSlices - targetSlice;
						effectiveGrainLength = std::max(
						    int32_t{1}, std::min({grain.grainLength, remainingTimeSlices, remainingBufferSlices}));
						size_t baseSliceLength = playbackLength / scatterNumSlices;
						currentSliceLength = baseSliceLength * static_cast<size_t>(effectiveGrainLength);
						// If this grain ends the bar, add remainder to prevent rushing
						if (scatterSliceIndex + effectiveGrainLength >= scatterNumSlices) {
							size_t expectedTotal = baseSliceLength * static_cast<size_t>(scatterNumSlices);
							size_t remainder = playbackLength - expectedTotal;
							currentSliceLength += remainder;
						}
						if (currentSliceLength < 256) {
							currentSliceLength = 256;
						}
						sliceStartOffset = targetSlice * baseSliceLength;
					}

					// Reverse decision (hash-based bool)
					scatterReversed = grain.shouldReverse;

					// Pitch-up decision (hash-based bool, 2x via sample decimation)
					scatterPitchUp = grain.shouldPitchUp;

					// Track consecutive playback: no offset, no transforms (all modes)
					// Used to skip ZC protection when audio flows naturally between slices
					scatterConsecutive = (grain.sliceOffset == 0) && !scatterReversed && !scatterPitchUp;

					// Peek at next grain to determine if decay envelope should apply
					// If next grain is consecutive, skip decay (content flows naturally)
					// If next grain is non-consecutive, apply decay (need crossfade transition)
					int32_t nextGrainIndex = isRepeat ? (scatterRepeatLoopIndex + 1) : (scatterSliceIndex + 1);
					auto nextGrain = deluge::dsp::scatter::computeGrainParams(zoneAParam, zoneBParam, macroConfigParam,
					                                                          macroParam, nextGrainIndex, &offsets);
					bool nextReversed = nextGrain.shouldReverse;
					bool nextPitchUp = nextGrain.shouldPitchUp;
					scatterNextConsecutive = (nextGrain.sliceOffset == 0) && !nextReversed && !nextPitchUp;

					// Dry decision (hash-based bool, macro can gate it)
					// Macro high = more likely to override grain and use dry
					float thresholdInfluence =
					    deluge::dsp::triangleSimpleUnipolar(macroConfigNorm * deluge::dsp::phi::kPhi, 0.5f);
					bool macroWantsDry = (macroNorm * thresholdInfluence > 0.5f);
					bool wantsDry = grain.useDry || macroWantsDry;

					// Density control: densityParam controls grain vs dry output
					// Linear: 0=all dry, 50=100% grains (normal behavior)
					float density = cachedDensityProb;
					if (density < 1.0f) {
						uint32_t densityHash = hash::mix(static_cast<uint32_t>(scatterSliceIndex) ^ 0xBADC0FFE);
						float densityRand = static_cast<float>(densityHash & 0xFFFF) / 65535.0f;
						if (densityRand >= density) {
							wantsDry = true;
						}
					}

					scatterDryMix = wantsDry ? 1.0f : 0.0f;
					scatterDryThreshold = 0.5f; // Fixed threshold for bool comparison

					// All timbral params from grain (computed with phase offset and gamma in computeGrainParams)
					scatterEnvShape = grain.envShape;
					scatterGateRatio = grain.gateRatio;
					scatterEnvDepth = grain.envDepth;

					// Pan: Repeat=bar-indexed direction, Shuffle=counter-indexed (disable for long grains)
					if (isRepeat) {
						float panDir = (deluge::dsp::phi::wrapPhase(static_cast<float>(scatterBarIndex) * 1.3f) < 0.5f)
						                   ? -1.0f
						                   : 1.0f;
						scatterPan = panDir * grain.panAmount;
					}
					else if (effectiveGrainLength > 1) {
						scatterPan = 0;
					}
					else {
						float panDir =
						    (deluge::dsp::phi::wrapPhase(static_cast<float>(scatterPanCounter++) * 5.3f) < 0.5f) ? -1.0f
						                                                                                         : 1.0f;
						scatterPan = panDir * grain.panAmount;
					}

					// Precompute pan coefficients (Q31, once per slice)
					float panAbs = (scatterPan > 0) ? scatterPan : -scatterPan;
					scatterPanActive = (panAbs > 0.001f);
					scatterPanFadeQ31 = static_cast<int32_t>((1.0f - panAbs) * 2147483647.0f);
					scatterPanCrossQ31 = static_cast<int32_t>((panAbs * 0.5f) * 2147483647.0f);
					scatterPanRight = (scatterPan > 0);

					// Subdivisions (ratchet) from grain params
					scatterSubdivisions = std::max(grain.subdivisions, int32_t{1});
					scatterSubdivIndex = 0; // Reset for new slice

					// Precompute sub-slice length, floor at 24ms (truncates at slice boundary)
					// Last subdivision gets remainder to prevent accumulated timing drift
					// IMPORTANT: Floor must not exceed currentSliceLength or reverse mode underflows
					constexpr float kMinSubSliceMs = 24.0f;
					constexpr size_t kMinSubSliceSamples = static_cast<size_t>(kMinSubSliceMs * 44.1f);
					scatterSubSliceLength = currentSliceLength / static_cast<size_t>(scatterSubdivisions);
					if (scatterSubSliceLength < kMinSubSliceSamples) {
						// Clamp floor to slice length to prevent playbackPos > currentSliceLength
						size_t effectiveFloor = std::min(kMinSubSliceSamples, currentSliceLength);
						scatterSubSliceLength = effectiveFloor;
						scatterLastSubSliceLength = effectiveFloor;
					}
					else {
						// Last subdivision plays remaining samples (base + truncation remainder)
						size_t truncatedTotal = scatterSubSliceLength * static_cast<size_t>(scatterSubdivisions);
						scatterLastSubSliceLength = scatterSubSliceLength + (currentSliceLength - truncatedTotal);
					}

					// Precompute envelope/gate active flags (once per slice, avoid per-sample checks)
					// Fast ratchets (<60ms) skip envelope but keep gate (hard chop adds punch)
					constexpr size_t kFastRatchetThreshold = 2646; // ~60ms at 44.1kHz
					bool isFastRatchet = (scatterSubdivisions > 1 && scatterSubSliceLength < kFastRatchetThreshold);
					scatterEnvActive = !isFastRatchet && (scatterEnvDepth > 0.001f);
					scatterGateActive = (scatterGateRatio < 0.999f);

					// Precompute Q31 envelope parameters (once per slice, used for all samples)
					FX_BENCH_START(benchEnvPrep);
					if (scatterEnvActive) {
						// Full envelope prep for slow slices
						int32_t envSliceLen = static_cast<int32_t>(scatterSubSliceLength);
						scatterEnvPrecomputed = deluge::dsp::scatter::prepareGrainEnvelopeQ31(
						    envSliceLen, scatterGateRatio, scatterEnvDepth, scatterEnvShape, scatterEnvWidth);
					}
					else if (scatterGateActive) {
						// Gate only (no envelope): hard cutoff, no fades
						scatterEnvPrecomputed.gatedLength =
						    static_cast<int32_t>(static_cast<float>(scatterSubSliceLength) * scatterGateRatio);
						// Explicitly zero fade lengths to prevent stale values causing fades
						scatterEnvPrecomputed.attackFadeLen = 0;
						scatterEnvPrecomputed.decayFadeLen = 0;
					}
					else {
						// No envelope, no gate: full passthrough (no fades, no cutoff)
						scatterEnvPrecomputed.gatedLength = static_cast<int32_t>(scatterSubSliceLength);
						scatterEnvPrecomputed.attackFadeLen = 0;
						scatterEnvPrecomputed.decayFadeLen = 0;
					}
					FX_BENCH_STOP(benchEnvPrep);

					// Delay send setup: fixed quarter-bar time, bit-shift send level
					// shouldDelay gates whether delay is used at all, delaySendBits controls send amount
					if (kEnableDelay && delayBuffer != nullptr && grain.shouldDelay && grain.delaySendBits > 0) {
						// Always quarter bar (1 beat) - classic rhythmic delay
						size_t quarterBar = playbackLength / 4;
						delayTime = std::min(quarterBar, kDelayBufferSize - 1);
						// Send level: bits 1-3 → shift 2,1,0 (25%, 50%, 100%)
						delaySendShift = 3 - grain.delaySendBits;
						delayActive = true;
					}
					else {
						delayActive = false;
					}

					// Tag slice benchmark with slice count and subdiv (combined in tag[2])
					// tag[0]="slice", tag[1]=mode, tag[2]="8s/x4" format
					if (!isRepeat) {
						static char sliceInfoTag[16];
						char* p = sliceInfoTag;
						intToString(scatterNumSlices, p, 1);
						while (*p)
							p++;
						*p++ = 's';
						*p++ = '/';
						*p++ = 'x';
						intToString(scatterSubdivisions, p, 1);
						FX_BENCH_SET_TAG(benchSlice, 2, sliceInfoTag);

						// Advance for next slice (skip by effectiveGrainLength for long grains)
						// Note: bar boundary handling (scatterBarIndex, resync) is done by tick-based sync
						// This sample-based advance just wraps the slice index
						int32_t nextSliceIndex = scatterSliceIndex + effectiveGrainLength;
						if (nextSliceIndex >= scatterNumSlices) {
							// Cancel repeat at bar boundary - compute fresh params for new bar
							scatterRepeatCounter = 0;
						}
						scatterSliceIndex = nextSliceIndex % scatterNumSlices;
					}
					break;
				}

				default:
					// Default: play full bar
					currentSliceLength = playbackLength;
					sliceStartOffset = 0;
					scatterDryMix = 0; // No density crossfade in default mode
					// Default: no subdivisions, play full bar
					scatterSubdivisions = 1;
					scatterSubdivIndex = 0;
					scatterSubSliceLength = currentSliceLength;
					scatterLastSubSliceLength = currentSliceLength;
					break;
				}
				FX_BENCH_STOP(benchSlice);
			}

			// Hoist slice-constant values to locals (avoid member access in hot loop)
			size_t loopPlaybackStartPos = playbackStartPos;
			size_t loopSliceStartOffset = sliceStartOffset;
			// Safety floor to prevent underflow in reverse read calculation
			size_t loopCurrentSliceLength = (currentSliceLength > 0) ? currentSliceLength : 256;
			size_t loopSubSliceLength = scatterSubSliceLength;
			size_t loopLastSubSliceLength = scatterLastSubSliceLength;
			int32_t loopLastSubdivIndex = scatterSubdivisions - 1;
			// Hoist effective sub-length (update only on subdivision change, not every sample)
			// Safety: ensure minimum to prevent audio-rate looping artifacts
			size_t loopEffectiveSubLen =
			    (scatterSubdivIndex == loopLastSubdivIndex) ? loopLastSubSliceLength : loopSubSliceLength;
			if (loopEffectiveSubLen < kMinGrainSize) {
				loopEffectiveSubLen = kMinGrainSize;
			}
			// Pitch up plays grain twice - track which loop we're on (persists across buffers)
			int loopPitchUpLoopCount = scatterPitchUpLoopCount;
			size_t loopPlaybackLength = playbackLength;    // For leaky write wrapping
			size_t loopLinearBarPos = scatterLinearBarPos; // Linear position for leaky writes

			// Hoist mode check and envelope params (constant during loop)
			// Repeat shares processing with Shuffle (unified code path)
			bool isShuffle =
			    (stutterConfig.scatterMode == ScatterMode::Shuffle || stutterConfig.scatterMode == ScatterMode::Grain
			     || stutterConfig.scatterMode == ScatterMode::Repeat || stutterConfig.scatterMode == ScatterMode::Time
			     || stutterConfig.scatterMode == ScatterMode::Pattern
			     || stutterConfig.scatterMode == ScatterMode::Pitch);
			bool isGrain = (stutterConfig.scatterMode == ScatterMode::Grain);
			bool isTime = (stutterConfig.scatterMode == ScatterMode::Time);
			bool isPitch = (stutterConfig.scatterMode == ScatterMode::Pitch);
			// pWrite applies to all looper modes except Repeat (continuous loop)
			// Slice modes: hash of slice index determines write decision per-slice
			// Grain mode: uses grainAWritesWet (per-grain decision at phase wrap)
			bool hasPWrite = stutterConfig.isLooperMode() && stutterConfig.scatterMode != ScatterMode::Repeat;
			bool pWriteGrainIsWet = false;
			if (hasPWrite && !isGrain) {
				// Slice-based modes: pWrite decision per-slice using hash
				// Grain mode uses grainAWritesWet directly (per-grain decision at phase wrap)
				float pWriteProb = cachedPWriteProb;
				uint8_t pWriteThreshold = static_cast<uint8_t>(pWriteProb * 16.0f);
				hash::Bits sliceBits(static_cast<uint32_t>(scatterSliceIndex) ^ (scatterBarIndex << 16) ^ 0xDEADBEEFu);
				pWriteGrainIsWet = sliceBits.threshold4(0, pWriteThreshold);

				// Check for read/write region overlap - duck grain if they intersect
				// Read region: [sliceStartOffset, sliceStartOffset + sliceLength)
				// Write region: [linearBarPos, linearBarPos + sliceLength)
				// In circular buffer, overlap if either start is within the other's range
				// EXCEPTION: Skip overlap check when writing dry/fresh input (density down)
				// Fresh input has no feedback concern - we're not reading what we just wrote
				bool sliceUsesDry = (scatterDryMix > scatterDryThreshold);
				if (pWriteGrainIsWet && playbackLength > 0 && !sliceUsesDry) {
					size_t readStart = sliceStartOffset;
					size_t writeStart = scatterLinearBarPos;
					size_t len = currentSliceLength;
					// Check: is writeStart within [readStart, readStart+len)?
					size_t writeInRead = (writeStart + playbackLength - readStart) % playbackLength;
					// Check: is readStart within [writeStart, writeStart+len)?
					size_t readInWrite = (readStart + playbackLength - writeStart) % playbackLength;
					if (writeInRead < len || readInWrite < len) {
						pWriteGrainIsWet = false; // Duck this grain - regions overlap
					}
				}
			}
			bool loopEnvActive = isShuffle && (scatterEnvActive || scatterGateActive);
			bool loopPanActive = scatterPanActive;
			bool loopReversed = scatterReversed && isShuffle;
			int32_t loopPitchIncrement = (scatterPitchUp && isShuffle) ? 2 : 1;
			// Skip ZC protection when slices are consecutive and no envelope (audio flows naturally)
			bool loopSkipZC = scatterConsecutive && !loopEnvActive;
			bool loopNextConsecutive = scatterNextConsecutive;

			// Hoist envelope precomputed values
			int32_t loopGatedLen = scatterEnvPrecomputed.gatedLength;
			int32_t loopAttackLen = scatterEnvPrecomputed.attackFadeLen;
			int32_t loopDecayLen = scatterEnvPrecomputed.decayFadeLen;
			int32_t loopInvAttackLen = scatterEnvPrecomputed.invAttackLen;
			int32_t loopInvDecayLen = scatterEnvPrecomputed.invDecayLen;

			// Release zone: last 15ms of grain (fixed window for ZC search, covers 33Hz min)
			size_t effectiveEnd =
			    std::min(loopEffectiveSubLen, loopGatedLen > 0 ? size_t(loopGatedLen) : loopEffectiveSubLen);
			size_t loopReleaseThreshold = effectiveEnd > kGrainReleaseZone ? effectiveEnd - kGrainReleaseZone : 0;

			// Hoist pan coefficients
			int32_t loopPanFadeQ31 = scatterPanFadeQ31;
			int32_t loopPanCrossQ31 = scatterPanCrossQ31;
			bool loopPanRight = scatterPanRight;
			// Time mode: only bar-end silence before phrase reset, not every bar
			bool loopBarEndSilenceEnabled = !isTime || ((scatterBarIndex % kTimePhraseLength) == kTimePhraseLength - 1);
			// Pitch mode: fixed-point pitch ratio (65536 = 1.0)
			uint32_t loopPitchRatioFP = isPitch ? scatterPitchRatioFP : 65536;
			uint32_t loopPitchPosFP = scatterPitchPosFP;

			// Grain mode: Zone B effects from scatterCachedGrain
			bool loopGrainReversed = isGrain && scatterCachedGrain.shouldReverse;
			bool loopGrainPitchUp = isGrain && scatterCachedGrain.shouldPitchUp;
			int32_t loopGrainRepeatSlices = isGrain ? scatterCachedGrain.repeatSlices : 1;
			int32_t loopGrainRepeatCounter = 0;
			float grainPanDir =
			    (deluge::dsp::phi::wrapPhase(static_cast<float>(scatterBarIndex) * 1.3f) < 0.5f) ? -1.0f : 1.0f;
			float grainPan = isGrain ? grainPanDir * scatterCachedGrain.panAmount : 0;
			float grainPanAbs = (grainPan > 0) ? grainPan : -grainPan;
			bool loopGrainPanActive = (grainPanAbs > 0.001f);
			int32_t loopGrainPanFadeQ31 = static_cast<int32_t>((1.0f - grainPanAbs) * 2147483647.0f);
			int32_t loopGrainPanCrossQ31 = static_cast<int32_t>((grainPanAbs * 0.5f) * 2147483647.0f);
			bool loopGrainPanRight = (grainPan > 0);

			for (StereoSample& sample : audio) {
				// NOTE: Recording for re-trigger is handled by recordStandby() which is called
				// BEFORE processStutter(). Recording here would double-record, causing
				// recordWritePos to advance at 2x speed and corrupt re-trigger playback.

				// Benchmark first sample only to avoid 128x overhead
				bool benchThisSample = (sampleIdx == 0);

				// === GRAIN MODE: dual-voice crossfade processing ===
				if (isGrain && looperBuffer != nullptr && loopPlaybackLength > 0) {
					q31_t dryL = sample.l;
					q31_t dryR = sample.r;

					// Triangular envelope: 0→max→0 over full phase cycle
					auto triangleEnv = [](uint32_t phase) -> int32_t {
						if (phase < 0x80000000u) {
							return static_cast<int32_t>(phase); // Rising: 0 → max
						}
						return static_cast<int32_t>(0xFFFFFFFFu - phase); // Falling: max → 0
					};

					int32_t envA = triangleEnv(grainPhaseA);
					int32_t envB = triangleEnv(grainPhaseB);

					// Read buffer samples (used if voice is wet)
					// Reverse: read from end of grain going backwards
					size_t effectiveOffsetA = loopGrainReversed
					                              ? (grainLength > grainOffsetA ? grainLength - 1 - grainOffsetA : 0)
					                              : grainOffsetA;
					size_t effectiveOffsetB = loopGrainReversed
					                              ? (grainLength > grainOffsetB ? grainLength - 1 - grainOffsetB : 0)
					                              : grainOffsetB;
					size_t localPosA = (grainPosA + effectiveOffsetA) % loopPlaybackLength;
					size_t localPosB = (grainPosB + effectiveOffsetB) % loopPlaybackLength;
					size_t posA = (loopPlaybackStartPos + localPosA) % kLooperBufferSize;
					size_t posB = (loopPlaybackStartPos + localPosB) % kLooperBufferSize;
					q31_t bufAL = looperBuffer[posA].l;
					q31_t bufAR = looperBuffer[posA].r;
					q31_t bufBL = looperBuffer[posB].l;
					q31_t bufBR = looperBuffer[posB].r;

					// Per-voice source selection (dry or buffer) - decided at grain start
					q31_t srcAL = grainAIsDry ? dryL : bufAL;
					q31_t srcAR = grainAIsDry ? dryR : bufAR;
					q31_t srcBL = grainBIsDry ? dryL : bufBL;
					q31_t srcBR = grainBIsDry ? dryR : bufBR;

					// Mix with envelopes - bypass entirely when both voices are dry
					q31_t outputL, outputR;
					if (grainAIsDry && grainBIsDry) {
						// Both voices dry: pass through without envelope coloring
						outputL = dryL;
						outputR = dryR;
					}
					else {
						// At least one voice wet: crossfade with triangular envelopes
						outputL = (multiply_32x32_rshift32(srcAL, envA) + multiply_32x32_rshift32(srcBL, envB)) << 1;
						outputR = (multiply_32x32_rshift32(srcAR, envA) + multiply_32x32_rshift32(srcBR, envB)) << 1;
					}

					// Apply pan (same as slice modes)
					if (loopGrainPanActive) {
						q31_t mixL = outputL;
						q31_t mixR = outputR;
						if (loopGrainPanRight) {
							outputL = multiply_32x32_rshift32(mixL, loopGrainPanFadeQ31) << 1;
							outputR = mixR + (multiply_32x32_rshift32(mixL - mixR, loopGrainPanCrossQ31) << 1);
						}
						else {
							outputR = multiply_32x32_rshift32(mixR, loopGrainPanFadeQ31) << 1;
							outputL = mixL + (multiply_32x32_rshift32(mixR - mixL, loopGrainPanCrossQ31) << 1);
						}
					}

					// pWrite: crossfade grain A into buffer at linear position
					// Blend: existing * (1-env) + new * env - smooth transitions at grain edges
					// Use grainAWritesWet directly (not hoisted pWriteGrainIsWet) for per-grain decision
					if (hasPWrite && grainAWritesWet) {
						size_t writePos = (loopPlaybackStartPos + loopLinearBarPos) % kLooperBufferSize;
						int32_t invEnvA = 0x7FFFFFFF - envA;
						q31_t existL = looperBuffer[writePos].l;
						q31_t existR = looperBuffer[writePos].r;
						q31_t newL = (multiply_32x32_rshift32(existL, invEnvA) + multiply_32x32_rshift32(srcAL, envA))
						             << 1;
						q31_t newR = (multiply_32x32_rshift32(existR, invEnvA) + multiply_32x32_rshift32(srcAR, envA))
						             << 1;
						looperBuffer[writePos] = {newL, newR};
					}

					// Advance offsets and linear position
					// Pitch up: advance by 2 (octave up via decimation)
					int32_t offsetInc = loopGrainPitchUp ? 2 : 1;
					grainOffsetA += offsetInc;
					grainOffsetB += offsetInc;
					loopLinearBarPos++;
					if (loopLinearBarPos >= loopPlaybackLength) {
						loopLinearBarPos = 0;
					}

					// Advance envelope phases (double for pitch up to maintain grain length)
					uint32_t phaseInc = grainLength > 0 ? (0xFFFFFFFFu / grainLength) : 0x10000000u;
					if (loopGrainPitchUp) {
						phaseInc *= 2;
					}
					uint32_t oldPhaseA = grainPhaseA;
					uint32_t oldPhaseB = grainPhaseB;
					grainPhaseA += phaseInc;
					grainPhaseB += phaseInc;

					// Fast RNG for new positions and density decisions
					auto fastRandom = [this]() -> uint32_t {
						grainRngState ^= grainRngState << 13;
						grainRngState ^= grainRngState >> 17;
						grainRngState ^= grainRngState << 5;
						return grainRngState;
					};

					// Density threshold
					float densityProb = cachedDensityProb;

					// On phase wrap: new grain position, reset offset, decide dry/wet and pWrite
					// Repeat: hold position for N grain cycles
					if (grainPhaseA < oldPhaseA) {
						grainOffsetA = 0;
						if (loopGrainRepeatCounter > 0) {
							loopGrainRepeatCounter--;
						}
						else {
							size_t spread = grainSpread > 0 ? grainSpread : loopPlaybackLength;
							grainPosA = fastRandom() % spread;
							loopGrainRepeatCounter = loopGrainRepeatSlices - 1;
						}
						grainAIsDry = (static_cast<float>(fastRandom() & 0xFFFF) / 65535.0f) >= densityProb;
						float pWriteProbGrain = cachedPWriteProb;
						grainAWritesWet = (static_cast<float>(fastRandom() & 0xFFFF) / 65535.0f) < pWriteProbGrain;
					}
					if (grainPhaseB < oldPhaseB) {
						grainOffsetB = 0;
						size_t spread = grainSpread > 0 ? grainSpread : loopPlaybackLength;
						grainPosB = fastRandom() % spread;
						grainBIsDry = (static_cast<float>(fastRandom() & 0xFFFF) / 65535.0f) >= densityProb;
					}

					sample.l = outputL;
					sample.r = outputR;
					sampleIdx++;
					continue;
				}

				// === PLAYBACK: read from current slice ===
				// Save dry input for potential crossfade (density zone)
				q31_t dryL = sample.l;
				q31_t dryR = sample.r;

				size_t playReadPos;
				// Clamp playbackPos to valid range (safety for throttle/param change races)
				// Pitch mode: use fixed-point position >> 16 to get integer position
				size_t effectivePos = (loopPitchRatioFP != 65536) ? (loopPitchPosFP >> 16) : playbackPos;
				size_t safePlaybackPos = (effectivePos < loopCurrentSliceLength) ? effectivePos : 0;
				if (loopReversed) {
					// Reverse: read from end of slice going backward
					playReadPos =
					    loopPlaybackStartPos + loopSliceStartOffset + (loopCurrentSliceLength - 1 - safePlaybackPos);
				}
				else {
					playReadPos = loopPlaybackStartPos + loopSliceStartOffset + safePlaybackPos;
				}
				// Wrap around circular buffer (handle potential double-wrap edge cases)
				while (playReadPos >= kLooperBufferSize) {
					playReadPos -= kLooperBufferSize;
				}
				// Density threshold: hard cut between grain and dry (not a blend)
				// dryMix > threshold = use dry signal for this grain, else use buffer grain
				// Threshold = macro * macroInfluence (macroConfig phi triangle gates macro's effect)
				bool useDry = (scatterDryMix > scatterDryThreshold);

				q31_t outputL, outputR;
				q31_t srcL, srcR;                // Pre-envelope source for crossfaded pWrite
				bool bufferZeroCrossing = false; // ZC detected in buffer (before processing)

				if (useDry) {
					// Use dry input signal
					outputL = dryL;
					outputR = dryR;
					srcL = dryL;
					srcR = dryR;
				}
				else {
					// Use grain from buffer - main SDRAM access point
					if (benchThisSample) {
						FX_BENCH_START(benchRead);
					}

					// Pitch mode: linear interpolation for non-integer pitch ratios
					// This avoids aliasing/bitcrushing artifacts from truncation
					if (loopPitchRatioFP != 65536) {
						// Read two samples and interpolate
						q31_t s0L = looperBuffer[playReadPos].l;
						q31_t s0R = looperBuffer[playReadPos].r;
						// Next sample position (handle buffer wrap and reverse)
						size_t nextPos;
						if (loopReversed) {
							nextPos = (playReadPos > 0) ? playReadPos - 1 : kLooperBufferSize - 1;
						}
						else {
							nextPos = (playReadPos + 1) % kLooperBufferSize;
						}
						q31_t s1L = looperBuffer[nextPos].l;
						q31_t s1R = looperBuffer[nextPos].r;
						// Fractional part [0, 65535] from 16.16 fixed-point
						int32_t frac = static_cast<int32_t>(loopPitchPosFP & 0xFFFF);
						// Linear interpolation: s0 + frac * (s1 - s0)
						outputL = s0L + (static_cast<int32_t>((static_cast<int64_t>(s1L - s0L) * frac) >> 16));
						outputR = s0R + (static_cast<int32_t>((static_cast<int64_t>(s1R - s0R) * frac) >> 16));
					}
					else {
						outputL = looperBuffer[playReadPos].l;
						outputR = looperBuffer[playReadPos].r;
					}
					srcL = outputL; // Save pre-envelope for pWrite
					srcR = outputR;

					// Pitch up: check ZC on skipped sample (increment=2 skips every other sample)
					if (loopPitchIncrement == 2 && playbackPos > 0) {
						size_t skippedPos = loopReversed ? (playReadPos + 1) % kLooperBufferSize
						                                 : (playReadPos > 0 ? playReadPos - 1 : kLooperBufferSize - 1);
						q31_t skippedL = looperBuffer[skippedPos].l;
						bufferZeroCrossing = (skippedL != 0) && ((outputL ^ skippedL) < 0);
					}

					if (benchThisSample) {
						FX_BENCH_STOP(benchRead);
					}
				}

				// Apply grain envelope and gate (using hoisted locals)
				// Note: envDepth not used (always full fade) - depth blend adds ~30% overhead
				// Skip envelope for dry grains - input audio should pass through unchanged
				// pWrite envelope: skip attack if current consecutive, skip decay if next consecutive
				int32_t pWriteEnvQ31 = 0x7FFFFFFF; // Default full (sustain region)
				if (loopEnvActive && !useDry) {
					if (benchThisSample) {
						FX_BENCH_START(benchEnv);
					}
					int32_t pos = static_cast<int32_t>(playbackPos);

					// Gate cutoff: don't hard-cut, let ZC system mute at zero crossing
					// The release threshold is set based on gatedLen, so ZC search starts before cutoff
					if (pos >= loopGatedLen) {
						// Past gate - releaseMuted should be true by now (set by ZC check)
						// If not, force it to avoid playing past intended cutoff
						releaseMutedL = releaseMutedR = true;
						pWriteEnvQ31 = 0;
					}
					else if (pos < loopAttackLen && !scatterConsecutive) {
						// Attack fade-in: linear ramp 0→1
						// Skip if current grain is consecutive (flows naturally from previous)
						int32_t envQ31 = pos * loopInvAttackLen;
						outputL = multiply_32x32_rshift32(outputL, envQ31) << 1;
						outputR = multiply_32x32_rshift32(outputR, envQ31) << 1;
						pWriteEnvQ31 = envQ31;
					}
					else if (pos > loopGatedLen - loopDecayLen && !loopNextConsecutive) {
						// Decay fade-out: linear ramp 1→0
						// Skip if NEXT grain is consecutive (will flow naturally into next)
						int32_t envQ31 = (loopGatedLen - pos) * loopInvDecayLen;
						outputL = multiply_32x32_rshift32(outputL, envQ31) << 1;
						outputR = multiply_32x32_rshift32(outputR, envQ31) << 1;
						pWriteEnvQ31 = envQ31;
					}
					// else: flat middle - sustain region, pWriteEnvQ31 stays at full
					if (benchThisSample) {
						FX_BENCH_STOP(benchEnv);
					}
				}

				// Apply crossfeed pan using hoisted Q31 coefficients (optimized: 2 muls instead of 3)
				// At pan=1: L=0, R=(L+R)/2  |  At pan=-1: L=(L+R)/2, R=0
				// Algebraic simplification: R + (L-R)*cross instead of R*keep + L*cross
				if (loopPanActive) {
					if (benchThisSample) {
						FX_BENCH_START(benchPan);
					}
					if (loopPanRight) {
						// Pan right: L fades, R gets crossfeed from L
						q31_t cross = multiply_32x32_rshift32(outputL - outputR, loopPanCrossQ31) << 1;
						outputL = multiply_32x32_rshift32(outputL, loopPanFadeQ31) << 1;
						outputR = outputR + cross;
					}
					else {
						// Pan left: R fades, L gets crossfeed from R
						q31_t cross = multiply_32x32_rshift32(outputR - outputL, loopPanCrossQ31) << 1;
						outputR = multiply_32x32_rshift32(outputR, loopPanFadeQ31) << 1;
						outputL = outputL + cross;
					}
					if (benchThisSample) {
						FX_BENCH_STOP(benchPan);
					}
				}

				// === ANTI-CLICK: per-channel zero-crossing based muting ===
				// Skip ZC when slices are consecutive and no envelope (audio flows naturally)
				// Also skip for dry grains - input audio is continuous, no clicks to suppress
				if (!loopSkipZC && !useDry) {
					bool zcL = ((prevOutputL != 0) && ((outputL ^ prevOutputL) < 0)) || bufferZeroCrossing;
					bool zcR = ((prevOutputR != 0) && ((outputR ^ prevOutputR) < 0)) || bufferZeroCrossing;
					prevOutputL = outputL;
					prevOutputR = outputR;

					// Attack: mute each channel until its ZC found
					if (waitingForZeroCrossL) {
						if (zcL) {
							waitingForZeroCrossL = false;
						}
						else {
							outputL = 0;
						}
					}
					if (waitingForZeroCrossR) {
						if (zcR) {
							waitingForZeroCrossR = false;
						}
						else {
							outputR = 0;
						}
					}
					// Release: mute each channel at its ZC when in release zone
					bool inReleaseZone = (playbackPos > loopReleaseThreshold)
					                     || (loopBarEndSilenceEnabled && loopPlaybackLength > kBarEndZone
					                         && loopLinearBarPos > loopPlaybackLength - kBarEndZone);
					if (inReleaseZone) {
						if (!releaseMutedL && zcL) {
							releaseMutedL = true;
						}
						if (!releaseMutedR && zcR) {
							releaseMutedR = true;
						}
					}
					if (releaseMutedL) {
						outputL = 0;
					}
					if (releaseMutedR) {
						outputR = 0;
					}
				}

				// Apply delay send/return (slice-synced echo with feedback)
				if (delayActive) {
					if (benchThisSample) {
						FX_BENCH_START(benchDelay);
					}
					// Read from delay line (behind write position by delayTime)
					// Use bitmask instead of modulo (~1 cycle vs ~40 cycles)
					constexpr size_t kDelayBufferMask = kDelayBufferSize - 1;
					size_t readPos = (delayWritePos + kDelayBufferSize - delayTime) & kDelayBufferMask;
					q31_t delayL = delayBuffer[readPos].l;
					q31_t delayR = delayBuffer[readPos].r;

					// Write to delay FIRST (before mixing return) to get correct feedback
					// Send = dry signal only, feedback = 50% of delay return
					q31_t sendL = outputL >> delaySendShift;
					q31_t sendR = outputR >> delaySendShift;
					delayBuffer[delayWritePos].l = add_saturate(sendL, delayL >> 1);
					delayBuffer[delayWritePos].r = add_saturate(sendR, delayR >> 1);
					delayWritePos = (delayWritePos + 1) & kDelayBufferMask;

					// THEN mix delay return into output
					outputL = add_saturate(outputL, delayL);
					outputR = add_saturate(outputR, delayR);
					if (benchThisSample) {
						FX_BENCH_STOP(benchDelay);
					}
				}

				// === pWRITE: crossfade source into buffer ===
				// Single buffer tape-loop: read from shuffled position, write to linear position
				// Uses grain envelope for crossfade (already handles small grains via prepareGrainEnvelopeQ31)
				if (hasPWrite && looperBuffer != nullptr && pWriteGrainIsWet) {
					size_t pWritePos = loopPlaybackStartPos + loopLinearBarPos;
					while (pWritePos >= kLooperBufferSize) {
						pWritePos -= kLooperBufferSize;
					}
					int32_t invEnv = 0x7FFFFFFF - pWriteEnvQ31;
					q31_t existL = looperBuffer[pWritePos].l;
					q31_t existR = looperBuffer[pWritePos].r;
					q31_t newL = (multiply_32x32_rshift32(existL, invEnv) + multiply_32x32_rshift32(srcL, pWriteEnvQ31))
					             << 1;
					q31_t newR = (multiply_32x32_rshift32(existR, invEnv) + multiply_32x32_rshift32(srcR, pWriteEnvQ31))
					             << 1;
					looperBuffer[pWritePos] = {newL, newR};
				}

				sample.l = outputL;
				sample.r = outputR;

				// === ADVANCE: move through slice with subdivisions (ratchet) ===
				// FUTURE MODE IDEA: Subgrain sampling - hash-based probability to skip/vary subdivisions
				// At subdivision boundary, evalBool(seed ^ subdivIdx, skipProb) to create broken ratchets
				// Cost: ~5 cycles per subdiv boundary (not per sample). Tie skipProb to zone knob triangle.
				if (benchThisSample) {
					FX_BENCH_START(benchAdvance);
				}
				// When subdivisions > 1, replay start of slice N times (ratchet)
				// Uses hoisted loopEffectiveSubLen (updated only on subdivision change, not every sample)
				// Pitch mode: use fixed-point accumulation, octave-up: increment by 2
				bool sliceBoundary = false;
				if (loopPitchRatioFP != 65536) {
					// Pitch mode: fixed-point position tracking
					loopPitchPosFP += loopPitchRatioFP;
					size_t newPos = loopPitchPosFP >> 16;
					if (newPos >= loopEffectiveSubLen) {
						loopPitchPosFP = 0;
						sliceBoundary = true;
					}
					playbackPos = newPos; // Keep integer pos in sync for other code
				}
				else {
					// Standard: integer increment (1 or 2 for octave-up)
					playbackPos += loopPitchIncrement;
					if (playbackPos >= loopEffectiveSubLen) {
						playbackPos = 0;
						// Pitch up: internal loop (first pass) vs real boundary (second pass)
						bool isInternalLoop = (loopPitchIncrement == 2 && loopPitchUpLoopCount == 0);
						if (isInternalLoop) {
							loopPitchUpLoopCount = 1; // Keep prevOutput to catch end→start discontinuity
						}
						else {
							sliceBoundary = true;
						}
					}
				}
				if (sliceBoundary) {
					// Only force ZC wait if next grain is non-consecutive (needs transition protection)
					// Consecutive grains flow naturally - no ZC needed
					if (!loopNextConsecutive) {
						waitingForZeroCrossL = waitingForZeroCrossR = true;
						prevOutputL = prevOutputR = 0;
					}
					else {
						// Consecutive grain: advance slice offset immediately for remaining samples
						// This ensures audio continuity within the current buffer
						sliceStartOffset = (sliceStartOffset + loopCurrentSliceLength) % loopPlaybackLength;
						loopSliceStartOffset = sliceStartOffset;
					}
					releaseMutedL = releaseMutedR = false;
					loopPitchUpLoopCount = 0;
					// Advance subdivision only on real boundary
					if (++scatterSubdivIndex >= scatterSubdivisions) {
						scatterSubdivIndex = 0;

						// Consecutive grains: handle inline, skip deferred block
						// Non-consecutive: defer to buffer start for full recomputation
						if (loopNextConsecutive && stutterConfig.scatterMode != ScatterMode::Repeat) {
							// Advance slice index (use effectiveGrainLength from current grain)
							int32_t grainLen = scatterCachedGrain.grainLength > 0 ? scatterCachedGrain.grainLength : 1;
							scatterSliceIndex = (scatterSliceIndex + grainLen) % scatterNumSlices;

							// Compute next grain's consecutive flag for decay envelope
							// Use cached zone params and offsets (throttle controls when these refresh)
							int32_t nextIdx = scatterSliceIndex + 1;
							auto nextGrain = deluge::dsp::scatter::computeGrainParams(
							    cachedZoneAParam, cachedZoneBParam, cachedMacroConfigParam, cachedMacroParam, nextIdx,
							    &cachedOffsets);
							scatterNextConsecutive =
							    (nextGrain.sliceOffset == 0) && !nextGrain.shouldReverse && !nextGrain.shouldPitchUp;
							loopNextConsecutive = scatterNextConsecutive;
							// Skip deferred block - consecutive grains don't need full recomputation
						}
						else {
							needsSliceSetup = true;
						}
					}
					// Update lengths for next subdivision
					loopEffectiveSubLen =
					    std::max(kMinGrainSize, scatterSubdivIndex == loopLastSubdivIndex ? loopLastSubSliceLength
					                                                                      : loopSubSliceLength);
					effectiveEnd =
					    std::min(loopEffectiveSubLen, loopGatedLen > 0 ? size_t(loopGatedLen) : loopEffectiveSubLen);
					loopReleaseThreshold = effectiveEnd > kGrainReleaseZone ? effectiveEnd - kGrainReleaseZone : 0;
				}

				// Advance linear bar position for leaky writes (always 1:1 with real time)
				loopLinearBarPos++;
				if (loopLinearBarPos >= loopPlaybackLength) {
					loopLinearBarPos = 0;
				}

				if (benchThisSample) {
					FX_BENCH_STOP(benchAdvance);
				}

				sampleIdx++;
			}

			// Write back state for next buffer
			scatterLinearBarPos = loopLinearBarPos;
			scatterPitchUpLoopCount = loopPitchUpLoopCount;
			scatterPitchPosFP = loopPitchPosFP;

			FX_BENCH_STOP(benchTotal);
		}
		return;
	}

	// Classic mode: original community behavior with resampling
	// Benchmark: classic stutter processing (separate from scatter modes)
	FX_BENCH_DECLARE(benchClassic, "stutter", "classic");
	FX_BENCH_SCOPE(benchClassic);

	int32_t rate = getStutterRate(paramManager, magnitude, timePerTickInverse);
	buffer.setupForRender(rate);

	if (status == Status::RECORDING) {
		for (StereoSample sample : audio) {
			int32_t strength1;
			int32_t strength2;

			if (buffer.isNative()) {
				buffer.clearAndMoveOn();
				sizeLeftUntilRecordFinished--;
			}
			else {
				strength2 = buffer.advance([&] {
					buffer.clearAndMoveOn();
					sizeLeftUntilRecordFinished--;
				});
				strength1 = 65536 - strength2;
			}

			buffer.write(sample, strength1, strength2);
		}

		if (sizeLeftUntilRecordFinished < 0) {
			if (currentReverse) {
				buffer.setCurrent(buffer.end() - 1);
			}
			else {
				buffer.setCurrent(buffer.begin());
			}
			// Gated stutter: capture grain length and rate at trigger time
			if (stutterConfig.scatterMode == ScatterMode::Burst) {
				gatedGrainLength = buffer.size() / 2;
				gatedInitialCycle = buffer.size();
				gatedInitialRate = rate;
				gatedGrainReadPos = 0;
				gatedCyclePos = 0;
			}
			status = Status::PLAYING;
		}
	}
	else { // PLAYING
		bool isGatedStutter = (stutterConfig.scatterMode == ScatterMode::Burst);

		if (isGatedStutter && gatedInitialRate > 0) {
			// Gated stutter: play fixed grain at 1:1 (no pitch change), rate controls spacing
			// currentCycleLength = initialCycle * (initialRate / currentRate)
			// Higher rate = shorter cycle = more frequent triggers
			size_t currentCycleLength =
			    static_cast<size_t>((uint64_t)gatedInitialCycle * gatedInitialRate / (uint32_t)rate);
			if (currentCycleLength < 64) {
				currentCycleLength = 64; // Minimum to prevent audio-rate chaos
			}
			// Clamp grain to fit in cycle (with some headroom for silence)
			size_t effectiveGrainLength = gatedGrainLength;
			if (effectiveGrainLength > currentCycleLength * 9 / 10) {
				effectiveGrainLength = currentCycleLength * 9 / 10; // Max 90% duty cycle
			}
			if (effectiveGrainLength < 32) {
				effectiveGrainLength = 32;
			}

			for (StereoSample& sample : audio) {
				if (gatedCyclePos < effectiveGrainLength) {
					// In grain: read at native speed (no pitch change)
					sample.l = buffer.begin()[gatedGrainReadPos].l;
					sample.r = buffer.begin()[gatedGrainReadPos].r;
					gatedGrainReadPos++;
					if (gatedGrainReadPos >= effectiveGrainLength) {
						gatedGrainReadPos = 0; // Wrap grain read for next cycle
					}
				}
				else {
					// After grain: silence until cycle completes
					sample.l = 0;
					sample.r = 0;
				}

				gatedCyclePos++;
				if (gatedCyclePos >= currentCycleLength) {
					gatedCyclePos = 0;
					gatedGrainReadPos = 0; // Reset grain read for next trigger
				}
			}
		}
		else {
			// Classic mode: normal interpolated playback
			for (StereoSample& sample : audio) {
				int32_t strength1;
				int32_t strength2;

				if (buffer.isNative()) {
					if (currentReverse) {
						buffer.moveBack();
					}
					else {
						buffer.moveOn();
					}
					sample.l = buffer.current().l;
					sample.r = buffer.current().r;
				}
				else {
					if (currentReverse) {
						strength2 = buffer.retreat([&] { buffer.moveBack(); });
					}
					else {
						strength2 = buffer.advance([&] { buffer.moveOn(); });
					}

					strength1 = 65536 - strength2;

					if (currentReverse) {
						StereoSample* prevPos = &buffer.current() - 1;
						if (prevPos < buffer.begin()) {
							prevPos = buffer.end() - 1;
						}
						StereoSample& fromDelay1 = buffer.current();
						StereoSample& fromDelay2 = *prevPos;
						sample.l = (multiply_32x32_rshift32(fromDelay1.l, strength1 << 14)
						            + multiply_32x32_rshift32(fromDelay2.l, strength2 << 14))
						           << 2;
						sample.r = (multiply_32x32_rshift32(fromDelay1.r, strength1 << 14)
						            + multiply_32x32_rshift32(fromDelay2.r, strength2 << 14))
						           << 2;
					}
					else {
						StereoSample* nextPos = &buffer.current() + 1;
						if (nextPos == buffer.end()) {
							nextPos = buffer.begin();
						}
						StereoSample& fromDelay1 = buffer.current();
						StereoSample& fromDelay2 = *nextPos;
						sample.l = (multiply_32x32_rshift32(fromDelay1.l, strength1 << 14)
						            + multiply_32x32_rshift32(fromDelay2.l, strength2 << 14))
						           << 2;
						sample.r = (multiply_32x32_rshift32(fromDelay1.r, strength1 << 14)
						            + multiply_32x32_rshift32(fromDelay2.r, strength2 << 14))
						           << 2;
					}
				}

				// Ping-pong
				if (stutterConfig.pingPong
				    && ((currentReverse && &buffer.current() == buffer.begin())
				        || (!currentReverse && &buffer.current() == buffer.end() - 1))) {
					currentReverse = !currentReverse;
				}
			}
		}
	}
}

void Stutterer::endStutter(ParamManagerForTimeline* paramManager) {
	bool isScatterMode =
	    (stutterConfig.scatterMode != ScatterMode::Classic && stutterConfig.scatterMode != ScatterMode::Burst);

	if (isScatterMode) {
		// Non-Classic/Burst modes: return to standby for continuous recording
		// Buffer content is preserved - pWrite evolves content on next trigger
		playbackPos = 0;

		// Return to standby, keep recording to buffer
		// Ready for instant re-trigger with preserved content
		// Don't reset looperWritePos - continue ring buffer recording
		status = Status::STANDBY;
		return;
	}

	// Classic mode: original community behavior
	if (startedFromStandby) {
		status = Status::STANDBY;
		buffer.setCurrent(buffer.begin() + delaySpaceBetweenReadAndWrite);
		startedFromStandby = false;
	}
	else {
		buffer.discard();
		status = Status::OFF;
		activeSource = nullptr;
		pendingSource = nullptr;
	}

	if (paramManager) {
		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

		if (stutterConfig.quantized) {
			unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(valueBeforeStuttering);
		}
		else {
			if (unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE) < 0) {
				unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);
			}
		}
	}
	lastQuantizedKnobDiff = 0;
	valueBeforeStuttering = 0;
}

Error Stutterer::enableStandby(void* source, int32_t magnitude, uint32_t timePerTickInverse) {
	if (status == Status::STANDBY && activeSource == source) {
		return Error::NONE;
	}

	if (status == Status::RECORDING || status == Status::PLAYING) {
		return Error::UNSPECIFIED;
	}

	if (status == Status::STANDBY) {
		buffer.discard();
	}

	// Allocate ring buffer for continuous recording
	Error error = buffer.initWithSize(kLooperBufferSize, false);
	if (error != Error::NONE) {
		return error;
	}
	buffer.setCurrent(buffer.begin());

	status = Status::STANDBY;
	activeSource = source;
	standbyIdleSamples = 0; // Start timeout counter fresh
	return Error::NONE;
}

void Stutterer::disableStandby() {
	if (status == Status::STANDBY) {
		// Classic mode: discard delay buffer
		buffer.discard();

		// Looper modes: deallocate buffer
		if (looperBuffer != nullptr) {
			delugeDealloc(looperBuffer);
			looperBuffer = nullptr;
		}
		if (delayBuffer != nullptr) {
			delugeDealloc(delayBuffer);
			delayBuffer = nullptr;
		}
		delayActive = false;

		status = Status::OFF;
		activeSource = nullptr;
		pendingSource = nullptr;
		releasedDuringStandby = false;
	}
}

void Stutterer::recordStandby(void* source, std::span<StereoSample> audio, int64_t lastSwungTick, uint32_t syncLength) {
	// === SINGLE-BUFFER OWNERSHIP MODEL ===
	// Only activeSource can write to looperBuffer during STANDBY.
	// During PLAYING, pWrite handles writes instead.

	if (source != activeSource) {
		return; // Not your buffer
	}

	// Check if looper buffer is in use (scatter mode)
	bool hasLooperBuffer = (looperBuffer != nullptr);

	if (hasLooperBuffer) {
		// Only record during STANDBY - pWrite handles writes during PLAYING
		if (status != Status::STANDBY) {
			return;
		}

		// Beat-quantized recording start using interpolated tick position
		if (waitingForRecordBeat) {
			int64_t currentBeatIndex = lastSwungTick / syncLength;
			if (recordStartTick == 0) {
				// Set target to NEXT beat boundary (store as index)
				recordStartTick = currentBeatIndex + 1;
			}
			if (currentBeatIndex < recordStartTick) {
				return; // Not yet at target beat boundary
			}
			// Beat boundary crossed - start recording (sample-accurate)
			waitingForRecordBeat = false;
			looperWritePos = 0;
			looperBufferFull = false;
		}

		// Standby timeout: count idle samples and release after N bars
		if (playbackLength > 0) {
			standbyIdleSamples += audio.size();
			if (standbyIdleSamples >= playbackLength * kStandbyTimeoutBars) {
				disableStandby();
				return;
			}
		}

		for (StereoSample sample : audio) {
			looperBuffer[looperWritePos] = sample;
			looperWritePos++;
			if (looperWritePos >= kLooperBufferSize) {
				looperWritePos = 0;
				looperBufferFull = true; // Ring buffer wrapped - full loop available
			}
		}
		// Also mark full if we've recorded at least playbackLength samples
		if (!looperBufferFull && playbackLength > 0 && looperWritePos >= playbackLength) {
			looperBufferFull = true;
		}
		return;
	}

	// Classic mode: use delay buffer during STANDBY only
	if (status != Status::STANDBY) {
		return;
	}
	for (StereoSample sample : audio) {
		buffer.current().l = sample.l;
		buffer.current().r = sample.r;
		buffer.moveOn();
	}
}

Error Stutterer::armStutter(void* source, ParamManagerForTimeline* paramManager, StutterConfig sc, int32_t magnitude,
                            uint32_t timePerTickInverse, int64_t targetTick, size_t loopLengthSamples, bool halfBar) {
	// This is called when a source wants to arm for playback.

	if (status == Status::RECORDING) {
		return Error::UNSPECIFIED; // Classic mode recording, can't interrupt
	}

	// Store config for when trigger fires
	armedConfig = sc;
	armedHalfBarMode = halfBar;
	armedLoopLengthSamples = loopLengthSamples;

	if (status == Status::PLAYING && looperBuffer != nullptr && activeSource != source) {
		// TAKEOVER: Someone else is playing, we want to inherit the buffer
		// Do immediate takeover - single tap to take over
		stutterConfig = sc;
		currentReverse = stutterConfig.reversed;
		halfBarMode = halfBar;
		playbackLength = std::min(loopLengthSamples, kLooperBufferSize);
		if (playbackLength == 0) {
			playbackLength = looperBufferFull ? kLooperBufferSize : looperWritePos;
		}
		triggerPlaybackNow(source);
		return Error::NONE;
	}

	// Not playing - this is initial setup, delegate to beginStutter
	return beginStutter(source, paramManager, sc, magnitude, timePerTickInverse, loopLengthSamples, halfBar);
}

bool Stutterer::checkArmedTrigger(int64_t currentTick, ParamManager* paramManager, int32_t magnitude,
                                  uint32_t timePerTickInverse) {
	// === SIMPLIFIED: No beat quantization for now ===
	// Takeover trigger happens via beginStutter when recordSource calls it.
	// This function is vestigial - always returns false.
	// TODO: Re-implement beat quantization properly later.
	return false;
}

bool Stutterer::checkPendingTrigger(void* source, int64_t lastSwungTick, uint32_t syncLength,
                                    ParamManager* paramManager, int32_t magnitude, uint32_t timePerTickInverse) {
	if (!pendingPlayTrigger || activeSource != source) {
		return false;
	}

	// Tick-boundary detection: check if we've crossed into a new beat
	// Uses interpolated tick position for accurate detection within audio buffers
	int64_t currentBeatIndex = lastSwungTick / syncLength;

	// On first check, set target to NEXT beat boundary
	if (playTriggerTick == 0) {
		playTriggerTick = currentBeatIndex + 1; // Store as beat index, not tick
	}

	// Check if we've reached or passed the target beat
	if (currentBeatIndex < playTriggerTick) {
		return false; // Not yet at target beat boundary
	}

	// Ensure we have enough recorded audio before triggering
	// If not, delay trigger to next beat
	bool hasEnoughSamples = looperBufferFull || looperWritePos >= playbackLength;
	if (!hasEnoughSamples) {
		// Push trigger to next beat
		playTriggerTick = currentBeatIndex + 1;
		return false;
	}

	// Beat boundary crossed with enough audio - trigger NOW
	triggerPlaybackNow(source);
	return true;
}

void Stutterer::triggerPlaybackNow(void* source) {
	pendingPlayTrigger = false;

	// Calculate where loop starts in buffer (single buffer, no swap)
	// looperWritePos is where we WOULD write next, so loop ends there
	// Buffer content is preserved - pWrite controls how fast new content overwrites old
	if (looperWritePos >= playbackLength) {
		playbackStartPos = looperWritePos - playbackLength;
	}
	else {
		playbackStartPos = kLooperBufferSize - (playbackLength - looperWritePos);
	}

	// Single buffer: no swap needed, content preserved for inheritance/pWrite evolution
	// Reset write position for pWrite (linear writes start from bar beginning)
	looperWritePos = 0;
	looperBufferFull = true; // Treat as full since we're playing from it

	// Apply fade at buffer wrap boundary (position 0) to eliminate ring buffer discontinuity
	// Position 0 and bufSize-1 were recorded ~4s apart - fade once here instead of per-sample
	if (looperBuffer != nullptr) {
		for (size_t i = 0; i < kBufferWrapFadeLen; i++) {
			// Fade in at start of buffer
			q31_t fadeIn = static_cast<q31_t>((static_cast<int64_t>(i) << 31) / kBufferWrapFadeLen);
			looperBuffer[i].l = multiply_32x32_rshift32(looperBuffer[i].l, fadeIn) << 1;
			looperBuffer[i].r = multiply_32x32_rshift32(looperBuffer[i].r, fadeIn) << 1;
			// Fade out at end of buffer
			size_t endIdx = kLooperBufferSize - kBufferWrapFadeLen + i;
			q31_t fadeOut =
			    static_cast<q31_t>((static_cast<int64_t>(kBufferWrapFadeLen - 1 - i) << 31) / kBufferWrapFadeLen);
			looperBuffer[endIdx].l = multiply_32x32_rshift32(looperBuffer[endIdx].l, fadeOut) << 1;
			looperBuffer[endIdx].r = multiply_32x32_rshift32(looperBuffer[endIdx].r, fadeOut) << 1;
		}
	}

	// Reset for playback
	playbackPos = 0;
	waitingForZeroCrossL = waitingForZeroCrossR = true;
	releaseMutedL = releaseMutedR = false;
	prevOutputL = prevOutputR = 0; // Reset for fresh zero crossing detection
	scatterLinearBarPos = 0;       // Reset linear position for pWrite
	currentSliceLength = playbackLength;
	sliceStartOffset = 0;
	scatterSliceIndex = 0;
	scatterBarIndex = 0; // Reset multi-bar counter for fresh pattern start
	scatterReversed = false;
	scatterPitchUp = false;
	scatterDryMix = 0;
	scatterDryThreshold = 1.0f;
	scatterEnvDepth = 0;
	scatterEnvShape = 0.5f;
	scatterEnvWidth = 1.0f;
	scatterGateRatio = 1.0f;
	scatterPan = 0;
	scatterSubdivisions = 1;
	scatterSubdivIndex = 0;
	scatterPitchUpLoopCount = 0;
	scatterRepeatCounter = 0;
	scatterRepeatLoopIndex = 1;                 // Start at 1 for non-zero hash seed
	scatterSubSliceLength = playbackLength;     // No subdivisions initially
	scatterLastSubSliceLength = playbackLength; // Same when no subdivisions
	needsSliceSetup = true;                     // Force slice setup on first buffer
	scatterParamThrottle = 10;                  // Bypass throttle for first setup
	staticTriangles.valid = false;              // Force recompute on first slice
	standbyIdleSamples = 0;                     // Reset timeout counter
	lastTickBarIndex = -1;                      // Reset bar boundary tracking
	// Reset Grain mode state for fresh start (important for takeover)
	grainPhaseA = 0;
	grainPhaseB = 0;
	grainPosA = 0;
	grainPosB = 0;
	grainOffsetA = 0;
	grainOffsetB = 0;
	grainAIsDry = false;
	grainBIsDry = false;
	grainAWritesWet = true; // Default to writing until first grain wrap decides
	status = Status::PLAYING;
	// Source now owns buffer
	activeSource = source;
	pendingSource = nullptr;

	// Momentary mode: if encoder was released during STANDBY/takeover, end immediately
	// Use armedConfig (set from source's config when they first pressed) instead of
	// stutterConfig (which may have been overwritten by updateLiveParams from previous player)
	if (releasedDuringStandby && !armedConfig.isLatched()) {
		releasedDuringStandby = false;
		endStutter(nullptr);
	}
}

void Stutterer::cancelArmed() {
	// Cancel pending takeover
	if (pendingSource != nullptr) {
		pendingSource = nullptr;
		return;
	}

	if (status == Status::ARMED) {
		// Classic mode armed - go back to standby or off
		if (startedFromStandby) {
			status = Status::STANDBY;
		}
		else {
			buffer.discard();
			status = Status::OFF;
			activeSource = nullptr;
			pendingSource = nullptr;
		}
	}
}
