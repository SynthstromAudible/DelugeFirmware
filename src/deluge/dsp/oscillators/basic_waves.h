//
// Created by Mark Adams on 2025-01-08.
//

#ifndef DELUGE_BASIC_WAVES_H
#define DELUGE_BASIC_WAVES_H

namespace deluge::dsp {
void renderWave(const int16_t* __restrict__ table, int32_t tableSizeMagnitude, int32_t amplitude,
                int32_t* __restrict__ outputBuffer, int32_t* bufferEnd, uint32_t phaseIncrement, uint32_t phase,
                bool applyAmplitude, uint32_t phaseToAdd, int32_t amplitudeIncrement);
void renderPulseWave(const int16_t* __restrict__ table, int32_t tableSizeMagnitude, int32_t amplitude,
                     int32_t* __restrict__ outputBuffer, int32_t* bufferEnd, uint32_t phaseIncrement, uint32_t phase,
                     bool applyAmplitude, uint32_t phaseToAdd, int32_t amplitudeIncrement);
uint32_t renderCrudeSawWaveWithAmplitude(int32_t* thisSample, int32_t* bufferEnd, uint32_t phaseNowNow,
                                         uint32_t phaseIncrementNow, int32_t amplitudeNow, int32_t amplitudeIncrement,
                                         int32_t numSamples);
uint32_t renderCrudeSawWaveWithoutAmplitude(int32_t* thisSample, int32_t* bufferEnd, uint32_t phaseNowNow,
                                            uint32_t phaseIncrementNow, int32_t numSamples);
/**
 * @brief Get a table number and size, depending on the increment
 *
 * @return table_number, table_size
 */
std::pair<int32_t, int32_t> getTableNumber(uint32_t phaseIncrement);
extern const int16_t* sawTables[20];
extern const int16_t* squareTables[20];
extern const int16_t* analogSquareTables[20];
extern const int16_t* analogSawTables[20];

} // namespace deluge::dsp

#endif // DELUGE_BASIC_WAVES_H
