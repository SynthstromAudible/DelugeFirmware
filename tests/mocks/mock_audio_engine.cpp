#include "CppUTest/TestHarness.h"
#include "dsp/reverb/freeverb/revmodel.hpp"
#include "dsp/timestretch/time_stretcher.h"
#include "model/sample/sample_recorder.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_vector.h"
#include "processing/engines/audio_engine.h"
#include "processing/live/live_input_buffer.h"

#include <cassert>
#include <iostream>

using namespace std;

#define SHOW_AUDIO_LOG 0

namespace AudioEngine {

void logAction(char const* item) {
#if SHOW_AUDIO_LOG
	cout << item << endl;
#endif
}

uint8_t numHopsEndedThisRoutineCall;
VoiceVector activeVoices;
bool micPluggedIn;
bool lineInPluggedIn;
bool mustUpdateReverbParamsBeforeNextRender = false;

int32_t cpuDireness = 0;
uint32_t timeDirenessChanged;
uint32_t timeThereWasLastSomeReverb = 0x8FFFFFFF;
int32_t numSamplesLastTime;
int32_t smoothedSamples;
uint32_t nextVoiceState = 1;
bool renderInStereo = true;
bool bypassCulling = false;
bool audioRoutineLocked = false;
uint32_t audioSampleTimer = 0;
uint32_t i2sTXBufferPos;
uint32_t i2sRXBufferPos;
SampleRecorder* firstRecorder = NULL;
RMSFeedbackCompressor mastercompressor{};
revmodel reverb{};
SideChain reverbCompressor{};
int32_t reverbCompressorVolume;
int32_t reverbCompressorShape;
int32_t reverbPan = 0;

int32_t sideChainHitPending = false;
uint32_t timeLastSideChainHit = 2147483648;
int32_t sizeLastSideChainHit;

Voice* solicitVoice(Sound* forSound) {
	Voice* voice = new Voice();

	voice->assignedToSound = forSound;

	return voice;
}

VoiceSample* solicitVoiceSample() {
	return new VoiceSample();
}

void unassignAllVoices(bool deletingSong) {
	FAIL("not yet implemented");
}

void unassignVoice(Voice* voice, Sound* sound, ModelStackWithSoundFlags* modelStack, bool removeFromVector,
                   bool shouldDispose) {
	FAIL("not yet implemented");
}

void voiceSampleUnassigned(VoiceSample* voiceSample) {
	// TODO: add to active voices maybe?
	delete voiceSample;
}

TimeStretcher* solicitTimeStretcher() {
	return new TimeStretcher();
}

void timeStretcherUnassigned(TimeStretcher* timeStretcher) {
	delete timeStretcher;
}

LiveInputBuffer* getOrCreateLiveInputBuffer(OscType inputType, bool mayCreate) {
	return new LiveInputBuffer();
}

void routine() {
	// TODO: add a mechanism to expect a certain number of calls of the routine, probably?
	FAIL("Recurssing in to the audio routine unexpected");
}

void routineWithClusterLoading(bool allowInputProcessing) {
	// TODO: add a mechanism to expect a certain number of calls of the routine, probably?
	FAIL("Recurssing in to the audio routine unexpected");
}

SampleRecorder* getNewRecorder(int32_t numChannels, AudioRecordingFolder folderID, AudioInputChannel mode,
                               bool keepFirstReasons, bool writeLoopPoints, int32_t buttonPressLatency) {
	SampleRecorder* newRecorder = new SampleRecorder();
	int32_t error =
	    newRecorder->setup(numChannels, mode, keepFirstReasons, writeLoopPoints, folderID, buttonPressLatency);
	if (error) {
		FAIL("recorder setup failed");
	}

	newRecorder->next = firstRecorder;
	firstRecorder = newRecorder;

	return newRecorder;
}

void discardRecorder(SampleRecorder* recorder) {
	int32_t count = 0;
	SampleRecorder** prevPointer = &firstRecorder;
	while (*prevPointer) {

		count++;
		if (ALPHA_OR_BETA_VERSION && !*prevPointer) {
			FAIL("invalid recorder chain");
		}
		if (*prevPointer == recorder) {
			*prevPointer = recorder->next;
			break;
		}

		prevPointer = &(*prevPointer)->next;
	}

	delete recorder;
}

void doRecorderCardRoutines() {
	FAIL("not yet implemented");
}

int32_t getNumSamplesLeftToOutputFromPreviousRender() {
	FAIL("not yet implemented");
	return 0;
}

void registerSideChainHit(int32_t strength) {
	FAIL("not yet implemented");
}

} // namespace AudioEngine
