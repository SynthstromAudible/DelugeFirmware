/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams, 2026 Synthstrom Audible Limited
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

#include "dsp-next/core/phasor.h"       // deluge::dsp::Phasor
#include "dsp-next/core/types.h"        // deluge::dsp::fixed::Buffer
#include "synth/source/source_engine.h" // BlockControls
#include <cstdint>

namespace deluge::synth::va_shape {

/// @brief The virtual-analog engine's wave set — closed over exactly the shapes below (unlike the app's open
/// `OscType`, which also has SAMPLE/DX7/INPUT/WAVETABLE that never reach this engine). The adapter maps the
/// post-downgrade `OscType` to one of these; the engine's factory switch is then exhaustive (`-Wswitch`).
enum class Wave : uint8_t { Sine, Triangle, Square, Saw, AnalogSaw, AnalogSquare };

/// @file
/// @brief The virtual-analog engine's shape vocabulary: one encapsulated stateful oscillator per wave. Each owns
/// its phase (a @ref deluge::dsp::Phasor) and renders a block, encapsulating that wave's crude↔band-limited
/// choice, hard sync, pulse, standalone scale, and VCA. @ref VirtualAnalogEngine holds the active shape in a
/// `std::variant` (the wave is fixed at note-on), replacing the old per-block decode + `switch` dispatch.
///
/// Shapes split two ways: the plain band-limited waves (@ref Sine, @ref AnalogSaw, @ref AnalogSquare — one
/// kernel, no fallback) and the crude↔band-limited "FallbackOscillator" waves (@ref Saw, @ref Square,
/// @ref Triangle), which pick the cheap aliasing render at low pitch / high CPU. Pitch (increment), amplitude,
/// pulse width, sync and `cpu_direness` arrive per block in @ref BlockControls; the phase persists in the shape.

/// @brief Band-limited sine — a single band covers every pitch (no aliasing, no fallback).
class Sine {
	deluge::dsp::Phasor phasor_;

public:
	explicit Sine(deluge::dsp::Phasor seed) : phasor_{seed} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	[[nodiscard]] uint32_t increment() const { return phasor_.increment(); }
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }
	void seek(uint32_t phase) { phasor_.seek(phase); }
	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch);
};

/// @brief Triangle: crude (half-scale, doubled VCA) at low pitch / dire CPU, else band-limited.
class Triangle {
	deluge::dsp::Phasor phasor_;

public:
	explicit Triangle(deluge::dsp::Phasor seed) : phasor_{seed} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	[[nodiscard]] uint32_t increment() const { return phasor_.increment(); }
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }
	void seek(uint32_t phase) { phasor_.seek(phase); }
	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch);
};

/// @brief Sawtooth: crude (phase ramp) at low table number / dire CPU, else band-limited.
class Saw {
	deluge::dsp::Phasor phasor_;

public:
	explicit Saw(deluge::dsp::Phasor seed) : phasor_{seed} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	[[nodiscard]] uint32_t increment() const { return phasor_.increment(); }
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }
	void seek(uint32_t phase) { phasor_.seek(phase); }
	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch);
};

/// @brief Square / pulse: crude switch at low table number / dire CPU; the perfect band-limited pulse (two
/// ring-modulated squares) when pulse-width is set; else the plain band-limited square.
class Square {
	deluge::dsp::Phasor phasor_;

public:
	explicit Square(deluge::dsp::Phasor seed) : phasor_{seed} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	[[nodiscard]] uint32_t increment() const { return phasor_.increment(); }
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }
	void seek(uint32_t phase) { phasor_.seek(phase); }
	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch);
};

/// @brief Analog sawtooth — band-limited, but downgrades internally to the crude digital saw at a high band
/// number when CPU is dire (matching the legacy, the downgrade keeps the digital saw's resetter tweak but not
/// its retrigger tweak).
class AnalogSaw {
	deluge::dsp::Phasor phasor_;

public:
	explicit AnalogSaw(deluge::dsp::Phasor seed) : phasor_{seed} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	[[nodiscard]] uint32_t increment() const { return phasor_.increment(); }
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }
	void seek(uint32_t phase) { phasor_.seek(phase); }
	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch);
};

/// @brief Analog square — always band-limited.
class AnalogSquare {
	deluge::dsp::Phasor phasor_;

public:
	explicit AnalogSquare(deluge::dsp::Phasor seed) : phasor_{seed} {}
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }
	[[nodiscard]] uint32_t increment() const { return phasor_.increment(); }
	void setIncrement(uint32_t increment) { phasor_.setIncrement(increment); }
	void seek(uint32_t phase) { phasor_.seek(phase); }
	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch);
};

} // namespace deluge::synth::va_shape
