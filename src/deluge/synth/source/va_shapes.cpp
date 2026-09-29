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
#include "util/fixedpoint.h"                // multiply_32x32_rshift32_rounded
#include "util/lookuptables/lookuptables.h" // sineWaveSmall
#include <cstdint>
#include <optional>

namespace deluge::synth::va_shape {
namespace {
namespace fixed = deluge::dsp::fixed;
using deluge::dsp::Phasor;
using deluge::dsp::Ramp;

// Phase is UQ0.32 — the whole uint32 range is one cycle — so these name the cycle fractions the rewrites add to
// or subtract from a phase. Using them (rather than the bare magic numbers) keeps the arithmetic byte-identical.
constexpr uint32_t kHalfCycle = 2147483648u;         // 2^31
constexpr uint32_t kThreeQuarterCycle = 3221225472u; // 3 · 2^30
constexpr uint32_t kQuarterCycle = 1073741824u;      // 2^30

// The triangle drops to its crude (aliasing) render below this phase increment — i.e. below this pitch the
// band-limited table is overkill, so the cheap ramp is used instead.
constexpr uint32_t kTriangleCrudeIncrement = 69273666u;

// The crude↔band-limited gate works off the band-ladder index: render crude once the band index falls within
// this margin of the dire-CPU threshold (a lower band needs less anti-aliasing, so aliasing is acceptable).
constexpr int32_t kQualityMargin = 6;

// Analog-saw tables 8 and up are already quite saw-shaped, so when CPU is dire those bands may be replaced by the
// cheap digital saw without sounding too different.
constexpr int32_t kAnalogSawDowngradeBand = 8;

/// How a shape's un-amplified ("standalone") output relates to the raw value its VCA consumes: band-limited /
/// pulse waves emit final-scale directly; the crude saw/square emit double (standalone >> 1); the crude triangle
/// emits half (standalone << 1).
enum class Standalone { kDirect, kHalve, kDouble };

/// @brief The per-block resolution of pulse-width + hard-sync into the values the render path actually consumes.
///
/// Mirrors the legacy preamble (`dsp/oscillators/oscillator.cpp`): under pulse-width modulation the saw/sine/
/// triangle/analog-square rewrite their render phase to a higher rate and hard-sync to their own untouched-rate
/// phase, so a "pulse" is two ramps stitched at the width. @ref render is the (possibly rewritten) phasor the
/// oscillator advances; @ref sync is the effective sync master (the real one, or the pulse-induced self-sync);
/// @ref retrigger is the per-wave-tweaked retrigger phase.
struct Resolved {
	Phasor render;
	std::optional<Phasor> sync;
	uint32_t retrigger;
};

/// Pulse-width-as-sync, "multiply" family (saw / sine / triangle, and the digital-saw fallback of analog saw):
/// the render phase/increment are scaled up so one rewritten cycle spans the pulse width, and the wave hard-syncs
/// to its own original-rate phase. @p resetter_tweak shifts that self-sync master's phase before it is used (saw
/// +half cycle, sine −3/4 cycle, triangle none); @p retrigger is added to the rewritten phase and carried to the
/// sync render.
Resolved multiplyPulseSync(Phasor original, uint32_t pulse_width, uint32_t retrigger, uint32_t resetter_tweak) {
	const uint32_t resetter_phase = original.phase() + resetter_tweak;
	const uint32_t resetter_increment = original.increment();
	const uint32_t pulse_width_abs = ((int32_t)pulse_width >= 0) ? pulse_width : -pulse_width;

	int32_t resetter_to_multiply = resetter_phase >> 1;
	if (resetter_phase >= (uint32_t)-(resetter_increment >> 1)) {
		// Count the last little bit of the cycle as actually a negative-number bit of the next one. (Subtracting
		// half a cycle in unsigned arithmetic — the int32 wraps two's-complement, as the legacy relies on.)
		resetter_to_multiply -= kHalfCycle;
	}

	const uint32_t render_phase =
	    ((uint32_t)multiply_32x32_rshift32_rounded((pulse_width_abs >> 1) + kQuarterCycle, resetter_to_multiply) << 3)
	    + retrigger;
	const uint32_t render_increment =
	    (uint32_t)multiply_32x32_rshift32_rounded((pulse_width_abs >> 1) + kQuarterCycle, resetter_increment >> 1) << 3;

	return {Phasor{render_phase, render_increment}, Phasor{resetter_phase, resetter_increment}, retrigger};
}

/// Pulse-width-as-sync, "divide" family (analog square only): the same idea, but the render rate is derived by
/// dividing the phase by the (half-)width rather than multiplying. No resetter tweak.
Resolved dividePulseSync(Phasor original, uint32_t pulse_width, uint32_t retrigger) {
	const uint32_t resetter_phase = original.phase();
	const uint32_t resetter_increment = original.increment();
	const uint32_t pulse_width_abs = ((int32_t)pulse_width >= 0) ? pulse_width : -pulse_width;

	int64_t resetter_to_divide = (uint64_t)resetter_phase << 30;
	if (resetter_phase >= (uint32_t)-(resetter_increment >> 1)) {
		resetter_to_divide -= (uint64_t)1 << 62;
	}

	const uint32_t render_phase = resetter_to_divide / (int32_t)((pulse_width_abs + kHalfCycle) >> 1) + retrigger;
	const uint32_t render_increment = ((uint64_t)resetter_increment << 31) / (pulse_width_abs + kHalfCycle);

	return {Phasor{render_phase, render_increment}, Phasor{resetter_phase, resetter_increment}, retrigger};
}

/// Resolve a "multiply-family" wave's pulse/sync: a real sync wins (no pulse rewrite); otherwise a non-zero
/// pulse-width becomes self-sync via @ref multiplyPulseSync; otherwise the original phasor renders straight.
Resolved resolveMultiply(Phasor original, const BlockControls& controls, uint32_t retrigger, uint32_t resetter_tweak) {
	if (!controls.sync && controls.pulse_width != 0) {
		return multiplyPulseSync(original, controls.pulse_width, retrigger, resetter_tweak);
	}
	return {original, controls.sync, retrigger};
}

/// out = (scratch · amplitude) + out — the dsp-next Amplitude mixer with a ramped amplitude vector. The hard-sync
/// arms render the wave into `scratch`, then mix it into `output`'s prior contents (the normal voice path).
void mixThroughVca(Ramp amplitude, fixed::Buffer output, fixed::Buffer scratch) {
	fixed::Amplitude vca{amplitude};
	vca.renderBlock(fixed::Signal{scratch}, fixed::Signal{output}, output);
}

/// Render `osc`'s raw wave, then either scale-and-mix it through its VCA (when amplitude applies) or write the
/// standalone wave (rescaled per @p scale) straight out. `make_vca` builds the VCA only when it is needed.
template <typename Osc, typename MakeVca>
void renderThroughVca(Osc& osc, MakeVca make_vca, Standalone scale, bool apply_amplitude, fixed::Buffer output,
                      fixed::Buffer scratch) {
	if (apply_amplitude) {
		osc.renderBlock(scratch);
		auto vca = make_vca();
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

/// Render a band-limited wave — a BandLimitedTableOscillator picks the band of `bands` for `selection_increment`
/// — straight or hard-synced, then apply amplitude. The sync arm crossfades the seam (SyncedOscillator) and
/// reports its advanced phase back through `phasor`.
void renderBandLimited(const fixed::BandlimitedTable& bands, uint32_t selection_increment, Ramp amplitude,
                       const std::optional<Phasor>& sync, uint32_t retrigger, bool apply_amplitude,
                       fixed::Buffer output, fixed::Buffer scratch, Phasor& phasor) {
	if (sync) {
		fixed::Buffer dst = apply_amplitude ? scratch : output;
		fixed::SyncedOscillator osc{
		    fixed::BandLimitedTableOscillator{bands, phasor.phase(), phasor.increment(), selection_increment},
		    phasor.phase(), phasor.increment(), fixed::Resetter{sync->phase(), sync->increment()}, retrigger};
		osc.renderBlock(dst);
		phasor.seek(osc.phase());
		mixThroughVca(amplitude, output, scratch);
	}
	else {
		fixed::BandLimitedTableOscillator osc{bands, phasor.phase(), phasor.increment(), selection_increment};
		renderThroughVca(
		    osc, [&] { return fixed::Amplitude{amplitude}; }, Standalone::kDirect, apply_amplitude, output, scratch);
	}
}

/// The perfect pulse: ring-modulate two band-limited squares offset by the pulse width. It runs at half the phase
/// rate (the legacy pulse trick), so a synced phase is doubled back before being reported. @p pulse_width is the
/// width already offset by half a cycle; @p band_select is the (mildly band-limited) increment that picks the band.
void renderPerfectPulse(Ramp amplitude, uint32_t pulse_width, uint32_t band_select, const std::optional<Phasor>& sync,
                        uint32_t retrigger, bool apply_amplitude, fixed::Buffer output, fixed::Buffer scratch,
                        Phasor& phasor) {
	const uint32_t phase_offset = -(pulse_width >> 1);
	const uint32_t phase = phasor.phase() >> 1;
	const uint32_t increment = phasor.increment() >> 1;
	fixed::PulseOscillator pulse{fixed::square_bandlimited_table().select(band_select), phase, increment, phase_offset};
	if (sync) {
		fixed::Buffer dst = apply_amplitude ? scratch : output;
		fixed::SyncedOscillator osc{pulse, phase, increment, fixed::Resetter{sync->phase(), sync->increment()},
		                            retrigger};
		osc.renderBlock(dst);
		phasor.seek(osc.phase() << 1);
		mixThroughVca(amplitude, output, scratch);
	}
	else if (apply_amplitude) {
		pulse.renderBlock(scratch);
		mixThroughVca(amplitude, output, scratch);
	}
	else {
		pulse.renderBlock(output);
	}
}

/// The crude↔band-limited gate for saw/square: drop to the cheap aliasing render once the band index for
/// @p selection_increment is within @ref kQualityMargin of the dire-CPU threshold.
[[nodiscard]] bool useCrude(uint32_t selection_increment, int32_t cpu_direness) {
	return fixed::getTableNumber(selection_increment).first < cpu_direness + kQualityMargin;
}

} // namespace

void Sine::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	// Sine has no aliasing — a single band (the small sine table) covers every pitch.
	static const fixed::Band kSineBand[] = {{sineWaveSmall, 8, 0xFFFFFFFFu}};
	static const fixed::BandlimitedTable kSineTable{kSineBand};
	// Retrigger shifts a quarter-cycle past half (3/4 cycle); its pulse-as-sync self-sync master shifts back 3/4.
	const uint32_t retrigger = controls.retrigger_phase + kThreeQuarterCycle;
	Resolved r = resolveMultiply(phasor_, controls, retrigger, 0u - kThreeQuarterCycle);
	renderBandLimited(kSineTable, r.render.increment(), controls.amplitude, r.sync, r.retrigger,
	                  controls.apply_amplitude, output, scratch, r.render);
	phasor_.seek(r.render.phase());
}

void Triangle::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	// Triangle takes no retrigger or self-sync resetter tweak.
	Resolved r = resolveMultiply(phasor_, controls, controls.retrigger_phase, 0u);
	if (r.render.increment() < kTriangleCrudeIncrement || controls.cpu_direness >= 7) {
		// Crude triangle: half-scale fed to a doubled-amplitude VCA, so the standalone output is doubled back.
		auto make_vca = [&] { return fixed::CrudeAmplitude{controls.amplitude << 1}; };
		if (r.sync) {
			fixed::CrudeTriangle osc{fixed::SyncedPhasor{r.render, *r.sync, r.retrigger}};
			renderThroughVca(osc, make_vca, Standalone::kDouble, controls.apply_amplitude, output, scratch);
			phasor_.seek(osc.phase());
		}
		else {
			fixed::CrudeTriangle osc{r.render};
			renderThroughVca(osc, make_vca, Standalone::kDouble, controls.apply_amplitude, output, scratch);
		}
		return;
	}
	renderBandLimited(fixed::triangle_bandlimited_table(), r.render.increment(), controls.amplitude, r.sync,
	                  r.retrigger, controls.apply_amplitude, output, scratch, r.render);
	phasor_.seek(r.render.phase());
}

void Saw::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	// Retrigger shifts half a cycle; its pulse-as-sync self-sync master shifts the same.
	const uint32_t retrigger = controls.retrigger_phase + kHalfCycle;
	Resolved r = resolveMultiply(phasor_, controls, retrigger, kHalfCycle);
	// The crude/band-limited choice (and band selection) is on the *original* pitch, not the rewritten pulse rate.
	if (useCrude(phasor_.increment(), controls.cpu_direness)) {
		// Crude saw: the phase ramp itself (full scale), so the standalone output is halved.
		auto make_vca = [&] { return fixed::CrudeAmplitude{controls.amplitude}; };
		if (r.sync) {
			fixed::CrudeSaw osc{fixed::SyncedPhasor{r.render, *r.sync, r.retrigger}};
			renderThroughVca(osc, make_vca, Standalone::kHalve, controls.apply_amplitude, output, scratch);
			phasor_.seek(osc.phase());
		}
		else {
			fixed::CrudeSaw osc{r.render};
			renderThroughVca(osc, make_vca, Standalone::kHalve, controls.apply_amplitude, output, scratch);
		}
		return;
	}
	renderBandLimited(fixed::saw_bandlimited_table(), phasor_.increment(), controls.amplitude << 1, r.sync, r.retrigger,
	                  controls.apply_amplitude, output, scratch, r.render);
	phasor_.seek(r.render.phase());
}

void Square::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	// The square's pulse is the *perfect* pulse (not pulse-as-sync), so its render phase is never rewritten — only
	// a real sync resets it. The width is offset half a cycle, and the band is mildly limited (×0.6) when pulsing.
	const bool pulse_wave = (controls.pulse_width != 0);
	const uint32_t square_pulse_width = controls.pulse_width + kHalfCycle;
	const uint32_t band_select = pulse_wave ? (uint32_t)(phasor_.increment() * 0.6) : phasor_.increment();

	if (useCrude(band_select, controls.cpu_direness)) {
		// Crude square: full-scale, so the standalone output is halved.
		auto make_vca = [&] { return fixed::CrudeAmplitude{controls.amplitude}; };
		if (controls.sync) {
			fixed::CrudeSquare osc{fixed::SyncedPhasor{phasor_, *controls.sync, controls.retrigger_phase},
			                       square_pulse_width};
			renderThroughVca(osc, make_vca, Standalone::kHalve, controls.apply_amplitude, output, scratch);
			phasor_.seek(osc.phase());
		}
		else {
			fixed::CrudeSquare osc{phasor_, square_pulse_width};
			renderThroughVca(osc, make_vca, Standalone::kHalve, controls.apply_amplitude, output, scratch);
		}
		return;
	}
	if (pulse_wave) {
		renderPerfectPulse(controls.amplitude << 1, square_pulse_width, band_select, controls.sync,
		                   controls.retrigger_phase, controls.apply_amplitude, output, scratch, phasor_);
		return;
	}
	renderBandLimited(fixed::square_bandlimited_table(), phasor_.increment(), controls.amplitude << 1, controls.sync,
	                  controls.retrigger_phase, controls.apply_amplitude, output, scratch, phasor_);
}

void AnalogSaw::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	// At a high band number on a dire CPU, downgrade to the crude digital saw. The downgrade keeps the digital
	// saw's self-sync resetter tweak (+half cycle) but — matching the legacy — *not* its retrigger tweak (the
	// analog saw never gets that). The decision is on the original pitch.
	const int32_t table_number = fixed::getTableNumber(phasor_.increment()).first;
	const bool downgrade =
	    (table_number >= kAnalogSawDowngradeBand && table_number < controls.cpu_direness + kQualityMargin);
	if (downgrade) {
		Resolved r = resolveMultiply(phasor_, controls, controls.retrigger_phase, kHalfCycle);
		auto make_vca = [&] { return fixed::CrudeAmplitude{controls.amplitude}; };
		if (r.sync) {
			fixed::CrudeSaw osc{fixed::SyncedPhasor{r.render, *r.sync, r.retrigger}};
			renderThroughVca(osc, make_vca, Standalone::kHalve, controls.apply_amplitude, output, scratch);
			phasor_.seek(osc.phase());
		}
		else {
			fixed::CrudeSaw osc{r.render};
			renderThroughVca(osc, make_vca, Standalone::kHalve, controls.apply_amplitude, output, scratch);
		}
		return;
	}
	// Genuine analog saw: band-limited, no resetter or retrigger tweak.
	Resolved r = resolveMultiply(phasor_, controls, controls.retrigger_phase, 0u);
	renderBandLimited(fixed::analog_saw_bandlimited_table(), phasor_.increment(), controls.amplitude << 1, r.sync,
	                  r.retrigger, controls.apply_amplitude, output, scratch, r.render);
	phasor_.seek(r.render.phase());
}

void AnalogSquare::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	// Analog square's pulse-as-sync uses the "divide" rewrite; no retrigger or resetter tweak.
	Resolved r = (controls.sync || controls.pulse_width == 0)
	                 ? Resolved{phasor_, controls.sync, controls.retrigger_phase}
	                 : dividePulseSync(phasor_, controls.pulse_width, controls.retrigger_phase);
	renderBandLimited(fixed::analog_square_bandlimited_table(), phasor_.increment(), controls.amplitude << 1, r.sync,
	                  r.retrigger, controls.apply_amplitude, output, scratch, r.render);
	phasor_.seek(r.render.phase());
}

} // namespace deluge::synth::va_shape
