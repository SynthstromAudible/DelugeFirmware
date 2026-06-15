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

#include "definitions_cxx.hpp"          // OscType
#include "dsp-next/core/phasor.h"       // deluge::dsp::Phasor
#include "synth/source/source_engine.h" // SourceEngine, BlockControls
#include <cstdint>

namespace deluge::synth {

/// @brief The virtual-analog source engine: the classic non-sampled oscillator waves — SINE, TRIANGLE, SQUARE,
/// ANALOG_SQUARE, SAW, ANALOG_SAW_2. It owns its phase (a @ref deluge::dsp::Phasor) and *composes* the dsp-next
/// oscillator / VCA / hard-sync objects, picking the crude (cheap, aliasing) vs band-limited rendering per block
/// from the supplied pitch + @ref BlockControls::cpu_direness. This is the first concrete @ref SourceEngine —
/// the VA arm of the legacy `OscType` megaswitch, recomposed as an object.
///
/// Pulse-width-as-sync, the retrigger/analog-saw-downgrade tweaks, and phase persistence stay in the app-side
/// adapter (`dsp/oscillators/oscillator.cpp`): they are *control derivation* (the adapter hands the engine the
/// already-finalised phase/increment/sync/quality) and they are shared with the WAVETABLE path. The engine reads
/// its post-render phase back via @ref phase() (the adapter persists it, exactly as the legacy code did).
class VirtualAnalogEngine : public SourceEngine {
public:
	/// @param wave the (post-downgrade) VA wave to render.
	/// @param phasor the engine's phase state, seeded with the block's render phase + increment (UQ0.32).
	VirtualAnalogEngine(OscType wave, deluge::dsp::Phasor phasor) : wave_{wave}, phasor_{phasor} {}

	/// The phase after rendering — meaningful on hard-sync blocks (where the synced oscillator advances it);
	/// the adapter reads it to persist per-voice phase.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
	                 deluge::dsp::fixed::Buffer scratch) override;

private:
	OscType wave_;
	deluge::dsp::Phasor phasor_;
};

} // namespace deluge::synth
