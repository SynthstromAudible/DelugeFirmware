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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ContextMenuSampleBrowserKit.h>
#include <ContextMenuSampleBrowserSynth.h>
#include <samplebrowser.h>
#include <sounddrum.h>
#include <soundinstrument.h>
#include "definitions.h"
#include "functions.h"
#include "numericdriver.h"
#include "AudioRecorder.h"
#include "View.h"
#include <string.h>
#include "GeneralMemoryAllocator.h"
#include "midiengine.h"
#include "kit.h"
#include "MultisampleRange.h"
#include "CVEngine.h"
#include "VoiceSample.h"
#include "TimeStretcher.h"
#include "LiveInputBuffer.h"
#include <new>
#include "Slicer.h"
#include "SampleRecorder.h"
#include "song.h"
#include "loadsongui.h"
#include "uitimermanager.h"
#include "storagemanager.h"
#include "AudioOutput.h"
#include "voice.h"
#include "revmodel.hpp"
#include "Metronome.h"
#include "VoiceVector.h"
#include "definitions.h"
#include "uart.h"
#include "PatchCableSet.h"
#include "ParamSet.h"

#if AUTOMATED_TESTER_ENABLED
#include "AutomatedTester.h"
#endif

extern "C" {
#include "sio_char.h"
#include "r_usb_basic_define.h"
#include "mtu_all_cpus.h"
#include "ssi_all_cpus.h"

#include "devdrv_intc.h"
//void *__dso_handle = NULL; // This fixes an insane error.
}

extern bool inSpamMode;
extern bool anythingProbablyPressed;
extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

//#define REPORT_CPU_USAGE 1

#define NUM_SAMPLES_FOR_CPU_USAGE_REPORT 32

#define AUDIO_OUTPUT_GAIN_DOUBLINGS 8

#ifdef REPORT_CPU_USAGE
#define REPORT_AVERAGE_NUM 10
int usageTimes[REPORT_AVERAGE_NUM];
#endif

extern "C" uint32_t getAudioSampleTimerMS() {
	return AudioEngine::audioSampleTimer / 44.1;
}

int16_t zeroMPEValues[NUM_EXPRESSION_DIMENSIONS] = {0, 0, 0};

namespace AudioEngine {

revmodel reverb;
Compressor reverbCompressor;
int32_t reverbCompressorVolume;
int32_t reverbCompressorShape;
int32_t reverbPan = 0;

int32_t reverbCompressorVolumeInEffect; // Active right now - possibly overridden by the sound with the most reverb
int32_t reverbCompressorShapeInEffect;

bool mustUpdateReverbParamsBeforeNextRender = false;

int32_t sideChainHitPending = false;

uint32_t timeLastSideChainHit = 2147483648;
int32_t sizeLastSideChainHit;

Metronome metronome;

SoundDrum* sampleForPreview;
ParamManagerForTimeline* paramManagerForSamplePreview;

char paramManagerForSamplePreviewMemory[sizeof(ParamManagerForTimeline)];
char sampleForPreviewMemory[sizeof(SoundDrum)];

SampleRecorder* firstRecorder = NULL;

// Let's keep these grouped - the stuff we're gonna access regularly during audio rendering
int cpuDireness = 0;
uint32_t timeDirenessChanged;
uint32_t timeThereWasLastSomeReverb = 0x8FFFFFFF;
int numSamplesLastTime;
uint32_t nextVoiceState = 1;
bool renderInStereo = true;
bool bypassCulling = false;
bool audioRoutineLocked = false;
uint32_t audioSampleTimer = 0;
uint32_t i2sTXBufferPos;
uint32_t i2sRXBufferPos;

bool headphonesPluggedIn;
bool micPluggedIn;
bool lineInPluggedIn;
uint8_t inputMonitoringMode = INPUT_MONITORING_SMART;
bool routineBeenCalled;
uint8_t numHopsEndedThisRoutineCall;

int32_t reverbSendPostLPF = 0;

VoiceVector activeVoices;

LiveInputBuffer* liveInputBuffers[3];

// For debugging
uint16_t lastRoutineTime;

StereoSample renderingBuffer[SSI_TX_BUFFER_NUM_SAMPLES] __attribute__((aligned(CACHE_LINE_SIZE)));

StereoSample* renderingBufferOutputPos = renderingBuffer;
StereoSample* renderingBufferOutputEnd = renderingBuffer;

int32_t masterVolumeAdjustmentL;
int32_t masterVolumeAdjustmentR;

bool doMonitoring;
int monitoringAction;

uint32_t saddr;

VoiceSample voiceSamples[NUM_VOICE_SAMPLES_STATIC] = {};
VoiceSample* firstUnassignedVoiceSample = voiceSamples;

TimeStretcher timeStretchers[NUM_TIME_STRETCHERS_STATIC] = {};
TimeStretcher* firstUnassignedTimeStretcher = timeStretchers;

// Hmm, I forgot this was still being used. It's not a great way of doing things... wait does this still actually get used? No?
Voice staticVoices[NUM_VOICES_STATIC] = {};
Voice* firstUnassignedVoice;

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
	    modelStack->addTimelineCounter(NULL)
	        ->addOtherTwoThingsButNoNoteRow(sampleForPreview, paramManagerForSamplePreview)
	        ->addParamCollectionSummary(paramManagerForSamplePreview->getPatchCableSetSummary());

	((PatchCableSet*)modelStackWithParamCollection->paramCollection)->setupPatching(modelStackWithParamCollection);
	sampleForPreview->patcher.performInitialPatching(sampleForPreview, paramManagerForSamplePreview);

	sampleForPreview->sideChainSendLevel = 2147483647;

	for (int i = 0; i < NUM_VOICE_SAMPLES_STATIC; i++) {
		voiceSamples[i].nextUnassigned = (i == NUM_VOICE_SAMPLES_STATIC - 1) ? NULL : &voiceSamples[i + 1];
	}

	for (int i = 0; i < NUM_TIME_STRETCHERS_STATIC; i++) {
		timeStretchers[i].nextUnassigned = (i == NUM_TIME_STRETCHERS_STATIC - 1) ? NULL : &timeStretchers[i + 1];
	}

	for (int i = 0; i < NUM_VOICES_STATIC; i++) {
		staticVoices[i].nextUnassigned = (i == NUM_VOICES_STATIC - 1) ? NULL : &staticVoices[i + 1];
	}

	i2sTXBufferPos = (uint32_t)getTxBufferStart();

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	i2sRXBufferPos = (uint32_t)getRxBufferStart()
	                 + ((SSI_RX_BUFFER_NUM_SAMPLES - SSI_TX_BUFFER_NUM_SAMPLES - 16)
	                    << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE)); // Subtracting 5 or more seems fine
#endif
}

void unassignAllVoices(bool deletingSong) {

	for (int v = 0; v < activeVoices.getNumElements(); v++) {
		Voice* thisVoice = activeVoices.getVoice(v);

		thisVoice->setAsUnassigned(NULL, deletingSong);
		disposeOfVoice(thisVoice);
	}
	activeVoices.empty();

	// Because we unfortunately don't have a master list of VoiceSamples or actively sounding AudioClips,
	// we have to unassign all of those by going through all AudioOutputs.
	// But if there's no currentSong, that's fine - it's already been deleted, and this has already been called for it before then.
	if (currentSong) {
		for (Output* output = currentSong->firstOutput; output; output = output->next) {
			if (output->type == OUTPUT_TYPE_AUDIO) {
				((AudioOutput*)output)->cutAllSound();
			}
		}
	}
}

void songSwapAboutToHappen() {

	uiTimerManager.unsetTimer(
	    TIMER_PLAY_ENABLE_FLASH); // Otherwise, a timer might get called and try to access Clips that we may have deleted below
	logAction("a1");
	currentSong->deleteSoundsWhichWontSound();
	logAction("a2");
	playbackHandler.stopAnyRecording();
}

// To be called when CPU is overloaded and we need to free it up. This stops the voice which has been releasing longest, or if none, the voice playing longest.
Voice* cullVoice(bool saveVoice, bool justDoFastRelease) {

	uint32_t bestRating = 0;
	Voice* bestVoice = NULL;

	for (int v = 0; v < activeVoices.getNumElements(); v++) {
		Voice* thisVoice = activeVoices.getVoice(v);

		uint32_t ratingThisVoice = thisVoice->getPriorityRating();

		if (ratingThisVoice > bestRating) {
			bestRating = ratingThisVoice;
			bestVoice = thisVoice;
		}
	}

	if (bestVoice) {
		activeVoices.checkVoiceExists(
		    bestVoice, bestVoice->assignedToSound,
		    "E196"); // ronronsen got!! https://forums.synthstrom.com/discussion/4097/beta-4-0-0-beta-1-e196-by-loading-wavetable-osc#latest

		if (justDoFastRelease) {
			if (bestVoice->envelopes[0].state < ENVELOPE_STAGE_FAST_RELEASE) {
				bool stillGoing = bestVoice->doFastRelease(65536);

				if (!stillGoing) {
					unassignVoice(bestVoice, bestVoice->assignedToSound);
				}

#if ALPHA_OR_BETA_VERSION
				Uart::print("soft-culled 1 voice. voices now: ");
				Uart::println(getNumVoices());
#endif
			}
			// Otherwise, it's already fast-releasing, so just leave it

			bestVoice = NULL; // We don't want to return it
		}

		else {
			unassignVoice(bestVoice, bestVoice->assignedToSound, NULL, true, !saveVoice);
		}
	}

	// Or if no Voices to cull, try culling an AudioClip...
	else {
		if (currentSong && !justDoFastRelease) currentSong->cullAudioClipVoice();
	}

	return bestVoice;
}

int getNumVoices() {
	return activeVoices.getNumElements();
}

void routineWithClusterLoading(bool mayProcessUserActionsBetween) {
	logAction("AudioDriver::routineWithClusterLoading");

	routineBeenCalled = false;
	audioFileManager.loadAnyEnqueuedClusters(128, mayProcessUserActionsBetween);
	if (!routineBeenCalled) {
		logAction("from routineWithClusterLoading()");
		routine(); // -----------------------------------
	}
}

#define DO_AUDIO_LOG 0 // For advavnced debugging printouts.
#define AUDIO_LOG_SIZE 128

#if DO_AUDIO_LOG
uint16_t audioLogTimes[AUDIO_LOG_SIZE];
char audioLogStrings[AUDIO_LOG_SIZE][64];
int numAudioLogItems = 0;
#endif

#define TICK_TYPE_SWUNG 1
#define TICK_TYPE_TIMER 2

extern uint16_t g_usb_usbmode;

void routine() {

	logAction("AudioDriver::routine");

	if (audioRoutineLocked) {
		logAction("AudioDriver::routine locked");
		return; // Prevents this from being called again from inside any e.g. memory allocation routines that get called from within this!
	}

	// See if some more outputting is left over from last time to do
	bool finishedOutputting = doSomeOutputting();
	if (!finishedOutputting) {
		logAction("AudioDriver::still outputting");
		//Uart::println("still waiting");
		return;
	}

	audioRoutineLocked = true;
	routineBeenCalled = true;

	playbackHandler.routine();

	// At this point, there may be MIDI, including clocks, waiting to be sent.

	generalMemoryAllocator.checkStack("AudioDriver::routine");

	saddr = (uint32_t)(getTxBufferCurrentPlace());
	uint32_t saddrPosAtStart = saddr >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE);
	int numSamples = ((uint32_t)(saddr - i2sTXBufferPos) >> (2 + NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE))
	                 & (SSI_TX_BUFFER_NUM_SAMPLES - 1);
	if (!numSamples) {
		audioRoutineLocked = false;
		return;
	}

#if AUTOMATED_TESTER_ENABLED
	AutomatedTester::possiblyDoSomething();
#endif

	// Flush everything out of the MIDI buffer now. At this stage, it would only really have live user-triggered output and MIDI THRU in it.
	// We want any messages like "start" to go out before we send any clocks below, and also want to give them a head-start being sent and out of the way so the clock messages can
	// be sent on-time
	bool anythingInMidiOutputBufferNow = midiEngine.anythingInOutputBuffer();
	bool anythingInGateOutputBufferNow =
	    cvEngine.gateOutputPending || cvEngine.clockOutputPending; // Not asapGateOutputPending (RUN)
	if (anythingInMidiOutputBufferNow || anythingInGateOutputBufferNow) {

		// We're only allowed to do this if the timer ISR isn't pending (i.e. we haven't enabled to timer to trigger it) - otherwise this will all get called soon anyway.
		// I thiiiink this is 100% immune to any synchronization problems?
		if (!isTimerEnabled(TIMER_MIDI_GATE_OUTPUT)) {
			if (anythingInGateOutputBufferNow) cvEngine.updateGateOutputs();
			if (anythingInMidiOutputBufferNow) midiEngine.flushMIDI();
		}
	}

#ifdef REPORT_CPU_USAGE
	if (numSamples < (NUM_SAMPLES_FOR_CPU_USAGE_REPORT)) {
		audioRoutineLocked = false;
		return;
	}
	numSamples = NUM_SAMPLES_FOR_CPU_USAGE_REPORT;
	int unadjustedNumSamplesBeforeLappingPlayHead = numSamples;
#else

	numSamplesLastTime = numSamples;

	// Consider direness and culling - before increasing the number of samples
	int numSamplesLimit = 40; //storageManager.devVarC;
	int direnessThreshold = numSamplesLimit - 17;

	if (numSamples >= direnessThreshold) { // 20

		int newDireness = numSamples - (direnessThreshold - 1);
		if (newDireness > 14) newDireness = 14;

		if (newDireness >= cpuDireness) {
			cpuDireness = newDireness;
			timeDirenessChanged = audioSampleTimer;
		}

		if (!bypassCulling) {
			int numSamplesOverLimit = numSamples - numSamplesLimit;

			// If it's real dire, do a proper immediate cull
			if (numSamplesOverLimit >= 0) {

				int numToCull = (numSamplesOverLimit >> 3) + 1;

				for (int i = 0; i < numToCull; i++) {
					cullVoice();
				}

#if ALPHA_OR_BETA_VERSION
				Uart::print("culled ");
				Uart::print(numToCull);
				Uart::print(" voices. numSamples: ");
				Uart::print(numSamples);

				Uart::print(". voices left: ");
				Uart::println(getNumVoices());
#endif
			}

			// Or if it's just a little bit dire, do a soft cull with fade-out
			else if (numSamplesOverLimit >= -6) {
				cullVoice(false, true);
			}
		}
		else {
			int numSamplesOverLimit = numSamples - numSamplesLimit;
			if (numSamplesOverLimit >= 0) {
				Uart::print("Won't cull, but numSamples is ");
				Uart::println(numSamples);
			}
		}
	}

	else if (numSamples < direnessThreshold - 10) {

		if ((int32_t)(audioSampleTimer - timeDirenessChanged) >= (44100 >> 3)) { // Only if it's been long enough
			timeDirenessChanged = audioSampleTimer;
			cpuDireness--;
			if (cpuDireness < 0) cpuDireness = 0;
			else {
				//Uart::print("direness: ");
				//Uart::println(cpuDireness);
			}
		}
	}
	bypassCulling = false;

	// Double the number of samples we're going to do - within some constraints
	int sampleThreshold = 6; // If too low, it'll lead to bigger audio windows and stuff
	int maxAdjustedNumSamples = SSI_TX_BUFFER_NUM_SAMPLES >> 1;

	int unadjustedNumSamplesBeforeLappingPlayHead = numSamples;

	if (numSamples < maxAdjustedNumSamples) {
		int samplesOverThreshold = numSamples - sampleThreshold;
		if (samplesOverThreshold > 0) {
			samplesOverThreshold = samplesOverThreshold << 1;
			numSamples = sampleThreshold + samplesOverThreshold;
			numSamples = getMin(numSamples, maxAdjustedNumSamples);
		}
	}

	// Want to round to be doing a multiple of 4 samples, so the NEON functions can be utilized most efficiently.
	// Note - this can take numSamples up as high as SSI_TX_BUFFER_NUM_SAMPLES (currently 128).
	if (numSamples >= 3) {
		numSamples = (numSamples + 2) & ~3;
	}

#endif

	int timeWithinWindowAtWhichMIDIOrGateOccurs = -1; // -1 means none

	// If a timer-tick is due during or directly after this window of audio samples...
	if (playbackHandler.isEitherClockActive()) {

startAgain:
		int nextTickType = 0;
		uint32_t timeNextTick = audioSampleTimer + 9999;

		if (playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) {
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

		// If tick occurs right now at the start of this window - which is fairly likely, cos we would have cut the previous window short if it had such a tick during it...
		if (timeTilNextTick <= 0) {
			if (nextTickType == TICK_TYPE_TIMER) {
				playbackHandler.actionTimerTick();
			}
			else if (nextTickType == TICK_TYPE_SWUNG) {
				playbackHandler.actionSwungTick();
				playbackHandler
				    .scheduleSwungTick(); // If that swung tick action just did a song swap, this will have already been called, but fortunately this doesn't break anything
			}

			// Those could have outputted clock or other MIDI / gate
			if (midiEngine.anythingInOutputBuffer() || cvEngine.clockOutputPending
			    || cvEngine.gateOutputPending) { // Not asapGateOutputPending. That probably actually couldn't have been
				                                 // generated by a actionSwungTick() anyway I think?
				timeWithinWindowAtWhichMIDIOrGateOccurs = 0;
			}

			goto startAgain;
		}

		// If the tick is during this window, shorten the window so we stop right at the tick
		if (timeTilNextTick < numSamples) {
			numSamples = timeTilNextTick;
		}

		// And now we know how long the window's definitely going to be, see if we want to do any trigger clock or MIDI clock out ticks during it

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

	memset(&renderingBuffer, 0, numSamples * sizeof(StereoSample));

	static int32_t reverbBuffer[SSI_TX_BUFFER_NUM_SAMPLES] __attribute__((aligned(CACHE_LINE_SIZE)));
	memset(&reverbBuffer, 0, numSamples * sizeof(int32_t));

#ifdef REPORT_CPU_USAGE
	uint16_t startTime = MTU2.TCNT_0;
#endif

	if (sideChainHitPending) {
		timeLastSideChainHit = audioSampleTimer;
		sizeLastSideChainHit = sideChainHitPending;
	}

	numHopsEndedThisRoutineCall = 0;

	// Render audio for song
	if (currentSong) {
		currentSong->renderAudio(renderingBuffer, numSamples, reverbBuffer, sideChainHitPending);
	}

#ifdef REPORT_CPU_USAGE
	uint16_t endTime = MTU2.TCNT_0;

	if (getRandom255() < 3) {

		int value = fastTimerCountToUS((endTime - startTime) * 10);

		int total = value;

		for (int i = 0; i < REPORT_AVERAGE_NUM - 1; i++) {
			usageTimes[i] = usageTimes[i + 1];
			total += usageTimes[i];
		}

		usageTimes[REPORT_AVERAGE_NUM - 1] = value;

		Uart::print("uS per ");
		Uart::print(NUM_SAMPLES_FOR_CPU_USAGE_REPORT * 10);
		Uart::print(" samples: ");
		Uart::println(total / REPORT_AVERAGE_NUM);
	}
#endif

	if (currentSong && mustUpdateReverbParamsBeforeNextRender) {
		updateReverbParams();
		mustUpdateReverbParamsBeforeNextRender = false;
	}

	// Render the reverb compressor
	int32_t compressorOutput = 0;
	if (reverbCompressorVolumeInEffect != 0) {
		if (sideChainHitPending != 0) reverbCompressor.registerHit(sideChainHitPending);
		compressorOutput = reverbCompressor.render(numSamples, reverbCompressorShapeInEffect);
	}

	int32_t reverbAmplitudeL;
	int32_t reverbAmplitudeR;

	bool reverbOn = ((uint32_t)(audioSampleTimer - timeThereWasLastSomeReverb)
	                 < 44100 * 12); // Stop reverb after 12 seconds of inactivity

	if (reverbOn) {
		// Patch that to reverb volume
		int32_t positivePatchedValue =
		    multiply_32x32_rshift32(compressorOutput, reverbCompressorVolumeInEffect) + 536870912;
		int32_t reverbOutputVolume = (positivePatchedValue >> 15) * (positivePatchedValue >> 14);

		// Reverb panning
		bool thisDoPanning = (renderInStereo && shouldDoPanning(reverbPan, &reverbAmplitudeL, &reverbAmplitudeR));

		if (thisDoPanning) {
			reverbAmplitudeL = multiply_32x32_rshift32(reverbAmplitudeL, reverbOutputVolume) << 2;
			reverbAmplitudeR = multiply_32x32_rshift32(reverbAmplitudeR, reverbOutputVolume) << 2;
		}
		else reverbAmplitudeL = reverbAmplitudeR = reverbOutputVolume;

		// HPF on reverb send, cos if it has DC offset, the reverb magnifies that, and the sound farts out
		{
			int32_t* reverbSample = reverbBuffer;
			int32_t* reverbBufferEnd = &reverbBuffer[numSamples];
			do {
				int32_t distanceToGoL = *reverbSample - reverbSendPostLPF;
				reverbSendPostLPF += distanceToGoL >> 11;
				*reverbSample -= reverbSendPostLPF;

				reverbSample++;
			} while (reverbSample != reverbBufferEnd);
		}

		// Mix reverb into main render
		int32_t* reverbSample = reverbBuffer;
		StereoSample* outputSample = renderingBuffer;

		StereoSample* outputBufferEnd = renderingBuffer + numSamples;

		do {
			int32_t reverbOutL;
			int32_t reverbOutR;
			reverb.process(*(reverbSample++) >> 1, &reverbOutL, &reverbOutR);

			outputSample->l += multiply_32x32_rshift32_rounded(reverbOutL, reverbAmplitudeL);
			outputSample->r += multiply_32x32_rshift32_rounded(reverbOutR, reverbAmplitudeR);

		} while (++outputSample != outputBufferEnd);
	}

	// Previewing sample
	if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &contextMenuFileBrowserKit
	    || getCurrentUI() == &contextMenuFileBrowserSynth || getCurrentUI() == &slicer) {

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);

		sampleForPreview->render(modelStack, renderingBuffer, numSamples, reverbBuffer, sideChainHitPending);
	}

	// LPF and stutter for song (must happen after reverb mixed in, which is why it's happening all the way out here
	masterVolumeAdjustmentL = 167763968; //getParamNeutralValue(PARAM_GLOBAL_VOLUME_POST_FX);
	masterVolumeAdjustmentR = 167763968; //getParamNeutralValue(PARAM_GLOBAL_VOLUME_POST_FX);
	// 167763968 is 134217728 made a bit bigger so that default filter resonance doesn't reduce volume overall

	if (currentSong) {
		FilterSetConfig filterSetConfig;
		currentSong->globalEffectable.setupFilterSetConfig(&filterSetConfig, &masterVolumeAdjustmentL,
		                                                   &currentSong->paramManager);
		currentSong->globalEffectable.processFilters(renderingBuffer, numSamples, &filterSetConfig);
		currentSong->globalEffectable.processSRRAndBitcrushing(renderingBuffer, numSamples, &masterVolumeAdjustmentL,
		                                                       &currentSong->paramManager);

		masterVolumeAdjustmentR = masterVolumeAdjustmentL; // This might have changed in the above function calls

		currentSong->globalEffectable.processStutter(renderingBuffer, numSamples, &currentSong->paramManager);

		// And we do panning for song here too - must be post reverb, and we had to do a volume adjustment below anyway
		int32_t pan =
		    currentSong->paramManager.getUnpatchedParamSet()->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN) >> 1;

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
	}

	masterVolumeAdjustmentL <<= 2;
	masterVolumeAdjustmentR <<= 2;

	metronome.render(renderingBuffer, numSamples);

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD

	// Monitoring setup
	doMonitoring = false;
	if (audioRecorder.recordingSource == AUDIO_INPUT_CHANNEL_STEREO
	    || audioRecorder.recordingSource == AUDIO_INPUT_CHANNEL_LEFT) {
		if (inputMonitoringMode == INPUT_MONITORING_SMART) {
			doMonitoring = (lineInPluggedIn || headphonesPluggedIn);
		}
		else {
			doMonitoring = (inputMonitoringMode == INPUT_MONITORING_ON);
		}
	}

	monitoringAction = 0;
	if (doMonitoring && audioRecorder.recorder) { // Double-check
		if (lineInPluggedIn) {                    // Line input
			if (audioRecorder.recorder->inputLooksDifferential()) monitoringAction = ACTION_SUBTRACT_RIGHT_CHANNEL;
			else if (audioRecorder.recorder->inputHasNoRightChannel()) monitoringAction = ACTION_REMOVE_RIGHT_CHANNEL;
		}

		else if (micPluggedIn) { // External mic
			if (audioRecorder.recorder->inputHasNoRightChannel()) monitoringAction = ACTION_REMOVE_RIGHT_CHANNEL;
		}

		else { // Internal mic
			monitoringAction = ACTION_REMOVE_RIGHT_CHANNEL;
		}
	}

#endif

	renderingBufferOutputPos = renderingBuffer;
	renderingBufferOutputEnd = renderingBuffer + numSamples;

	doSomeOutputting();

	/*
    if (!getRandom255()) {
    	Uart::print("samples: ");
        Uart::print(numSamples);
    	Uart::print(". voices: ");
    	Uart::println(getNumVoices());
    }
*/

	bool anyGateOutputPending =
	    cvEngine.gateOutputPending || cvEngine.clockOutputPending || cvEngine.asapGateOutputPending;

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

		if (!samplesTilMIDIOrGate) samplesTilMIDIOrGate = SSI_TX_BUFFER_NUM_SAMPLES;

		//samplesTilMIDI += 10; This gets the start of stuff perfectly lined up. About 10 for MIDI, 12 for gate

		if (anyGateOutputPending) {
			// If a gate note-on was processed at the same time as a gate note-off, the note-off will have already been sent, but we need to make sure that the
			// note-on now doesn't happen until a set amount of time after the note-off.
			int32_t gateMinDelayInSamples =
			    ((uint32_t)cvEngine.minGateOffTime * 289014) >> 16; // That's *4.41 (derived from 44100)
			int32_t samplesTilAllowedToSend =
			    cvEngine.mostRecentSwitchOffTimeOfPendingNoteOn + gateMinDelayInSamples - audioSampleTimer;

			if (samplesTilAllowedToSend > 0) {

				samplesTilAllowedToSend -= (saddrMovementSinceStart & (SSI_TX_BUFFER_NUM_SAMPLES - 1));

				if (samplesTilMIDIOrGate < samplesTilAllowedToSend) samplesTilMIDIOrGate = samplesTilAllowedToSend;
			}
		}

		R_INTC_Enable(INTC_ID_TGIA[TIMER_MIDI_GATE_OUTPUT]);

		*TGRA[TIMER_MIDI_GATE_OUTPUT] = ((uint32_t)samplesTilMIDIOrGate * 766245)
		                                >> 16; // Set delay time. This is samplesTilMIDIOrGate * 515616 / 44100.
		enableTimer(TIMER_MIDI_GATE_OUTPUT);
	}

#if DO_AUDIO_LOG
	uint16_t currentTime = *TCNT[TIMER_SYSTEM_FAST];
	uint16_t timePassedA = (uint16_t)currentTime - lastRoutineTime;
	uint32_t timePassedUSA = fastTimerCountToUS(timePassedA);
	if (timePassedUSA > storageManager.devVarA * 10) {
		Uart::println("");
		for (int i = 0; i < numAudioLogItems; i++) {
			uint16_t timePassed = (uint16_t)audioLogTimes[i] - lastRoutineTime;
			uint32_t timePassedUS = fastTimerCountToUS(timePassed);
			Uart::print(timePassedUS);
			Uart::print(": ");
			Uart::println(audioLogStrings[i]);
		}

		Uart::print(timePassedUSA);
		Uart::println(": end");
	}

	lastRoutineTime = *TCNT[TIMER_SYSTEM_FAST];
	numAudioLogItems = 0;
#endif

	sideChainHitPending = 0;
	audioSampleTimer += numSamples;

	audioRoutineLocked = false;
}

int getNumSamplesLeftToOutputFromPreviousRender() {
	return ((uint32_t)renderingBufferOutputEnd - (uint32_t)renderingBufferOutputPos) >> 3;
}

// Returns whether we got to the end
bool doSomeOutputting() {

	// Copy to actual output buffer, and apply heaps of gain too, with clipping
	int numSamplesOutputted = 0;

	StereoSample* __restrict__ outputBufferForResampling = (StereoSample*)spareRenderingBuffer;
	StereoSample* __restrict__ renderingBufferOutputPosNow = renderingBufferOutputPos;
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
			      & (SSI_TX_BUFFER_NUM_SAMPLES - 1)))
				break;
		}

		// Here we're going to do something equivalent to a multiply_32x32_rshift32(), but add dithering.
		// Because dithering is good, and also because the annoying codec chip freaks out if it sees the same 0s for too long.
		int64_t lAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->l * (int64_t)masterVolumeAdjustmentL + (int64_t)getNoise();
		int64_t rAdjustedBig =
		    (int64_t)renderingBufferOutputPosNow->r * (int64_t)masterVolumeAdjustmentR + (int64_t)getNoise();

		int32_t lAdjusted = lAdjustedBig >> 32;
		int32_t rAdjusted = rAdjustedBig >> 32;

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD

		if (doMonitoring) {

			if (monitoringAction == ACTION_SUBTRACT_RIGHT_CHANNEL) {
				int32_t value = (inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS + 1))
				                - (inputReadPos[1] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS));
				lAdjusted += value;
				rAdjusted += value;
			}

			else {
				lAdjusted += inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);

				if (monitoringAction == 0) rAdjusted += inputReadPos[1] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);
				else // Remove right channel
					rAdjusted += inputReadPos[0] >> (AUDIO_OUTPUT_GAIN_DOUBLINGS);
			}

			inputReadPos += NUM_MONO_INPUT_CHANNELS;
			if (inputReadPos >= getRxBufferEnd()) inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
		}

#endif

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
		i2sTXBufferPosNow[0] = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(lAdjusted);
		i2sTXBufferPosNow[1] = lshiftAndSaturate<AUDIO_OUTPUT_GAIN_DOUBLINGS>(rAdjusted);

		outputBufferForResampling[numSamplesOutputted].l = i2sTXBufferPosNow[0];
		outputBufferForResampling[numSamplesOutputted].r = i2sTXBufferPosNow[1];
#endif

#if ALLOW_SPAM_MODE
		if (inSpamMode) {
			i2sTXBufferPosNow[0] = getNoise() >> 4;
			i2sTXBufferPosNow[1] = getNoise() >> 4;
		}
#endif

		i2sTXBufferPosNow += NUM_MONO_OUTPUT_CHANNELS;
		if (i2sTXBufferPosNow == getTxBufferEnd()) i2sTXBufferPosNow = getTxBufferStart();

		numSamplesOutputted++;
		++renderingBufferOutputPosNow;
	}

	renderingBufferOutputPos = renderingBufferOutputPosNow; // Write back from __restrict__ pointer to permanent pointer
	i2sTXBufferPos = (uint32_t)i2sTXBufferPosNow;

	if (numSamplesOutputted) {

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
		i2sRXBufferPos += (numSamplesOutputted << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
		if (i2sRXBufferPos >= (uint32_t)getRxBufferEnd())
			i2sRXBufferPos -= (SSI_RX_BUFFER_NUM_SAMPLES << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 2));
#endif

		// Go through each SampleRecorder, feeding them audio
		for (SampleRecorder* recorder = firstRecorder; recorder; recorder = recorder->next) {

			if (recorder->status >= RECORDER_STATUS_FINISHED_CAPTURING_BUT_STILL_WRITING) continue;

			// Recording final output
			if (recorder->mode == AUDIO_INPUT_CHANNEL_OUTPUT) {
				recorder->feedAudio((int32_t*)outputBufferForResampling, numSamplesOutputted);
			}

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
			// Recording from an input source
			else if (recorder->mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {

				// We'll feed a bunch of samples to the SampleRecorder - normally all of what we just advanced, but if the buffer wrapped around,
				// we'll just go to the end of the buffer, and not worry about the extra bit at the start - that'll get done next time.
				uint32_t stopPos =
				    (i2sRXBufferPos < (uint32_t)recorder->sourcePos) ? (uint32_t)getRxBufferEnd() : i2sRXBufferPos;

				int32_t* streamToRecord = recorder->sourcePos;
				int numSamplesFeedingNow =
				    (stopPos - (uint32_t)recorder->sourcePos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE);

				// We also enforce a firm limit on how much to feed, to keep things sane. Any remaining will get done next time.
				numSamplesFeedingNow = getMin(numSamplesFeedingNow, 256);

				if (recorder->mode == AUDIO_INPUT_CHANNEL_RIGHT) streamToRecord++;

				recorder->feedAudio(streamToRecord, numSamplesFeedingNow);

				recorder->sourcePos += numSamplesFeedingNow << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
				if (recorder->sourcePos >= getRxBufferEnd())
					recorder->sourcePos -= SSI_RX_BUFFER_NUM_SAMPLES << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
			}
#endif
		}
	}

	return (renderingBufferOutputPos == renderingBufferOutputEnd);
}

void logAction(char const* string) {
#if DO_AUDIO_LOG
	if (numAudioLogItems >= AUDIO_LOG_SIZE) return;
	audioLogTimes[numAudioLogItems] = *TCNT[TIMER_SYSTEM_FAST];
	strcpy(audioLogStrings[numAudioLogItems], string);
	numAudioLogItems++;
#endif
}

void logAction(int number) {
#if DO_AUDIO_LOG
	char buffer[12];
	intToString(number, buffer);
	logAction(buffer);
#endif
}

void updateReverbParams() {

	// If reverb compressor on "auto" settings...
	if (reverbCompressorVolume < 0) {

		// Just leave everything as is if parts deleted cos loading new song
		if (getCurrentUI() == &loadSongUI && loadSongUI.deletedPartsOfOldSong) return;

		Sound* soundWithMostReverb = NULL;
		ParamManager* paramManagerWithMostReverb = NULL;
		GlobalEffectableForClip* globalEffectableWithMostReverb = NULL;

		// Set the initial "highest amount found" to that of the song itself, which can't be affected by sidechain. If nothing found with more reverb,
		// then we don't want the reverb affected by sidechain
		int32_t highestReverbAmountFound = currentSong->paramManager.getUnpatchedParamSet()->getValue(
		    PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT);

		for (Output* thisOutput = currentSong->firstOutput; thisOutput; thisOutput = thisOutput->next) {
			thisOutput->getThingWithMostReverb(&soundWithMostReverb, &paramManagerWithMostReverb,
			                                   &globalEffectableWithMostReverb, &highestReverbAmountFound);
		}

		ModControllableAudio* modControllable;

		if (soundWithMostReverb) {
			modControllable = soundWithMostReverb;

			ParamDescriptor paramDescriptor;
			paramDescriptor.setToHaveParamOnly(PARAM_GLOBAL_VOLUME_POST_REVERB_SEND);

			PatchCableSet* patchCableSet = paramManagerWithMostReverb->getPatchCableSet();

			int whichCable = patchCableSet->getPatchCableIndex(PATCH_SOURCE_COMPRESSOR, paramDescriptor);
			if (whichCable != 255) {
				reverbCompressorVolumeInEffect =
				    patchCableSet->getModifiedPatchCableAmount(whichCable, PARAM_GLOBAL_VOLUME_POST_REVERB_SEND);
			}
			else reverbCompressorVolumeInEffect = 0;
			goto compressorFound;
		}
		else if (globalEffectableWithMostReverb) {
			modControllable = globalEffectableWithMostReverb;

			reverbCompressorVolumeInEffect =
			    globalEffectableWithMostReverb->getSidechainVolumeAmountAsPatchCableDepth(paramManagerWithMostReverb);

compressorFound:
			reverbCompressorShapeInEffect =
			    paramManagerWithMostReverb->getUnpatchedParamSet()->getValue(PARAM_UNPATCHED_COMPRESSOR_SHAPE);
			reverbCompressor.attack = modControllable->compressor.attack;
			reverbCompressor.release = modControllable->compressor.release;
			reverbCompressor.syncLevel = modControllable->compressor.syncLevel;
			return;
		}

		// Or if no thing has more reverb than the Song itself...
		else {
			reverbCompressorVolumeInEffect = 0;
			return;
		}
	}

	// Otherwise, use manually set params for reverb compressor
	reverbCompressorVolumeInEffect = reverbCompressorVolume;
	reverbCompressorShapeInEffect = reverbCompressorShape;
}

void registerSideChainHit(int32_t strength) {
	sideChainHitPending = combineHitStrengths(strength, sideChainHitPending);
}

void previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound) {
	stopAnyPreviewing();
	MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].getOrCreateFirstRange();
	if (!range) return;
	range->sampleHolder.filePath.set(path);
	int error = range->sampleHolder.loadFile(false, true, true, CLUSTER_LOAD_IMMEDIATELY, filePointer);

	if (error) numericDriver.displayError(error); // Rare, shouldn't cause later problems.

	if (shouldActuallySound) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = setupModelStackWithThreeMainThingsButNoNoteRow(
		    modelStackMemory, currentSong, sampleForPreview, NULL, paramManagerForSamplePreview);
		sampleForPreview->Sound::noteOn(modelStack, &sampleForPreview->arpeggiator, NOTE_FOR_DRUM, zeroMPEValues);
		bypassCulling =
		    true; // Needed - Dec 2021. I think it's during SampleBrowser::selectEncoderAction() that we may have gone a while without an audio routine call.
	}
}

void stopAnyPreviewing() {
	sampleForPreview->unassignAllVoices();
	if (sampleForPreview->sources[0].ranges.getNumElements()) {
		MultisampleRange* range = (MultisampleRange*)sampleForPreview->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(NULL);
	}
}

void getReverbParamsFromSong(Song* song) {
	reverb.setroomsize(song->reverbRoomSize);
	reverb.setdamp(song->reverbDamp);
	reverb.setwidth(song->reverbWidth);
	reverbPan = song->reverbPan;
	reverbCompressorVolume = song->reverbCompressorVolume;
	reverbCompressorShape = song->reverbCompressorShape;
	reverbCompressor.attack = song->reverbCompressorAttack;
	reverbCompressor.release = song->reverbCompressorRelease;
	reverbCompressor.syncLevel = song->reverbCompressorSync;
}

Voice* solicitVoice(Sound* forSound) {

	Voice* newVoice;

	if (numSamplesLastTime >= 100 && activeVoices.getNumElements()) {

		numSamplesLastTime -=
		    10; // Stop this triggering for lots of new voices. We just don't know how they'll weigh up to the ones being culled
		Uart::println("soliciting via culling");
doCull:
		newVoice = cullVoice(true);
	}

	else if (firstUnassignedVoice) {

		newVoice = firstUnassignedVoice;
		firstUnassignedVoice = firstUnassignedVoice->nextUnassigned;
	}

	else {
		void* memory = generalMemoryAllocator.alloc(sizeof(Voice), NULL, false, true);
		if (!memory) {
			if (activeVoices.getNumElements()) goto doCull;
			else return NULL;
		}

		newVoice = new (memory) Voice();
	}

	newVoice->assignedToSound = forSound;

	uint32_t keyWords[2];
	keyWords[0] = (uint32_t)forSound;
	keyWords[1] = (uint32_t)newVoice;

	int i = activeVoices.insertAtKeyMultiWord(keyWords);
	if (i == -1) {
		// if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("E193"); // No, having run out of RAM here isn't a reason to not continue.
		disposeOfVoice(newVoice);
		return NULL;
	}
	return newVoice;
}

// **** This is the main function that enacts the unassigning of the Voice
// sound only required if removing from vector
// modelStack can be NULL if you really insist
void unassignVoice(Voice* voice, Sound* sound, ModelStackWithSoundFlags* modelStack, bool removeFromVector,
                   bool shouldDispose) {

	activeVoices.checkVoiceExists(voice, sound, "E195");

	voice->setAsUnassigned(modelStack->addVoice(voice));
	if (removeFromVector) {
		uint32_t keyWords[2];
		keyWords[0] = (uint32_t)sound;
		keyWords[1] = (uint32_t)voice;
		activeVoices.deleteAtKeyMultiWord(keyWords);
	}

	if (shouldDispose) disposeOfVoice(voice);
}

void disposeOfVoice(Voice* voice) {
	if (voice >= staticVoices && voice < &staticVoices[NUM_TIME_STRETCHERS_STATIC]) {
		voice->nextUnassigned = firstUnassignedVoice;
		firstUnassignedVoice = voice;
	}
	else {
		generalMemoryAllocator.dealloc(voice);
	}
}

VoiceSample* solicitVoiceSample() {
	if (firstUnassignedVoiceSample) {
		VoiceSample* toReturn = firstUnassignedVoiceSample;
		firstUnassignedVoiceSample = firstUnassignedVoiceSample->nextUnassigned;
		return toReturn;
	}
	else {
		void* memory = generalMemoryAllocator.alloc(sizeof(VoiceSample), NULL, false, true);
		if (!memory) return NULL;

		return new (memory) VoiceSample();
	}
}

void voiceSampleUnassigned(VoiceSample* voiceSample) {
	if (voiceSample >= voiceSamples && voiceSample < &voiceSamples[NUM_VOICE_SAMPLES_STATIC]) {
		voiceSample->nextUnassigned = firstUnassignedVoiceSample;
		firstUnassignedVoiceSample = voiceSample;
	}
	else {
		generalMemoryAllocator.dealloc(voiceSample);
	}
}

TimeStretcher* solicitTimeStretcher() {
	if (firstUnassignedTimeStretcher) {

		TimeStretcher* toReturn = firstUnassignedTimeStretcher;
		firstUnassignedTimeStretcher = firstUnassignedTimeStretcher->nextUnassigned;
		return toReturn;
	}

	else {
		void* memory = generalMemoryAllocator.alloc(sizeof(TimeStretcher), NULL, false, true);
		if (!memory) return NULL;

		return new (memory) TimeStretcher();
	}
}

// There are no destructors. You gotta clean it up before you call this
void timeStretcherUnassigned(TimeStretcher* timeStretcher) {
	if (timeStretcher >= timeStretchers && timeStretcher < &timeStretchers[NUM_TIME_STRETCHERS_STATIC]) {
		timeStretcher->nextUnassigned = firstUnassignedTimeStretcher;
		firstUnassignedTimeStretcher = timeStretcher;
	}
	else {
		generalMemoryAllocator.dealloc(timeStretcher);
	}
}

// TODO: delete unused ones
LiveInputBuffer* getOrCreateLiveInputBuffer(int inputType, bool mayCreate) {
	if (!liveInputBuffers[inputType - OSC_TYPE_INPUT_L]) {
		if (!mayCreate) return NULL;

		int size = sizeof(LiveInputBuffer);
		if (inputType == OSC_TYPE_INPUT_STEREO) size += INPUT_RAW_BUFFER_SIZE * sizeof(int32_t);

		void* memory = generalMemoryAllocator.alloc(size, NULL, false, true);
		if (!memory) return NULL;

		liveInputBuffers[inputType - OSC_TYPE_INPUT_L] = new (memory) LiveInputBuffer();
	}

	return liveInputBuffers[inputType - OSC_TYPE_INPUT_L];
}

bool createdNewRecorder;

void doRecorderCardRoutines() {

	SampleRecorder** prevPointer = &firstRecorder;
	int count = 0;
	while (true) {
		count++;

		SampleRecorder* recorder = *prevPointer;
		if (!recorder) break;

		int error = recorder->cardRoutine();
		if (error) numericDriver.displayError(error);

		// If, while in the card routine, a new Recorder was added, then our linked list traversal state thing will be out of wack, so let's just get out and
		// come back later
		if (createdNewRecorder) break;

		// If complete, discard it
		if (recorder->status == RECORDER_STATUS_AWAITING_DELETION) {
			Uart::println("deleting recorder");
			*prevPointer = recorder->next;
			recorder->~SampleRecorder();
			generalMemoryAllocator.dealloc(recorder);
		}

		// Otherwise, move on
		else {
			prevPointer = &recorder->next;
		}
	}

	if (ALPHA_OR_BETA_VERSION && ENABLE_CLIP_CUTTING_DIAGNOSTICS && count >= 10 && !numericDriver.popupActive) {
		numericDriver.displayPopup("MORE");
	}
}

void slowRoutine() {

	// The RX buffer is much bigger than the TX, and we get our timing from the TX buffer's sending.
	// However, if there's an audio glitch, and also at start-up, it's possible we might have missed an entire cycle of the TX buffer.
	// That would cause the RX buffer's latency to increase. So here, we check for that and correct it.
	// The correct latency for the RX buffer is between (SSI_TX_BUFFER_NUM_SAMPLES) and (2 * SSI_TX_BUFFER_NUM_SAMPLES).

	uint32_t rxBufferWriteAddr = (uint32_t)getRxBufferCurrentPlace();
	uint32_t latencyWithinAppropriateWindow =
	    (((rxBufferWriteAddr - (uint32_t)i2sRXBufferPos) >> (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE))
	     - SSI_TX_BUFFER_NUM_SAMPLES)
	    & (SSI_RX_BUFFER_NUM_SAMPLES - 1);

	if (latencyWithinAppropriateWindow >= SSI_TX_BUFFER_NUM_SAMPLES) {
		i2sRXBufferPos += (SSI_TX_BUFFER_NUM_SAMPLES << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE));
		if (i2sRXBufferPos >= (uint32_t)getRxBufferEnd())
			i2sRXBufferPos -= (SSI_RX_BUFFER_NUM_SAMPLES << (2 + NUM_MONO_INPUT_CHANNELS_MAGNITUDE));
	}

	// Discard any LiveInputBuffers which aren't in use
	for (int i = 0; i < 3; i++) {
		if (liveInputBuffers[i]) {
			if (liveInputBuffers[i]->upToTime != audioSampleTimer) {
				liveInputBuffers[i]->~LiveInputBuffer();
				generalMemoryAllocator.dealloc(liveInputBuffers[i]);
				liveInputBuffers[i] = NULL;
			}
		}
	}

	createdNewRecorder = false;

	// Go through all SampleRecorders, getting them to write etc
	doRecorderCardRoutines();
}

SampleRecorder* getNewRecorder(int numChannels, int folderID, int mode, bool keepFirstReasons, bool writeLoopPoints,
                               int buttonPressLatency) {
	int error;

	void* recorderMemory = generalMemoryAllocator.alloc(sizeof(SampleRecorder), NULL, false, true);
	if (!recorderMemory) {
		return NULL;
	}

	SampleRecorder* newRecorder = new (recorderMemory) SampleRecorder();

	error = newRecorder->setup(numChannels, mode, keepFirstReasons, writeLoopPoints, folderID, buttonPressLatency);
	if (error) {
		newRecorder->~SampleRecorder();
		generalMemoryAllocator.dealloc(recorderMemory);
		return NULL;
	}

	newRecorder->next = firstRecorder;
	firstRecorder = newRecorder;

	createdNewRecorder = true;

	if (mode >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		renderInStereo =
		    true; // Quickly set it to true. It'll keep getting reset to true now every time inputRoutine() is called
	}

	return newRecorder;
}

// PLEASE don't call this if there's any chance you might be in the SD card routine...
void discardRecorder(SampleRecorder* recorder) {
	int count = 0;
	SampleRecorder** prevPointer = &firstRecorder;
	while (*prevPointer) {

		count++;
		if (ALPHA_OR_BETA_VERSION && !*prevPointer) numericDriver.freezeWithError("E264");
		if (*prevPointer == recorder) {
			*prevPointer = recorder->next;
			break;
		}

		prevPointer = &(*prevPointer)->next;
	}

	recorder->~SampleRecorder();
	generalMemoryAllocator.dealloc(recorder);
}

bool isAnyInternalRecordingHappening() {
	for (SampleRecorder* recorder = firstRecorder; recorder; recorder = recorder->next) {
		if (recorder->mode >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) return true;
	}

	return false;
}

} // namespace AudioEngine

//     for (Voice* thisVoice = voices; thisVoice != &voices[numVoices]; thisVoice++) {
