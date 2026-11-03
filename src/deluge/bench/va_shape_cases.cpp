/// @file
/// @brief The virtual-analog shape cases' rig, setup and blocks.
/// @details Each block is a shape's block as a voice renders it: synth::va_shape's render, mixing into the rig's own
/// output block (the shared one is read-only outside cases.cpp).

#include "bench/cases.h"

#include "dsp-next/core/phasor.h"
#include "dsp-next/core/ramp.h"
#include "synth/sources/source.h"
#include "synth/sources/va/va_shapes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace deluge::bench {

namespace {

namespace va = deluge::synth::va_shape;

/// @brief The VCA's amplitude: a quarter, steady.
constexpr q31 kAmplitude = q31{1} << 29;

dsp::Phasor phasor(uint32_t increment) {
	return dsp::Phasor{dsp::UQ<32>::from_raw(0), dsp::UQ<32>::from_raw(increment)};
}

struct VaShapeRig {
	std::variant<va::Saw, va::Square, va::Triangle, va::AnalogSaw> shape{va::Saw{phasor(0)}};
	dsp::Phasor master = phasor(0);
	alignas(16) std::array<q31, kMaxBlock> output{};
	alignas(16) std::array<q31, kMaxBlock> scratch{};
};

VaShapeRig rig{};

} // namespace

void VaShapeCase::setup(std::size_t /*n*/) {
	rig.output.fill(0);
	const dsp::Phasor audible = phasor(config_.increment);
	switch (config_.wave) {
	case VaWave::SAW:
		rig.shape = va::Saw{audible};
		break;
	case VaWave::SQUARE:
		rig.shape = va::Square{audible};
		break;
	case VaWave::TRIANGLE:
		rig.shape = va::Triangle{audible};
		break;
	case VaWave::ANALOG_SAW:
		rig.shape = va::AnalogSaw{audible};
		break;
	}
	rig.master = phasor(config_.increment / 5 * 2);
}

void VaShapeCase::block(std::size_t n) {
	synth::BlockControls controls{.amplitude = dsp::Ramp{dsp::Q<31>::from_raw(kAmplitude), dsp::Q<31>::from_raw(0)},
	                              .apply_amplitude = true};
	if (config_.sync) {
		controls.sync = rig.master;
	}
	const dsp::fixed::Buffer output{rig.output.data(), n};
	const dsp::fixed::Buffer scratch{rig.scratch.data(), n};
	std::visit([&](auto& shape) { shape.render_block(controls, output, scratch); }, rig.shape);
	rig.master.seek(
	    dsp::UQ<32>::from_raw(rig.master.phase().raw() + (rig.master.increment().raw() * static_cast<uint32_t>(n))));
}

} // namespace deluge::bench
