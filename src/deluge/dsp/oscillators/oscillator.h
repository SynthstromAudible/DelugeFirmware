//
// Created by Mark Adams on 2025-01-08.
//

#ifndef DELUGE_OSCILLATOR_H
#define DELUGE_OSCILLATOR_H

#include "storage/wave_table/wave_table.h"
namespace deluge {
namespace dsp {

class oscillator {

	static void maybeStorePhase(const OscType& type, uint32_t* startPhase, uint32_t phase, bool doPulseWave);
	static void applyAmplitudeVectorToBuffer(int32_t amplitude, int32_t numSamples, int32_t amplitudeIncrement,
	                                         int32_t* outputBufferPos, int32_t* inputBuferPos);

public:
	static void renderOsc(OscType type, int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
	                      uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase, bool applyAmplitude,
	                      int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
	                      uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
	                      int sourceWaveIndexLastTime, WaveTable* waveTable);
};

} // namespace dsp
} // namespace deluge

#endif // DELUGE_OSCILLATOR_H
