/*
 * Copyright © 2024 The Deluge Community
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

#include "sid_waves.h"
#include "dsp/sid/arm_intrinsics.h"
#include <algorithm>
#include <cstring>

namespace deluge {
namespace dsp {
namespace sid {

// Global amplitude boost factor to compensate for removed amplitude * 8 in oscillator.cpp
// This makes it easy to adjust all SID oscillators consistently
const float SID_AMPLITUDE_BOOST = 8.0f;

// Define the wave tables
std::array<int16_t, 4096> sidTriangleTable = {};
std::array<int16_t, 4096> sidSawTable = {};
std::array<int16_t, 4096> sidPulseTable = {};
std::array<int16_t, 4096> sidNoiseTable = {};

// Shift register for noise generation
static uint32_t shiftRegister = 0x7FFFFF;

// Initialize the SID wave tables based on reSID algorithm
void initSidWaveTables() {
	// Generate triangle table using reSID's algorithm
	uint32_t acc = 0;
	for (int i = 0; i < 4096; i++) {
		uint32_t msb = acc & 0x800000;
		// Triangle calculation - the ^-!!msb part inverts based on MSB state
		// This is almost directly from the reSID source for the triangle wave
		sidTriangleTable[i] = ((acc ^ -!!msb) >> 11) & 0xffe;

		// Sawtooth is simpler - just use upper bits of accumulator
		sidSawTable[i] = acc >> 12;

		// Pulse starts high (will be controlled by pulse width comparison)
		sidPulseTable[i] = 0xfff;

		// Initialize noise table to all ones (will be dynamically generated)
		sidNoiseTable[i] = 0xfff;

		acc += 0x1000;
	}
}

// Helper function to get table index from phase
inline uint32_t getTableIndex(uint32_t phase) {
	// Use upper 12 bits of phase for table lookup
	return (phase >> 20) & 0xfff;
}

// Create a vectorized version of the triangle wave generator
std::tuple<int32x4_t, uint32_t> generateSidTriangleVector(uint32_t phase, uint32_t phaseIncrement,
                                                          uint32_t phaseOffset) {
	// Apply phase offset if provided
	phase += phaseOffset;

	// Get indices for 4 consecutive samples
	uint32_t index1 = getTableIndex(phase);
	phase += phaseIncrement;
	uint32_t index2 = getTableIndex(phase);
	phase += phaseIncrement;
	uint32_t index3 = getTableIndex(phase);
	phase += phaseIncrement;
	uint32_t index4 = getTableIndex(phase);
	phase += phaseIncrement;

	// Load triangle wave values from the table
	int32_t value1 = sidTriangleTable[index1];
	int32_t value2 = sidTriangleTable[index2];
	int32_t value3 = sidTriangleTable[index3];
	int32_t value4 = sidTriangleTable[index4];

	// Create vector of triangle values
	int32x4_t triangleValues = vdupq_n_s32(0);
	triangleValues = vsetq_lane_s32(value1, triangleValues, 0);
	triangleValues = vsetq_lane_s32(value2, triangleValues, 1);
	triangleValues = vsetq_lane_s32(value3, triangleValues, 2);
	triangleValues = vsetq_lane_s32(value4, triangleValues, 3);

	// BOOST: Apply consistent amplitude boost
	triangleValues = vmulq_n_s32(triangleValues, SID_AMPLITUDE_BOOST);

	// Scale to 24-bit range and shift for proper alignment in Q format
	triangleValues = vshlq_n_s32(triangleValues, 16);

	return std::make_tuple(triangleValues, phase);
}

// Create a vectorized version of the saw wave generator
std::tuple<int32x4_t, uint32_t> generateSidSawVector(uint32_t phase, uint32_t phaseIncrement, uint32_t phaseOffset) {
	// Apply phase offset if provided
	phase += phaseOffset;

	// Get indices for 4 consecutive samples
	uint32_t index1 = getTableIndex(phase);
	phase += phaseIncrement;
	uint32_t index2 = getTableIndex(phase);
	phase += phaseIncrement;
	uint32_t index3 = getTableIndex(phase);
	phase += phaseIncrement;
	uint32_t index4 = getTableIndex(phase);
	phase += phaseIncrement;

	// Load saw wave values from the table
	int32_t value1 = sidSawTable[index1];
	int32_t value2 = sidSawTable[index2];
	int32_t value3 = sidSawTable[index3];
	int32_t value4 = sidSawTable[index4];

	// Create vector of saw values
	int32x4_t sawValues = vdupq_n_s32(0);
	sawValues = vsetq_lane_s32(value1, sawValues, 0);
	sawValues = vsetq_lane_s32(value2, sawValues, 1);
	sawValues = vsetq_lane_s32(value3, sawValues, 2);
	sawValues = vsetq_lane_s32(value4, sawValues, 3);

	// BOOST: Apply consistent amplitude boost
	sawValues = vmulq_n_s32(sawValues, SID_AMPLITUDE_BOOST);

	// Scale to 24-bit range and shift for proper alignment in Q format
	sawValues = vshlq_n_s32(sawValues, 16);

	return std::make_tuple(sawValues, phase);
}

// Create a vectorized version of the pulse wave generator
std::tuple<int32x4_t, uint32_t> generateSidPulseVector(uint32_t phase, uint32_t phaseIncrement, uint32_t pulseWidth) {
	// Calculate minimum pulse width step based on phase increment (anti-aliasing)
	uint32_t minPwStep = (phaseIncrement + (1 << 19)) >> 20; // Convert to 12-bit range
	if (minPwStep < 1)
		minPwStep = 1;

	// Handle edge cases to avoid clicks and ensure proper behavior
	if (pulseWidth == 0 || pulseWidth >= 0xFFF) {
		// Return silence for extreme pulse width values (0% or 100%)
		int32x4_t silenceValues = vdupq_n_s32(0);
		phase += phaseIncrement * 4;
		return std::make_tuple(silenceValues, phase);
	}

	// Apply minimum width check for narrow pulses
	if (pulseWidth > 0 && pulseWidth < minPwStep) {
		pulseWidth = minPwStep;
	}

	// Also check for values too close to maximum
	if (pulseWidth < 0xFFF && (0xFFF - pulseWidth) < minPwStep) {
		pulseWidth = 0xFFF - minPwStep;
	}

	// Calculate compensation factor based on pulse width
	// Start with the base boost factor
	float compensationFactor = SID_AMPLITUDE_BOOST;

	// For duty cycles other than 0% and 100%
	if (pulseWidth > 0 && pulseWidth < 0xFFF) {
		// Calculate effective on-time (distance from 50%)
		uint32_t effectiveOnTime;
		if (pulseWidth <= 0x800) {
			effectiveOnTime = pulseWidth; // For narrow pulses (0-50%)
		}
		else {
			effectiveOnTime = 0xFFF - pulseWidth; // For wide pulses (50-100%)
		}

		// Apply much more aggressive compensation
		// Use a square root relationship for more balanced compensation across the range
		if (effectiveOnTime < 0x800) {
			// Square root relationship provides more boost to wider range of values
			float normalizedDuty = (float)effectiveOnTime / 0x800;
			compensationFactor = SID_AMPLITUDE_BOOST * (float)0x800 / (float)effectiveOnTime;

			// Increase compensation cap to 16x the base boost (128.0 at base of 8.0)
			if (compensationFactor > SID_AMPLITUDE_BOOST * 16.0f) {
				compensationFactor = SID_AMPLITUDE_BOOST * 16.0f;
			}
		}
	}

	// Get the 12-bit phase value for first sample
	uint32_t phase_12 = (phase >> 20) & 0xFFF;
	int32_t value1 = (phase_12 < pulseWidth) ? 0xFFF : 0;
	phase += phaseIncrement;

	// Get the 12-bit phase value for second sample
	phase_12 = (phase >> 20) & 0xFFF;
	int32_t value2 = (phase_12 < pulseWidth) ? 0xFFF : 0;
	phase += phaseIncrement;

	// Get the 12-bit phase value for third sample
	phase_12 = (phase >> 20) & 0xFFF;
	int32_t value3 = (phase_12 < pulseWidth) ? 0xFFF : 0;
	phase += phaseIncrement;

	// Get the 12-bit phase value for fourth sample
	phase_12 = (phase >> 20) & 0xFFF;
	int32_t value4 = (phase_12 < pulseWidth) ? 0xFFF : 0;
	phase += phaseIncrement;

	// Create vector of pulse values with the compensated amplitude
	int32_t compensatedValue = (int32_t)(0xFFF * compensationFactor / SID_AMPLITUDE_BOOST);
	__simd128_int32_t pulseVector = vdupq_n_s32(0);
	pulseVector = vsetq_lane_s32(value1 ? compensatedValue : 0, pulseVector, 0);
	pulseVector = vsetq_lane_s32(value2 ? compensatedValue : 0, pulseVector, 1);
	pulseVector = vsetq_lane_s32(value3 ? compensatedValue : 0, pulseVector, 2);
	pulseVector = vsetq_lane_s32(value4 ? compensatedValue : 0, pulseVector, 3);

	// Scale to 24-bit range and shift for proper alignment in Q format
	pulseVector = vshlq_n_s32(pulseVector, 16);

	return std::make_tuple(pulseVector, phase);
}

// Helper function to update the shift register for noise generation
// Simplified version of reSID's algorithm
inline void clockShiftRegister() {
	// bit0 = (bit22 | test) ^ bit17
	uint32_t bit0 = ((shiftRegister >> 22) ^ (shiftRegister >> 17)) & 0x1;
	shiftRegister = ((shiftRegister << 1) | bit0) & 0x7fffff;
}

// Function to get noise output from the shift register
inline int32_t getNoiseOutput() {
	// Extract bits from the shift register according to reSID's algorithm
	return ((shiftRegister & 0x100000) >> 9) | ((shiftRegister & 0x040000) >> 8) | ((shiftRegister & 0x004000) >> 5)
	       | ((shiftRegister & 0x000800) >> 3) | ((shiftRegister & 0x000200) >> 2) | ((shiftRegister & 0x000020) << 1)
	       | ((shiftRegister & 0x000004) << 3) | ((shiftRegister & 0x000001) << 4);
}

// Create a vectorized version of the noise generator
std::tuple<int32x4_t, uint32_t> generateSidNoiseVector(uint32_t phase, uint32_t phaseIncrement) {
	// Clock the shift register based on bit 19 of the accumulator
	uint32_t prevPhase = phase;

	// Check if bit 19 changed from 0 to 1 across samples
	int32_t values[4];

	for (int i = 0; i < 4; i++) {
		uint32_t nextPhase = phase + phaseIncrement;
		// Check if bit 19 transitions from 0 to 1
		if (!(prevPhase & 0x080000) && (nextPhase & 0x080000)) {
			clockShiftRegister();
		}
		prevPhase = nextPhase;
		phase = nextPhase;

		// Get noise output for this sample
		values[i] = getNoiseOutput();
	}

	// Create vector of noise values
	int32x4_t noiseValues = vld1q_s32(values);

	// BOOST: Apply consistent amplitude boost
	noiseValues = vmulq_n_s32(noiseValues, SID_AMPLITUDE_BOOST);

	// Scale to 24-bit range and shift for proper alignment in Q format
	noiseValues = vshlq_n_s32(noiseValues, 16);

	return std::make_tuple(noiseValues, phase);
}

// Helper function to create amplitude vector for 4 samples
inline int32x4_t createAmplitudeVector(int32_t amplitude, int32_t amplitudeIncrement) {
	int32x4_t result = vdupq_n_s32(amplitude);
	result = vsetq_lane_s32(amplitude + amplitudeIncrement, result, 1);
	result = vsetq_lane_s32(amplitude + (amplitudeIncrement * 2), result, 2);
	result = vsetq_lane_s32(amplitude + (amplitudeIncrement * 3), result, 3);
	return result;
}

// Main render function for SID triangle waveform
void renderSidTriangle(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement,
                       uint32_t& phase, bool applyAmplitude, uint32_t phaseOffset, int32_t amplitudeIncrement) {

	int32_t numSamples = bufferEnd - bufferStart;
	int32_t* currentPos = bufferStart;

	// Process blocks of 4 samples at a time (vector processing)
	while (currentPos + 4 <= bufferEnd) {
		int32x4_t triangleVector;
		std::tie(triangleVector, phase) = generateSidTriangleVector(phase, phaseIncrement, phaseOffset);

		if (applyAmplitude) {
			// Apply amplitude envelope
			int32x4_t ampVector = createAmplitudeVector(amplitude, amplitudeIncrement);
			triangleVector = vqdmulhq_s32(triangleVector, ampVector);
			amplitude += amplitudeIncrement * 4;

			// Add to existing buffer content
			int32x4_t existingData = vld1q_s32(currentPos);
			triangleVector = vaddq_s32(triangleVector, existingData);
		}

		// Store result
		vst1q_s32(currentPos, triangleVector);
		currentPos += 4;
	}

	// Process any remaining samples
	while (currentPos < bufferEnd) {
		uint32_t index = getTableIndex(phase);
		int32_t value = sidTriangleTable[index];

		// BOOST: Apply consistent amplitude boost
		value *= SID_AMPLITUDE_BOOST;

		value <<= 16; // Scale to match the vector implementation

		if (applyAmplitude) {
			value = ((int64_t)value * amplitude) >> 31;
			amplitude += amplitudeIncrement;
			value += *currentPos;
		}

		*currentPos++ = value;
		phase += phaseIncrement;
	}
}

// Main render function for SID saw waveform
void renderSidSaw(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement, uint32_t& phase,
                  bool applyAmplitude, uint32_t phaseOffset, int32_t amplitudeIncrement) {

	int32_t numSamples = bufferEnd - bufferStart;
	int32_t* currentPos = bufferStart;

	// Process blocks of 4 samples at a time (vector processing)
	while (currentPos + 4 <= bufferEnd) {
		int32x4_t sawVector;
		std::tie(sawVector, phase) = generateSidSawVector(phase, phaseIncrement, phaseOffset);

		if (applyAmplitude) {
			// Apply amplitude envelope
			int32x4_t ampVector = createAmplitudeVector(amplitude, amplitudeIncrement);
			sawVector = vqdmulhq_s32(sawVector, ampVector);
			amplitude += amplitudeIncrement * 4;

			// Add to existing buffer content
			int32x4_t existingData = vld1q_s32(currentPos);
			sawVector = vaddq_s32(sawVector, existingData);
		}

		// Store result
		vst1q_s32(currentPos, sawVector);
		currentPos += 4;
	}

	// Process any remaining samples
	while (currentPos < bufferEnd) {
		uint32_t index = getTableIndex(phase);
		int32_t value = sidSawTable[index];

		// BOOST: Apply consistent amplitude boost
		value *= SID_AMPLITUDE_BOOST;

		value <<= 16; // Scale to match the vector implementation

		if (applyAmplitude) {
			value = ((int64_t)value * amplitude) >> 31;
			amplitude += amplitudeIncrement;
			value += *currentPos;
		}

		*currentPos++ = value;
		phase += phaseIncrement;
	}
}

// Main render function for SID pulse waveform
void renderSidPulse(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement,
                    uint32_t& phase, uint32_t pulseWidth, bool applyAmplitude, int32_t amplitudeIncrement) {

	int32_t numSamples = bufferEnd - bufferStart;
	int32_t* currentPos = bufferStart;

	// Calculate minimum pulse width step based on phase increment (anti-aliasing)
	uint32_t minPwStep = (phaseIncrement + (1 << 19)) >> 20; // Convert to 12-bit range
	if (minPwStep < 1)
		minPwStep = 1;

	// Handle edge cases to avoid clicks and ensure proper behavior
	if (pulseWidth == 0 || pulseWidth >= 0xFFF) {
		// For extreme pulse width values (0% or 100%), output silence
		if (applyAmplitude) {
			// We still need to respect existing buffer content
			return; // Buffer is already initialized, so just return
		}
		else {
			// Clear the buffer if we're not adding to existing content
			memset(bufferStart, 0, numSamples * sizeof(int32_t));
			phase += phaseIncrement * numSamples;
			return;
		}
	}

	// Apply minimum width check for narrow pulses
	if (pulseWidth > 0 && pulseWidth < minPwStep) {
		pulseWidth = minPwStep;
	}

	// Also check for values too close to maximum
	if (pulseWidth < 0xFFF && (0xFFF - pulseWidth) < minPwStep) {
		pulseWidth = 0xFFF - minPwStep;
	}

	// Process blocks of 4 samples at a time (vector processing)
	while (currentPos + 4 <= bufferEnd) {
		int32x4_t pulseVector;
		std::tie(pulseVector, phase) = generateSidPulseVector(phase, phaseIncrement, pulseWidth);

		if (applyAmplitude) {
			// Apply amplitude envelope
			int32x4_t ampVector = createAmplitudeVector(amplitude, amplitudeIncrement);
			pulseVector = vqdmulhq_s32(pulseVector, ampVector);
			amplitude += amplitudeIncrement * 4;

			// Add to existing buffer content
			int32x4_t existingData = vld1q_s32(currentPos);
			pulseVector = vaddq_s32(pulseVector, existingData);
		}

		// Store result
		vst1q_s32(currentPos, pulseVector);
		currentPos += 4;
	}

	// Process any remaining samples one by one
	while (currentPos < bufferEnd) {
		uint32_t phase_12 = (phase >> 20) & 0xFFF;
		int32_t value = (phase_12 < pulseWidth) ? 0xFFF : 0;

		// Calculate duty cycle for compensation
		float compensationFactor = SID_AMPLITUDE_BOOST;
		if (pulseWidth > 0 && pulseWidth < 0xFFF) {
			uint32_t effectiveOnTime;
			if (pulseWidth <= 0x800) {
				effectiveOnTime = pulseWidth;
			}
			else {
				effectiveOnTime = 0xFFF - pulseWidth;
			}

			if (effectiveOnTime < 0x800) {
				compensationFactor = SID_AMPLITUDE_BOOST * (float)0x800 / (float)effectiveOnTime;
				if (compensationFactor > SID_AMPLITUDE_BOOST * 16.0f) {
					compensationFactor = SID_AMPLITUDE_BOOST * 16.0f;
				}
			}
		}

		// Apply compensation to the output value
		if (value) {
			value = (int32_t)(value * compensationFactor / SID_AMPLITUDE_BOOST);
		}

		value <<= 16; // Scale to match the vector implementation

		if (applyAmplitude) {
			value = ((int64_t)value * amplitude) >> 31;
			amplitude += amplitudeIncrement;
			value += *currentPos;
		}

		*currentPos++ = value;
		phase += phaseIncrement;
	}
}

// Main render function for SID noise waveform
void renderSidNoise(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, uint32_t phaseIncrement,
                    uint32_t& phase, bool applyAmplitude, int32_t amplitudeIncrement) {

	int32_t numSamples = bufferEnd - bufferStart;
	int32_t* currentPos = bufferStart;

	// Store previous phase to detect bit transitions
	uint32_t prevPhase = phase;

	// Process blocks of 4 samples at a time (vector processing)
	while (currentPos + 4 <= bufferEnd) {
		int32x4_t noiseVector;
		std::tie(noiseVector, phase) = generateSidNoiseVector(prevPhase, phaseIncrement);
		prevPhase = phase;

		if (applyAmplitude) {
			// Apply amplitude envelope
			int32x4_t ampVector = createAmplitudeVector(amplitude, amplitudeIncrement);
			noiseVector = vqdmulhq_s32(noiseVector, ampVector);
			amplitude += amplitudeIncrement * 4;

			// Add to existing buffer content
			int32x4_t existingData = vld1q_s32(currentPos);
			noiseVector = vaddq_s32(noiseVector, existingData);
		}

		// Store result
		vst1q_s32(currentPos, noiseVector);
		currentPos += 4;
	}

	// Process any remaining samples
	while (currentPos < bufferEnd) {
		uint32_t nextPhase = phase + phaseIncrement;
		// Check if bit 19 transitions from 0 to 1
		if (!(phase & 0x080000) && (nextPhase & 0x080000)) {
			clockShiftRegister();
		}
		phase = nextPhase;

		int32_t value = getNoiseOutput() << 16;

		// BOOST: Apply consistent amplitude boost
		value *= SID_AMPLITUDE_BOOST;

		if (applyAmplitude) {
			value = ((int64_t)value * amplitude) >> 31;
			amplitude += amplitudeIncrement;
			value += *currentPos;
		}

		*currentPos++ = value;
	}
}

} // namespace sid
} // namespace dsp
} // namespace deluge
