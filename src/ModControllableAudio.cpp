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

#include <AudioEngine.h>
#include <ParamManager.h>
#include <sound.h>
#include "ModControllableAudio.h"
#include "storagemanager.h"
#include "numericdriver.h"
#include "storagemanager.h"
#include <string.h>
#include <SessionView.h>
#include "playbackhandler.h"
#include "uart.h"
#include "GeneralMemoryAllocator.h"
#include "View.h"
#include "TimelineCounter.h"
#include "song.h"
#include "ModelStack.h"
#include "MIDIDevice.h"
#include "InstrumentClip.h"
#include "NoteRow.h"
#include "ParamSet.h"

extern "C" {}

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

ModControllableAudio::ModControllableAudio() {

	// Mod FX
	modFXBuffer = NULL;
	modFXBufferWriteIndex = 0;

	// EQ
	withoutTrebleL = 0;
	bassOnlyL = 0;
	withoutTrebleR = 0;
	bassOnlyR = 0;

	// Stutter
	stutterer.sync = 7;
	stutterer.status = STUTTERER_STATUS_OFF;

	// Sample rate reduction
	sampleRateReductionOnLastTime = false;

	// Saturation
	clippingAmount = 0;
}

ModControllableAudio::~ModControllableAudio() {
	// delay.discardBuffers(); // No! The DelayBuffers will themselves destruct and do this

	// Free the mod fx memory
	if (modFXBuffer) {
		generalMemoryAllocator.dealloc(modFXBuffer);
	}
}

void ModControllableAudio::cloneFrom(ModControllableAudio* other) {
	lpfMode = other->lpfMode;
	clippingAmount = other->clippingAmount;
	modFXType = other->modFXType;
	bassFreq = other->bassFreq; // Eventually, these shouldn't be variables like this
	trebleFreq = other->trebleFreq;
	compressor.cloneFrom(&other->compressor);
	midiKnobArray.cloneFrom(&other->midiKnobArray); // Could fail if no RAM... not too big a concern
	delay.cloneFrom(&other->delay);
}

void ModControllableAudio::initParams(ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->params[PARAM_UNPATCHED_BASS].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[PARAM_UNPATCHED_TREBLE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[PARAM_UNPATCHED_BASS_FREQ].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[PARAM_UNPATCHED_TREBLE_FREQ].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[PARAM_UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[PARAM_UNPATCHED_MOD_FX_OFFSET].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION].setCurrentValueBasicForSetup(-2147483648);

	unpatchedParams->params[PARAM_UNPATCHED_BITCRUSHING].setCurrentValueBasicForSetup(-2147483648);

	unpatchedParams->params[PARAM_UNPATCHED_COMPRESSOR_SHAPE].setCurrentValueBasicForSetup(-601295438);
}

bool ModControllableAudio::hasBassAdjusted(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(PARAM_UNPATCHED_BASS) != 0);
}

bool ModControllableAudio::hasTrebleAdjusted(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(PARAM_UNPATCHED_TREBLE) != 0);
}

void ModControllableAudio::processFX(StereoSample* buffer, int numSamples, int modFXType, int32_t modFXRate,
                                     int32_t modFXDepth, DelayWorkingState* delayWorkingState, int32_t* postFXVolume,
                                     ParamManager* paramManager, int analogDelaySaturationAmount) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	StereoSample* bufferEnd = buffer + numSamples;

	// Mod FX -----------------------------------------------------------------------------------
	if (modFXType != MOD_FX_TYPE_NONE) {

		int modFXLFOWaveType;
		int modFXDelayOffset;
		int thisModFXDelayDepth;
		int32_t feedback;

		if (modFXType == MOD_FX_TYPE_FLANGER || modFXType == MOD_FX_TYPE_PHASER) {

			int32_t a = unpatchedParams->getValue(PARAM_UNPATCHED_MOD_FX_FEEDBACK) >> 1;
			int32_t b = 2147483647 - ((a + 1073741824) >> 2) * 3;
			int32_t c = multiply_32x32_rshift32(b, b);
			int32_t d = multiply_32x32_rshift32(b, c);

			feedback = 2147483648 - (d << 2);

			// Adjust volume for flanger feedback
			int32_t squared = multiply_32x32_rshift32(feedback, feedback) << 1;
			int32_t squared2 = multiply_32x32_rshift32(squared, squared) << 1;
			squared2 = multiply_32x32_rshift32(squared2, squared) << 1;
			squared2 = (multiply_32x32_rshift32(squared2, squared2) >> 4)
			           * 23; // 22 // Make bigger to have more of a volume cut happen at high resonance
			//*postFXVolume = (multiply_32x32_rshift32(*postFXVolume, 2147483647 - squared2) >> 1) * 3;
			*postFXVolume = multiply_32x32_rshift32(*postFXVolume, 2147483647 - squared2);
			if (modFXType == MOD_FX_TYPE_FLANGER) *postFXVolume <<= 1;
			// Though, this would be more ideally placed affecting volume before the flanger

			if (modFXType == MOD_FX_TYPE_FLANGER) {
				modFXDelayOffset = flangerOffset;
				thisModFXDelayDepth = flangerAmplitude;
				modFXLFOWaveType = OSC_TYPE_TRIANGLE;
			}
			else { // Phaser
				modFXLFOWaveType = OSC_TYPE_SINE;
			}
		}
		else if (modFXType == MOD_FX_TYPE_CHORUS) {
			modFXDelayOffset = multiply_32x32_rshift32(
			    modFXMaxDelay, (unpatchedParams->getValue(PARAM_UNPATCHED_MOD_FX_OFFSET) >> 1) + 1073741824);
			thisModFXDelayDepth = multiply_32x32_rshift32(modFXDelayOffset, modFXDepth) << 2;
			modFXLFOWaveType = OSC_TYPE_SINE;
			*postFXVolume = multiply_32x32_rshift32(*postFXVolume, 1518500250) << 1; // Divide by sqrt(2)
		}

		StereoSample* currentSample = buffer;
		do {

			int32_t lfoOutput = modFXLFO.render(1, modFXLFOWaveType, modFXRate);

			if (modFXType == MOD_FX_TYPE_PHASER) {

				// "1" is sorta represented by 1073741824 here
				int32_t _a1 =
				    1073741824
				    - multiply_32x32_rshift32_rounded((((uint32_t)lfoOutput + (uint32_t)2147483648) >> 1), modFXDepth);

				phaserMemory.l = currentSample->l + (multiply_32x32_rshift32_rounded(phaserMemory.l, feedback) << 1);
				phaserMemory.r = currentSample->r + (multiply_32x32_rshift32_rounded(phaserMemory.r, feedback) << 1);

				// Do the allpass filters
				for (int i = 0; i < PHASER_NUM_ALLPASS_FILTERS; i++) {
					StereoSample whatWasInput = phaserMemory;

					phaserMemory.l = (multiply_32x32_rshift32_rounded(phaserMemory.l, -_a1) << 2) + allpassMemory[i].l;
					allpassMemory[i].l = (multiply_32x32_rshift32_rounded(phaserMemory.l, _a1) << 2) + whatWasInput.l;

					phaserMemory.r = (multiply_32x32_rshift32_rounded(phaserMemory.r, -_a1) << 2) + allpassMemory[i].r;
					allpassMemory[i].r = (multiply_32x32_rshift32_rounded(phaserMemory.r, _a1) << 2) + whatWasInput.r;
				}

				currentSample->l += phaserMemory.l;
				currentSample->r += phaserMemory.r;
			}
			else {

				int32_t delayTime = multiply_32x32_rshift32(lfoOutput, thisModFXDelayDepth) + modFXDelayOffset;

				int32_t strength2 = (delayTime & 65535) << 15;
				int32_t strength1 = (65535 << 15) - strength2;
				int sample1Pos = modFXBufferWriteIndex - ((delayTime) >> 16);

				int32_t scaledValue1L =
				    multiply_32x32_rshift32_rounded(modFXBuffer[sample1Pos & modFXBufferIndexMask].l, strength1);
				int32_t scaledValue2L =
				    multiply_32x32_rshift32_rounded(modFXBuffer[(sample1Pos - 1) & modFXBufferIndexMask].l, strength2);
				int32_t modFXOutputL = scaledValue1L + scaledValue2L;

				int32_t scaledValue1R =
				    multiply_32x32_rshift32_rounded(modFXBuffer[sample1Pos & modFXBufferIndexMask].r, strength1);
				int32_t scaledValue2R =
				    multiply_32x32_rshift32_rounded(modFXBuffer[(sample1Pos - 1) & modFXBufferIndexMask].r, strength2);
				int32_t modFXOutputR = scaledValue1R + scaledValue2R;

				if (modFXType == MOD_FX_TYPE_FLANGER) {
					modFXOutputL = multiply_32x32_rshift32_rounded(modFXOutputL, feedback) << 2;
					modFXBuffer[modFXBufferWriteIndex].l = modFXOutputL + currentSample->l; // Feedback
					modFXOutputR = multiply_32x32_rshift32_rounded(modFXOutputR, feedback) << 2;
					modFXBuffer[modFXBufferWriteIndex].r = modFXOutputR + currentSample->r; // Feedback
				}

				else { // Chorus
					modFXOutputL <<= 1;
					modFXBuffer[modFXBufferWriteIndex].l = currentSample->l; // Feedback
					modFXOutputR <<= 1;
					modFXBuffer[modFXBufferWriteIndex].r = currentSample->r; // Feedback
				}

				currentSample->l += modFXOutputL;
				currentSample->r += modFXOutputR;
				modFXBufferWriteIndex = (modFXBufferWriteIndex + 1) & modFXBufferIndexMask;
			}
		} while (++currentSample != bufferEnd);
	}

	// EQ -------------------------------------------------------------------------------------
	bool thisDoBass = hasBassAdjusted(paramManager);
	bool thisDoTreble = hasTrebleAdjusted(paramManager);

	// Bass. No-change represented by 0. Off completely represented by -536870912
	int32_t positive = (unpatchedParams->getValue(PARAM_UNPATCHED_BASS) >> 1) + 1073741824;
	int32_t bassAmount = (multiply_32x32_rshift32_rounded(positive, positive) << 1) - 536870912;

	// Treble. No-change represented by 536870912
	positive = (unpatchedParams->getValue(PARAM_UNPATCHED_TREBLE) >> 1) + 1073741824;
	int32_t trebleAmount = multiply_32x32_rshift32_rounded(positive, positive) << 1;

	if (thisDoBass || thisDoTreble) {

		if (thisDoBass) {
			bassFreq = getExp(120000000, (unpatchedParams->getValue(PARAM_UNPATCHED_BASS_FREQ) >> 5) * 6);
		}

		if (thisDoTreble) {
			trebleFreq = getExp(700000000, (unpatchedParams->getValue(PARAM_UNPATCHED_TREBLE_FREQ) >> 5) * 6);
		}

		StereoSample* currentSample = buffer;
		do {
			doEQ(thisDoBass, thisDoTreble, &currentSample->l, &currentSample->r, bassAmount, trebleAmount);
		} while (++currentSample != bufferEnd);
	}

	// Delay ----------------------------------------------------------------------------------
	DelayBufferSetup delayPrimarySetup;
	DelayBufferSetup delaySecondarySetup;

	if (delayWorkingState->doDelay) {

		if (delayWorkingState->userDelayRate != delay.userRateLastTime) {
			delay.userRateLastTime = delayWorkingState->userDelayRate;
			delay.countCyclesWithoutChange = 0;
		}
		else delay.countCyclesWithoutChange += numSamples;

		// If just a single buffer is being used for reading and writing, we can consider making a 2nd buffer
		if (!delay.secondaryBuffer.isActive()) {

			// If resampling previously recorded as happening, or just about to be recorded as happening
			if (delay.primaryBuffer.isResampling
			    || delayWorkingState->userDelayRate != delay.primaryBuffer.nativeRate) {

				// If delay speed has settled for a split second...
				if (delay.countCyclesWithoutChange >= (44100 >> 5)) {
					//Uart::println("settling");
					initializeSecondaryDelayBuffer(delayWorkingState->userDelayRate, true);
				}

				// If spinning at double native rate, there's no real need to be using such a big buffer, so make a new (smaller) buffer at our new rate
				else if (delayWorkingState->userDelayRate >= (delay.primaryBuffer.nativeRate << 1)) {
					initializeSecondaryDelayBuffer(delayWorkingState->userDelayRate, false);
				}

				// If spinning below native rate, the quality's going to be suffering, so make a new buffer whose native rate is half our current rate (double the quality)
				else if (delayWorkingState->userDelayRate < delay.primaryBuffer.nativeRate) {
					initializeSecondaryDelayBuffer(delayWorkingState->userDelayRate >> 1, false);
				}
			}
		}

		// Figure some stuff out for the primary buffer
		delay.primaryBuffer.setupForRender(delayWorkingState->userDelayRate, &delayPrimarySetup);

		// Figure some stuff out for the secondary buffer - only if it's active
		if (delay.secondaryBuffer.isActive()) {
			delay.secondaryBuffer.setupForRender(delayWorkingState->userDelayRate, &delaySecondarySetup);
		}

		bool wrapped = false;

		int32_t* delayWorkingBuffer = spareRenderingBuffer[0];

		generalMemoryAllocator.checkStack("delay");

		int32_t* workingBufferEnd = delayWorkingBuffer + numSamples * 2;

		StereoSample* primaryBufferOldPos;
		uint32_t primaryBufferOldLongPos;
		uint8_t primaryBufferOldLastShortPos;

		// If nothing to read yet, easy
		if (!delay.primaryBuffer.isActive()) {
			memset(delayWorkingBuffer, 0, numSamples * 2 * sizeof(int32_t));
		}

		// Or...
		else {

			primaryBufferOldPos = delay.primaryBuffer.bufferCurrentPos;
			primaryBufferOldLongPos = delay.primaryBuffer.longPos;
			primaryBufferOldLastShortPos = delay.primaryBuffer.lastShortPos;

			// Native read
			if (!delay.primaryBuffer.isResampling) {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					wrapped = delay.primaryBuffer.clearAndMoveOn() || wrapped;
					workingBufferPos[0] = delay.primaryBuffer.bufferCurrentPos->l;
					workingBufferPos[1] = delay.primaryBuffer.bufferCurrentPos->r;

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			// Or, resampling read
			else {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					// Move forward, and clear buffer as we go
					delay.primaryBuffer.longPos += delayPrimarySetup.actualSpinRate;
					uint8_t newShortPos = delay.primaryBuffer.longPos >> 24;
					uint8_t shortPosDiff = newShortPos - delay.primaryBuffer.lastShortPos;
					delay.primaryBuffer.lastShortPos = newShortPos;

					while (shortPosDiff > 0) {
						wrapped = delay.primaryBuffer.clearAndMoveOn() || wrapped;
						shortPosDiff--;
					}

					int32_t primaryStrength2 = (delay.primaryBuffer.longPos >> 8) & 65535;
					int32_t primaryStrength1 = 65536 - primaryStrength2;

					StereoSample* nextPos = delay.primaryBuffer.bufferCurrentPos + 1;
					if (nextPos == delay.primaryBuffer.bufferEnd) nextPos = delay.primaryBuffer.bufferStart;
					int32_t fromDelay1L = delay.primaryBuffer.bufferCurrentPos->l;
					int32_t fromDelay1R = delay.primaryBuffer.bufferCurrentPos->r;
					int32_t fromDelay2L = nextPos->l;
					int32_t fromDelay2R = nextPos->r;

					workingBufferPos[0] = (multiply_32x32_rshift32(fromDelay1L, primaryStrength1 << 14)
					                       + multiply_32x32_rshift32(fromDelay2L, primaryStrength2 << 14))
					                      << 2;
					workingBufferPos[1] = (multiply_32x32_rshift32(fromDelay1R, primaryStrength1 << 14)
					                       + multiply_32x32_rshift32(fromDelay2R, primaryStrength2 << 14))
					                      << 2;

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}
		}

		if (delay.analog) {

			{
				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					delay.impulseResponseProcessor.process(workingBufferPos[0], workingBufferPos[1],
					                                       &workingBufferPos[0], &workingBufferPos[1]);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			{
				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					int32_t fromDelayL = workingBufferPos[0];
					int32_t fromDelayR = workingBufferPos[1];

					//delay.impulseResponseProcessor.process(fromDelayL, fromDelayR, &fromDelayL, &fromDelayR);

					// Reduce headroom, since this sounds ok with analog sim
					workingBufferPos[0] =
					    getTanHUnknown(multiply_32x32_rshift32(fromDelayL, delayWorkingState->delayFeedbackAmount),
					                   analogDelaySaturationAmount)
					    << 2;
					workingBufferPos[1] =
					    getTanHUnknown(multiply_32x32_rshift32(fromDelayR, delayWorkingState->delayFeedbackAmount),
					                   analogDelaySaturationAmount)
					    << 2;

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}
		}

		else {
			int32_t* workingBufferPos = delayWorkingBuffer;
			do {
				// Leave more headroom, because making it clip sounds bad with pure digital
				workingBufferPos[0] =
				    signed_saturate(
				        multiply_32x32_rshift32(workingBufferPos[0], delayWorkingState->delayFeedbackAmount), 32 - 3)
				    << 2;
				workingBufferPos[1] =
				    signed_saturate(
				        multiply_32x32_rshift32(workingBufferPos[1], delayWorkingState->delayFeedbackAmount), 32 - 3)
				    << 2;

				workingBufferPos += 2;
			} while (workingBufferPos != workingBufferEnd);
		}

		// HPF on delay output, to stop it "farting out". Corner frequency is somewhere around 40Hz after many repetitions
		{
			int32_t* workingBufferPos = delayWorkingBuffer;
			do {
				int32_t distanceToGoL = workingBufferPos[0] - delay.postLPFL;
				delay.postLPFL += distanceToGoL >> 11;
				workingBufferPos[0] -= delay.postLPFL;

				int32_t distanceToGoR = workingBufferPos[1] - delay.postLPFR;
				delay.postLPFR += distanceToGoR >> 11;
				workingBufferPos[1] -= delay.postLPFR;

				workingBufferPos += 2;
			} while (workingBufferPos != workingBufferEnd);
		}

		{
			StereoSample* currentSample = buffer;
			int32_t* workingBufferPos = delayWorkingBuffer;
			// Go through what we grabbed, sending it to the audio output buffer, and also preparing it to be fed back into the delay
			do {

				int32_t fromDelayL = workingBufferPos[0];
				int32_t fromDelayR = workingBufferPos[1];

				/*
				if (delay.analog) {
					// Reduce headroom, since this sounds ok with analog sim
					fromDelayL = getTanH(multiply_32x32_rshift32(fromDelayL, delayWorkingState->delayFeedbackAmount), 8) << 2;
					fromDelayR = getTanH(multiply_32x32_rshift32(fromDelayR, delayWorkingState->delayFeedbackAmount), 8) << 2;
				}

				else {
					fromDelayL = signed_saturate(multiply_32x32_rshift32(fromDelayL, delayWorkingState->delayFeedbackAmount), 32 - 3) << 2;
					fromDelayR = signed_saturate(multiply_32x32_rshift32(fromDelayR, delayWorkingState->delayFeedbackAmount), 32 - 3) << 2;
				}
				*/

				/*
				// HPF on delay output, to stop it "farting out". Corner frequency is somewhere around 40Hz after many repetitions
				int32_t distanceToGoL = fromDelayL - delay.postLPFL;
				delay.postLPFL += distanceToGoL >> 11;
				fromDelayL -= delay.postLPFL;

				int32_t distanceToGoR = fromDelayR - delay.postLPFR;
				delay.postLPFR += distanceToGoR >> 11;
				fromDelayR -= delay.postLPFR;
				*/

				// Feedback calculation, and combination with input
				if (delay.pingPong && AudioEngine::renderInStereo) {
					workingBufferPos[0] = fromDelayR;
					workingBufferPos[1] = ((currentSample->l + currentSample->r) >> 1) + fromDelayL;
				}
				else {
					workingBufferPos[0] = currentSample->l + fromDelayL;
					workingBufferPos[1] = currentSample->r + fromDelayR;
				}

				// Output
				currentSample->l += fromDelayL;
				currentSample->r += fromDelayR;

				currentSample++;
				workingBufferPos += 2;
			} while (workingBufferPos != workingBufferEnd);
		}

		// And actually feedback being applied back into the actual delay primary buffer...
		if (delay.primaryBuffer.isActive()) {

			// Native
			if (!delay.primaryBuffer.isResampling) {
				int32_t* workingBufferPos = delayWorkingBuffer;

				StereoSample* writePos = primaryBufferOldPos - delaySpaceBetweenReadAndWrite;
				if (writePos < delay.primaryBuffer.bufferStart) writePos += delay.primaryBuffer.sizeIncludingExtra;

				do {
					delay.primaryBuffer.writeNativeAndMoveOn(workingBufferPos[0], workingBufferPos[1], &writePos);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			// Resampling
			else {
				delay.primaryBuffer.bufferCurrentPos = primaryBufferOldPos;
				delay.primaryBuffer.longPos = primaryBufferOldLongPos;
				delay.primaryBuffer.lastShortPos = primaryBufferOldLastShortPos;

				int32_t* workingBufferPos = delayWorkingBuffer;

				do {

					// Move forward, and clear buffer as we go
					delay.primaryBuffer.longPos += delayPrimarySetup.actualSpinRate;
					uint8_t newShortPos = delay.primaryBuffer.longPos >> 24;
					uint8_t shortPosDiff = newShortPos - delay.primaryBuffer.lastShortPos;
					delay.primaryBuffer.lastShortPos = newShortPos;

					while (shortPosDiff > 0) {
						delay.primaryBuffer.moveOn();
						shortPosDiff--;
					}

					int32_t primaryStrength2 = (delay.primaryBuffer.longPos >> 8) & 65535;
					int32_t primaryStrength1 = 65536 - primaryStrength2;

					delay.primaryBuffer.writeResampled(workingBufferPos[0], workingBufferPos[1], primaryStrength1,
					                                   primaryStrength2, &delayPrimarySetup);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}
		}

		// And secondary buffer
		// If secondary buffer active, we need to tick it along and write to it too
		if (delay.secondaryBuffer.isActive()) {

			wrapped =
			    false; // We want to disregard whatever the primary buffer told us, and just use the secondary one now

			// Native
			if (!delay.secondaryBuffer.isResampling) {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					wrapped = delay.secondaryBuffer.clearAndMoveOn() || wrapped;
					delay.sizeLeftUntilBufferSwap--;

					// Write to secondary buffer
					delay.secondaryBuffer.writeNative(workingBufferPos[0], workingBufferPos[1]);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			// Resampled
			else {

				int32_t* workingBufferPos = delayWorkingBuffer;
				do {
					// Move forward, and clear buffer as we go
					delay.secondaryBuffer.longPos += delaySecondarySetup.actualSpinRate;
					uint8_t newShortPos = delay.secondaryBuffer.longPos >> 24;
					uint8_t shortPosDiff = newShortPos - delay.secondaryBuffer.lastShortPos;
					delay.secondaryBuffer.lastShortPos = newShortPos;

					while (shortPosDiff > 0) {
						wrapped = delay.secondaryBuffer.clearAndMoveOn() || wrapped;
						delay.sizeLeftUntilBufferSwap--;
						shortPosDiff--;
					}

					int32_t secondaryStrength2 = (delay.secondaryBuffer.longPos >> 8) & 65535;
					int32_t secondaryStrength1 = 65536 - secondaryStrength2;

					// Write to secondary buffer
					delay.secondaryBuffer.writeResampled(workingBufferPos[0], workingBufferPos[1], secondaryStrength1,
					                                     secondaryStrength2, &delaySecondarySetup);

					workingBufferPos += 2;
				} while (workingBufferPos != workingBufferEnd);
			}

			if (delay.sizeLeftUntilBufferSwap < 0) {
				delay.copySecondaryToPrimary();
			}
		}

		if (wrapped) {
			delay.hasWrapped();
		}
	}
}

void ModControllableAudio::processReverbSendAndVolume(StereoSample* buffer, int numSamples, int32_t* reverbBuffer,
                                                      int32_t postFXVolume, int32_t postReverbVolume,
                                                      int32_t reverbSendAmount, int32_t pan, bool doAmplitudeIncrement,
                                                      int32_t amplitudeIncrement) {

	StereoSample* bufferEnd = buffer + numSamples;

	int32_t reverbSendAmountAndPostFXVolume = multiply_32x32_rshift32(postFXVolume, reverbSendAmount) << 5;

	int32_t postFXAndReverbVolumeL, postFXAndReverbVolumeR, amplitudeIncrementL, amplitudeIncrementR;
	postFXAndReverbVolumeL = postFXAndReverbVolumeR = (multiply_32x32_rshift32(postReverbVolume, postFXVolume) << 5);

	// The amplitude increment applies to the post-FX volume. We want to have it just so that we can respond better to sidechain volume ducking, which is done through post-FX volume.
	if (doAmplitudeIncrement) {
		amplitudeIncrementL = amplitudeIncrementR = (multiply_32x32_rshift32(postFXVolume, amplitudeIncrement) << 5);
	}

	if (pan != 0 && AudioEngine::renderInStereo) {
		// Set up panning
		int32_t amplitudeL;
		int32_t amplitudeR;
		shouldDoPanning(pan, &amplitudeL, &amplitudeR);

		postFXAndReverbVolumeL = multiply_32x32_rshift32(postFXAndReverbVolumeL, amplitudeL) << 2;
		postFXAndReverbVolumeR = multiply_32x32_rshift32(postFXAndReverbVolumeR, amplitudeR) << 2;

		amplitudeIncrementL = multiply_32x32_rshift32(amplitudeIncrementL, amplitudeL) << 2;
		amplitudeIncrementR = multiply_32x32_rshift32(amplitudeIncrementR, amplitudeR) << 2;
	}

	StereoSample* inputSample = buffer;

	do {
		StereoSample processingSample = *inputSample;

		// Send to reverb
		if (reverbSendAmount != 0) {
			*(reverbBuffer++) +=
			    multiply_32x32_rshift32(processingSample.l + processingSample.r, reverbSendAmountAndPostFXVolume) << 1;
		}

		if (doAmplitudeIncrement) {
			postFXAndReverbVolumeL += amplitudeIncrementL;
			postFXAndReverbVolumeR += amplitudeIncrementR;
		}

		// Apply post-fx and post-reverb-send volume
		inputSample->l = multiply_32x32_rshift32(processingSample.l, postFXAndReverbVolumeL) << 5;
		inputSample->r = multiply_32x32_rshift32(processingSample.r, postFXAndReverbVolumeR) << 5;

	} while (++inputSample != bufferEnd);

	// We've generated some sound. If reverb is happening, make note
	if (reverbSendAmount != 0) AudioEngine::timeThereWasLastSomeReverb = AudioEngine::audioSampleTimer;
}

bool ModControllableAudio::isBitcrushingEnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(PARAM_UNPATCHED_BITCRUSHING) >= -2113929216);
}

bool ModControllableAudio::isSRREnabled(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	return (unpatchedParams->getValue(PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION) != -2147483648);
}

void ModControllableAudio::processSRRAndBitcrushing(StereoSample* buffer, int numSamples, int32_t* postFXVolume,
                                                    ParamManager* paramManager) {
	StereoSample const* const bufferEnd = buffer + numSamples;

	uint32_t bitCrushMaskForSRR = 0xFFFFFFFF;

	bool srrEnabled = isSRREnabled(paramManager);

	// Bitcrushing ------------------------------------------------------------------------------
	if (isBitcrushingEnabled(paramManager)) {
		uint32_t positivePreset =
		    (paramManager->getUnpatchedParamSet()->getValue(PARAM_UNPATCHED_BITCRUSHING) + 2147483648) >> 29;
		if (positivePreset > 4) *postFXVolume >>= (positivePreset - 4);

		// If not also doing SRR
		if (!srrEnabled) {
			uint32_t mask = 0xFFFFFFFF << (19 + (positivePreset));
			StereoSample* __restrict__ currentSample = buffer;
			do {
				currentSample->l &= mask;
				currentSample->r &= mask;
			} while (++currentSample != bufferEnd);
		}

		else bitCrushMaskForSRR = 0xFFFFFFFF << (18 + (positivePreset));
	}

	// Sample rate reduction -------------------------------------------------------------------------------------
	if (srrEnabled) {

		// Set up for first time
		if (!sampleRateReductionOnLastTime) {
			sampleRateReductionOnLastTime = true;
			lastSample.l = lastSample.r = 0;
			grabbedSample.l = grabbedSample.r = 0;
			lowSampleRatePos = 0;
		}

		// This function, slightly unusually, uses 22 bits to represent "1". That's 4194304. I tried using 24, but stuff started clipping off where I needed it if sample rate too low

		uint32_t positivePreset =
		    paramManager->getUnpatchedParamSet()->getValue(PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION) + 2147483648;
		int32_t lowSampleRateIncrement = getExp(4194304, (positivePreset >> 3));
		int32_t highSampleRateIncrement = ((uint32_t)0xFFFFFFFF / (lowSampleRateIncrement >> 6)) << 6;
		//int32_t highSampleRateIncrement = getExp(4194304, -(int32_t)(positivePreset >> 3)); // This would work too

		StereoSample* currentSample = buffer;
		do {

			// Convert down.
			// If time to "grab" another sample for down-conversion...
			if (lowSampleRatePos < 4194304) {
				int32_t strength2 = lowSampleRatePos;
				int32_t strength1 = 4194303 - strength2;

				lastGrabbedSample = grabbedSample; // What was current is now last
				grabbedSample.l = multiply_32x32_rshift32_rounded(lastSample.l, strength1 << 9)
				                  + multiply_32x32_rshift32_rounded(currentSample->l, strength2 << 9);
				grabbedSample.r = multiply_32x32_rshift32_rounded(lastSample.r, strength1 << 9)
				                  + multiply_32x32_rshift32_rounded(currentSample->r, strength2 << 9);
				grabbedSample.l &= bitCrushMaskForSRR;
				grabbedSample.r &= bitCrushMaskForSRR;

				// Set the "time" at which we want to "grab" our next sample for down-conversion.
				lowSampleRatePos += lowSampleRateIncrement;

				// "Re-sync" the up-conversion spinner.
				// I previously had it using strength2 instead of "lowSampleRatePos & 16777215", but that just works better. Ah, writing the massive explanation would take ages.
				highSampleRatePos =
				    multiply_32x32_rshift32_rounded(lowSampleRatePos & 4194303, highSampleRateIncrement << 8) << 2;
			}
			lowSampleRatePos -= 4194304; // We're one step closer to grabbing our next sample for down-conversion
			lastSample = *currentSample;

			// Convert up
			int32_t strength2 =
			    getMin(highSampleRatePos,
			           (uint32_t)4194303); // Would only overshoot if we raised the sample rate during playback
			int32_t strength1 = 4194303 - strength2;
			currentSample->l = (multiply_32x32_rshift32_rounded(lastGrabbedSample.l, strength1 << 9)
			                    + multiply_32x32_rshift32_rounded(grabbedSample.l, strength2 << 9))
			                   << 2;
			currentSample->r = (multiply_32x32_rshift32_rounded(lastGrabbedSample.r, strength1 << 9)
			                    + multiply_32x32_rshift32_rounded(grabbedSample.r, strength2 << 9))
			                   << 2;

			highSampleRatePos += highSampleRateIncrement;
		} while (++currentSample != bufferEnd);
	}
	else sampleRateReductionOnLastTime = false;
}

void ModControllableAudio::processStutter(StereoSample* buffer, int numSamples, ParamManager* paramManager) {
	if (stutterer.status == STUTTERER_STATUS_OFF) return;

	StereoSample* bufferEnd = buffer + numSamples;

	StereoSample* thisSample = buffer;

	DelayBufferSetup delayBufferSetup;
	int32_t rate = getStutterRate(paramManager);

	stutterer.buffer.setupForRender(rate, &delayBufferSetup);

	if (stutterer.status == STUTTERER_STATUS_RECORDING) {

		do {

			int32_t strength1;
			int32_t strength2;

			// First, tick it along, as if we were reading from it

			// Non-resampling tick-along
			if (!stutterer.buffer.isResampling) {
				stutterer.buffer.clearAndMoveOn();
				stutterer.sizeLeftUntilRecordFinished--;

				//stutterer.buffer.writeNative(thisSample->l, thisSample->r);
			}

			// Or, resampling tick-along
			else {

				// Move forward, and clear buffer as we go
				stutterer.buffer.longPos += delayBufferSetup.actualSpinRate;
				uint8_t newShortPos = stutterer.buffer.longPos >> 24;
				uint8_t shortPosDiff = newShortPos - stutterer.buffer.lastShortPos;
				stutterer.buffer.lastShortPos = newShortPos;

				while (shortPosDiff > 0) {
					stutterer.buffer.clearAndMoveOn();
					stutterer.sizeLeftUntilRecordFinished--;
					shortPosDiff--;
				}

				strength2 = (stutterer.buffer.longPos >> 8) & 65535;
				strength1 = 65536 - strength2;

				//stutterer.buffer.writeResampled(thisSample->l, thisSample->r, strength1, strength2, &delayBufferSetup);
			}

			stutterer.buffer.write(thisSample->l, thisSample->r, strength1, strength2, &delayBufferSetup);

		} while (++thisSample != bufferEnd);

		// If we've finished recording, remember to play next time instead
		if (stutterer.sizeLeftUntilRecordFinished < 0) {
			stutterer.status = STUTTERER_STATUS_PLAYING;
		}
	}

	else { // PLAYING

		do {
			int32_t strength1;
			int32_t strength2;

			// Non-resampling read
			if (!stutterer.buffer.isResampling) {
				stutterer.buffer.moveOn();
				thisSample->l = stutterer.buffer.bufferCurrentPos->l;
				thisSample->r = stutterer.buffer.bufferCurrentPos->r;
			}

			// Or, resampling read
			else {

				// Move forward, and clear buffer as we go
				stutterer.buffer.longPos += delayBufferSetup.actualSpinRate;
				uint8_t newShortPos = stutterer.buffer.longPos >> 24;
				uint8_t shortPosDiff = newShortPos - stutterer.buffer.lastShortPos;
				stutterer.buffer.lastShortPos = newShortPos;

				while (shortPosDiff > 0) {
					stutterer.buffer.moveOn();
					shortPosDiff--;
				}

				strength2 = (stutterer.buffer.longPos >> 8) & 65535;
				strength1 = 65536 - strength2;

				StereoSample* nextPos = stutterer.buffer.bufferCurrentPos + 1;
				if (nextPos == stutterer.buffer.bufferEnd) nextPos = stutterer.buffer.bufferStart;
				int32_t fromDelay1L = stutterer.buffer.bufferCurrentPos->l;
				int32_t fromDelay1R = stutterer.buffer.bufferCurrentPos->r;
				int32_t fromDelay2L = nextPos->l;
				int32_t fromDelay2R = nextPos->r;

				thisSample->l = (multiply_32x32_rshift32(fromDelay1L, strength1 << 14)
				                 + multiply_32x32_rshift32(fromDelay2L, strength2 << 14))
				                << 2;
				thisSample->r = (multiply_32x32_rshift32(fromDelay1R, strength1 << 14)
				                 + multiply_32x32_rshift32(fromDelay2R, strength2 << 14))
				                << 2;
			}
		} while (++thisSample != bufferEnd);
	}
}

int32_t ModControllableAudio::getStutterRate(ParamManager* paramManager) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	int32_t rate =
	    getFinalParameterValueExp(paramNeutralValues[PARAM_GLOBAL_DELAY_RATE],
	                              cableToExpParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_STUTTER_RATE)));

	if (stutterer.sync != 0) {
		rate = multiply_32x32_rshift32(rate, playbackHandler.getTimePerInternalTickInverse());

		// Limit to the biggest number we can store...
		int lShiftAmount =
		    stutterer.sync + 6
		    - (currentSong->insideWorldTickMagnitude + currentSong->insideWorldTickMagnitudeOffsetFromBPM);
		int32_t limit = 2147483647 >> lShiftAmount;
		rate = getMin(rate, limit);
		rate <<= lShiftAmount;
	}
	return rate;
}

void ModControllableAudio::initializeSecondaryDelayBuffer(int32_t newNativeRate,
                                                          bool makeNativeRatePreciseRelativeToOtherBuffer) {
	uint8_t result = delay.secondaryBuffer.init(newNativeRate, delay.primaryBuffer.size);
	if (result == NO_ERROR) {
		//Uart::print("new buffer, size: ");
		//Uart::println(delay.secondaryBuffer.size);

		// 2 different options here for different scenarios. I can't very clearly remember how to describe the difference
		if (makeNativeRatePreciseRelativeToOtherBuffer) {
			//Uart::println("making precise");
			delay.primaryBuffer.makeNativeRatePreciseRelativeToOtherBuffer(&delay.secondaryBuffer);
		}
		else {
			delay.primaryBuffer.makeNativeRatePrecise();
			delay.secondaryBuffer.makeNativeRatePrecise();
		}
		delay.sizeLeftUntilBufferSwap = delay.secondaryBuffer.size + 5;
	}
}

inline void ModControllableAudio::doEQ(bool doBass, bool doTreble, int32_t* inputL, int32_t* inputR, int32_t bassAmount,
                                       int32_t trebleAmount) {
	int32_t trebleOnlyL;
	int32_t trebleOnlyR;

	if (doTreble) {
		int32_t distanceToGoL = *inputL - withoutTrebleL;
		int32_t distanceToGoR = *inputR - withoutTrebleR;
		withoutTrebleL += multiply_32x32_rshift32(distanceToGoL, trebleFreq) << 1;
		withoutTrebleR += multiply_32x32_rshift32(distanceToGoR, trebleFreq) << 1;
		trebleOnlyL = *inputL - withoutTrebleL;
		trebleOnlyR = *inputR - withoutTrebleR;
		*inputL = withoutTrebleL; // Input now has had the treble removed. Or is this bad?
		*inputR = withoutTrebleR; // Input now has had the treble removed. Or is this bad?
	}

	if (doBass) {
		int32_t distanceToGoL = *inputL - bassOnlyL;
		int32_t distanceToGoR = *inputR - bassOnlyR;
		bassOnlyL += multiply_32x32_rshift32(distanceToGoL, bassFreq); // 33554432
		bassOnlyR += multiply_32x32_rshift32(distanceToGoR, bassFreq); // 33554432
	}

	if (doTreble) {
		*inputL += (multiply_32x32_rshift32(trebleOnlyL, trebleAmount) << 3);
		*inputR += (multiply_32x32_rshift32(trebleOnlyR, trebleAmount) << 3);
	}
	if (doBass) {
		*inputL += (multiply_32x32_rshift32(bassOnlyL, bassAmount) << 3);
		*inputR += (multiply_32x32_rshift32(bassOnlyR, bassAmount) << 3);
	}
}

void ModControllableAudio::writeAttributesToFile() {
	storageManager.writeAttribute("lpfMode", (char*)lpfTypeToString(lpfMode));
	storageManager.writeAttribute("modFXType", (char*)fxTypeToString(modFXType));
	if (clippingAmount) storageManager.writeAttribute("clippingAmount", clippingAmount);
}

void ModControllableAudio::writeTagsToFile() {
	// Delay
	storageManager.writeOpeningTagBeginning("delay");
	storageManager.writeAttribute("pingPong", delay.pingPong);
	storageManager.writeAttribute("analog", delay.analog);
	storageManager.writeSyncTypeToFile(currentSong, "syncType", delay.syncType);
	storageManager.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", delay.syncLevel);
	storageManager.closeTag();

	// Sidechain compressor
	storageManager.writeOpeningTagBeginning("compressor");
	storageManager.writeSyncTypeToFile(currentSong, "syncType", compressor.syncType);
	storageManager.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", compressor.syncLevel);
	storageManager.writeAttribute("attack", compressor.attack);
	storageManager.writeAttribute("release", compressor.release);
	storageManager.closeTag();

	// MIDI knobs
	if (midiKnobArray.getNumElements()) {
		storageManager.writeOpeningTag("midiKnobs");
		for (int k = 0; k < midiKnobArray.getNumElements(); k++) {
			MIDIKnob* knob = midiKnobArray.getElement(k);
			storageManager.writeOpeningTagBeginning("midiKnob");
			knob->midiInput.writeAttributesToFile(
			    MIDI_MESSAGE_CC); // Writes channel and CC, but not device - we do that below.
			storageManager.writeAttribute("relative", knob->relative);
			storageManager.writeAttribute("controlsParam", paramToString(knob->paramDescriptor.getJustTheParam()));
			if (!knob->paramDescriptor.isJustAParam()) { // TODO: this only applies to Sounds
				storageManager.writeAttribute("patchAmountFromSource",
				                              sourceToString(knob->paramDescriptor.getTopLevelSource()));

				if (knob->paramDescriptor.hasSecondSource()) {
					storageManager.writeAttribute("patchAmountFromSecondSource",
					                              sourceToString(knob->paramDescriptor.getSecondSourceFromTop()));
				}
			}

			// Because we manually called LearnedMIDI::writeAttributesToFile() above, we have to give the MIDIDevice its own tag, cos that can't be written as just an attribute.
			if (knob->midiInput.device) {
				storageManager.writeOpeningTagEnd();
				knob->midiInput.device->writeReferenceToFile();
				storageManager.writeClosingTag("midiKnob");
			}
			else {
				storageManager.closeTag();
			}
		}
		storageManager.writeClosingTag("midiKnobs");
	}
}

void ModControllableAudio::writeParamAttributesToFile(ParamManager* paramManager, bool writeAutomation,
                                                      int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute("stutterRate", PARAM_UNPATCHED_STUTTER_RATE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("sampleRateReduction", PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION,
	                                       writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("bitCrush", PARAM_UNPATCHED_BITCRUSHING, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("modFXOffset", PARAM_UNPATCHED_MOD_FX_OFFSET, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("modFXFeedback", PARAM_UNPATCHED_MOD_FX_FEEDBACK, writeAutomation, false,
	                                       valuesForOverride);
}

void ModControllableAudio::writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
                                                int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	storageManager.writeOpeningTagBeginning("equalizer");
	unpatchedParams->writeParamAsAttribute("bass", PARAM_UNPATCHED_BASS, writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("treble", PARAM_UNPATCHED_TREBLE, writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("bassFrequency", PARAM_UNPATCHED_BASS_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("trebleFrequency", PARAM_UNPATCHED_TREBLE_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	storageManager.closeTag();
}

bool ModControllableAudio::readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                                int32_t readAutomationUpToPos) {
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "equalizer")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "bass")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_BASS, readAutomationUpToPos);
				storageManager.exitTag("bass");
			}
			else if (!strcmp(tagName, "treble")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_TREBLE, readAutomationUpToPos);
				storageManager.exitTag("treble");
			}
			else if (!strcmp(tagName, "bassFrequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_BASS_FREQ, readAutomationUpToPos);
				storageManager.exitTag("bassFrequency");
			}
			else if (!strcmp(tagName, "trebleFrequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_TREBLE_FREQ, readAutomationUpToPos);
				storageManager.exitTag("trebleFrequency");
			}
		}
		storageManager.exitTag("equalizer");
	}

	else if (!strcmp(tagName, "stutterRate")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_STUTTER_RATE, readAutomationUpToPos);
		storageManager.exitTag("stutterRate");
	}

	else if (!strcmp(tagName, "sampleRateReduction")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION,
		                           readAutomationUpToPos);
		storageManager.exitTag("sampleRateReduction");
	}

	else if (!strcmp(tagName, "bitCrush")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_BITCRUSHING, readAutomationUpToPos);
		storageManager.exitTag("bitCrush");
	}

	else if (!strcmp(tagName, "modFXOffset")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_MOD_FX_OFFSET, readAutomationUpToPos);
		storageManager.exitTag("modFXOffset");
	}

	else if (!strcmp(tagName, "modFXFeedback")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_MOD_FX_FEEDBACK, readAutomationUpToPos);
		storageManager.exitTag("modFXFeedback");
	}

	else return false;

	return true;
}

// paramManager is optional
int ModControllableAudio::readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                          int32_t readAutomationUpToPos, Song* song) {

	// All of this is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
	//if (paramManager && ModControllableAudio::readParamTagFromFile(tagName, paramManager, readAutomation)) {}

	int p;

	//else
	if (!strcmp(tagName, "lpfMode")) {
		lpfMode = stringToLPFType(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("lpfMode");
	}

	else if (!strcmp(tagName, "clippingAmount")) {
		clippingAmount = storageManager.readTagOrAttributeValueInt();
		storageManager.exitTag("clippingAmount");
	}

	else if (!strcmp(tagName, "delay")) {
		delay.sync = 0; // Default, in case this info wasn't included... although it always is since July 2015

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {

			// These first two ensure compatibility with old files (pre late 2016 I think?)
			if (!strcmp(tagName, "feedback")) {
				p = PARAM_GLOBAL_DELAY_FEEDBACK;
doReadPatchedParam:
				if (paramManager) {
					if (!paramManager->containsAnyMainParamCollections()) {
						int error = Sound::createParamManagerForLoading(paramManager);
						if (error) return error;
					}
					ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();
					PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;
					patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_DELAY_FEEDBACK, readAutomationUpToPos);
				}
				storageManager.exitTag();
			}
			else if (!strcmp(tagName, "rate")) {
				p = PARAM_GLOBAL_DELAY_RATE;
				goto doReadPatchedParam;
			}

			else if (!strcmp(tagName, "pingPong")) {
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				delay.pingPong = getMax((int32_t)0, getMin((int32_t)1, contents));
				storageManager.exitTag("pingPong");
			}
			else if (!strcmp(tagName, "analog")) {
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				delay.analog = getMax((int32_t)0, getMin((int32_t)1, contents));
				storageManager.exitTag("analog");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				delay.sync = storageManager.readAbsoluteSyncLevelFromFile(song);
				storageManager.exitTag("syncLevel");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("delay");
	}

	else if (!strcmp(tagName, "compressor")) { // Remember, Song doesn't use this

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				compressor.attack = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("attack");
			}
			else if (!strcmp(tagName, "release")) {
				compressor.release = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("release");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				compressor.sync = storageManager.readAbsoluteSyncLevelFromFile(song);
				storageManager.exitTag("syncLevel");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("compressor");
	}

	else if (!strcmp(tagName, "midiKnobs")) {

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "midiKnob")) {

				MIDIDevice* device = NULL;
				uint8_t channel;
				uint8_t ccNumber;
				bool relative;
				uint8_t p = PARAM_NONE;
				uint8_t s = 255;
				uint8_t s2 = 255;

				while (*(tagName = storageManager.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "device")) {
						device = MIDIDeviceManager::readDeviceReferenceFromFile();
					}
					else if (!strcmp(tagName, "channel")) {
						channel = storageManager.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "ccNumber")) {
						ccNumber = storageManager.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "relative")) {
						relative = storageManager.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "controlsParam")) {
						p = stringToParam(storageManager.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(storageManager.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(storageManager.readTagOrAttributeValue());
					}
					storageManager.exitTag();
				}

				if (p != PARAM_NONE && p != PARAM_PLACEHOLDER_RANGE) {
					MIDIKnob* newKnob = midiKnobArray.insertKnobAtEnd();
					if (newKnob) {
						newKnob->midiInput.device = device;
						newKnob->midiInput.channelOrZone = channel;
						newKnob->midiInput.noteOrCC = ccNumber;
						newKnob->relative = relative;

						if (s == 255) {
							newKnob->paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == 255) {
							newKnob->paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							newKnob->paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}
					}
				}
			}
			storageManager.exitTag();
		}
		storageManager.exitTag("midiKnobs");
	}

	else return RESULT_TAG_UNUSED;

	return NO_ERROR;
}

ModelStackWithAutoParam* ModControllableAudio::getParamFromMIDIKnob(MIDIKnob* knob,
                                                                    ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	int paramId = knob->paramDescriptor.getJustTheParam() - PARAM_UNPATCHED_SECTION;

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(paramCollection, summary, paramId);

	ModelStackWithAutoParam* modelStackWithAutoParam = paramCollection->getAutoParamFromId(modelStackWithParamId);

	return modelStackWithAutoParam;
}

ModelStackWithThreeMainThings* ModControllableAudio::addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
                                                                             int noteRowIndex) {
	NoteRow* noteRow = NULL;
	int noteRowId = 0;
	ParamManager* paramManager;

	if (noteRowIndex != -1) {
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
#if ALPHA_OR_BETA_VERSION
		if (noteRowIndex >= clip->noteRows.getNumElements()) numericDriver.freezeWithError("E406");
#endif
		noteRow = clip->noteRows.getElement(noteRowIndex);
		noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
		paramManager = &noteRow->paramManager;
	}
	else {
		if (modelStack->timelineCounterIsSet()) {
			paramManager = &modelStack->getTimelineCounter()->paramManager;
		}
		else {
			paramManager = modelStack->song->getBackedUpParamManagerPreferablyWithClip(
			    this,
			    NULL); // Could be NULL if a NonAudioInstrument - those don't back up any paramManagers (when they even have them).
		}
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addNoteRow(noteRowId, noteRow)->addOtherTwoThings(this, paramManager);
	return modelStackWithThreeMainThings;
}

bool ModControllableAudio::offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber,
                                                          uint8_t value, ModelStackWithTimelineCounter* modelStack,
                                                          int noteRowIndex) {

	bool messageUsed = false;

	// For each MIDI knob...
	for (int k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);

		// If this is the knob...
		if (knob->midiInput.equalsNoteOrCC(fromDevice, channel, ccNumber)) {

			messageUsed = true;

			// See if this message is evidence that the knob is not "relative"
			if (value >= 16 && value < 112) knob->relative = false;

			// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a region.
			int32_t modPos = 0;
			int32_t modLength = 0;

			if (modelStack->timelineCounterIsSet()) {
				if (view.modLength
				    && modelStack->getTimelineCounter()
				           == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
				}

				modelStack->getTimelineCounter()->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam->autoParam) {
				int newKnobPos;

				if (knob->relative) {
					int offset = value;
					if (offset >= 64) offset -= 128;

					int32_t previousValue =
					    modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
					int knobPos =
					    modelStackWithParam->paramCollection->paramValueToKnobPos(previousValue, modelStackWithParam);
					int lowerLimit = getMin(-64, knobPos);
					newKnobPos = knobPos + offset;
					newKnobPos = getMax(newKnobPos, lowerLimit);
					newKnobPos = getMin(newKnobPos, 64);
					if (newKnobPos == knobPos) continue;
				}
				else {
					newKnobPos = 64;
					if (value < 127) newKnobPos = (int)value - 64;
				}

				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);
			}
		}
	}
	return messageUsed;
}

// Returns true if the message was used by something
bool ModControllableAudio::offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1,
                                                                 uint8_t data2,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int noteRowIndex) {

	bool messageUsed = false;

	// For each MIDI knob...
	for (int k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);

		// If this is the knob...
		if (knob->midiInput.equalsNoteOrCC(fromDevice, channel,
		                                   128)) { // I've got 128 representing pitch bend here... why again?

			messageUsed = true;

			// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a region.
			int32_t modPos = 0;
			int32_t modLength = 0;

			if (modelStack->timelineCounterIsSet()) {
				if (view.modLength
				    && modelStack->getTimelineCounter()
				           == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
				}

				modelStack->getTimelineCounter()->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam->autoParam) {

				uint32_t value14 = (uint32_t)data1 | ((uint32_t)data2 << 7);

				int32_t newValue = (value14 << 18) - 2147483648;

				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);
				return true;
			}
		}
	}
	return messageUsed;
}

void ModControllableAudio::beginStutter(ParamManagerForTimeline* paramManager) {
	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	    && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW
	    && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)
		return;

	// You'd think I should apply "false" here, to make it not add extra space to the buffer, but somehow this seems to sound as good if not better (in terms of ticking / crackling)...
	bool error = stutterer.buffer.init(getStutterRate(paramManager), 0, true);
	if (error == NO_ERROR) {
		stutterer.status = STUTTERER_STATUS_RECORDING;
		stutterer.sizeLeftUntilRecordFinished = stutterer.buffer.size;
		enterUIMode(UI_MODE_STUTTERING);
	}
}

// paramManager is optional - if you don't send it, it won't change the stutter rate
void ModControllableAudio::endStutter(ParamManagerForTimeline* paramManager) {
	stutterer.buffer.discard();
	stutterer.status = STUTTERER_STATUS_OFF;
	exitUIMode(UI_MODE_STUTTERING);

	if (paramManager) {

		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

		// Normally we shouldn't call this directly, but it's ok because automation isn't allowed for stutter anyway
		if (unpatchedParams->getValue(PARAM_UNPATCHED_STUTTER_RATE) < 0) {
			unpatchedParams->params[PARAM_UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);
			view.notifyParamAutomationOccurred(paramManager);
		}
	}
}

void ModControllableAudio::switchDelayPingPong() {
	delay.pingPong = !delay.pingPong;

	char* displayText;
	switch (delay.pingPong) {
	case 0:
		displayText = "Normal delay";
		break;

	default:
		displayText = "Ping-pong delay";
		break;
	}
	numericDriver.displayPopup(displayText);
}

void ModControllableAudio::switchDelayAnalog() {
	delay.analog = !delay.analog;

	char const* displayText;
	switch (delay.analog) {
	case 0:
		displayText = "Digital delay";
		break;

	default:
		displayText = HAVE_OLED ? "Analog delay" : "ANA";
		break;
	}
	numericDriver.displayPopup(displayText);
}

void ModControllableAudio::switchLPFMode() {
	lpfMode++;
	if (lpfMode >= NUM_LPF_MODES) lpfMode = 0;

	char const* displayText;
	switch (lpfMode) {
	case LPF_MODE_12DB:
		displayText = "12DB LPF";
		break;

	case LPF_MODE_TRANSISTOR_24DB:
		displayText = "24DB LPF";
		break;

	case LPF_MODE_TRANSISTOR_24DB_DRIVE:
		displayText = "DRIVE LPF";
		break;

	case LPF_MODE_DIODE:
		displayText = "DIODE LPF";
		break;
	}
	numericDriver.displayPopup(displayText);
}

// This can get called either for hibernation, or because drum now has no active noteRow
void ModControllableAudio::wontBeRenderedForAWhile() {
	delay.discardBuffers();
	endStutter(NULL);
}

void ModControllableAudio::clearModFXMemory() {
	if (modFXType == MOD_FX_TYPE_FLANGER || modFXType == MOD_FX_TYPE_CHORUS) {
		if (modFXBuffer) {
			memset(modFXBuffer, 0, modFXBufferSize * sizeof(StereoSample));
		}
	}
	else if (modFXType == MOD_FX_TYPE_PHASER) {
		memset(allpassMemory, 0, sizeof(allpassMemory));
		memset(&phaserMemory, 0, sizeof(phaserMemory));
	}
}

bool ModControllableAudio::setModFXType(int newType) {

	// For us ModControllableAudios, this is really simple. Memory gets allocated in GlobalEffectable::processFXForGlobalEffectable().
	// This function is overridden in Sound
	modFXType = newType;
	return true;
}

// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool ModControllableAudio::learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob,
                                     uint8_t modKnobMode, uint8_t midiChannel, Song* song) {

	bool overwroteExistingKnob = false;

	// If a mod knob
	if (midiChannel >= 16) {
		return false;

		// TODO: make function not virtual after this changed

		/*
		// If that knob was patched to something else...
		overwroteExistingKnob = (modKnobs[modKnobMode][whichKnob].s != s || modKnobs[modKnobMode][whichKnob].p != p);

		modKnobs[modKnobMode][whichKnob].s = s;
		modKnobs[modKnobMode][whichKnob].p = p;
		*/
	}

	// If a MIDI knob
	else {

		MIDIKnob* knob;

		// Was this MIDI knob already set to control this thing?
		for (int k = 0; k < midiKnobArray.getNumElements(); k++) {
			knob = midiKnobArray.getElement(k);
			if (knob->midiInput.equalsNoteOrCC(fromDevice, midiChannel, whichKnob)
			    && paramDescriptor == knob->paramDescriptor) {
				//overwroteExistingKnob = (midiKnobs[k].s != s || midiKnobs[k].p != p);
				goto midiKnobFound;
			}
		}

		// Or if we're here, it doesn't already exist, so find an unused MIDIKnob
		knob = midiKnobArray.insertKnobAtEnd();
		if (!knob) return false;

midiKnobFound:
		knob->midiInput.noteOrCC = whichKnob;
		knob->midiInput.channelOrZone = midiChannel;
		knob->midiInput.device = fromDevice;
		knob->paramDescriptor = paramDescriptor;
		knob->relative = (whichKnob != 128); // Guess that it's relative, unless this is a pitch-bend "knob"
	}

	if (overwroteExistingKnob) ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);

	return true;
}

// Returns whether anything was found to unlearn
bool ModControllableAudio::unlearnKnobs(ParamDescriptor paramDescriptor, Song* song) {
	bool anythingFound = false;

	// I've deactivated the unlearning of mod knobs, mainly because, if you want to unlearn a MIDI knob, you might not want to also deactivate a mod knob to the same param at the same time

	for (int k = 0; k < midiKnobArray.getNumElements();) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		if (knob->paramDescriptor == paramDescriptor) {
			anythingFound = true;
			midiKnobArray.deleteAtIndex(k);
		}
		else k++;
	}

	if (anythingFound) ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);

	return anythingFound;
}

char const* ModControllableAudio::paramToString(uint8_t param) {

	switch (param) {

		// Unpatched params
	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_STUTTER_RATE:
		return "stutterRate";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_BASS:
		return "bass";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_TREBLE:
		return "treble";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_BASS_FREQ:
		return "bassFreq";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_TREBLE_FREQ:
		return "trebleFreq";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION:
		return "sampleRateReduction";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_BITCRUSHING:
		return "bitcrushAmount";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_MOD_FX_OFFSET:
		return "modFXOffset";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_MOD_FX_FEEDBACK:
		return "modFXFeedback";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_COMPRESSOR_SHAPE:
		return "compressorShape";

	default:
		return "none";
	}
}

int ModControllableAudio::stringToParam(char const* string) {
	for (int p = PARAM_UNPATCHED_SECTION; p < PARAM_UNPATCHED_SECTION + NUM_SHARED_UNPATCHED_PARAMS; p++) {
		if (!strcmp(string, ModControllableAudio::paramToString(p))) return p;
	}
	return PARAM_NONE;
}

ModelStackWithAutoParam* ModControllableAudio::getParamFromModEncoder(int whichModEncoder,
                                                                      ModelStackWithThreeMainThings* modelStack,
                                                                      bool allowCreation) {

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	int paramId;
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	ModelStackWithParamId* newModelStack1 = modelStack->addParamCollectionAndId(paramCollection, summary, paramId);
	return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
}
