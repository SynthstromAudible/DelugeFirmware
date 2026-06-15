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
#include "synth/source/virtual_analog_engine.h"
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
#include <optional>

namespace deluge::synth {
namespace {
namespace fixed = deluge::dsp::fixed;
using deluge::dsp::Phasor;
using deluge::dsp::Ramp;

/// How an oscillator's un-amplified ("standalone") output relates to the raw value its VCA consumes:
/// band-limited / pulse waves emit final-scale directly; the crude saw/square emit double (standalone >> 1);
/// the crude triangle emits half (standalone << 1).
enum class Standalone { kDirect, kHalve, kDouble };

/// The per-block working state: the @ref BlockControls already-modulated values, decoded alongside the output /
/// scratch spans. The render phase/increment is *not* here — it is the engine's @ref deluge::dsp::Phasor, read
/// (and, on sync, advanced) directly. `sync`, when present, is the hard-sync master's phasor.
struct Block {
	fixed::Buffer output;
	fixed::Buffer scratch;
	Ramp amplitude;
	bool applyAmplitude;
	uint32_t pulseWidth;
	bool pulseWave;
	uint32_t retriggerPhase;
	std::optional<Phasor> sync;
	uint32_t bandlimitIncrement; ///< pitch that picks the anti-aliasing band / crude↔BL choice
	int32_t tableNumber;         ///< band index for that pitch (crude when < cpuDireness + 6)
	int32_t cpuDireness;
};

/// out = (scratch * amplitude) + out — the dsp-next Amplitude mixer with a ramped amplitude vector. The hard-sync
/// arms fill the sync scratch then mix it into the output (`output` is both the mix-in and the destination).
void mixThroughVca(Ramp amplitude, const Block& b) {
	fixed::Amplitude vca{amplitude};
	vca.renderBlock(fixed::Signal{b.scratch}, fixed::Signal{b.output}, b.output);
}

/// Fill `osc`'s raw wave, then either scale-and-mix it through its VCA (when amplitude applies) or write the
/// standalone wave (scaled) straight out. `makeVca` builds the VCA only when needed.
template <typename Osc, typename MakeVca>
void renderThroughVca(Osc& osc, MakeVca makeVca, Standalone scale, const Block& b) {
	if (b.applyAmplitude) {
		osc.renderBlock(b.scratch);
		auto vca = makeVca();
		vca.renderBlock(fixed::Signal{b.scratch}, fixed::Signal{b.output}, b.output);
	}
	else {
		osc.renderBlock(b.output);
		if (scale == Standalone::kHalve) {
			for (auto& sample : b.output) {
				sample >>= 1;
			}
		}
		else if (scale == Standalone::kDouble) {
			for (auto& sample : b.output) {
				sample <<= 1;
			}
		}
	}
}

/// Render a band-limited wave — a BandLimitedTableOscillator picks the right band of `bands` for `selectionIncr`
/// — straight or with osc sync, then apply amplitude. The sync arm crossfades the seam (SyncedOscillator) and
/// reports its advanced phase back to the engine's `phasor`.
void renderBandLimited(const fixed::BandlimitedTable& bands, uint32_t selectionIncr, Ramp amplitude, const Block& b,
                       Phasor& phasor) {
	if (b.sync) {
		fixed::Buffer dst = b.applyAmplitude ? b.scratch : b.output;
		fixed::SyncedOscillator osc{
		    fixed::BandLimitedTableOscillator{bands, phasor.phase(), phasor.increment(), selectionIncr}, phasor.phase(),
		    phasor.increment(), fixed::Resetter{b.sync->phase(), b.sync->increment()}, b.retriggerPhase};
		osc.renderBlock(dst);
		phasor.seek(osc.phase());
		mixThroughVca(amplitude, b);
	}
	else {
		fixed::BandLimitedTableOscillator osc{bands, phasor.phase(), phasor.increment(), selectionIncr};
		renderThroughVca(osc, [&] { return fixed::Amplitude{amplitude}; }, Standalone::kDirect, b);
	}
}

/// The perfect pulse: ring-modulate two band-limited squares offset by the pulse width. Renders at half the
/// phase rate (the legacy pulse trick), so the synced phase is doubled back before it is stored.
void renderPerfectPulse(Ramp amplitude, const Block& b, Phasor& phasor) {
	const uint32_t phaseToAdd = -(b.pulseWidth >> 1);
	const uint32_t phase = phasor.phase() >> 1;
	const uint32_t increment = phasor.increment() >> 1;
	fixed::PulseOscillator pulse{fixed::square_bandlimited_table().select(b.bandlimitIncrement), phase, increment,
	                             phaseToAdd};
	if (b.sync) {
		fixed::Buffer dst = b.applyAmplitude ? b.scratch : b.output;
		fixed::SyncedOscillator osc{pulse, phase, increment, fixed::Resetter{b.sync->phase(), b.sync->increment()},
		                            b.retriggerPhase};
		osc.renderBlock(dst);
		phasor.seek(osc.phase() << 1);
		mixThroughVca(amplitude, b);
	}
	else if (b.applyAmplitude) {
		pulse.renderBlock(b.scratch);
		mixThroughVca(amplitude, b);
	}
	else {
		pulse.renderBlock(b.output);
	}
}

void renderSine(const Block& b, Phasor& phasor) {
	// Sine has no aliasing — a single band (the small sine table) covers every pitch.
	static const fixed::Band kSineBand[] = {{sineWaveSmall, 8, 0xFFFFFFFFu}};
	static const fixed::BandlimitedTable kSineTable{kSineBand};
	renderBandLimited(kSineTable, phasor.increment(), b.amplitude, b, phasor);
}

void renderTriangle(const Block& b, Phasor& phasor) {
	if (phasor.increment() < 69273666 || b.cpuDireness >= 7) {
		// Crude triangle: half-scale fed to a doubled-amplitude VCA, so the standalone output is << 1.
		auto makeVca = [&] { return fixed::CrudeAmplitude{b.amplitude << 1}; };
		if (b.sync) {
			fixed::CrudeTriangle osc{fixed::SyncedPhasor{phasor, *b.sync, b.retriggerPhase}};
			renderThroughVca(osc, makeVca, Standalone::kDouble, b);
			phasor.seek(osc.phase());
		}
		else {
			fixed::CrudeTriangle osc{phasor};
			renderThroughVca(osc, makeVca, Standalone::kDouble, b);
		}
		return;
	}
	renderBandLimited(fixed::triangle_bandlimited_table(), phasor.increment(), b.amplitude, b, phasor);
}

void renderSaw(const Block& b, Phasor& phasor) {
	// If frequency low enough, we use the crude calculation without anti-aliasing.
	if (b.tableNumber < b.cpuDireness + 6) {
		// Crude saw: the phase ramp itself (full scale), so the standalone output is >> 1.
		auto makeVca = [&] { return fixed::CrudeAmplitude{b.amplitude}; };
		if (b.sync) {
			fixed::CrudeSaw osc{fixed::SyncedPhasor{phasor, *b.sync, b.retriggerPhase}};
			renderThroughVca(osc, makeVca, Standalone::kHalve, b);
			phasor.seek(osc.phase());
		}
		else {
			fixed::CrudeSaw osc{phasor};
			renderThroughVca(osc, makeVca, Standalone::kHalve, b);
		}
		return;
	}
	renderBandLimited(fixed::saw_bandlimited_table(), b.bandlimitIncrement, b.amplitude << 1, b, phasor);
}

void renderSquare(const Block& b, Phasor& phasor) {
	// If frequency low enough, we use the crude calculation without anti-aliasing.
	if (b.tableNumber < b.cpuDireness + 6) {
		// Crude square: full-scale, so the standalone output is >> 1 (half scale).
		auto makeVca = [&] { return fixed::CrudeAmplitude{b.amplitude}; };
		if (b.sync) {
			fixed::CrudeSquare osc{fixed::SyncedPhasor{phasor, *b.sync, b.retriggerPhase}, b.pulseWidth};
			renderThroughVca(osc, makeVca, Standalone::kHalve, b);
			phasor.seek(osc.phase());
		}
		else {
			fixed::CrudeSquare osc{phasor, b.pulseWidth};
			renderThroughVca(osc, makeVca, Standalone::kHalve, b);
		}
		return;
	}
	// The perfect mathematical pulse has its own ring-modulation routine; the plain square is band-limited.
	if (b.pulseWave) {
		renderPerfectPulse(b.amplitude << 1, b, phasor);
		return;
	}
	renderBandLimited(fixed::square_bandlimited_table(), b.bandlimitIncrement, b.amplitude << 1, b, phasor);
}

void renderAnalog(const fixed::BandlimitedTable& bands, const Block& b, Phasor& phasor) {
	// The analog saw/square are always band-limited (the analog-saw CPU downgrade to digital SAW already happened
	// adapter-side, so this engine only sees a genuine analog wave here).
	renderBandLimited(bands, b.bandlimitIncrement, b.amplitude << 1, b, phasor);
}

} // namespace

void VirtualAnalogEngine::renderBlock(const BlockControls& controls, fixed::Buffer output, fixed::Buffer scratch) {
	Block b{
	    .output = output,
	    .scratch = scratch,
	    .amplitude = controls.amplitude,
	    .applyAmplitude = controls.apply_amplitude,
	    .pulseWidth = controls.pulse_width,
	    .pulseWave = controls.pulse_wave,
	    .retriggerPhase = controls.retrigger_phase,
	    .sync = controls.sync,
	    .bandlimitIncrement = controls.bandlimit_increment,
	    .tableNumber = fixed::getTableNumber(controls.bandlimit_increment).first,
	    .cpuDireness = controls.cpu_direness,
	};

	switch (wave_) {
	case OscType::SINE:
		renderSine(b, phasor_);
		break;
	case OscType::TRIANGLE:
		renderTriangle(b, phasor_);
		break;
	case OscType::SAW:
		renderSaw(b, phasor_);
		break;
	case OscType::SQUARE:
		renderSquare(b, phasor_);
		break;
	case OscType::ANALOG_SAW_2:
		renderAnalog(fixed::analog_saw_bandlimited_table(), b, phasor_);
		break;
	case OscType::ANALOG_SQUARE:
		renderAnalog(fixed::analog_square_bandlimited_table(), b, phasor_);
		break;
	default:
		// SAMPLE / DX7 / INPUT_* / WAVETABLE never reach the VA engine (the adapter handles them).
		break;
	}
}

} // namespace deluge::synth
