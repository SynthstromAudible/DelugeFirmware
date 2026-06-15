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
#include "synth/source/va_shapes.h"
#include "dsp-next/core/ramp.h"
#include "dsp-next/fixed/bandlimited/analog_saw.h"
#include "dsp-next/fixed/bandlimited/analog_square.h"
#include "dsp-next/fixed/bandlimited/band_limited_table.h"
#include "dsp-next/fixed/bandlimited/saw.h"
#include "dsp-next/fixed/bandlimited/square.h"
#include "dsp-next/fixed/bandlimited/triangle.h"
#include "dsp-next/fixed/osc/band_limited_table_oscillator.h"
#include "dsp-next/fixed/osc/crude_oscillators.h"
#include "dsp-next/fixed/osc/pulse_oscillator.h"
#include "dsp-next/fixed/sync/resetter.h"
#include "dsp-next/fixed/sync/synced_oscillator.h"
#include "dsp-next/fixed/sync/synced_phasor.h"
#include "dsp-next/fixed/vca/amplitude.h"
#include "dsp-next/fixed/vca/crude_amplitude.h"
#include "util/lookuptables/lookuptables.h" // sineWaveSmall
#include <cstdint>

namespace deluge::synth::va_shape {
namespace {
namespace fixed = deluge::dsp::fixed;
using deluge::dsp::Phasor;
using deluge::dsp::Ramp;

/// How a shape's un-amplified ("standalone") output relates to the raw value its VCA consumes: band-limited /
/// pulse waves emit final-scale directly; the crude saw/square emit double (standalone >> 1); the crude triangle
/// emits half (standalone << 1).
enum class Standalone { kDirect, kHalve, kDouble };

/// out = (scratch * amplitude) + out — the dsp-next Amplitude mixer with a ramped amplitude vector. The hard-sync
/// arms fill the sync scratch then mix it into the output (`output` is both the mix-in and the destination).
void mixThroughVca(Ramp amplitude, fixed::Buffer output, fixed::Buffer scratch) {
	fixed::Amplitude vca{amplitude};
	vca.renderBlock(fixed::Signal{scratch}, fixed::Signal{output}, output);
}

/// Fill `osc`'s raw wave, then either scale-and-mix it through its VCA (when amplitude applies) or write the
/// standalone wave (scaled) straight out. `makeVca` builds the VCA only when needed.
template <typename Osc, typename MakeVca>
void renderThroughVca(Osc& osc, MakeVca makeVca, Standalone scale, const BlockControls& controls, fixed::Buffer output,
                      fixed::Buffer scratch) {
	if (controls.apply_amplitude) {
		osc.renderBlock(scratch);
		auto vca = makeVca();
		vca.renderBlock(fixed::Signal{scratch}, fixed::Signal{output}, output);
	}
	else {
		osc.renderBlock(output);
		if (scale == Standalone::kHalve) {
			for (auto& sample : output) {
				sample >>= 1;
			}
		}
		else if (scale == Standalone::kDouble) {
			for (auto& sample : output) {
				sample <<= 1;
			}
		}
	}
}

/// Render a band-limited wave — a BandLimitedTableOscillator picks the right band of `bands` for `selectionIncr`
/// — straight or with osc sync, then apply amplitude. The sync arm crossfades the seam (SyncedOscillator) and
/// reports its advanced phase back to `phasor`.
void renderBandLimited(const fixed::BandlimitedTable& bands, uint32_t selectionIncr, Ramp amplitude,
                       const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch, Phasor& phasor) {
	if (controls.sync) {
		fixed::Buffer dst = controls.apply_amplitude ? scratch : output;
		fixed::SyncedOscillator osc{
		    fixed::BandLimitedTableOscillator{bands, phasor.phase(), phasor.increment(), selectionIncr}, phasor.phase(),
		    phasor.increment(), fixed::Resetter{controls.sync->phase(), controls.sync->increment()},
		    controls.retrigger_phase};
		osc.renderBlock(dst);
		phasor.seek(osc.phase());
		mixThroughVca(amplitude, output, scratch);
	}
	else {
		fixed::BandLimitedTableOscillator osc{bands, phasor.phase(), phasor.increment(), selectionIncr};
		renderThroughVca(
		    osc, [&] { return fixed::Amplitude{amplitude}; }, Standalone::kDirect, controls, output, scratch);
	}
}

/// The perfect pulse: ring-modulate two band-limited squares offset by the pulse width. Renders at half the phase
/// rate (the legacy pulse trick), so the synced phase is doubled back before it is stored.
void renderPerfectPulse(Ramp amplitude, const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch,
                        Phasor& phasor) {
	const uint32_t phaseToAdd = -(controls.pulse_width >> 1);
	const uint32_t phase = phasor.phase() >> 1;
	const uint32_t increment = phasor.increment() >> 1;
	fixed::PulseOscillator pulse{fixed::square_bandlimited_table().select(controls.bandlimit_increment), phase,
	                             increment, phaseToAdd};
	if (controls.sync) {
		fixed::Buffer dst = controls.apply_amplitude ? scratch : output;
		fixed::SyncedOscillator osc{pulse, phase, increment,
		                            fixed::Resetter{controls.sync->phase(), controls.sync->increment()},
		                            controls.retrigger_phase};
		osc.renderBlock(dst);
		phasor.seek(osc.phase() << 1);
		mixThroughVca(amplitude, output, scratch);
	}
	else if (controls.apply_amplitude) {
		pulse.renderBlock(scratch);
		mixThroughVca(amplitude, output, scratch);
	}
	else {
		pulse.renderBlock(output);
	}
}

/// The crude↔band-limited threshold for saw/square: the band-index ladder, dropping to the cheap aliasing render
/// when the band is low enough that aliasing is acceptable or CPU is dire.
[[nodiscard]] bool useCrude(const BlockControls& controls) {
	return fixed::getTableNumber(controls.bandlimit_increment).first < controls.cpu_direness + 6;
}

} // namespace

void Sine::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	// Sine has no aliasing — a single band (the small sine table) covers every pitch.
	static const fixed::Band kSineBand[] = {{sineWaveSmall, 8, 0xFFFFFFFFu}};
	static const fixed::BandlimitedTable kSineTable{kSineBand};
	renderBandLimited(kSineTable, phasor_.increment(), controls.amplitude, controls, output, scratch, phasor_);
}

void Triangle::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	if (phasor_.increment() < 69273666 || controls.cpu_direness >= 7) {
		// Crude triangle: half-scale fed to a doubled-amplitude VCA, so the standalone output is << 1.
		auto makeVca = [&] { return fixed::CrudeAmplitude{controls.amplitude << 1}; };
		if (controls.sync) {
			fixed::CrudeTriangle osc{fixed::SyncedPhasor{phasor_, *controls.sync, controls.retrigger_phase}};
			renderThroughVca(osc, makeVca, Standalone::kDouble, controls, output, scratch);
			phasor_.seek(osc.phase());
		}
		else {
			fixed::CrudeTriangle osc{phasor_};
			renderThroughVca(osc, makeVca, Standalone::kDouble, controls, output, scratch);
		}
		return;
	}
	renderBandLimited(fixed::triangle_bandlimited_table(), phasor_.increment(), controls.amplitude, controls, output,
	                  scratch, phasor_);
}

void Saw::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	if (useCrude(controls)) {
		// Crude saw: the phase ramp itself (full scale), so the standalone output is >> 1.
		auto makeVca = [&] { return fixed::CrudeAmplitude{controls.amplitude}; };
		if (controls.sync) {
			fixed::CrudeSaw osc{fixed::SyncedPhasor{phasor_, *controls.sync, controls.retrigger_phase}};
			renderThroughVca(osc, makeVca, Standalone::kHalve, controls, output, scratch);
			phasor_.seek(osc.phase());
		}
		else {
			fixed::CrudeSaw osc{phasor_};
			renderThroughVca(osc, makeVca, Standalone::kHalve, controls, output, scratch);
		}
		return;
	}
	renderBandLimited(fixed::saw_bandlimited_table(), controls.bandlimit_increment, controls.amplitude << 1, controls,
	                  output, scratch, phasor_);
}

void Square::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	if (useCrude(controls)) {
		// Crude square: full-scale, so the standalone output is >> 1 (half scale).
		auto makeVca = [&] { return fixed::CrudeAmplitude{controls.amplitude}; };
		if (controls.sync) {
			fixed::CrudeSquare osc{fixed::SyncedPhasor{phasor_, *controls.sync, controls.retrigger_phase},
			                       controls.pulse_width};
			renderThroughVca(osc, makeVca, Standalone::kHalve, controls, output, scratch);
			phasor_.seek(osc.phase());
		}
		else {
			fixed::CrudeSquare osc{phasor_, controls.pulse_width};
			renderThroughVca(osc, makeVca, Standalone::kHalve, controls, output, scratch);
		}
		return;
	}
	// The perfect mathematical pulse has its own ring-modulation routine; the plain square is band-limited.
	if (controls.pulse_wave) {
		renderPerfectPulse(controls.amplitude << 1, controls, output, scratch, phasor_);
		return;
	}
	renderBandLimited(fixed::square_bandlimited_table(), controls.bandlimit_increment, controls.amplitude << 1,
	                  controls, output, scratch, phasor_);
}

void AnalogSaw::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	renderBandLimited(fixed::analog_saw_bandlimited_table(), controls.bandlimit_increment, controls.amplitude << 1,
	                  controls, output, scratch, phasor_);
}

void AnalogSquare::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	renderBandLimited(fixed::analog_square_bandlimited_table(), controls.bandlimit_increment, controls.amplitude << 1,
	                  controls, output, scratch, phasor_);
}

} // namespace deluge::synth::va_shape
