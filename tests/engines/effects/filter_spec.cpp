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

// effects::Filter: a host chain's filters, each switched on by its params. Exact reference values pin the gates, the
// modes reaching the set, the parallel route with the high-pass fading in, and the volume compensation, each case from
// a fresh filter with the synth-wide noise generator seeded the same.

#include "engines/effects/filter.h"

#include "util/fixedpoint.h"
#include "util/waves.h" // jcong

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <limits>
#include <tuple>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

constexpr uint32_t kSeed = 123456789;
/// A clip chain's post-FX volume at neutral (Q31), as the master starts from.
constexpr int32_t kVolume = 167763968;
constexpr int32_t kMinimum = std::numeric_limits<int32_t>::min();
constexpr int32_t kMaximum = std::numeric_limits<int32_t>::max();

/// 32 interleaved frames: the left two sines (3 and 11 cycles at 2^25 and 2^24), the right one (5 cycles at 2^25).
const std::vector<int32_t> kInput{
    0,         0,         32591589,  27899491,  15500126,  31000253,  36182765,  6546145,   35589850,  -23726566,
    -9908702,  -32909693, -6420363,  -12840725, -18578569, 18641844,  -50331648, 33554432,  -18578569, 18641844,
    -6420363,  -12840725, -9908702,  -32909693, 35589850,  -23726566, 36182765,  6546145,   15500126,  31000253,
    32591589,  27899491,  0,         0,         -32591589, -27899491, -15500126, -31000253, -36182765, -6546145,
    -35589850, 23726566,  9908702,   32909693,  6420363,   12840725,  18578569,  -18641844, 50331648,  -33554432,
    18578569,  -18641844, 6420363,   12840725,  9908702,   32909693,  -35589850, 23726566,  -36182765, -6546145,
    -15500126, -31000253, -32591589, -27899491};

/// The outputs of a 24dB low-pass partly closed in parallel with the high-pass, the high-pass's params at their
/// minimum (off) for the first block and its cutoff param raised (on, fading in) for the second.
constexpr int32_t kLowOnlyVolume = 167960548;
const std::vector<int32_t> kLowOnly{
    0,         0,         33033353,  28278883,  18253170,  33598217,  44417897,  14635705,  52015110,  -8789266,
    14868402,  -15707833, 22140101,  -3014433,  4338243,   13116008,  -43452652, 13588532,  -34263293, -4852264,
    -43678391, -25852001, -58622906, -28218737, -8310198,  -5954218,  13321601,  24799665,  23935022,  38776637,
    72146329,  23448551,  59147348,  -9567684,  26968983,  -33934815, 23948498,  -30822137, -31498269, -4583261,
    -68906998, 21628990,  -51954370, 26236589,  -64133869, 7620777,   -36475271, -15457804, 30215940,  -21319016,
    41070109,  -4961304,  64790499,  17672465,  84484610,  24489193,  29373602,  7666682,   -3117549,  -18747065,
    -25627362, -31038685, -82642089, -17113535};
constexpr int32_t kBothVolume = 167570990;
const std::vector<int32_t> kBoth{
    -73214924, 12204500,  -39287858, 32147108,  -30965276, 25492362,  30568108,  -2264492,  72522164,  -27543984,
    57338412,  -29132250, 68583534,  -6640724,  38182924,  19432300,  -31581592, 26144268,  -44243836, 8255516,
    -68072798, -17436456, -86226878, -27136116, -29061986, -11617802, 4980986,   15193858,  27644548,  28851020,
    83436396,  16396984,  71868484,  -11955124, 35807210,  -31357592, 26566506,  -24294034, -35009198, 3361604,
    -75826644, 27468208,  -59021850, 27147072,  -68468864, 2794648,   -37117032, -24220574, 31940488,  -30133846,
    43252946,  -10113492, 65455298,  17458114,  82578570,  27927688,  24563862,  12094738,  -9020354,  -15908634,
    -30727014, -30747944, -85340530, -18692418};
constexpr int32_t kNoiseAfter = 1658262933;
/// The output of the drive ladder with its cutoff fully open.
constexpr int32_t kDriveOpenVolume = 107494750;
const std::vector<int32_t> kDriveOpen{
    0,         0,         18499997,  16269687,  26424598,  28212058,  22603296,  21075005,  30334959,  -18406510,
    17334862,  -28089072, -17426819, -24390229, -6349487,  10298805,  -28564889, 26833522,  -31023677, 26369776,
    -11206673, -1792553,  -2753693,  -27343122, 16203524,  -27477149, 31214478,  -7720721,  26841232,  24975433,
    21427288,  27749643,  19288632,  15647576,  -24870033, -22390854, -27028110, -28247293, -22589937, -21032369,
    -30345985, 18422258,  -17305816, 28089338,  17434442,  24424083,  6359653,   -10197208, 28587831,  -26844361,
    31004758,  -26375883, 10997239,  1824393,   2847491,   27342442,  -16501300, 27477137,  -31235052, 7808702,
    -26754504, -24939117, -21456107, -27749910};
constexpr int32_t kDriveOpenNoiseAfter = 1658262933;

/// Each gate opened alone from every filter at minimum (kOpen), in the default route: the low-pass SVF band by its
/// morph param, the 24dB low-pass by its cutoff param just below fully open, the high-pass ladder by its cutoff param,
/// and the high-pass SVF notch by its morph param.
constexpr int32_t kLowMorphGateVolume = 134368438;
const std::vector<int32_t> kLowMorphGate{
    0,         0,         17934795,  15398622,  51122892,  53637447,  5237589,   13971030,  58287465,  -31003941,
    19310130,  -38107779, -54148536, -24468702, 12108846,  17073600,  -44187399, 45180714,  -74904045, 28106832,
    31881195,  -10377270, -12418425, -40095705, -8577726,  -35765268, 91047738,  2075607,   11287608,  37351782,
    651054,    39163680,  57240333,  6753360,   -54743178, -31993968, -48898860, -42384987, 11790018,  -14789226,
    -73957428, 25659024,  -15057285, 43442001,  57767094,  22602681,  -17344101, -18364188, 46844718,  -42993894,
    75334419,  -29385939, -33403782, 10310076,  13561536,  40855473,  8424084,   35104191,  -91431021, -1904103,
    -10874787, -37144635, -815445,   -39449772};
constexpr int32_t kLowCutoffGateVolume = 167960548;
const std::vector<int32_t> kLowCutoffGate{
    0,         0,         20762732,  17714644,  17489380,  26431604,  18368812,  4429644,   35198244,  -14691016,
    -10079984, -26381068, -5103192,  -10718592, -9676576,  12655700,  -40409604, 24477644,  -13628752, 16635636,
    -6139652,  -9615084,  -5745460,  -22733312, 19908092,  -20601884, 33734524,  4536972,   7604320,   21778392,
    24867128,  22263992,  5193672,   1821756,   -28253920, -20680148, -8851948,  -22904980, -27227760, -7866732,
    -27198472, 18020992,  3507248,   23765604,  9277920,   12262128,  8207984,   -13339780, 38986552,  -24879188,
    17504000,  -15321940, 520368,    7649108,   12278776,  25206012,  -26598448, 18011068,  -27978596, -2172772,
    -11971268, -23696152, -22341524, -21018992};
constexpr int32_t kHighCutoffGateVolume = 134368436;
const std::vector<int32_t> kHighCutoffGate{
    0,         0,         31326464,  26816512,  6054912,   22226432,  23923200,  -7811584,  16332544,  -35096832,
    -32425984, -34212096, -20364032, -5061888,  -26296832, 27097088,  -49194240, 34154752,  -4394496,  10176512,
    9277184,   -23287552, 3863296,   -36330240, 46801664,  -17249280, 34327296,  17066496,  4422144,   36161024,
    19039232,  23089664,  -18093824, -10512128, -44794624, -34768128, -15880448, -28113920, -30955520, 3537664,
    -21271296, 32052736,  29024000,  32084736,  18068736,  3603968,   24783616,  -28075008, 48222976,  -34795520,
    3791616,   -10584576, -9635584,  23036928,  -4062976,  36183296,  -46901760, 17169152,  -34367488, -17105152,
    -4428032,  -36174848, -19027200, -23089664};
constexpr int32_t kHighMorphNotchVolume = 134368438;
const std::vector<int32_t> kHighMorphNotch{
    0,         0,         42776460,  36618081,  20343912,  40687830,  47489877,  8591814,   46711674,  -31141119,
    -13005171, -43193973, -8426727,  -16853454, -24384372, 24467418,  -66060288, 44040189,  -24384372, 24467418,
    -8426727,  -16853454, -13005171, -43193973, 46711674,  -31141119, 47489877,  8591814,   20343912,  40687830,
    42776460,  36618081,  0,         0,         -42776463, -36618084, -20343915, -40687833, -47489880, -8591817,
    -46711677, 31141116,  13005168,  43193970,  8426724,   16853451,  24384369,  -24467421, 66060285,  -44040192,
    24384369,  -24467421, 8426724,   16853451,  13005168,  43193970,  -46711677, 31141116,  -47489880, -8591817,
    -20343915, -40687833, -42776463, -36618084};
/// The low-pass partly closed (kLowOnlyParams) as a 12dB and as a 24dB ladder, in the default route.
constexpr int32_t kLow12dBVolume = 167960548;
const std::vector<int32_t> kLow12dB{
    0,         0,         -2790780,  -2407064,  -4741088,   -5580584,  2342396,   2263536,   15774536,  24436504,
    35676604,  42914224,  55111020,  35541232,  53224268,   -855996,   28505640,  -43570136, -10600036, -59749468,
    -56284532, -34676392, -84993684, 14692864,  -80985080,  51933220,  -49079668, 49581988,  4118900,   11745224,
    59885356,  -30029948, 93648068,  -42896084, 96885336,   -19827952, 65852640,  15789384,  8781792,   31904624,
    -49493976, 16074972,  -93108640, -14431824, -108192604, -29504196, -81442288, -14301912, -26296852, 17151892,
    36010640,  34960032,  90359260,  21031636,  111895880,  -13904808, 90481124,  -39227616, 40676008,  -31730804,
    -23359412, 3375176,   -80329544, 36439416};
constexpr int32_t kLow24dBVolume = 167960548;
const std::vector<int32_t> kLow24dB{
    0,         0,         441764,    379392,   2753044,   2597964,  8235132,   8089560,   16425260,  14937300,
    24777104,  17201860,  28560464,  9826292,  22916812,  -5525836, 6878996,   -19965900, -15684724, -23494108,
    -37258028, -13011276, -48714204, 4690956,  -43900048, 17772348, -22861164, 18253520,  8434896,   7776384,
    39554740,  -4450940,  59147348,  -9567684, 59560572,  -6035324, 39448624,  178116,    4684496,   1962884,
    -33317148, -2097576,  -61863072, -6673104, -70554232, -5219948, -55053840, 3184040,   -20115708, 12235416,
    22491540,  13680540,  58370136,  4831740,  74575908,  -8420500, 64963452,  -16059884, 33065216,  -12200920,
    -10127236, -38432,    -50050500, 10785956};

/// Every filter at minimum: the low-pass fully open, the high-pass off.
constexpr FilterParams kOpen{
    .lpf_frequency = kMaximum,
    .lpf_resonance = 1 << 28,
    .lpf_morph = 0,
    .hpf_frequency = 0,
    .hpf_resonance = 0,
    .hpf_morph = 0,
    .lpf_frequency_param = kMaximum,
    .lpf_morph_param = kMinimum,
    .hpf_frequency_param = kMinimum,
    .hpf_morph_param = kMinimum,
};

/// The low-pass partly closed; the high-pass's final values set but its params at minimum.
constexpr FilterParams kLowOnlyParams{
    .lpf_frequency = 1 << 24,
    .lpf_resonance = 1 << 28,
    .lpf_morph = 1 << 27,
    .hpf_frequency = 1 << 23,
    .hpf_resonance = 1 << 27,
    .hpf_morph = 1 << 26,
    .lpf_frequency_param = 0,
    .lpf_morph_param = kMinimum,
    .hpf_frequency_param = kMinimum,
    .hpf_morph_param = kMinimum,
};

/// The interleaved samples as frames.
std::vector<StereoSample> frames(const std::vector<int32_t>& samples) {
	std::vector<StereoSample> block;
	for (std::size_t i = 0; i < samples.size(); i += 2) {
		block.push_back({.l = samples[i], .r = samples[i + 1]});
	}
	return block;
}

/// The frames as interleaved samples.
std::vector<int32_t> samples(const std::vector<StereoSample>& block) {
	std::vector<int32_t> out;
	for (const StereoSample& frame : block) {
		out.push_back(frame.l);
		out.push_back(frame.r);
	}
	return out;
}

/// A filter, the scratch it renders with, and the volume its last configure returned.
struct Rig {
	Filter filter;
	std::array<int32_t, 64> fade{};
	std::array<int32_t, 64> parallel{};
	int32_t volume{0};

	/// Configures for @p state and @p params from kVolume, then filters kInput, returning the interleaved output.
	std::vector<int32_t> run(const FilterState& state, const FilterParams& params) {
		volume = filter.configure(state, params, kVolume);
		std::vector<StereoSample> block = frames(kInput);
		filter.process(block, {.fade = fade, .parallel = parallel});
		return samples(block);
	}
};

/// The output of a fresh filter for @p state and @p params, and the volume it returned.
struct Fresh {
	std::vector<int32_t> output;
	int32_t volume;
};
Fresh fresh(const FilterState& state, const FilterParams& params) {
	jcong = kSeed;
	Rig rig;
	std::vector<int32_t> output = rig.run(state, params);
	return {.output = output, .volume = rig.volume};
}

/// The volume after only the set's scaling (no filter's compensation).
int32_t scaled_only(int32_t volume) {
	return multiply_32x32_rshift32(volume, 1720000000) << 1;
}

} // namespace

// clang-format off
describe filter("a host chain's filters, switched on by their params", ${
	it("leaves the block untouched with the low-pass fully open and the high-pass at minimum", _ {
		jcong = kSeed;
		Rig rig;
		expect(rig.run({}, kOpen)).to_equal(kInput);
		expect(rig.volume).to_equal(scaled_only(kVolume));
	});
	it("runs the drive ladder fully open", _ {
		jcong = kSeed;
		Rig rig;
		expect(rig.run({.lpf_mode = FilterMode::TRANSISTOR_24DB_DRIVE}, kOpen)).to_equal(kDriveOpen);
		expect(rig.volume).to_equal(kDriveOpenVolume);
		expect(static_cast<int32_t>(jcong)).to_equal(kDriveOpenNoiseAfter);
	});
	it("runs the low-pass while its morph is above minimum", _ {
		FilterParams params = kOpen;
		params.lpf_morph_param = kMinimum + 1;
		const Fresh run = fresh({.lpf_mode = FilterMode::SVF_BAND}, params);
		expect(run.output).not_().to_equal(kInput);
		expect(run.output).to_equal(kLowMorphGate);
		expect(run.volume).to_equal(kLowMorphGateVolume);
	});
	it("runs the low-pass while its cutoff param is below fully open", _ {
		FilterParams params = kOpen;
		params.lpf_frequency_param = Filter::kLowPassOpen - 1;
		const Fresh run = fresh({}, params);
		expect(run.output).not_().to_equal(kInput);
		expect(run.output).to_equal(kLowCutoffGate);
		expect(run.volume).to_equal(kLowCutoffGateVolume);
	});
	it("counts a cutoff param within the open margin as fully open", _ {
		FilterParams params = kOpen;
		params.lpf_frequency_param = Filter::kLowPassOpen;
		expect(fresh({}, params).output).to_equal(kInput);
	});
	it("runs the high-pass while its cutoff param is above minimum", _ {
		FilterParams params = kOpen;
		params.hpf_frequency_param = kMinimum + 1;
		params.hpf_frequency = 1 << 23;
		const Fresh run = fresh({}, params);
		expect(run.output).not_().to_equal(kInput);
		expect(run.output).to_equal(kHighCutoffGate);
		expect(run.volume).to_equal(kHighCutoffGateVolume);
	});
	it("runs the high-pass while its morph is above minimum, in the state's mode", _ {
		FilterParams params = kOpen;
		params.hpf_morph_param = kMinimum + 1;
		params.hpf_morph = 1 << 26;
		const Fresh run = fresh({.hpf_mode = FilterMode::SVF_NOTCH}, params);
		expect(run.output).not_().to_equal(kInput);
		expect(run.output).to_equal(kHighMorphNotch);
		expect(run.volume).to_equal(kHighMorphNotchVolume);
	});
	it("runs the low-pass in the state's mode", _ {
		const Fresh half = fresh({.lpf_mode = FilterMode::TRANSISTOR_12DB}, kLowOnlyParams);
		const Fresh full = fresh({.lpf_mode = FilterMode::TRANSISTOR_24DB}, kLowOnlyParams);
		expect(half.output).not_().to_equal(full.output);
		expect(half.output).to_equal(kLow12dB);
		expect(half.volume).to_equal(kLow12dBVolume);
		expect(full.output).to_equal(kLow24dB);
		expect(full.volume).to_equal(kLow24dBVolume);
	});
	it("runs both in parallel, the high-pass switched on fading in", _ {
		jcong = kSeed;
		Rig rig;
		const FilterState state{.lpf_mode = FilterMode::TRANSISTOR_24DB, .route = FilterRoute::PARALLEL};
		expect(rig.run(state, kLowOnlyParams)).to_equal(kLowOnly);
		expect(rig.volume).to_equal(kLowOnlyVolume);
		FilterParams both = kLowOnlyParams;
		both.hpf_frequency_param = 0;
		expect(rig.run(state, both)).to_equal(kBoth);
		expect(rig.volume).to_equal(kBothVolume);
		expect(static_cast<int32_t>(jcong)).to_equal(kNoiseAfter);
	});
	it("resets to a fresh filter", _ {
		const FilterState state{.lpf_mode = FilterMode::TRANSISTOR_24DB, .route = FilterRoute::PARALLEL};
		jcong = kSeed;
		Rig rig;
		std::ignore = rig.run(state, kLowOnlyParams);
		rig.filter.reset();
		jcong = kSeed;
		expect(rig.run(state, kLowOnlyParams)).to_equal(kLowOnly);
	});
});

CPPSPEC_SPEC(filter)
