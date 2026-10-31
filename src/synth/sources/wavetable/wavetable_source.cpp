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
#include "synth/sources/wavetable/wavetable_source.h"
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32_rounded
#include "dsp-next/fixed/vca/amplitude.h"

namespace deluge::synth {

void WavetableSource::render_block(const BlockControls& controls, deluge::dsp::fixed::Buffer output,
                                   deluge::dsp::fixed::Buffer scratch) {
	const uint32_t orig_phase = phasor_.phase();
	const uint32_t orig_inc = phasor_.increment();

	// Pulse-width-as-sync preamble (relocated verbatim from the legacy renderOsc WAVETABLE body). Wavetable is
	// never SINE/SAW/SQUARE, so it takes none of the per-wave retrigger/resetter tweaks — only the plain multiply
	// rewrite — and a real osc sync wins over pulse.
	const bool real_sync = controls.sync.has_value();
	bool do_osc_sync = real_sync;
	const bool do_pulse_wave = (controls.pulse_width != 0 && !real_sync);
	uint32_t render_phase = orig_phase;
	uint32_t render_inc = orig_inc;
	uint32_t resetter_phase = real_sync ? controls.sync->phase() : 0;
	uint32_t resetter_inc = real_sync ? controls.sync->increment() : 0;

	if (do_pulse_wave) {
		do_osc_sync = true;
		const uint32_t pulse_width_absolute =
		    (static_cast<int32_t>(controls.pulse_width) >= 0) ? controls.pulse_width : -controls.pulse_width;
		resetter_phase = orig_phase;
		resetter_inc = orig_inc;

		auto resetter_phase_to_multiply = static_cast<int32_t>(resetter_phase >> 1);
		if (resetter_phase >= -(resetter_inc >> 1)) {
			// Count the last little bit of the cycle as actually a negative-number bit of the next one: half a cycle
			// subtracted in unsigned arithmetic, the int32 wrapping two's-complement.
			resetter_phase_to_multiply =
			    static_cast<int32_t>(static_cast<uint32_t>(resetter_phase_to_multiply) - (uint32_t{1} << 31));
		}

		const auto half_width = static_cast<q31_t>((pulse_width_absolute >> 1) + 1073741824);
		render_phase =
		    (static_cast<uint32_t>(multiply_32x32_rshift32_rounded(half_width, resetter_phase_to_multiply)) << 3)
		    + controls.retrigger_phase;
		render_inc =
		    static_cast<uint32_t>(multiply_32x32_rshift32_rounded(half_width, static_cast<q31_t>(orig_inc >> 1))) << 3;
	}

	const int32_t wave_index = wave_index_last_ + 1073741824;
	int32_t* dst = controls.apply_amplitude ? scratch.data() : output.data();

	// Render through the dsp-next view-based oscillator — the same non-sync/sync dispatch WaveTable::render does,
	// now driven directly over our band-data view (no storage reach).
	uint32_t rendered_phase = 0;
	if (!do_osc_sync) {
		deluge::dsp::fixed::WavetableOscillator osc{table_, render_phase, render_inc, wave_index,
		                                            wave_index_increment_};
		osc.render_block(deluge::dsp::fixed::Buffer{dst, output.size()});
		rendered_phase = osc.phase();
	}
	else {
		deluge::dsp::fixed::WavetableSyncOscillator osc{
		    table_,
		    render_phase,
		    render_inc,
		    deluge::dsp::fixed::Resetter{.phase = resetter_phase, .phase_increment = resetter_inc},
		    controls.retrigger_phase,
		    wave_index,
		    wave_index_increment_};
		osc.render_block(deluge::dsp::fixed::Buffer{dst, output.size()});
		rendered_phase = osc.phase();
	}

	if (controls.apply_amplitude) {
		// The wavetable's amplitude is pre-shifted <<3 (as the legacy renderOsc did); out = scratch*amp + out.
		deluge::dsp::fixed::Amplitude vca{controls.amplitude << 3};
		vca.render_block(deluge::dsp::fixed::Signal{scratch}, deluge::dsp::fixed::Signal{output}, output);
	}

	// Phase persistence — the legacy maybeStorePhase rule, now internal: a pulse keeps the original-domain advance
	// (the rewritten render phase is discarded), otherwise the rendered (possibly hard-synced) phase persists.
	if (do_pulse_wave) {
		phasor_.seek(orig_phase + orig_inc * static_cast<uint32_t>(output.size()));
	}
	else {
		phasor_.seek(rendered_phase);
	}
}

} // namespace deluge::synth
