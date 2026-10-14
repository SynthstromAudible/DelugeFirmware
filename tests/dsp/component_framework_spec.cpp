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
#include "dsp-next/core/as_block.h"
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/generator.h"
#include "dsp-next/core/mixer.h"
#include "dsp-next/core/processor.h"
#include "dsp-next/core/types.h"

#include <array>
#include <cppspec.hpp>
#include <numeric>
#include <type_traits>

using namespace deluge::dsp;

namespace {

/// A scalar processor: doubles each sample (q31, wrapping).
struct ScalarDouble : Processor<ScalarDouble, q31> {
	[[nodiscard]] q31 render(q31 s) { return static_cast<q31>(static_cast<uint32_t>(s) * 2u); }
};
/// The same arithmetic on the SIMD path.
struct SimdDouble : Processor<SimdDouble, q31> {
	[[nodiscard]] Argon<q31> render(Argon<q31> v) { return v + v; }
};
/// A scalar generator counting up from 1.
struct ScalarCount : Generator<ScalarCount, q31> {
	q31 next = 1;
	[[nodiscard]] q31 render() { return next++; }
};
/// A scalar mixer: a + b.
struct ScalarAdd : Mixer<ScalarAdd, q31> {
	[[nodiscard]] q31 render(q31 a, q31 b) { return a + b; }
};
struct SimdAdd : Mixer<SimdAdd, q31> {
	[[nodiscard]] Argon<q31> render(Argon<q31> a, Argon<q31> b) { return a + b; }
};
/// A stereo gain, scalar and SIMD (the interleaved path).
struct ScalarStereoDouble : Processor<ScalarStereoDouble, StereoSample> {
	[[nodiscard]] StereoSample render(StereoSample f) { return {.l = f.l + f.l, .r = f.r + f.r}; }
};
struct SimdStereoDouble : Processor<SimdStereoDouble, StereoSample> {
	[[nodiscard]] StereoVector render(StereoVector v) { return {.l = v.l + v.l, .r = v.r + v.r}; }
};
/// An asymmetric stereo processor (left doubled, right unchanged), so a channel swap shows; scalar and SIMD.
struct ScalarLeftDouble : Processor<ScalarLeftDouble, StereoSample> {
	[[nodiscard]] StereoSample render(StereoSample f) { return {.l = f.l + f.l, .r = f.r}; }
};
struct SimdLeftDouble : Processor<SimdLeftDouble, StereoSample> {
	[[nodiscard]] StereoVector render(StereoVector v) { return {.l = v.l + v.l, .r = v.r}; }
};
/// The scalar count on the SIMD path; it counts its own calls.
struct SimdCount : Generator<SimdCount, q31> {
	Argon<q31> next = Argon<q31>::Iota(1);
	std::size_t calls = 0;
	[[nodiscard]] Argon<q31> render() {
		calls++;
		Argon<q31> v = next;
		next = next + Argon<q31>{static_cast<q31>(Argon<q31>::lanes)};
		return v;
	}
};
/// Stereo generators: frame i is {i + 1, -(i + 1)}, scalar and SIMD.
struct ScalarStereoCount : Generator<ScalarStereoCount, StereoSample> {
	q31 next = 1;
	[[nodiscard]] StereoSample render() {
		q31 v = next++;
		return {.l = v, .r = -v};
	}
};
struct SimdStereoCount : Generator<SimdStereoCount, StereoSample> {
	Argon<q31> next = Argon<q31>::Iota(1);
	[[nodiscard]] StereoVector render() {
		Argon<q31> v = next;
		next = next + Argon<q31>{static_cast<q31>(Argon<q31>::lanes)};
		return {.l = v, .r = Argon<q31>{0} - v};
	}
};
/// Stereo mixers: a + b per channel, scalar and SIMD.
struct ScalarStereoAdd : Mixer<ScalarStereoAdd, StereoSample> {
	[[nodiscard]] StereoSample render(StereoSample a, StereoSample b) { return a + b; }
};
struct SimdStereoAdd : Mixer<SimdStereoAdd, StereoSample> {
	[[nodiscard]] StereoVector render(StereoVector a, StereoVector b) { return {.l = a.l + b.l, .r = a.r + b.r}; }
};
/// A block-rate control signal.
struct Ramp {
	using value_type = q31;
	q31 value = 0;
	[[nodiscard]] q31 advance(std::size_t n) { return value += static_cast<q31>(n); }
};
/// Custom verbs, as the transcribed classes have: must satisfy no concept.
struct CustomVerbs {
	using value_type = q31;
	void renderMono(std::span<q31>) {}
};

static_assert(processor<ScalarDouble>);
static_assert(processor<SimdDouble> || block_processor<SimdDouble>);
static_assert(generator<ScalarCount>);
static_assert(mixer<ScalarAdd>);
static_assert(control_generator<Ramp>);
static_assert(!processor<CustomVerbs> && !block_processor<CustomVerbs> && !generator<CustomVerbs>);
static_assert(block_generator<SimdCount> && generator<ScalarStereoCount> && block_generator<SimdStereoCount>);
static_assert(mixer<ScalarStereoAdd> && block_mixer<SimdStereoAdd>);
static_assert(sizeof(ScalarDouble) == 1, "the mixins add no vptr");
static_assert(!std::is_polymorphic_v<ScalarDouble> && !std::is_polymorphic_v<SimdDouble>
                  && !std::is_polymorphic_v<ScalarCount> && !std::is_polymorphic_v<SimdCount>
                  && !std::is_polymorphic_v<ScalarAdd> && !std::is_polymorphic_v<SimdAdd>
                  && !std::is_polymorphic_v<SimdStereoAdd>,
              "no mixin path adds a vptr");
static_assert(sizeof(ScalarAdd) == 1 && sizeof(ScalarStereoCount) == sizeof(q31), "the mixins add no state");

std::vector<q31> ramp(std::size_t n) {
	std::vector<q31> v(n);
	std::iota(v.begin(), v.end(), 1000);
	return v;
}

std::vector<StereoSample> stereo_ramp(std::size_t frames) {
	std::vector<StereoSample> v(frames);
	for (std::size_t i = 0; i < frames; i++) {
		v[i] = {.l = static_cast<q31>(i + 1), .r = -static_cast<q31>(i + 1)};
	}
	return v;
}

} // namespace

// clang-format off
describe component_framework("the component mixins, AsBlock and control_generator", ${
	it("fills every sample on the scalar and SIMD paths alike, whatever the length", _ {
		for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 13u}) {
			auto in = ramp(n);
			std::vector<q31> scalar(n, -1), simd(n, -1);
			ScalarDouble{}.renderBlock(in, scalar);
			SimdDouble{}.renderBlock(in, simd);
			expect(simd).to_equal(scalar);
			expect(scalar.back()).to_equal(static_cast<q31>(in.back() * 2));
		}
	});
	it("gives the same block in place as out of place", _ {
		for (std::size_t n : {4u, 6u, 7u}) {
			auto in = ramp(n);
			std::vector<q31> out(n);
			SimdDouble{}.renderBlock(in, out);
			auto inplace = ramp(n);
			SimdDouble{}.renderBlock(inplace, inplace);
			expect(inplace).to_equal(out);
			auto scalar_inplace = ramp(n);
			ScalarDouble{}.renderBlock(scalar_inplace, scalar_inplace);
			expect(scalar_inplace).to_equal(out);
		}
	});
	it("processes every stereo frame on the scalar and interleaved SIMD paths alike", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u, 9u, 10u}) {
			std::vector<StereoSample> in(frames);
			for (std::size_t i = 0; i < frames; i++) {
				in[i] = {.l = static_cast<q31>(i + 1), .r = -static_cast<q31>(i + 1)};
			}
			std::vector<StereoSample> scalar(frames), simd(frames);
			ScalarStereoDouble{}.renderBlock(in, scalar);
			SimdStereoDouble{}.renderBlock(in, simd);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd[i].l).to_equal(scalar[i].l);
				expect(simd[i].r).to_equal(scalar[i].r);
			}
			expect(scalar.back().r).to_equal(-2 * static_cast<q31>(frames));
		}
	});
	it("mixes every sample on both paths, tails included", _ {
		for (std::size_t n : {1u, 2u, 3u, 5u, 6u, 8u, 10u}) {
			auto a = ramp(n);
			auto b = ramp(n);
			std::vector<q31> scalar(n), simd(n);
			ScalarAdd{}.renderBlock(a, b, scalar);
			SimdAdd{}.renderBlock(a, b, simd);
			expect(simd).to_equal(scalar);
		}
	});
	it("generates each sample once, in order", _ {
		std::vector<q31> out(5);
		ScalarCount{}.renderBlock(out);
		expect(out).to_equal(std::vector<q31>{1, 2, 3, 4, 5});
	});
	it("mixes in place into either input as out of place, on both paths", _ {
		for (std::size_t n : {2u, 3u, 4u, 6u, 7u}) {
			auto a = ramp(n);
			auto b = ramp(n);
			std::vector<q31> out(n);
			SimdAdd{}.renderBlock(a, b, out);
			auto simd_a = ramp(n);
			SimdAdd{}.renderBlock(simd_a, b, simd_a);
			expect(simd_a).to_equal(out);
			auto simd_b = ramp(n);
			SimdAdd{}.renderBlock(a, simd_b, simd_b);
			expect(simd_b).to_equal(out);
			auto scalar_a = ramp(n);
			ScalarAdd{}.renderBlock(scalar_a, b, scalar_a);
			expect(scalar_a).to_equal(out);
			auto scalar_b = ramp(n);
			ScalarAdd{}.renderBlock(a, scalar_b, scalar_b);
			expect(scalar_b).to_equal(out);
		}
	});
	it("processes stereo in place as out of place, on both paths", _ {
		for (std::size_t frames : {2u, 3u, 4u, 5u, 6u}) {
			auto in = stereo_ramp(frames);
			std::vector<StereoSample> out(frames);
			ScalarStereoDouble{}.renderBlock(in, out);
			auto simd = stereo_ramp(frames);
			SimdStereoDouble{}.renderBlock(simd, simd);
			auto scalar = stereo_ramp(frames);
			ScalarStereoDouble{}.renderBlock(scalar, scalar);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd[i].l).to_equal(out[i].l);
				expect(simd[i].r).to_equal(out[i].r);
				expect(scalar[i].l).to_equal(out[i].l);
				expect(scalar[i].r).to_equal(out[i].r);
			}
		}
	});
	it("generates on the SIMD path as on the scalar one, rendering once per step, the partial one included", _ {
		constexpr std::size_t lanes = Argon<q31>::lanes;
		for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 13u}) {
			std::vector<q31> scalar(n, -1), simd(n, -1);
			ScalarCount{}.renderBlock(scalar);
			SimdCount generator;
			generator.renderBlock(simd);
			expect(simd).to_equal(scalar);
			expect(generator.calls).to_equal((n + lanes - 1) / lanes);
		}
	});
	it("generates and mixes stereo on the interleaved SIMD path as on the scalar one, tails included", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u, 9u, 10u}) {
			std::vector<StereoSample> scalar(frames), simd(frames);
			ScalarStereoCount{}.renderBlock(scalar);
			SimdStereoCount{}.renderBlock(simd);
			auto a = stereo_ramp(frames);
			std::vector<StereoSample> scalar_mix(frames), simd_mix(frames);
			ScalarStereoAdd{}.renderBlock(a, a, scalar_mix);
			SimdStereoAdd{}.renderBlock(a, a, simd_mix);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd[i].l).to_equal(scalar[i].l);
				expect(simd[i].r).to_equal(scalar[i].r);
				expect(simd_mix[i].l).to_equal(scalar_mix[i].l);
				expect(simd_mix[i].r).to_equal(scalar_mix[i].r);
			}
			expect(scalar.back().r).to_equal(-static_cast<q31>(frames));
			expect(scalar_mix.back().l).to_equal(2 * static_cast<q31>(frames));
		}
	});
	it("keeps each channel its own on the scalar and interleaved SIMD paths, in place and out", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u, 9u}) {
			auto in = stereo_ramp(frames);
			std::vector<StereoSample> scalar(frames), simd(frames);
			ScalarLeftDouble{}.renderBlock(in, scalar);
			SimdLeftDouble{}.renderBlock(in, simd);
			auto inplace = stereo_ramp(frames);
			SimdLeftDouble{}.renderBlock(inplace, inplace);
			for (std::size_t i = 0; i < frames; i++) {
				const auto expected_l = 2 * static_cast<q31>(i + 1);
				const auto expected_r = -static_cast<q31>(i + 1);
				expect(scalar[i].l).to_equal(expected_l);
				expect(scalar[i].r).to_equal(expected_r);
				expect(simd[i].l).to_equal(expected_l);
				expect(simd[i].r).to_equal(expected_r);
				expect(inplace[i].l).to_equal(expected_l);
				expect(inplace[i].r).to_equal(expected_r);
			}
		}
	});
	it("mixes stereo in place into its first input as out of place", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u}) {
			auto a = stereo_ramp(frames);
			auto b = stereo_ramp(frames);
			std::vector<StereoSample> out(frames);
			ScalarStereoAdd{}.renderBlock(a, b, out);
			auto simd_a = stereo_ramp(frames);
			SimdStereoAdd{}.renderBlock(simd_a, b, simd_a);
			auto scalar_a = stereo_ramp(frames);
			ScalarStereoAdd{}.renderBlock(scalar_a, b, scalar_a);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd_a[i].l).to_equal(out[i].l);
				expect(simd_a[i].r).to_equal(out[i].r);
				expect(scalar_a[i].l).to_equal(out[i].l);
				expect(scalar_a[i].r).to_equal(out[i].r);
			}
		}
	});
	it("forwards through AsBlock unchanged", _ {
		auto in = ramp(7);
		std::vector<q31> direct(7), through(7);
		SimdDouble{}.renderBlock(in, direct);
		AsBlock<SimdDouble> wrapped{SimdDouble{}};
		BlockProcessor<q31>& selected = wrapped;
		selected.renderBlock(in, through);
		expect(through).to_equal(direct);
	});
	it("advances a control signal by the samples asked", _ {
		Ramp r;
		expect(r.advance(128)).to_equal(128);
		expect(r.advance(3)).to_equal(131);
	});
});

CPPSPEC_MAIN(component_framework);
