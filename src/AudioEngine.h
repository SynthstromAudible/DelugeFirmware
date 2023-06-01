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

#ifndef AUDIODRIVER_H
#define AUDIODRIVER_H

#include "r_typedefs.h"

extern "C" {
#include "ff.h"
}

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
class String;
class Compressor;
class VoiceVector;
class revmodel;
class Metronome;
class ModelStackWithSoundFlags;
class SoundDrum;

namespace AudioEngine {
    void routine();
    void routineWithChunkLoading(bool mayProcessUserActionsBetween = false);

    void init();
    void previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound);
    void stopAnyPreviewing();

    Voice* solicitVoice(Sound* forSound);
    void unassignVoice(Voice* voice, Sound* sound, ModelStackWithSoundFlags* modelStack = NULL, bool removeFromVector = true, bool shouldDispose = true);
    void disposeOfVoice(Voice* voice);

    void songSwapAboutToHappen();
    void unassignAllVoices(bool deletingSong = false);
    void logAction(char const* string);
    void logAction(int number);

    void getReverbParamsFromSong(Song* song);

    VoiceSample* solicitVoiceSample();
    void voiceSampleUnassigned(VoiceSample* voiceSample);

    TimeStretcher* solicitTimeStretcher();
    void timeStretcherUnassigned(TimeStretcher* timeStretcher);

    LiveInputBuffer* getOrCreateLiveInputBuffer(int inputType, bool mayCreate);
    void slowRoutine();
    void doRecorderCardRoutines();

    int getNumSamplesLeftToOutputFromPreviousRender();

    void registerSideChainHit(int32_t strength);


	SampleRecorder* getNewRecorder(int numChannels, int folderID, int mode, bool keepFirstReasons = false, bool writeLoopPoints = false, int buttonPressLatency = 0);
	void discardRecorder(SampleRecorder* recorder);
	bool isAnyInternalRecordingHappening();

#ifdef FLIGHTDATA
    char log[32][64];
    uint8_t logNextIndex = 0;
    uint32_t logAbsoluteIndex = 0;
    char *getEmptyLogEntry();
    void printLog();
#endif

    int getNumVoices();
    Voice* cullVoice(bool saveVoice = false, bool justDoFastRelease = false);

    bool doSomeOutputting();
    void updateReverbParams();

    extern bool headphonesPluggedIn;
    extern bool micPluggedIn;
    extern bool lineInPluggedIn;
    extern bool renderInStereo;
    extern uint32_t audioSampleTimer;
    extern bool mustUpdateReverbParamsBeforeNextRender;
    extern bool bypassCulling;
    extern uint32_t i2sTXBufferPos;
    extern uint32_t i2sRXBufferPos;
    extern int cpuDireness;
    extern uint8_t inputMonitoringMode;
    extern bool audioRoutineLocked;
    extern uint8_t numHopsEndedThisRoutineCall;
    extern Compressor reverbCompressor;
    extern uint32_t timeThereWasLastSomeReverb;
    extern VoiceVector activeVoices;
    extern revmodel reverb;
    extern uint32_t nextVoiceState;
    extern SoundDrum* sampleForPreview;
    extern int32_t reverbCompressorVolume;
    extern int32_t reverbCompressorShape;
    extern int32_t reverbPan;
    extern SampleRecorder* firstRecorder;
    extern Metronome metronome;
    extern uint32_t timeLastSideChainHit;
    extern int32_t sizeLastSideChainHit;
}


#endif // AUDIODRIVER_H
