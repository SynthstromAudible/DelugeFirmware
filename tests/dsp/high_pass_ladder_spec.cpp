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

// HighPassLadder: the transistor high-pass, mono and stereo, with its dry/wet fade. Exact reference values pin the
// arithmetic, each case from a filter zeroed as the filter set zeroes it and with the synth-wide noise generator seeded
// the same.
//
// HighPassLadderKernel: the same ladder as a processor kernel, `Mono` and `Stereo`. Against the verbatim reference
// (reference_filters.h) over every high-pass workload (filter_workloads.h): each block's output, the gain, the CONG
// draws (none) and the whole state, the saturator's position included, at block lengths cycling 1–9, 24, 64 and 128;
// and what it renders must equal the record. Every coefficient equals the reference's configure across the cutoff and
// resonance range, the saturator switches at the reference's resonances, and every switch between bypass, tanh and the
// antialiased tanh carries the state.

#include "dsp-next/fixed/filter/high_pass_ladder.h"
#include "dsp-next/fixed/filter/high_pass_ladder_kernel.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/shaping/tanh.h" // kTanhCentre
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "kernel_filters.h"
#include "reference_filters.h"
#include "util/waves.h" // jcong, CONG

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace deluge::dsp::fixed;

namespace {

constexpr uint32_t kSeed = 123456789;
constexpr int32_t kGain = 536870912;

/// Two sines (3 and 11 cycles per 64 samples, at 2^25 and 2^24): a mono block, or 32 interleaved L/R frames.
const std::vector<int32_t> kInput{
    0,         24536521,  32591589,  24293472,  15500126,  20423895,  36182765,  45647162,  35589850,  10947281,
    -9908702,  -13932263, -6420363,  -4590277,  -18578569, -40018314, -50331648, -40018314, -18578569, -4590277,
    -6420363,  -13932263, -9908702,  10947281,  35589850,  45647162,  36182765,  20423895,  15500126,  24293472,
    32591589,  24536521,  0,         -24536521, -32591589, -24293472, -15500126, -20423895, -36182765, -45647162,
    -35589850, -10947281, 9908702,   13932263,  6420363,   4590277,   18578569,  40018314,  50331648,  40018314,
    18578569,  4590277,   6420363,   13932263,  9908702,   -10947281, -35589850, -45647162, -36182765, -20423895,
    -15500126, -24293472, -32591589, -24536521};

constexpr FilterConfig kDark{.frequency = 1 << 22, .resonance = 0, .morph = 0, .allow_oversampling = true};
constexpr FilterConfig kMid{.frequency = 1 << 24, .resonance = 1 << 28, .morph = 1 << 27, .allow_oversampling = true};
constexpr FilterConfig kBright{
    .frequency = 50000000, .resonance = 500000000, .morph = 1 << 28, .allow_oversampling = true};

/// One block's output, the compensated gain, and the noise generator's next value after the block.
struct Rendered {
	std::vector<int32_t> out;
	int32_t gain;
	int32_t next_noise;
};
/// The noise generator's next value when nothing has drawn from it since the seed.
constexpr int32_t kUndrawnNextNoise = 1528111448;

Rendered renderMono(const FilterConfig& config) {
	jcong = kSeed;
	HighPassLadder filter;
	filter.zero();
	const int32_t gain = filter.configure(kGain, config);
	std::vector<int32_t> block = kInput;
	std::vector<int32_t> scratch(block.size());
	filter.renderMono(block, scratch);
	return {.out = block, .gain = gain, .next_noise = static_cast<int32_t>(CONG)};
}

Rendered renderStereo(const FilterConfig& config) {
	jcong = kSeed;
	HighPassLadder filter;
	filter.zero();
	const int32_t gain = filter.configure(kGain, config);
	std::vector<int32_t> block = kInput;
	std::vector<int32_t> scratch(block.size());
	filter.renderStereo(block, scratch);
	return {.out = block, .gain = gain, .next_noise = static_cast<int32_t>(CONG)};
}

const std::vector<int32_t> kHpDarkMono{
    0,         25392896,  29979136,  16881664,  5121792,   9267456,   24029440,  30096640,  15016192,  -12991744,
    -32945920, -32443392, -19936768, -15042304, -27185408, -45216256, -49005056, -30774784, -3615744,  11850496,
    8598528,   -156672,   4243456,   25378816,  47268352,  50726400,  33311744,  11811072,  4635648,   12732928,
    19159552,  7718144,   -19106816, -41947136, -44251136, -29127424, -15581184, -18160384, -31556864, -36440064,
    -20337920, 8547328,   29251328,  29387008,  17421056,  12982528,  25508352,  43859456,  47914240,  29904384,
    2927104,   -12390400, -9017344,  -163840,   -4484864,  -25556992, -47396608, -50814976, -33370112, -11845888,
    -4652800,  -12737024, -19153920, -7706112};
const std::vector<int32_t> kHpDarkStereo{
    0,         25392896,  33729024,  21391360,  11059968,  14145280,  30721536,  38021376,  25443584,  -3665920,
    -25606656, -29131008, -18469888, -15364608, -28454400, -49835520, -57161472, -42488832, -15806208, 601856,
    -659712,   -8890624,  -3929344,  18388736,  43942400,  51795712,  38250752,  18159616,  11207936,  19411456,
    27124992,  16677888,  -10742784, -36732672, -43084544, -31246080, -19165184, -22665472, -37708800, -45352448,
    -31438592, -2612480,  20486656,  23778560,  14116864,  10822144,  24769792,  45997568,  54056704,  39260416,
    13201664,  -3304960,  -1515264,  6637312,   2121472,   -20258304, -45437952, -53338880, -39481600, -19427072,
    -12215552, -20446720, -27945472, -17518848};
const std::vector<int32_t> kHpMidMono{
    0,         23927936,  27401856,  4731904,   -20536320, -20860672, 2418688,   21733248,  11258496,  -24072448,
    -45555200, -38694400, -9689600,  15924480,  13491200,  -10552960, -24639488, -11114240, 19613568,  37052544,
    28194816,  2866432,   -10048128, 3103744,   25146752,  28069248,  1941760,   -30285440, -36242816, -13859584,
    15422592,  18582528,  -10904192, -40412544, -42579328, -19181568, 11960320,  21842944,  6874112,   -9281664,
    -2317056,  23249536,  39358080,  30127488,  -330240,   -21838848, -13834112, 13577856,  27953920,  9711872,
    -27088512, -42950528, -29474176, 1105664,   17142144,  1314688,   -27802368, -36558208, -17665664, 14906240,
    30711040,  20378624,  242944,    -2302336};
const std::vector<int32_t> kHpMidStereo{
    0,         23927936,  30774144,  19974144,  9298432,   3002496,   15078528,  14414848,  2228352,   -28716928,
    -48682496, -51443712, -43138560, -31781504, -35856128, -41604608, -42383872, -20588416, 13265664,  37752576,
    44411392,  40901120,  41988224,  50654208,  57601664,  53145472,  30060160,  -7022336,  -28784000, -34303104,
    -32415872, -38043776, -51373824, -59373056, -57200512, -40025088, -17452416, -5695616,  -7148928,  -5419392,
    13917056,  42039168,  57677440,  58903552,  48225280,  35238656,  33794944,  36107136,  29864192,  3091840,
    -32551424, -51451008, -53493888, -44867712, -40272768, -45928192, -52959616, -50497920, -30274304, 817792,
    21118080,  25478528,  25151360,  35133056};
const std::vector<int32_t> kHpBrightMono{
    -100094720, 32736128, 48544000, -33339904, -29604736, 40879360,  9904384,  -25663104, 3172352,  11011072, -20425600,
    -9092736,   17264640, 7068160,  -18591232, -14693632, 5015936,   14152192, 8771200,   -1233152, -4729344, -1568768,
    3704192,    8176256,  5925632,  -1701888,  -4548480,  -2390656,  369024,   3495808,   2292352,  -4557056, -9286016,
    -7382784,   96640,    7450624,  4953216,   -5872000,  -10666752, -2930432, 9847168,   13933440, 3633408,  -6016512,
    -2881536,   3431040,  5282048,  4107776,   237696,    -4339840,  -5733888, -2303360,  3291264,  3730432,  -3414784,
    -9856384,   -9161728, -1297920, 8840192,   9736960,   -1595392,  -9594496, -3524480,  8515200};
const std::vector<int32_t> kHpBrightStereo{
    -100094720, -97121024, 34278144,  35654016,  39300736,  44180224,  -33554688, -26775808, -14654720, -28541568,
    20440064,   25673728,  -8046080,  12841088,  -13342848, -27761280, -11283072, -21820416, 15558528,  32446464,
    17748096,   13083776,  -12533376, -20921984, -293504,   8087936,   13369984,  6152448,   -12723840, -15453824,
    -5108864,   2561152,   2398592,   -7438592,  -17111040, -14213248, -897920,   8959872,   6879616,   -694528,
    -5296000,   -993280,   10809600,  14789120,  6352000,   -2892928,  -8135552,  -3677696,  8385792,   7715584,
    -4682112,   -12433280, -17003776, -7120256,  8722176,   4226304,   -6907776,  -15477120, -19801216, -1849856,
    12931840,   13686016,  8981248,   -1475072};
const std::vector<int32_t> kHpFreshMono{
    0,         24530434,  32488312,  23712474,  14080146,  18400542,  34206896,  44022578,  33710124,  7918678,
    -13317148, -16523914, -6791798,  -2077650,  -14369276, -35894884, -46515276, -35478730, -12258294, 2648552,
    -117180,   -10735940, -9936360,  9328486,   33351666,  41741724,  28308810,  8372450,   2808578,   14647312,
    28122502,  22942656,  -2998868,  -29017840, -35482508, -22777526, -7163498,  -7297888,  -22514556, -33854886,
    -24575612, 601602,    20049326,  19615126,  4007776,   -5025024,  6580160,   30064152,  41767470,  28232660,
    540350,    -14475676, -8189918,  8635350,   12938310,  -5740296,  -32238128, -41683518, -28003136, -4620144,
    5426294,   -3819582,  -17365172, -14106588};
const std::vector<int32_t> kHpFadeMono{
    0,         24530434,  32488312,  23712474,  14080146,  18400542,  34206896,  44022578,  33710124,  7918678,
    -13317148, -16523914, -6791798,  -2077650,  -14369276, -35894884, -46515276, -35478730, -12258294, 2648552,
    -117180,   -10735940, -9936360,  9328486,   33351666,  41741724,  28308810,  8372450,   2808578,   14647312,
    28122502,  22942656,  -2998868,  -29017840, -35482508, -22777526, -7163498,  -7297888,  -22514556, -33854886,
    -24575612, 601602,    20049326,  19615126,  4007776,   -5025024,  6580160,   30064152,  41767470,  28232660,
    540350,    -14475676, -8189918,  8635350,   12938310,  -5740296,  -32238128, -41683518, -28003136, -4620144,
    5426294,   -3819582,  -17365172, -14106588};
const std::vector<int32_t> kHpFadeStereo{
    0,         24530434,  32555422,  24165182,  15255754,  19570074,  34947748,  43525404,  33012498,  7517010,
    -13616174, -17858278, -10592108, -7920632,  -20846322, -40240304, -49151068, -36966716, -13308786, 2770240,
    2835746,   -3498998,  386062,    19142222,  40307462,  47313126,  34774830,  13901098,  4638082,   9478620,
    15670166,  7783982,   -14128814, -34369814, -39714564, -28958722, -16092816, -15850014, -26966106, -32602462,
    -19201662, 6947200,   26357430,  29712572,  21360956,  15740602,  24211310,  38545854,  42498550,  25658244,
    -1617466,  -17884742, -17966746, -10349960, -11108980, -25801756, -43065776, -47762554, -33572800, -10922140,
    1082186,   -1482220,  -5814346,  3454148};
const std::vector<int32_t> kHpResetMono{
    0,         23927936,  27401856,  4731904,   -20536320, -20860672, 2418688,   21733248,  11258496,  -24072448,
    -45555200, -38694400, -9689600,  15924480,  13491200,  -10552960, -24639488, -11114240, 19613568,  37052544,
    28194816,  2866432,   -10048128, 3103744,   25146752,  28069248,  1941760,   -30285440, -36242816, -13859584,
    15422592,  18582528,  -10904192, -40412544, -42579328, -19181568, 11960320,  21842944,  6874112,   -9281664,
    -2317056,  23249536,  39358080,  30127488,  -330240,   -21838848, -13834112, 13577856,  27953920,  9711872,
    -27088512, -42950528, -29474176, 1105664,   17142144,  1314688,   -27802368, -36558208, -17665664, 14906240,
    30711040,  20378624,  242944,    -2302336};
const std::vector<int32_t> kHpMidFadeResetMono{
    0,         24244606,  30075280,  14708022,  -2342030,  -225120,   19126454,  33448440,  23058990,  -7258028,
    -28610994, -27041670, -8166526,  6462624,   -1152170,  -23872586, -36137324, -23920130, 2861830,   18969928,
    13314130,  -4282926,  -9989384,  6375444,   29459222,  35255450,  15800184,  -9966992,  -15717544, 1123540,
    22097620,  20874196,  -6749182,  -34423528, -38849266, -21071592, 1908910,   6526628,   -8572466,  -22197260,
    -14016076, 11345862,  29209466,  24602226,  1949806,   -13001562, -3104446,  22242992,  35214272,  19446332,
    -12566884, -27984248, -18287246, 5063238,   14932622,  -2393386,  -30133788, -39252054, -23099002, 4643242,
    17421466,  7660130,   -9011822,  -8506610};

} // namespace

namespace {

using deluge::dsp::Mono;
using deluge::dsp::q31;
using deluge::dsp::Stereo;
using filter_workloads::WorkloadId;
using filters_reference::ReferenceHighPassLadder;
using Ladder = HighPassLadderKernel;

static_assert(deluge::dsp::processor_kernel<Ladder>);
static_assert(deluge::dsp::moded_kernel<Ladder>);
static_assert(!std::is_polymorphic_v<Ladder>);
static_assert(deluge::dsp::processor<Mono<Ladder>>);
static_assert(deluge::dsp::processor<Stereo<Ladder>>);

/// Every high-pass workload: the saturator bypassed, tanh, the antialiased tanh, the resonance swept through all three,
/// and the fade, mono and stereo.
constexpr std::array<WorkloadId, 10> kHighPassWorkloads{
    WorkloadId::HP_MONO,
    WorkloadId::HP_STEREO,
    WorkloadId::HP_TANH_MONO,
    WorkloadId::HP_TANH_STEREO,
    WorkloadId::HP_SATURATED_MONO,
    WorkloadId::HP_SATURATED_STEREO,
    WorkloadId::HP_RESONANCE_SWEEP_MONO,
    WorkloadId::HP_RESONANCE_SWEEP_STEREO,
    WorkloadId::HP_FADE_MONO,
    WorkloadId::HP_FADE_STEREO,
};

/// @return Each of @p ids where the kernel parts from the reference, named with the block and what parted.
std::vector<std::string> partings(std::span<const WorkloadId> ids) {
	std::vector<std::string> parted;
	for (const WorkloadId id : ids) {
		const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
		const auto parting = kernel_filters::first_parting(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
}

/// @return The high-pass settings at @p resonance (Q29), the cutoff and morph mid-range.
FilterConfig config_at(q31 resonance) {
	return {.frequency = 1 << 23, .resonance = resonance, .morph = 1 << 26, .allow_oversampling = true};
}

/// The saturator's three settings: bypassed, tanh and the antialiased tanh, by resonance.
constexpr std::array<q31, 3> kSaturations{filter_workloads::detail::kHpBypass, filter_workloads::detail::kHpTanh,
                                          filter_workloads::detail::kHpSaturated};

/// @return The resonances (Q29) at which the kernel's saturator first leaves BYPASS and first leaves TANH, found by
/// bisection. The processed resonance the switch reads does not depend on the cutoff.
std::vector<q31> mode_changes() {
	const auto mode = [](q31 resonance) {
		return Ladder::coefficients({.gain = kGain, .config = config_at(resonance)}).mode;
	};
	std::vector<q31> changes;
	for (const Ladder::Mode below : {Ladder::Mode::BYPASS, Ladder::Mode::TANH}) {
		q31 low = 0;
		q31 high = 536870911;
		while (high - low > 1) {
			const q31 middle = low + ((high - low) / 2);
			(mode(middle) <= below ? low : high) = middle;
		}
		changes.push_back(high);
	}
	return changes;
}

/// The configs the coefficient check sweeps: dark to bright, resonance off, each saturation and past the limit, morph
/// off, low and high.
std::vector<FilterConfig> sweep_configs() {
	std::vector<FilterConfig> configs;
	for (const q31 frequency : {q31{1 << 20}, q31{1 << 22}, q31{1 << 23}, q31{1 << 24}, q31{1 << 26}, q31{ONE_Q31}}) {
		for (const q31 resonance : {q31{0}, filter_workloads::detail::kHpBypass, filter_workloads::detail::kHpTanh,
		                            filter_workloads::detail::kHpSaturated, q31{536870911}, q31{1 << 30}}) {
			for (const q31 morph : {q31{0}, q31{1 << 26}, q31{536870911}}) {
				configs.push_back(
				    {.frequency = frequency, .resonance = resonance, .morph = morph, .allow_oversampling = true});
			}
		}
	}
	return configs;
}

/// A channel's three sections' memories (Q31) and the saturator's position (unsigned over 32 bits).
using ChannelWords = std::array<int64_t, 4>;

ChannelWords words(const Ladder::State& s) {
	return {s.high_pass_1.z, s.low_pass_1.z, s.high_pass_3.z, s.saturator.position};
}

ChannelWords words(const ReferenceHighPassLadder::Channel& c) {
	return {c.high_pass_1.memory(), c.low_pass_1.memory(), c.high_pass_3.memory(), c.last_working_value};
}

} // namespace

// clang-format off
describe high_pass_ladder("HighPassLadder", ${
	it("renders dark, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kDark);
		expect(r.out).to_equal(kHpDarkMono);
		expect(r.gain).to_equal(536870910);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders dark, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kDark);
		expect(r.out).to_equal(kHpDarkStereo);
		expect(r.gain).to_equal(536870910);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders mid, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kMid);
		expect(r.out).to_equal(kHpMidMono);
		expect(r.gain).to_equal(516947966);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders mid, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kMid);
		expect(r.out).to_equal(kHpMidStereo);
		expect(r.gain).to_equal(516947966);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders bright, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kBright);
		expect(r.out).to_equal(kHpBrightMono);
		expect(r.gain).to_equal(297057200);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders bright, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kBright);
		expect(r.out).to_equal(kHpBrightStereo);
		expect(r.gain).to_equal(297057200);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("fades in from dry when fresh (its initializers start a fade)", _ {
		jcong = kSeed;
		HighPassLadder filter;
		std::ignore = filter.configure(kGain, kMid);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		expect(block).to_equal(kHpFreshMono);
	});
	it("fades in after reset(true) in mono", _ {
		jcong = kSeed;
		HighPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(true);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kHpFadeMono);
	});
	it("fades in after reset(true) in stereo, each channel's sample stepping the fade", _ {
		jcong = kSeed;
		HighPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderStereo(block, scratch);
		filter.reset(true);
		block = kInput;
		filter.renderStereo(block, scratch);
		expect(block).to_equal(kHpFadeStereo);
	});
	it("clears its memories without a fade after reset(false)", _ {
		jcong = kSeed;
		HighPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(false);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kHpResetMono);
	});
	it("keeps fading through reset(false) when a fade is under way", _ {
		jcong = kSeed;
		HighPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid);
		filter.reset(true);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(false);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kHpMidFadeResetMono);
	});
});
// clang-format on

// clang-format off
describe high_pass_ladder_kernel("HighPassLadderKernel", ${
	it("renders every high-pass workload as the reference does, mono and stereo: each block's output, gain and CONG "
	   "draws, and the whole state", _ {
		expect(partings(kHighPassWorkloads)).to_equal(std::vector<std::string>{});
	});

	it("renders every high-pass workload as recorded", _ {
		for (const WorkloadId id : kHighPassWorkloads) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			expect(kernel_filters::record(workload)).to_equal(filter_workloads::kRecorded.at(static_cast<size_t>(id)));
		}
	});

	it("configures every coefficient and compensates the gain as the reference's configure does", _ {
		std::array<std::size_t, 3> modes{};
		for (const FilterConfig& config : sweep_configs()) {
			ReferenceHighPassLadder reference;
			reference.zero();
			const q31 gain = reference.configure(kGain, config);
			const Ladder::Coefficients c = Ladder::coefficients({.gain = kGain, .config = config});
			expect(Ladder::gain(c)).to_equal(gain);
			++modes.at(static_cast<std::size_t>(c.mode));

			// Every coefficient: the adapter's digest folds them all, as the reference's folds its members.
			kernel_filters::KernelHighPassLadder<false> kernel;
			kernel.zero();
			expect(kernel.configure(kGain, config)).to_equal(gain);
			filter_workloads::Digest kernel_digest;
			filter_workloads::Digest reference_digest;
			kernel.digest(kernel_digest);
			reference.digest(reference_digest);
			expect(kernel_digest.value).to_equal(reference_digest.value);
		}
		for (const std::size_t count : modes) {
			expect(count).to_be_greater_than(0u);
		}
	});

	it("switches the saturator at the reference's resonances: either side of each switch, mono and stereo", _ {
		std::vector<q31> scratch(kInput.size());
		const std::vector<q31> changes = mode_changes();
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[0] - 1)}).mode)
		    .to_equal(Ladder::Mode::BYPASS);
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[0])}).mode).to_equal(Ladder::Mode::TANH);
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[1] - 1)}).mode)
		    .to_equal(Ladder::Mode::TANH);
		expect(Ladder::coefficients({.gain = kGain, .config = config_at(changes[1])}).mode)
		    .to_equal(Ladder::Mode::ANTIALIASED);
		for (const q31 frequency : {q31{1 << 20}, q31{1 << 23}, q31{1 << 26}}) {
			for (const q31 change : changes) {
				for (const q31 resonance : {change - 1, change}) {
					FilterConfig config = config_at(resonance);
					config.frequency = frequency;
					for (const bool stereo : {false, true}) {
						ReferenceHighPassLadder reference;
						reference.zero();
						std::vector<q31> expected = kInput;
						const q31 gain = reference.configure(kGain, config);
						std::vector<q31> out = kInput;
						if (stereo) {
							reference.renderStereo(expected, scratch);
							Stereo<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = config}};
							const auto frames = deluge::dsp::as_frames(out);
							ladder.renderBlock(frames, frames);
							expect(words(ladder.left())).to_equal(words(reference.left()));
							expect(words(ladder.right())).to_equal(words(reference.right()));
							expect(Ladder::gain(ladder.coefficients())).to_equal(gain);
						}
						else {
							reference.renderMono(expected, scratch);
							Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = config}};
							ladder.renderBlock(out, out);
							expect(words(ladder.state())).to_equal(words(reference.left()));
							expect(Ladder::gain(ladder.coefficients())).to_equal(gain);
						}
						expect(out).to_equal(expected);
					}
				}
			}
		}
	});

	it("carries each channel's state across every switch between bypass, tanh and the antialiased tanh, the bypass "
	   "placing the saturator where the reference does", _ {
		std::vector<q31> scratch(kInput.size());
		for (const q31 from : kSaturations) {
			for (const q31 to : kSaturations) {
				ReferenceHighPassLadder reference;
				reference.zero();
				Stereo<Ladder> ladder;
				for (const q31 resonance : {from, to, from}) {
					std::vector<q31> expected = kInput;
					std::ignore = reference.configure(kGain, config_at(resonance));
					reference.renderStereo(expected, scratch);

					const Ladder::State left = ladder.left();
					const Ladder::State right = ladder.right();
					ladder.configure({.gain = kGain, .config = config_at(resonance)});
					expect(words(ladder.left())).to_equal(words(left));
					expect(words(ladder.right())).to_equal(words(right));

					std::vector<q31> out = kInput;
					const auto frames = deluge::dsp::as_frames(out);
					ladder.renderBlock(frames, frames);

					expect(out).to_equal(expected);
					expect(words(ladder.left())).to_equal(words(reference.left()));
					expect(words(ladder.right())).to_equal(words(reference.right()));
				}
			}
		}
	});

	it("starts with the saturator at position 0, silences the sections on reset and keeps the saturator's position, as "
	   "the reference does", _ {
		std::vector<q31> scratch(kInput.size());
		ReferenceHighPassLadder reference;
		reference.zero();
		Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = config_at(filter_workloads::detail::kHpSaturated)}};
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state())).to_equal(ChannelWords{0, 0, 0, 0});

		std::ignore = reference.configure(kGain, config_at(filter_workloads::detail::kHpSaturated));
		std::vector<q31> expected = kInput;
		reference.renderMono(expected, scratch);
		reference.reset(false);

		std::vector<q31> out = kInput;
		ladder.renderBlock(out, out);
		ladder.reset();

		expect(ladder.state().saturator.position).not_().to_equal(0u);
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state()))
		    .to_equal(ChannelWords{0, 0, 0, ladder.state().saturator.position});
	});
});
// clang-format on

CPPSPEC_MAIN(high_pass_ladder, high_pass_ladder_kernel);
