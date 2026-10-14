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

// The delay's analog impulse response as a kernel. Exact reference values recorded from the reference class pin an
// impulse (each tap in order, halved and rounded) and a block of two sines through both channels, on both the kernel
// and the stereo reference (bench/reference_impulse_response.h). Stereo<AnalogImpulseResponse> is pinned against that
// reference sample by sample: over 4096 seeded stereo samples at a quarter of full scale, where the reference's plain
// adds stay in q31, in blocks of every length 1 to 9; and over two blocks at every length 1 to 9. Each run ends with 25
// silent samples, which bring out every input still in the history, so the kernel's history and the reference's
// pending sums are compared through the output; after a long full-scale input, a block of zeros rings out the
// reference's tail exactly. At full scale (INT32_MIN held, INT32_MAX held, the two alternating, the taps' signs
// reversed in time, and full-scale noise) a sum can leave q31, and the kernel is pinned against the reference with its
// adds wrapping as the hardware's do; the reversed signs are pinned to reach the wrap, in many outputs of a long
// stream, where the even and odd taps' sums together leave q31.

#include "dsp-next/fixed/delay/analog_impulse_response.h"

#include "bench/reference_impulse_response.h"
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/noise/noise.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using deluge::bench::reference::ReferenceImpulseResponse;
using deluge::bench::reference::WrappingReferenceImpulseResponse;

namespace {

static_assert(processor_kernel<AnalogImpulseResponse>);
static_assert(!moded_kernel<AnalogImpulseResponse>);
static_assert(!std::is_polymorphic_v<AnalogImpulseResponse>);
static_assert(processor<Stereo<AnalogImpulseResponse>>);
static_assert(detail::plain_value<AnalogImpulseResponse::State>);
static_assert(std::extent_v<decltype(AnalogImpulseResponse::State::history)> == 50);
static_assert(AnalogImpulseResponse::kHistory == 25);
static_assert(std::size(AnalogImpulseResponse::kResponse) == 26);

/// The extremes of q31.
constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();
/// The samples of the long run.
constexpr std::size_t kLongRun = 4096;
/// The block lengths every case runs.
constexpr std::size_t kMaxLength = 9;
/// The silent samples that bring out every input still in the history.
constexpr std::size_t kFlush = 25;

/// @p n stereo samples of seeded noise shifted down by @p shift, the channels from two streams.
std::vector<StereoSample> noise(std::size_t n, uint32_t seed, int shift) {
	SeededNoise left{seed};
	SeededNoise right{seed ^ 0x5bd1e995u};
	std::vector<StereoSample> v(n);
	for (StereoSample& s : v) {
		s = {.l = left.render() >> shift, .r = right.render() >> shift};
	}
	return v;
}

/// The full-scale patterns: INT32_MIN held, INT32_MAX held, the two alternating, and the taps' signs reversed in time.
constexpr int kFullScalePatterns = 4;

/// @p n stereo samples at full scale in @p pattern, the right channel opposite the left. The last pattern gives every
/// 26th sample's product with each tap the tap's sign, which drives the response's output past full scale.
std::vector<StereoSample> full_scale(std::size_t n, int pattern) {
	std::vector<StereoSample> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		const std::size_t reversed = AnalogImpulseResponse::kTaps - 1 - (i % AnalogImpulseResponse::kTaps);
		const bool high = pattern == 0   ? false
		                  : pattern == 1 ? true
		                  : pattern == 2 ? i % 2 == 1
		                                 : AnalogImpulseResponse::kResponse[reversed] > 0;
		v[i] = {.l = high ? kMax : kMin, .r = high ? kMin : kMax};
	}
	return v;
}

/// @p frames as interleaved samples, left first.
std::vector<q31> interleaved(const std::vector<StereoSample>& frames) {
	std::vector<q31> v;
	for (const StereoSample& f : frames) {
		v.push_back(f.l);
		v.push_back(f.r);
	}
	return v;
}

/// An impulse of -1.0 on the left and +1.0 (ONE_Q31) on the right, then silence: 30 samples, interleaved L/R. Each
/// output is a tap times the impulse with the rounded high-word multiply, so the taps appear in order, halved.
const std::vector<int32_t> kImpulse{
    1601958,    -1601958,  -4428924,  4428924,   -12406568, 12406568,  -20768904,  20768904,  -17608736,  17608736,
    -7597816,   7597816,   13769296,  -13769296, 30992064,  -30992064, -972327424, 972327424, -906790464, 906790464,
    -219231392, 219231392, -50562544, 50562544,  -3021024,  3021024,   11214744,   -11214744, 23109432,   -23109432,
    28319280,   -28319280, 32392656,  -32392656, 26054264,  -26054264, 18628496,   -18628496, 5931928,    -5931928,
    -695176,    695176,    -7331648,  7331648,   -6392232,  6392232,   -7127400,   7127400,   -2845456,   2845456,
    -2245368,   2245368,   0,         0,         0,         0,         0,          0,         0,          0};

/// 440 Hz plus 1250 Hz on the left and their difference on the right, each sine 2^29 at its peak, 128 samples
/// through the response, interleaved L/R.
const std::vector<int32_t> kTwoSine{
    0,          0,          -96039,      45859,      75784,       -37213,     989526,      -473733,    3124223,
    -1482665,   6238486,    -2926032,    9656781,    -4448452,    12016263,   -5363830,    12229779,   -5130178,
    70446565,   -32470347,  181323725,   -84215634,  300953189,   -138227492, 416347740,   -187059906, 521928900,
    -226885099, 614414161,  -255124816,  691054724,  -269655245,  749974195,  -269007200,  789889679,  -252259406,
    810703416,  -219338169, 812946540,   -170753125, 798013374,   -107700565, 767440191,   -31698224,  723221319,
    55288975,   667291721,  151128621,   602017604,  253320736,   529718562,  359266567,   453111824,  466097897,
    374904852,  570918192,  297940309,   670780227,  224970087,   762842157,  158566994,   844456165,  101042344,
    913251181,  54371994,   967207072,   20133245,   1004717916,  -545477,    1024642419,  -7020461,   1026339852,
    846942,     1009690327, 22686194,    975098880,  57629778,    923483133,  104341342,   856244936,  161058856,
    775226963,  225651492,  682655504,   295688344,  581071390,   368516856,  473251121,   441348431,  362120752,
    511348574,  250665247,  575728637,   141836119,  631836284,   38460338,   677241805,   -56846696,  709817576,
    -141761308, 727808063,  -214322343,  729888202,  -272990349,  715208307,  -316693315,  683423987,  -344857501,
    634710274,  -357422410, 569759301,   -354839427, 489761760,   -338054233, 396372645,   -308473472, 291662306,
    -267916816, 178054455,  -218555934,  58252969,   -162842305,  -64840131,  -103426158,  -188213022, -43069173,
    -308835852, 15446391,   -423753194,  69409100,   -530174133,  116267504,  -625557252,  153714179,  -707687764,
    179761977,  -774744455, 192809991,   -825354211, 191697300,   -858632546, 175742618,   -874208730, 144768634,
    -872234878, 99110334,   -853378569,  39606941,   -818799387,  -32422295,  -770109988,  -115217580, -709323067,
    -206632225, -638785834, -304198691,  -561104100, -405204989,  -479058352, -506778988,  -395514549, -605978015,
    -313332381, -699880873, -235273966,  -785679398, -163915828,  -860766660, -101566964,  -922819006, -50195613,
    -969869361, -11367009,  -1000369478, 13805930,   -1013239145, 24697299,   -1007900802, 21187403,   -984298509,
    3665592,    -942900560, -26983088,   -884685792, -69407240,   -811113909, -121833338,  -724080806, -182123529,
    -625860286, -247845157, -519034030,  -316349779, -406411971,  -384859170, -290945648,  -450555622, -175637252,
    -510673638, -63447188,  -562590099,  42796858,   -603910103,  140486540,  -632545664,  227312412,  -646784821,
    301333914,  -645349000, 361037597,   -627436667, 405381736,   -592752050, 433825965,   -541517853, 446344983,
    -474471668, 443425918,  -392846105,  426049444,  -298333287,  395655268,  -193034822,  354093091,  -79398798,
    303560618,  39854167,   246530540,   161813581,  185668911,   283467735,  123747384,   401795687,  63552246,
    513859540,  7793015,    616894229,   -40986462,  708391939,   -80491508,  786178459,   -108752182, 848479161,
    -124188422, 893972424,  -125662720,  921828884,  -112518658,  931735302,  -84604065,   923902217,  -42278074,
    899055246,  13598197,   858410232,   81686994};

/// Two sines at 440 Hz and 1250 Hz (44.1 kHz), each 2^29 at its peak, one sample per next(): the left channel their
/// sum, the right their difference. Integer resonators, so every platform makes the same block.
class TwoSines {
public:
	StereoSample next() {
		const auto s440 = static_cast<int32_t>(a1_);
		const auto s1250 = static_cast<int32_t>(b1_);
		step(a1_, a2_, 1071632635);
		step(b1_, b2_, 1056758439);
		return {.l = s440 + s1250, .r = s440 - s1250};
	}

private:
	/// One step of y[n] = 2 cos(w) y[n-1] - y[n-2], with cos(w) as Q30 @p cosine.
	static void step(int64_t& older, int64_t& newer, int64_t cosine) {
		const int64_t next = ((2 * cosine * newer) >> 30) - older;
		older = newer;
		newer = next;
	}
	int64_t a1_ = 0;
	int64_t a2_ = 33634063;
	int64_t b1_ = 0;
	int64_t b2_ = 95109287;
};

/// @p seeded full-scale noise samples, then 26 * 8 at full scale whose signs follow the taps reversed in time (the
/// left upwards, the right downwards), so every 26th output's products all share a sign and its sum leaves q31; then
/// @p zeros silent samples.
std::vector<StereoSample> wrap_stream(std::size_t seeded, std::size_t zeros) {
	std::vector<StereoSample> v = noise(seeded, 0x12345678, 0);
	const std::vector<StereoSample> reversed = full_scale(AnalogImpulseResponse::kTaps * 8, 3);
	v.insert(v.end(), reversed.begin(), reversed.end());
	v.insert(v.end(), zeros, StereoSample{.l = 0, .r = 0});
	return v;
}

/// How many of the outputs over @p in are sums outside q31: the exact 26-product sum, in 64 bits, past either end.
std::size_t wraps(const std::vector<StereoSample>& in) {
	std::size_t count = 0;
	for (std::size_t n = 0; n < in.size(); ++n) {
		int64_t l = 0;
		int64_t r = 0;
		for (std::size_t i = 0; i < AnalogImpulseResponse::kTaps && i <= n; ++i) {
			l += multiply_32x32_rshift32_rounded(in[n - i].l, AnalogImpulseResponse::kResponse[i]);
			r += multiply_32x32_rshift32_rounded(in[n - i].r, AnalogImpulseResponse::kResponse[i]);
		}
		for (const int64_t sum : {l, r}) {
			if (sum > kMax || sum < kMin) {
				++count;
			}
		}
	}
	return count;
}

/// @p in through a fresh @p Reference one sample at a time, interleaved.
template <typename Reference>
std::vector<q31> reference_of(const std::vector<StereoSample>& in) {
	Reference reference;
	std::vector<StereoSample> out(in.size());
	for (std::size_t i = 0; i < in.size(); ++i) {
		out[i] = reference.process(in[i]);
	}
	return interleaved(out);
}

/// @p in through a fresh Stereo<AnalogImpulseResponse> in blocks of @p block, interleaved.
std::vector<q31> kernel_of(const std::vector<StereoSample>& in, std::size_t block) {
	Stereo<AnalogImpulseResponse> kernel;
	std::vector<StereoSample> out(in.size());
	for (std::size_t start = 0; start < in.size(); start += block) {
		const std::size_t n = std::min(block, in.size() - start);
		kernel.renderBlock(std::span<const StereoSample>{in}.subspan(start, n),
		                   std::span<StereoSample>{out}.subspan(start, n));
	}
	return interleaved(out);
}

/// What a run gave: the kernel's output and the reference's, interleaved.
struct Run {
	std::vector<q31> got;
	std::vector<q31> want;
};

/// @p input, then kFlush silent samples, through Stereo<AnalogImpulseResponse> in blocks of @p block, and through
/// a fresh @p Reference one sample at a time.
template <typename Reference>
Run run(const std::vector<StereoSample>& input, std::size_t block) {
	std::vector<StereoSample> in = input;
	in.resize(input.size() + kFlush, StereoSample{.l = 0, .r = 0});

	Stereo<AnalogImpulseResponse> kernel;
	std::vector<StereoSample> out(in.size());
	for (std::size_t start = 0; start < in.size(); start += block) {
		const std::size_t n = std::min(block, in.size() - start);
		kernel.renderBlock(std::span<const StereoSample>{in}.subspan(start, n),
		                   std::span<StereoSample>{out}.subspan(start, n));
	}

	Reference reference;
	std::vector<StereoSample> want(in.size());
	for (std::size_t i = 0; i < in.size(); ++i) {
		want[i] = reference.process(in[i]);
	}
	return {.got = interleaved(out), .want = interleaved(want)};
}

/// @p input(i) for @p count samples through a fresh Stereo<AnalogImpulseResponse>, one block, and through the
/// reference class, interleaved.
Run run_recorded(auto&& input, int count) {
	std::vector<StereoSample> in;
	for (int i = 0; i < count; i++) {
		in.push_back(input(i));
	}
	Stereo<AnalogImpulseResponse> kernel;
	std::vector<StereoSample> out(in.size());
	kernel.renderBlock(in, out);
	ReferenceImpulseResponse reference;
	std::vector<StereoSample> want(in.size());
	for (std::size_t i = 0; i < in.size(); ++i) {
		want[i] = reference.process(in[i]);
	}
	return {.got = interleaved(out), .want = interleaved(want)};
}

} // namespace

// clang-format off
describe analog_impulse_response("AnalogImpulseResponse", ${
	it("answers an impulse with its 26 taps in order, then silence, as recorded from the class", _ {
		const Run r = run_recorded([](int i) {
			return i == 0 ? StereoSample{.l = kMin, .r = kMax} : StereoSample{.l = 0, .r = 0};
		}, 30);
		expect(r.got).to_equal(kImpulse);
		expect(r.want).to_equal(kImpulse);
	});

	it("filters a block of two sines exactly, each channel on its own, as recorded from the class", _ {
		TwoSines sines;
		const Run r = run_recorded([&](int) { return sines.next(); }, 128);
		expect(r.got).to_equal(kTwoSine);
		expect(r.want).to_equal(kTwoSine);
	});

	it("starts silent: a fresh response outputs nothing for silence", _ {
		const Run r = run_recorded([](int) { return StereoSample{.l = 0, .r = 0}; }, 26);
		expect(r.got).to_equal(std::vector<q31>(52, 0));
		expect(r.want).to_equal(std::vector<q31>(52, 0));
	});

	it("equals the class over 4096 seeded stereo samples, in blocks of every length 1 to 9", _ {
		for (std::size_t block = 1; block <= kMaxLength; ++block) {
			const Run r = run<ReferenceImpulseResponse>(noise(kLongRun, 0x1a2b3c4d, 2), block);
			expect(r.got).to_equal(r.want);
		}
	});

	it("equals the class over two blocks, at every length 1 to 9", _ {
		for (std::size_t n = 1; n <= kMaxLength; ++n) {
			const Run r = run<ReferenceImpulseResponse>(noise(2 * n, 0x0bad5eed + static_cast<uint32_t>(n), 2), n);
			expect(r.got).to_equal(r.want);
		}
	});

	it("equals the class, its adds wrapping, at full scale", _ {
		for (int pattern = 0; pattern < kFullScalePatterns; ++pattern) {
			for (std::size_t n = 1; n <= kMaxLength; ++n) {
				const Run r = run<WrappingReferenceImpulseResponse>(full_scale(64, pattern), n);
				expect(r.got).to_equal(r.want);
			}
		}
		for (std::size_t block = 1; block <= kMaxLength; ++block) {
			const Run r = run<WrappingReferenceImpulseResponse>(noise(kLongRun, 0x600df00d, 0), block);
			expect(r.got).to_equal(r.want);
		}
	});

	it("wraps where the class's plain adds would leave q31", _ {
		// The taps' signs reversed in time give the 26th output every tap's product at full scale with the tap's
		// sign: their sum leaves q31, so the full-scale case above reaches the wrap.
		int64_t sum = 0;
		for (const int32_t t : AnalogImpulseResponse::kResponse) {
			sum += multiply_32x32_rshift32_rounded(t > 0 ? kMax : kMin, t);
		}
		expect(sum).to_be_greater_than(int64_t{kMax});
		const Run r = run<WrappingReferenceImpulseResponse>(full_scale(AnalogImpulseResponse::kTaps, 3), 1);
		expect(r.got[2 * (AnalogImpulseResponse::kTaps - 1)]).to_equal(static_cast<q31>(static_cast<uint32_t>(sum)));
	});

	it("wraps as the class does over a long stream that leaves q31 many times, in blocks of every length 1 to 9", _ {
		const std::vector<StereoSample> in = wrap_stream(kLongRun, 64);
		expect(wraps(in)).to_be_greater_than(std::size_t{8});
		const std::vector<q31> want = reference_of<WrappingReferenceImpulseResponse>(in);
		const auto tail = want.end() - (2 * 64);
		expect(std::count(tail, tail + (2 * kFlush), 0)).to_be_less_than(std::ptrdiff_t{4});
		expect(std::count(tail + (2 * kFlush), want.end(), 0)).to_equal(std::ptrdiff_t{2 * (64 - kFlush)});
		for (std::size_t block = 1; block <= kMaxLength; ++block) {
			expect(kernel_of(in, block)).to_equal(want);
		}
	});

	it("rings out the class's tail from a block of zeros after a long input: the history holds what its sums do", _ {
		std::vector<StereoSample> in = noise(kLongRun, 0x5eed7a11, 0);
		in.insert(in.end(), 32, StereoSample{.l = 0, .r = 0});
		const std::vector<q31> want = reference_of<WrappingReferenceImpulseResponse>(in);
		const std::vector<q31> tail{want.end() - 64, want.end()};
		expect(std::count(tail.begin(), tail.begin() + (2 * kFlush), 0)).to_be_less_than(std::ptrdiff_t{4});
		expect(std::count(tail.begin() + (2 * kFlush), tail.end(), 0)).to_equal(std::ptrdiff_t{64 - (2 * kFlush)});
		const std::vector<q31> got = kernel_of(in, 32);
		expect(std::vector<q31>{got.end() - 64, got.end()}).to_equal(tail);
	});
});
// clang-format on

CPPSPEC_MAIN(analog_impulse_response);
