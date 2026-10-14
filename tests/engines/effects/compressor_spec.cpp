#include "engines/effects/compressor.h"

#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"

#include <cppspec.hpp>
#include <cstdint>

using namespace deluge::effects;
using deluge::dsp::fixed::compressor_attack_ms;
using deluge::dsp::fixed::compressor_release_ms;

namespace {

/// A state whose every knob differs from its default and from each other.
constexpr CompressorState kState{
    .attack = 1 << 30,
    .release = 3 << 28,
    .threshold = 5 << 26,
    .ratio = 7 << 24,
    .sidechain_hpf = 9 << 22,
    .blend = 11 << 20,
};

} // namespace

// clang-format off
describe compressor("an owner's compressor on the dsp-next compressor", ${
	it("carries each knob across", _ {
		constexpr deluge::dsp::fixed::CompressorKnobs knobs = compressor_knobs(kState);
		expect(knobs.attack).to_equal(1 << 30);
		expect(knobs.release).to_equal(3 << 28);
		expect(knobs.threshold).to_equal(5 << 26);
		expect(knobs.ratio).to_equal(7 << 24);
		expect(knobs.sidechain_hpf).to_equal(9 << 22);
		expect(knobs.blend).to_equal(11 << 20);
	});
	it("carries the defaults across as the dsp-next compressor's own", _ {
		expect(compressor_knobs(CompressorState{})).to_equal(deluge::dsp::fixed::CompressorKnobs{});
	});
	it("takes the state's time constants and threshold", _ {
		Compressor c;
		c.configure(kState);
		expect(c.dsp().attackMs()).to_equal(compressor_attack_ms(1 << 30));
		expect(c.dsp().releaseMs()).to_equal(compressor_release_ms(3 << 28));
		expect(c.dsp().threshold()).to_equal(5 << 26);
	});
	it("leaves a configured compressor unchanged on an equal state", _ {
		Compressor c;
		c.configure(kState);
		c.dsp().setThreshold(1 << 29); // an owner's threshold param, set after configure
		c.configure(kState);
		expect(c.dsp().attackMs()).to_equal(compressor_attack_ms(1 << 30));
		expect(c.dsp().releaseMs()).to_equal(compressor_release_ms(3 << 28));
		expect(c.dsp().threshold()).to_equal(1 << 29);
	});
	it("follows a changed attack", _ {
		Compressor c;
		c.configure(kState);
		CompressorState faster = kState;
		faster.attack = 1 << 26;
		c.configure(faster);
		expect(c.dsp().attackMs()).to_equal(compressor_attack_ms(1 << 26));
		expect(c.dsp().attackMs()).not_().to_equal(compressor_attack_ms(1 << 30));
		expect(c.dsp().releaseMs()).to_equal(compressor_release_ms(3 << 28));
	});
});

CPPSPEC_SPEC(compressor)
