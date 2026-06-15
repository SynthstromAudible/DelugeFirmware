/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include "synth/source/source_engine.h" // SourceEngine, BlockControls
#include "synth/source/va_shapes.h"     // the shape vocabulary + va_shape::Wave
#include <cstdint>
#include <variant>

namespace deluge::synth {

/// @brief The virtual-analog source engine: the classic non-sampled waves (SINE/TRIANGLE/SQUARE/ANALOG_SQUARE/
/// SAW/ANALOG_SAW_2). It holds the **active shape** (a properly-encapsulated stateful oscillator that owns its
/// phase — see @ref va_shape) in a `std::variant`, chosen once at construction from the OscType, and dispatches
/// per block with `std::visit`. The wave is fixed for the note's life, so there is no per-block `switch`; the
/// crude↔band-limited choice, pulse, hard sync, scale and VCA all live inside each shape.
///
/// Pulse-width-as-sync, the retrigger/analog-saw-downgrade tweaks, and phase persistence stay in the app-side
/// adapter (`dsp/oscillators/oscillator.cpp`): it hands the engine the already-finalised @ref BlockControls and
/// reads @ref phase() back. (The engine is currently rebuilt per block by that adapter; making it persistent in
/// Voice is the following stage.)
class VirtualAnalogEngine : public SourceEngine {
public:
	/// @param wave the VA wave to render — selects the variant alternative.
	/// @param seed the engine's phase state, seeded with the block's render phase + increment (UQ0.32).
	VirtualAnalogEngine(va_shape::Wave wave, deluge::dsp::Phasor seed);

	/// The phase after rendering — the adapter reads it to persist per-voice phase (meaningful on hard-sync blocks).
	[[nodiscard]] uint32_t phase() const {
		return std::visit([](const auto& shape) { return shape.phase(); }, shape_);
	}

	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch) override {
		std::visit([&](auto& shape) { shape.renderBlock(controls, output, scratch); }, shape_);
	}

private:
	using Shape = std::variant<va_shape::Sine, va_shape::Triangle, va_shape::Square, va_shape::Saw, va_shape::AnalogSaw,
	                           va_shape::AnalogSquare>;

	/// Build the variant alternative for @p wave (the one note-on dispatch; replaces the per-block megaswitch).
	static Shape makeShape(va_shape::Wave wave, deluge::dsp::Phasor seed);

	Shape shape_;
};

} // namespace deluge::synth
