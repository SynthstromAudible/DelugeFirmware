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
#include <numeric>
#include <span>
#include <type_traits>
#include <vector>

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
	[[nodiscard]] StereoSample render(StereoSample f) { return {.l = add_wrap(f.l, f.l), .r = add_wrap(f.r, f.r)}; }
};
struct SimdStereoDouble : Processor<SimdStereoDouble, StereoSample> {
	[[nodiscard]] StereoVector render(StereoVector v) { return {.l = v.l + v.l, .r = v.r + v.r}; }
};
/// An asymmetric stereo processor (left doubled, right unchanged), so a channel swap shows; scalar and SIMD.
struct ScalarLeftDouble : Processor<ScalarLeftDouble, StereoSample> {
	[[nodiscard]] StereoSample render(StereoSample f) { return {.l = add_wrap(f.l, f.l), .r = f.r}; }
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
		return {.l = Q<31>::from_raw(v), .r = Q<31>::from_raw(-v)};
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
	std::iota(v.begin(), v.end(), 1000);
	return v;
}

std::vector<StereoSample> stereo_ramp(std::size_t frames) {
	std::vector<StereoSample> v(frames);
	for (std::size_t i = 0; i < frames; i++) {
		v[i] = {.l = Q<31>::from_raw(static_cast<q31>(i + 1)), .r = Q<31>::from_raw(-static_cast<q31>(i + 1))};
	}
	return v;
}

/// The SIMD doubling processor over Q<31>: the typed path, against SimdDouble's raw one.
struct TypedDouble : Processor<TypedDouble, Q<31>> {
	[[nodiscard]] QVec<31> render(QVec<31> v) { return add_wrap(v, v); }
};
/// The SIMD adding mixer over Q<31>.
struct TypedAdd : Mixer<TypedAdd, Q<31>> {
	[[nodiscard]] QVec<31> render(QVec<31> a, QVec<31> b) { return add_wrap(a, b); }
};
/// A SIMD generator over Q<31> counting up by four lanes a vector, against a raw twin.
struct TypedCount : Generator<TypedCount, Q<31>> {
	QVec<31> next = QVec<31>::from_raw(Argon<int32_t>{int32x4_t{1, 2, 3, 4}});
	[[nodiscard]] QVec<31> render() {
		const QVec<31> out = next;
		next = add_wrap(next, QVec<31>::splat(Q<31>::from_raw(4)));
		return out;
	}
};
struct RawCount : Generator<RawCount, q31> {
	Argon<q31> next = Argon<q31>{int32x4_t{1, 2, 3, 4}};
	[[nodiscard]] Argon<q31> render() {
		const Argon<q31> out = next;
		next = next + Argon<q31>{4};
		return out;
	}
};

/// Samples past a block's end, holding kSentinel: a load reading them changes nothing stored, a store writing them is
/// seen.
constexpr std::size_t kTail = 4;
constexpr q31 kSentinel = 0x5a5a'5a5a;

std::vector<Q<31>> typed(const std::vector<q31>& raw) {
	std::vector<Q<31>> v(raw.size());
	for (std::size_t i = 0; i < raw.size(); i++) {
		v[i] = Q<31>::from_raw(raw[i]);
	}
	return v;
}

std::vector<q31> raw(std::span<const Q<31>> typed) {
	std::vector<q31> v(typed.size());
	for (std::size_t i = 0; i < typed.size(); i++) {
		v[i] = typed[i].raw();
	}
	return v;
}

/// The typed mixins against their raw twins at @p n samples: whether every sample agrees and the tail is untouched.
bool typed_mixins_agree(std::size_t n) {
	const auto in = ramp(n);
	auto typed_in = typed(in);
	typed_in.resize(n + kTail, Q<31>::from_raw(kSentinel));
	const std::span<const Q<31>> a = std::span{typed_in}.first(n);
	bool agree = true;
	const auto check = [&](const std::vector<q31>& want, std::vector<Q<31>>& got) {
		agree = agree && raw(std::span{got}.first(n)) == want;
		for (std::size_t i = n; i < got.size(); i++) {
			agree = agree && got[i].raw() == kSentinel;
		}
	};
	std::vector<q31> want(n);
	std::vector<Q<31>> got(n + kTail, Q<31>::from_raw(kSentinel));
	SimdDouble{}.render_block(in, want);
	TypedDouble{}.render_block(a, std::span{got}.first(n));
	check(want, got);
	got.assign(n + kTail, Q<31>::from_raw(kSentinel));
	SimdAdd{}.render_block(in, in, want);
	TypedAdd{}.render_block(a, a, std::span{got}.first(n));
	check(want, got);
	got.assign(n + kTail, Q<31>::from_raw(kSentinel));
	RawCount{}.render_block(want);
	TypedCount{}.render_block(std::span{got}.first(n));
	check(want, got);
	return agree;
}

} // namespace

// clang-format off
describe component_framework("the component mixins, AsBlock and control_generator", ${
	it("fills every sample on the scalar and SIMD paths alike, whatever the length", _ {
		for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 13u}) {
			auto in = ramp(n);
			std::vector<q31> scalar(n, -1), simd(n, -1);
			ScalarDouble{}.render_block(in, scalar);
			SimdDouble{}.render_block(in, simd);
			expect(simd).to_equal(scalar);
			expect(scalar.back()).to_equal(static_cast<q31>(in.back() * 2));
		}
	});
	it("gives the same block in place as out of place", _ {
		for (std::size_t n : {4u, 6u, 7u}) {
			auto in = ramp(n);
			std::vector<q31> out(n);
			SimdDouble{}.render_block(in, out);
			auto inplace = ramp(n);
			SimdDouble{}.render_block(inplace, inplace);
			expect(inplace).to_equal(out);
			auto scalar_inplace = ramp(n);
			ScalarDouble{}.render_block(scalar_inplace, scalar_inplace);
			expect(scalar_inplace).to_equal(out);
		}
	});
	it("processes every stereo frame on the scalar and interleaved SIMD paths alike", _ {
		for (std::size_t frames : {1u, 2u, 3u, 4u, 5u, 6u, 9u, 10u}) {
			std::vector<StereoSample> in(frames);
			for (std::size_t i = 0; i < frames; i++) {
				in[i] = {.l = Q<31>::from_raw(static_cast<q31>(i + 1)), .r = Q<31>::from_raw(-static_cast<q31>(i + 1))};
			}
			std::vector<StereoSample> scalar(frames), simd(frames);
			ScalarStereoDouble{}.render_block(in, scalar);
			SimdStereoDouble{}.render_block(in, simd);
			for (std::size_t i = 0; i < frames; i++) {
				expect(simd[i].l.raw()).to_equal(scalar[i].l.raw());
				expect(simd[i].r.raw()).to_equal(scalar[i].r.raw());
			}
			expect(scalar.back().r.raw()).to_equal(-2 * static_cast<q31>(frames));
		}
	});
	it("mixes every sample on both paths, tails included", _ {
		for (std::size_t n : {1u, 2u, 3u, 5u, 6u, 8u, 10u}) {
			auto a = ramp(n);
			auto b = ramp(n);
			std::vector<q31> scalar(n), simd(n);
			ScalarAdd{}.render_block(a, b, scalar);
			SimdAdd{}.render_block(a, b, simd);
			expect(simd).to_equal(scalar);
		}
	});
	it("generates each sample once, in order", _ {
		std::vector<q31> out(5);
		ScalarCount{}.render_block(out);
		expect(out).to_equal(std::vector<q31>{1, 2, 3, 4, 5});
	});
	it("mixes in place into either input as out of place, on both paths", _ {
		for (std::size_t n : {2u, 3u, 4u, 6u, 7u}) {
			auto a = ramp(n);
			auto b = ramp(n);
			std::vector<q31> out(n);
			SimdAdd{}.render_block(a, b, out);
			auto simd_a = ramp(n);
			SimdAdd{}.render_block(simd_a, b, simd_a);
			expect(simd_a).to_equal(out);
			auto simd_b = ramp(n);
			SimdAdd{}.render_block(a, simd_b, simd_b);
			expect(simd_b).to_equal(out);
			auto scalar_a = ramp(n);
			ScalarAdd{}.render_block(scalar_a, b, scalar_a);
			expect(scalar_a).to_equal(out);
			auto scalar_b = ramp(n);
			ScalarAdd{}.render_block(a, scalar_b, scalar_b);
			expect(scalar_b).to_equal(out);
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
		constexpr std::size_t lanes = Argon<q31>::lanes;
		for (std::size_t n : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 13u}) {
			std::vector<q31> scalar(n, -1), simd(n, -1);
			ScalarCount{}.render_block(scalar);
			SimdCount generator;
			generator.render_block(simd);
			expect(simd).to_equal(scalar);
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
			expect(scalar.back().r.raw()).to_equal(-static_cast<q31>(frames));
			expect(scalar_mix.back().l.raw()).to_equal(2 * static_cast<q31>(frames));
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
				const auto expected_l = 2 * static_cast<q31>(i + 1);
				const auto expected_r = -static_cast<q31>(i + 1);
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
		expect(through).to_equal(direct);
	});
	it("steps Q<31> vectors through each mixin as its raw twin does, the partial last vector included", _ {
		for (std::size_t n : {1u, 3u, 4u, 5u, 7u, 8u, 33u}) {
			expect(typed_mixins_agree(n)).to_be_true();
		}
	});
	it("advances a control signal by the samples asked", _ {
		Ramp r;
		expect(r.advance(128)).to_equal(128);
		expect(r.advance(3)).to_equal(131);
	});
});

CPPSPEC_MAIN(component_framework);
