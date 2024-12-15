#include "blocks/gain_ramp.hpp"
#include "util/functions.h"
#include <cppspec.hpp>

std::ostream& operator<<(std::ostream& os, const StereoFloatSample& sample) {
	return os << "StereoSampleFloat{" << sample.l << ", " << sample.r << "}";
}

// clang-format off
describe dsp_gain_ramp("Gain Ramp", $ {
	using namespace deluge::dsp;
	it("processes a block of mono samples", _ {
		blocks::GainRamp gain_ramp{0.0f, 1.0f};
		std::array<float, 3> in = {1.f, 1.f, 1.f};
		std::array<float, 3> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(std::array{0.0f, 0.5f, 1.0f});
	});

	it("processes a block of stereo samples", _ {
		blocks::GainRamp gain_ramp{0.0f, 1.0f};
		std::array<StereoFloatSample, 3> in = {StereoFloatSample{1.0f, 1.0f}, StereoFloatSample{1.0f, 1.0f}, StereoFloatSample{1.0f, 1.0f}};
		std::array<StereoFloatSample, 3> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(std::array{StereoFloatSample{0.0f, 0.0f}, StereoFloatSample{0.5f, 0.5f}, StereoFloatSample{1.0f, 1.0f}});
	});

	it("returns the start and end values", _ {
		blocks::GainRamp gain_ramp{0.0f, 1.0f};
		expect(gain_ramp.start()).to_equal(0.0f);
		expect(gain_ramp.end()).to_equal(1.0f);
	});

	it("processes a block of mono samples with a negative gain", _ {
		blocks::GainRamp gain_ramp{1.0f, 0.0f};
		std::array<float, 3> in = {1.f, 1.f, 1.f};
		std::array<float, 3> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(std::array{1.0f, 0.5f, 0.0f});
	});

	it("processes a large set of mono samples using NEON", _ {
		blocks::GainRamp gain_ramp{0.0f, 0.9375f};
		std::array<float, 16> in;
		in.fill(1.0f);
		std::array<float, 16> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(std::array{0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f, 0.3125f, 0.375f, 0.4375f, 0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 0.9375f});
	});

	it("processes a large set of mono samples using NEON and non-NEON codepaths", _ {
		blocks::GainRamp gain_ramp{0.0f, 1.0f};
		std::array<float, 17> in;
		in.fill(1.0f);
		std::array<float, 17> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(std::array{0.0f, 0.0625f, 0.125f, 0.1875f, 0.25f, 0.3125f, 0.375f, 0.4375f, 0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 0.9375f, 1.0f});
	});

	it("processes a large set of stereo samples using NEON", _ {
		blocks::GainRamp gain_ramp{0.0f, 0.9375f};
		std::array<StereoFloatSample, 16> in;
		in.fill(StereoFloatSample{1.0f, 1.0f});
		std::array<StereoFloatSample, 16> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(
			std::array{
				StereoFloatSample{0.0f, 0.0f},
				StereoFloatSample{0.0625f, 0.0625f},
				StereoFloatSample{0.125f, 0.125f},
				StereoFloatSample{0.1875f, 0.1875f},
				StereoFloatSample{0.25f, 0.25f},
				StereoFloatSample{0.3125f, 0.3125f},
				StereoFloatSample{0.375f, 0.375f},
				StereoFloatSample{0.4375f, 0.4375f},
				StereoFloatSample{0.5f, 0.5f},
				StereoFloatSample{0.5625f, 0.5625f},
				StereoFloatSample{0.625f, 0.625f},
				StereoFloatSample{0.6875f, 0.6875f},
				StereoFloatSample{0.75f, 0.75f},
				StereoFloatSample{0.8125f, 0.8125f},
				StereoFloatSample{0.875f, 0.875f},
				StereoFloatSample{0.9375f, 0.9375f},
			}
		);
	});

	it("processes a large set of stereo samples using NEON and non-NEON codepaths", _ {
		blocks::GainRamp gain_ramp{0.0f, 1.0f};
		std::array<StereoFloatSample, 17> in;
		in.fill(StereoFloatSample{1.0f, 1.0f});
		std::array<StereoFloatSample, 17> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(
			std::array{
				StereoFloatSample{0.0f, 0.0f},
				StereoFloatSample{0.0625f, 0.0625f},
				StereoFloatSample{0.125f, 0.125f},
				StereoFloatSample{0.1875f, 0.1875f},
				StereoFloatSample{0.25f, 0.25f},
				StereoFloatSample{0.3125f, 0.3125f},
				StereoFloatSample{0.375f, 0.375f},
				StereoFloatSample{0.4375f, 0.4375f},
				StereoFloatSample{0.5f, 0.5f},
				StereoFloatSample{0.5625f, 0.5625f},
				StereoFloatSample{0.625f, 0.625f},
				StereoFloatSample{0.6875f, 0.6875f},
				StereoFloatSample{0.75f, 0.75f},
				StereoFloatSample{0.8125f, 0.8125f},
				StereoFloatSample{0.875f, 0.875f},
				StereoFloatSample{0.9375f, 0.9375f},
				StereoFloatSample{1.0f, 1.0f},
			}
		);
	});

	it("immediately applies the end value if the length is only 1", _ {
		blocks::GainRamp gain_ramp{0.0f, 2.0f};
		std::array<float, 1> in = {1.f};
		std::array<float, 1> out;
		gain_ramp.processBlock(in, out);
		expect(out).to_equal(std::array{2.0f});
	});
});

CPPSPEC_SPEC(dsp_gain_ramp);
