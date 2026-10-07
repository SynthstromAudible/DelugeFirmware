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

#include "dsp-next/fixed/filter/high_pass_ladder.h"
#include "util/waves.h" // jcong, CONG

#include <cppspec.hpp>
#include <cstdint>
#include <tuple>
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

constexpr FilterConfig kDark{
    .frequency = 1 << 22, .resonance = 0, .morph = 0, .gain = kGain, .allow_oversampling = true};
constexpr FilterConfig kMid{
    .frequency = 1 << 24, .resonance = 1 << 28, .morph = 1 << 27, .gain = kGain, .allow_oversampling = true};
constexpr FilterConfig kBright{
    .frequency = 50000000, .resonance = 500000000, .morph = 1 << 28, .gain = kGain, .allow_oversampling = true};

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
	const int32_t gain = filter.configure(config);
	std::vector<int32_t> block = kInput;
	std::vector<int32_t> scratch(block.size());
	filter.renderMono(block, scratch);
	return {.out = block, .gain = gain, .next_noise = static_cast<int32_t>(CONG)};
}

Rendered renderStereo(const FilterConfig& config) {
	jcong = kSeed;
	HighPassLadder filter;
	filter.zero();
	const int32_t gain = filter.configure(config);
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
constexpr int32_t kHpFadeBlocks = 11;

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
		std::ignore = filter.configure(kMid);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		expect(block).to_equal(kHpFreshMono);
	});
	it("fades in after reset(true) in mono", _ {
		jcong = kSeed;
		HighPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kMid);
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
		std::ignore = filter.configure(kMid);
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
		std::ignore = filter.configure(kMid);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(false);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kHpResetMono);
	});
});
// clang-format on

CPPSPEC_MAIN(high_pass_ladder);
