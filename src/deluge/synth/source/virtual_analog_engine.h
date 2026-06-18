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

#include "dsp-next/core/phasor.h"           // deluge::dsp::Phasor
#include "synth/source/oscillator_source.h" // OscillatorSource (+ Source, SourceRenderContext, BlockControls)
#include "synth/source/va_shapes.h"         // the shape vocabulary + va_shape::Wave
#include <cstdint>
#include <variant>

namespace deluge::synth {

/// @brief The virtual-analog source engine: the classic non-sampled waves (SINE/TRIANGLE/SQUARE/ANALOG_SQUARE/
/// SAW/ANALOG_SAW_2). It holds the **active shape** (a properly-encapsulated stateful oscillator that owns its
/// phase — see @ref va_shape) in a `std::variant`, chosen once at construction from the OscType, and dispatches
/// per block with `std::visit`. The wave is fixed for the note's life, so there is no per-block `switch`; the
/// crude↔band-limited choice, pulse-width-as-sync, the retrigger/analog-saw-downgrade tweaks, hard sync, scale
/// and VCA all live inside each shape — the engine owns its full per-block render derivation.
///
/// The app-side adapter (`dsp/oscillators/oscillator.cpp`) hands the engine the *original* (unrewritten) phase +
/// increment and the *raw* @ref BlockControls, and reads @ref phase() back only to persist a real hard-sync's
/// advanced phase. (The engine is currently rebuilt per block by that adapter; making it persistent in Voice is
/// the following stage.)
///
/// @note Per the L1 reframe this is a **source component**, not a swappable engine — see
/// `docs/dev/synth_l1_engine_architecture.md`. The `*Engine` name predates that doc; the rename rides the
/// engine-forming slices.
class VirtualAnalogEngine : public OscillatorSource {
public:
	/// @param wave the VA wave to render — selects the variant alternative.
	/// @param seed the engine's phase state, seeded with the block's render phase + increment (UQ0.32).
	VirtualAnalogEngine(va_shape::Wave wave, deluge::dsp::Phasor seed);

	/// The engine's current oscillator phase (UQ0.32). Persists across blocks — Voice reads it to seed osc-sync
	/// from the master and to advance a skipped source.
	[[nodiscard]] uint32_t phase() const {
		return std::visit([](const auto& shape) { return shape.phase(); }, shape_);
	}

	/// The current phase increment (UQ0.32). The *original* note pitch — pulse waves rewrite a local render rate
	/// per block but never touch this.
	[[nodiscard]] uint32_t increment() const {
		return std::visit([](const auto& shape) { return shape.increment(); }, shape_);
	}

	/// Set the per-block pitch (UQ0.32). Voice calls this each block before @ref renderBlock (pitch modulation).
	void setIncrement(uint32_t increment) {
		std::visit([&](auto& shape) { shape.setIncrement(increment); }, shape_);
	}

	/// Jump the phase (e.g. a skipped source still advances, or an external reseed). Leaves the increment.
	void seek(uint32_t phase) {
		std::visit([&](auto& shape) { shape.seek(phase); }, shape_);
	}

	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch) {
		// The engine owns its phase across blocks. The shape advances/seeks its phasor only meaningfully on a real
		// hard-sync block (where it lands on the synced phase); otherwise we advance the original-domain phase by
		// the block length here — exactly the legacy "phase += increment * numSamples, real-sync overrides" rule.
		const uint32_t start = phase();
		const uint32_t inc = increment();
		std::visit([&](auto& shape) { shape.renderBlock(controls, output, scratch); }, shape_);
		if (!controls.sync) {
			seek(start + inc * static_cast<uint32_t>(output.size()));
		}
	}

	/// Render one block into @p output (sets the per-block pitch first), VCA per @p controls. The public
	/// block-render API used by @ref RingModEngine (raw render) and by @ref renderOscBlock. The engine owns its
	/// phase across blocks.
	void renderInto(const BlockControls& controls, uint32_t phaseIncrement, deluge::dsp::fixed::Buffer output,
	                deluge::dsp::fixed::Buffer scratch) {
		setIncrement(phaseIncrement);
		renderBlock(controls, output, scratch);
	}

protected:
	void renderOscBlock(const BlockControls& controls, const SourceRenderContext& ctx,
	                    deluge::dsp::fixed::Buffer out) override {
		renderInto(controls, ctx.phaseIncrement, out,
		           deluge::dsp::fixed::Buffer{ctx.syncScratch, static_cast<size_t>(ctx.numSamples)});
	}

private:
	using Shape = std::variant<va_shape::Sine, va_shape::Triangle, va_shape::Square, va_shape::Saw, va_shape::AnalogSaw,
	                           va_shape::AnalogSquare>;

	/// Build the variant alternative for @p wave (the one note-on dispatch; replaces the per-block megaswitch).
	static Shape makeShape(va_shape::Wave wave, deluge::dsp::Phasor seed);

	Shape shape_;
};

} // namespace deluge::synth
