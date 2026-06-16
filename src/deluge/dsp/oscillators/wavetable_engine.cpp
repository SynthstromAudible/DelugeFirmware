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
#include "wavetable_engine.h"
#include "dsp-next/fixed/vca/amplitude.h"
#include "util/fixedpoint.h" // multiply_32x32_rshift32_rounded

namespace deluge::dsp {

void WavetableEngine::renderBlock(const synth::BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	const uint32_t origPhase = phasor_.phase();
	const uint32_t origInc = phasor_.increment();

	// Pulse-width-as-sync preamble (relocated verbatim from the legacy renderOsc WAVETABLE body). Wavetable is
	// never SINE/SAW/SQUARE, so it takes none of the per-wave retrigger/resetter tweaks — only the plain multiply
	// rewrite — and a real osc sync wins over pulse.
	const bool realSync = controls.sync.has_value();
	bool doOscSync = realSync;
	const bool doPulseWave = (controls.pulse_width != 0 && !realSync);
	uint32_t renderPhase = origPhase;
	uint32_t renderInc = origInc;
	uint32_t resetterPhase = realSync ? controls.sync->phase() : 0;
	uint32_t resetterInc = realSync ? controls.sync->increment() : 0;

	if (doPulseWave) {
		doOscSync = true;
		const uint32_t pulseWidthAbsolute =
		    ((int32_t)controls.pulse_width >= 0) ? controls.pulse_width : -controls.pulse_width;
		resetterPhase = origPhase;
		resetterInc = origInc;

		int32_t resetterPhaseToMultiply = resetterPhase >> 1;
		if (resetterPhase >= (uint32_t)-(resetterInc >> 1)) {
			resetterPhaseToMultiply -= ((uint32_t)1 << 31); // Count the last little bit of the cycle as actually a
			                                                // negative-number bit of the next one.
		}

		renderPhase =
		    ((uint32_t)multiply_32x32_rshift32_rounded((pulseWidthAbsolute >> 1) + 1073741824, resetterPhaseToMultiply)
		     << 3)
		    + controls.retrigger_phase;
		renderInc = (uint32_t)multiply_32x32_rshift32_rounded((pulseWidthAbsolute >> 1) + 1073741824, origInc >> 1)
		            << 3;
	}

	const int32_t waveIndex = wave_index_last_ + 1073741824;
	int32_t* dst = controls.apply_amplitude ? scratch.data() : output.data();

	WavetableOscillator osc{table_,
	                        Phasor{renderPhase, renderInc},
	                        doOscSync,
	                        Phasor{resetterPhase, resetterInc},
	                        controls.retrigger_phase,
	                        waveIndex,
	                        wave_index_increment_};
	osc.renderBlock(fixed::Buffer{dst, output.size()});

	if (controls.apply_amplitude) {
		// The wavetable's amplitude is pre-shifted <<3 (as the legacy renderOsc did); out = scratch*amp + out.
		fixed::Amplitude vca{controls.amplitude << 3};
		vca.renderBlock(fixed::Signal{scratch}, fixed::Signal{output}, output);
	}

	// Phase persistence — the legacy maybeStorePhase rule, now internal: a pulse keeps the original-domain advance
	// (the rewritten render phase is discarded), otherwise the rendered (possibly hard-synced) phase persists.
	if (doPulseWave) {
		phasor_.seek(origPhase + origInc * static_cast<uint32_t>(output.size()));
	}
	else {
		phasor_.seek(osc.phase());
	}
}

} // namespace deluge::dsp
