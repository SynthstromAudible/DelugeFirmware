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
#include "dsp-next/core/fixed_point_neon.h"
#include "dsp-next/core/generator.h"
#include "dsp-next/core/mixer.h"
#include "dsp-next/core/processor.h"
#include "dsp-next/core/types.h"

#include <array>
#include <cppspec.hpp>
#include <span>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;

namespace {

/// A scalar processor: doubles each sample (wrapping).
struct ScalarDouble : Processor<ScalarDouble, q31> {
	[[nodiscard]] q31 render(q31 s) { return add_wrap(s, s); }
};
/// The same arithmetic on the SIMD path.
struct SimdDouble : Processor<SimdDouble, q31> {
	[[nodiscard]] QVec<31> render(QVec<31> v) { return add_wrap(v, v); }
};
/// A scalar generator counting up from 1.
struct ScalarCount : Generator<ScalarCount, q31> {
	q31 next = q31::from_raw(1);
	[[nodiscard]] q31 render() {
		const q31 v = next;
		next = add_wrap(next, q31::from_raw(1));
		return v;
	}
};
/// A scalar mixer: a + b.
struct ScalarAdd : Mixer<ScalarAdd, q31> {
	[[nodiscard]] q31 render(q31 a, q31 b) { return add_wrap(a, b); }
};
struct SimdAdd : Mixer<SimdAdd, q31> {
	[[nodiscard]] QVec<31> render(QVec<31> a, QVec<31> b) { return add_wrap(a, b); }
};
/// A stereo gain, scalar and SIMD (the interleaved path).
struct ScalarStereoDouble : Processor<ScalarStereoDouble, StereoSample> {
	[[nodiscard]] StereoSample render(StereoSample f) { return {.l = add_wrap(f.l, f.l), .r = add_wrap(f.r, f.r)}; }
};
struct SimdStereoDouble : Processor<SimdStereoDouble, StereoSample> {
	[[nodiscard]] StereoVector render(StereoVector v) { return {.l = add_wrap(v.l, v.l), .r = add_wrap(v.r, v.r)}; }
};
/// An asymmetric stereo processor (left doubled, right unchanged), so a channel swap shows; scalar and SIMD.
struct ScalarLeftDouble : Processor<ScalarLeftDouble, StereoSample> {
	[[nodiscard]] StereoSample render(StereoSample f) { return {.l = add_wrap(f.l, f.l), .r = f.r}; }
};
struct SimdLeftDouble : Processor<SimdLeftDouble, StereoSample> {
	[[nodiscard]] StereoVector render(StereoVector v) { return {.l = add_wrap(v.l, v.l), .r = v.r}; }
};
/// Four lanes counting up from 1: {1, 2, 3, 4}.
QVec<31> iota_from_one() {
	return QVec<31>::from_raw(Argon<int32_t>::Iota(1));
}
/// The scalar count on the SIMD path; it counts its own calls.
struct SimdCount : Generator<SimdCount, q31> {
	QVec<31> next = iota_from_one();
	std::size_t calls = 0;
	[[nodiscard]] QVec<31> render() {
		calls++;
		const QVec<31> v = next;
		next = add_wrap(next, QVec<31>::splat(q31::from_raw(static_cast<int32_t>(Argon<int32_t>::lanes))));
		return v;
	}
};
/// Stereo generators: frame i is {i + 1, -(i + 1)}, scalar and SIMD.
struct ScalarStereoCount : Generator<ScalarStereoCount, StereoSample> {
	int32_t next = 1;
	[[nodiscard]] StereoSample render() {
		int32_t v = next++;
		return {.l = Q<31>::from_raw(v), .r = Q<31>::from_raw(-v)};
	}
};
struct SimdStereoCount : Generator<SimdStereoCount, StereoSample> {
	QVec<31> next = iota_from_one();
	[[nodiscard]] StereoVector render() {
		const QVec<31> v = next;
		next = add_wrap(next, QVec<31>::splat(q31::from_raw(static_cast<int32_t>(Argon<int32_t>::lanes))));
		return {.l = v, .r = sub_wrap(QVec<31>::splat(q31::from_raw(0)), v)};
	}
};
/// Stereo mixers: a + b per channel, scalar and SIMD.
struct ScalarStereoAdd : Mixer<ScalarStereoAdd, StereoSample> {
	[[nodiscard]] StereoSample render(StereoSample a, StereoSample b) { return a + b; }
};
struct SimdStereoAdd : Mixer<SimdStereoAdd, StereoSample> {
	[[nodiscard]] StereoVector render(StereoVector a, StereoVector b) {
		return {.l = add_wrap(a.l, b.l), .r = add_wrap(a.r, b.r)};
	}
};
/// A block-rate control signal.
struct Ramp {
	using value_type = q31;
	q31 value = q31::from_raw(0);
	[[nodiscard]] q31 advance(std::size_t n) { return value = add_wrap(value, q31::from_raw(static_cast<int32_t>(n))); }
};
/// Custom verbs, as the transcribed classes have: must satisfy no concept.
struct CustomVerbs {
	using value_type = q31;
	void render_mono(std::span<q31>) {}
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
	for (std::size_t i = 0; i < n; i++) {
		v[i] = q31::from_raw(static_cast<int32_t>(1000 + i));
	}
	return v;
}

std::vector<StereoSample> stereo_ramp(std::size_t frames) {
	std::vector<StereoSample> v(frames);
	for (std::size_t i = 0; i < frames; i++) {
		v[i] = {.l = Q<31>::from_raw(static_cast<int32_t>(i + 1)), .r = Q<31>::from_raw(-static_cast<int32_t>(i + 1))};
	}
	return v;
}

/// @return @p block's raw words, which CppSpec compares and prints.
std::vector<int32_t> words(std::span<const q31> block) {
	std::vector<int32_t> v(block.size());
	for (std::size_t i = 0; i < block.size(); i++) {
		v[i] = block[i].raw();
	}
	return v;
}

/// Samples past a block's end, holding kSentinel: a load reading them changes nothing stored, a store writing them is
/// seen.
constexpr std::size_t kTail = 4;
constexpr int32_t kSentinel = 0x5a5a'5a5a;

/// The SIMD mixins against their scalar twins at @p n samples: whether every sample agrees and the tail is untouched.
bool simd_mixins_agree(std::size_t n) {
	auto in = ramp(n);
	in.resize(n + kTail, q31::from_raw(kSentinel));
	const std::span<const q31> a = std::span{in}.first(n);
	bool agree = true;
	const auto check = [&](const std::vector<q31>& want, std::vector<q31>& got) {
		agree = agree && words(std::span{got}.first(n)) == words(want);
		for (std::size_t i = n; i < got.size(); i++) {
			agree = agree && got[i].raw() == kSentinel;
		}
	};
	std::vector<q31> want(n);
	std::vector<q31> got(n + kTail, q31::from_raw(kSentinel));
	ScalarDouble{}.render_block(a, want);
	SimdDouble{}.render_block(a, std::span{got}.first(n));
	check(want, got);
	got.assign(n + kTail, q31::from_raw(kSentinel));
	ScalarAdd{}.render_block(a, a, want);
	SimdAdd{}.render_block(a, a, std::span{got}.first(n));
	check(want, got);
	got.assign(n + kTail, q31::from_raw(kSentinel));
	ScalarCount{}.render_block(want);
	SimdCount{}.render_block(std::span{got}.first(n));
	check(want, got);
	return agree;
}

} // namespace

// clang-format off
describe component_framework("the component mixins, AsBlock and control_generator", ${
	it("fills every sample on the scalar and SIMD paths alike, whatever the length", _ {
		for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 13u}) {
			auto in = ramp(n);
			std::vector<q31> scalar(n, q31::from_raw(-1)), simd(n, q31::from_raw(-1));
			ScalarDouble{}.render_block(in, scalar);
			SimdDouble{}.render_block(in, simd);
			expect(words(simd)).to_equal(words(scalar));
			expect(scalar.back().raw()).to_equal(in.back().raw() * 2);
		}
	});
	it("gives the same block in place as out of place", _ {
		for (std::size_t n : {4u, 6u, 7u}) {
			auto in = ramp(n);
			std::vector<q31> out(n);
			SimdDouble{}.render_block(in, out);
			auto inplace = ramp(n);
			SimdDouble{}.render_block(inplace, inplace);
			expect(words(inplace)).to_equal(words(out));
			auto scalar_inplace = ramp(n);
			ScalarDouble{}.render_block(scalar_inplace, scalar_inplace);
			expect(words(scalar_inplace)).to_equal(words(out));
		}
	});
	it("processes every stereo frame on the scalar and interleaved SIMD paths alike", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u, 9u, 10u}) {
			std::vector<StereoSample> in(frames);
			for (std::size_t i = 0; i < frames; i++) {
				in[i] = {.l = Q<31>::from_raw(static_cast<int32_t>(i + 1)), .r = Q<31>::from_raw(-static_cast<int32_t>(i + 1))};
			}
			std::vector<StereoSample> scalar(frames), simd(frames);
			ScalarStereoDouble{}.render_block(in, scalar);
			SimdStereoDouble{}.render_block(in, simd);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd[i].l.raw()).to_equal(scalar[i].l.raw());
				expect(simd[i].r.raw()).to_equal(scalar[i].r.raw());
			}
			expect(scalar.back().r.raw()).to_equal(-2 * static_cast<int32_t>(frames));
		}
	});
	it("mixes every sample on both paths, tails included", _ {
		for (std::size_t n : {1u, 2u, 3u, 5u, 6u, 8u, 10u}) {
			auto a = ramp(n);
			auto b = ramp(n);
			std::vector<q31> scalar(n), simd(n);
			ScalarAdd{}.render_block(a, b, scalar);
			SimdAdd{}.render_block(a, b, simd);
			expect(words(simd)).to_equal(words(scalar));
		}
	});
	it("generates each sample once, in order", _ {
		std::vector<q31> out(5);
		ScalarCount{}.render_block(out);
		expect(words(out)).to_equal(std::vector<int32_t>{1, 2, 3, 4, 5});
	});
	it("mixes in place into either input as out of place, on both paths", _ {
		for (std::size_t n : {2u, 3u, 4u, 6u, 7u}) {
			auto a = ramp(n);
			auto b = ramp(n);
			std::vector<q31> out(n);
			SimdAdd{}.render_block(a, b, out);
			auto simd_a = ramp(n);
			SimdAdd{}.render_block(simd_a, b, simd_a);
			expect(words(simd_a)).to_equal(words(out));
			auto simd_b = ramp(n);
			SimdAdd{}.render_block(a, simd_b, simd_b);
			expect(words(simd_b)).to_equal(words(out));
			auto scalar_a = ramp(n);
			ScalarAdd{}.render_block(scalar_a, b, scalar_a);
			expect(words(scalar_a)).to_equal(words(out));
			auto scalar_b = ramp(n);
			ScalarAdd{}.render_block(a, scalar_b, scalar_b);
			expect(words(scalar_b)).to_equal(words(out));
		}
	});
	it("processes stereo in place as out of place, on both paths", _ {
		for (std::size_t frames : {2u, 3u, 4u, 5u, 6u}) {
			auto in = stereo_ramp(frames);
			std::vector<StereoSample> out(frames);
			ScalarStereoDouble{}.render_block(in, out);
			auto simd = stereo_ramp(frames);
			SimdStereoDouble{}.render_block(simd, simd);
			auto scalar = stereo_ramp(frames);
			ScalarStereoDouble{}.render_block(scalar, scalar);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd[i].l.raw()).to_equal(out[i].l.raw());
				expect(simd[i].r.raw()).to_equal(out[i].r.raw());
				expect(scalar[i].l.raw()).to_equal(out[i].l.raw());
				expect(scalar[i].r.raw()).to_equal(out[i].r.raw());
			}
		}
	});
	it("generates on the SIMD path as on the scalar one, rendering once per step, the partial one included", _ {
		constexpr std::size_t lanes = Argon<int32_t>::lanes;
		for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 13u}) {
			std::vector<q31> scalar(n, q31::from_raw(-1)), simd(n, q31::from_raw(-1));
			ScalarCount{}.render_block(scalar);
			SimdCount generator;
			generator.render_block(simd);
			expect(words(simd)).to_equal(words(scalar));
			expect(generator.calls).to_equal((n + lanes - 1) / lanes);
		}
	});
	it("generates and mixes stereo on the interleaved SIMD path as on the scalar one, tails included", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u, 9u, 10u}) {
			std::vector<StereoSample> scalar(frames), simd(frames);
			ScalarStereoCount{}.render_block(scalar);
			SimdStereoCount{}.render_block(simd);
			auto a = stereo_ramp(frames);
			std::vector<StereoSample> scalar_mix(frames), simd_mix(frames);
			ScalarStereoAdd{}.render_block(a, a, scalar_mix);
			SimdStereoAdd{}.render_block(a, a, simd_mix);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd[i].l.raw()).to_equal(scalar[i].l.raw());
				expect(simd[i].r.raw()).to_equal(scalar[i].r.raw());
				expect(simd_mix[i].l.raw()).to_equal(scalar_mix[i].l.raw());
				expect(simd_mix[i].r.raw()).to_equal(scalar_mix[i].r.raw());
			}
			expect(scalar.back().r.raw()).to_equal(-static_cast<int32_t>(frames));
			expect(scalar_mix.back().l.raw()).to_equal(2 * static_cast<int32_t>(frames));
		}
	});
	it("keeps each channel its own on the scalar and interleaved SIMD paths, in place and out", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u, 9u}) {
			auto in = stereo_ramp(frames);
			std::vector<StereoSample> scalar(frames), simd(frames);
			ScalarLeftDouble{}.render_block(in, scalar);
			SimdLeftDouble{}.render_block(in, simd);
			auto inplace = stereo_ramp(frames);
			SimdLeftDouble{}.render_block(inplace, inplace);
			for (std::size_t i = 0; i < frames; i++) {
				const auto expected_l = 2 * static_cast<int32_t>(i + 1);
				const auto expected_r = -static_cast<int32_t>(i + 1);
				expect(scalar[i].l.raw()).to_equal(expected_l);
				expect(scalar[i].r.raw()).to_equal(expected_r);
				expect(simd[i].l.raw()).to_equal(expected_l);
				expect(simd[i].r.raw()).to_equal(expected_r);
				expect(inplace[i].l.raw()).to_equal(expected_l);
				expect(inplace[i].r.raw()).to_equal(expected_r);
			}
		}
	});
	it("mixes stereo in place into its first input as out of place", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u}) {
			auto a = stereo_ramp(frames);
			auto b = stereo_ramp(frames);
			std::vector<StereoSample> out(frames);
			ScalarStereoAdd{}.render_block(a, b, out);
			auto simd_a = stereo_ramp(frames);
			SimdStereoAdd{}.render_block(simd_a, b, simd_a);
			auto scalar_a = stereo_ramp(frames);
			ScalarStereoAdd{}.render_block(scalar_a, b, scalar_a);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd_a[i].l.raw()).to_equal(out[i].l.raw());
				expect(simd_a[i].r.raw()).to_equal(out[i].r.raw());
				expect(scalar_a[i].l.raw()).to_equal(out[i].l.raw());
				expect(scalar_a[i].r.raw()).to_equal(out[i].r.raw());
			}
		}
	});
	it("forwards through AsBlock unchanged", _ {
		auto in = ramp(7);
		std::vector<q31> direct(7), through(7);
		SimdDouble{}.render_block(in, direct);
		AsBlock<SimdDouble> wrapped{SimdDouble{}};
		BlockProcessor<q31>& selected = wrapped;
		selected.render_block(in, through);
		expect(words(through)).to_equal(words(direct));
	});
	it("steps Q<31> vectors through each mixin as its scalar twin does, the partial last vector included", _ {
		for (std::size_t n : {1u, 3u, 4u, 5u, 7u, 8u, 33u}) {
			expect(simd_mixins_agree(n)).to_be_true();
		}
	});
	it("advances a control signal by the samples asked", _ {
		Ramp r;
		expect(r.advance(128).raw()).to_equal(128);
		expect(r.advance(3).raw()).to_equal(131);
	});
});

CPPSPEC_MAIN(component_framework);
