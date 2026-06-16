/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams
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

#include "definitions_cxx.hpp"      // OscType
#include "synth/source/va_shapes.h" // va_shape::Wave
#include <cstdint>
namespace deluge::dsp {

/// Shared osc-sync / hard-sync scratch buffer (defined in oscillator.cpp). Reused as the per-source engines'
/// per-block scratch when Voice drives them directly.
extern int32_t oscSyncRenderingBuffer[];

/// Whether @p type is one of the six virtual-analog waves rendered by @ref synth::VirtualAnalogEngine (the first
/// OscType entries, up to ANALOG_SAW_2). WAVETABLE/SAMPLE/DX7/INPUT_* are not. This is the app-side boundary
/// predicate that decides whether a source's phase lives in the engine or in @c oscPos.
[[nodiscard]] bool isVirtualAnalog(OscType type);

/// The app's open OscType → the VA engine's closed wave set. Only valid when @ref isVirtualAnalog (else SINE).
[[nodiscard]] synth::va_shape::Wave vaWaveFor(OscType type);

} // namespace deluge::dsp
