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
#include "dsp-next/fixed/vca/amplitude.h"
#include "synth/sources/va/va_shapes.h" // multiply_pulse_rewrite

namespace deluge::synth {

void WavetableSource::render_block(const BlockControls& controls, std::span<dsp::Q<31>> output,
                                   std::span<dsp::Q<31>> scratch) {
	// Raw: the pulse rewrite works on the phases' words
	const uint32_t orig_phase = phasor_.phase().raw();
	const uint32_t orig_inc = phasor_.increment().raw();

	// Pulse-width-as-sync preamble (relocated verbatim from the legacy renderOsc WAVETABLE body). Wavetable is
	// never SINE/SAW/SQUARE, so it takes none of the per-wave retrigger/resetter tweaks — only the plain multiply
	// rewrite — and a real osc sync wins over pulse.
	const bool real_sync = controls.sync.has_value();
	bool do_osc_sync = real_sync;
	const bool do_pulse_wave = (controls.pulse_width != 0 && !real_sync);
	uint32_t render_phase = orig_phase;
	uint32_t render_inc = orig_inc;
	uint32_t resetter_phase = real_sync ? controls.sync->phase().raw() : 0;
	uint32_t resetter_inc = real_sync ? controls.sync->increment().raw() : 0;

	if (do_pulse_wave) {
		do_osc_sync = true;
		resetter_phase = orig_phase;
		resetter_inc = orig_inc;
		const va_shape::PulseRewrite rewrite =
		    va_shape::multiply_pulse_rewrite(controls.pulse_width, resetter_phase, resetter_inc);
		render_phase = rewrite.phase + controls.retrigger_phase;
		render_inc = rewrite.increment;
	}

	const int32_t wave_index = wave_index_last_ + 1073741824;
	dsp::Q<31>* dst = controls.apply_amplitude ? scratch.data() : output.data();

	// Render through the dsp-next view-based oscillator — the same non-sync/sync dispatch WaveTable::render does,
	// now driven directly over our band-data view (no storage reach).
	dsp::UQ<32> rendered_phase{};
	if (!do_osc_sync) {
		deluge::dsp::fixed::WavetableOscillator osc{table_, dsp::UQ<32>::from_raw(render_phase),
		                                            dsp::UQ<32>::from_raw(render_inc), wave_index,
		                                            wave_index_increment_};
		osc.render_block(std::span<dsp::Q<31>>{dst, output.size()});
		rendered_phase = osc.phase();
	}
	else {
		deluge::dsp::fixed::WavetableSyncOscillator osc{
		    table_,
		    dsp::UQ<32>::from_raw(render_phase),
		    dsp::UQ<32>::from_raw(render_inc),
		    deluge::dsp::fixed::Resetter{.phase = dsp::UQ<32>::from_raw(resetter_phase),
		                                 .phase_increment = dsp::UQ<32>::from_raw(resetter_inc)},
		    dsp::UQ<32>::from_raw(controls.retrigger_phase),
		    wave_index,
		    wave_index_increment_};
		osc.render_block(std::span<dsp::Q<31>>{dst, output.size()});
		rendered_phase = osc.phase();
	}

	if (controls.apply_amplitude) {
		// The wavetable's amplitude is pre-shifted <<3 (as the legacy renderOsc did); out = scratch*amp + out.
		deluge::dsp::fixed::Amplitude vca{scale_shl_wrap<3>(controls.amplitude)};
		vca.render_block(scratch, output, output);
	}

	// Phase persistence — the legacy maybeStorePhase rule, now internal: a pulse keeps the original-domain advance
	// (the rewritten render phase is discarded), otherwise the rendered (possibly hard-synced) phase persists.
	if (do_pulse_wave) {
		seek(dsp::advanced(phasor_.phase(), phasor_.increment(), static_cast<uint32_t>(output.size())));
	}
	else {
		seek(rendered_phase);
	}
}

} // namespace deluge::synth
