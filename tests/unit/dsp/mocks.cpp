/*
 * Firmware globals the oscillator kernel reads, stood up for the host driver.
 *
 * Deliberately minimal: the specs here exercise renderOsc's arithmetic, not the
 * engine around it. Anything that needs real behaviour should link the real
 * translation unit instead of growing this file.
 */
#include "storage/wave_table/wave_table.h"
#include <cstdint>

namespace AudioEngine {
/// CPU-load hint. renderOsc only reads it to decide whether to downgrade
/// ANALOG_SAW_2 to the crude aliasing saw; 0 = "not dire", the normal case.
int32_t cpuDireness = 0;
} // namespace AudioEngine

/// Only reached for OscType::WAVETABLE, which no spec here renders (they pass a
/// null WaveTable). Defined so the driver links; traps rather than pretending to
/// render, so a future wavetable spec fails loudly instead of silently passing.
uint32_t WaveTable::render(int32_t* outputBuffer, int32_t numSamples, uint32_t phaseIncrementNow, uint32_t phase,
                           bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
                           int32_t resetterDivideByPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
                           int32_t waveIndexLastTime) {
	__builtin_trap();
}
