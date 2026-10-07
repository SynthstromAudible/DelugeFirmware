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

// effects::Filter: a host chain's filters, each switched on by its params. The exact values are the legacy host-chain
// path's outputs (GlobalEffectable::setupFilterSetConfig's gates feeding the legacy filter set), from a fresh filter
// with the synth-wide noise generator seeded the same.

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

/// The legacy outputs of a 24dB low-pass partly closed in parallel with the high-pass, the high-pass's params at their
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
/// The legacy output of the drive ladder with its cutoff fully open.
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

/// The output of a fresh filter for @p state and @p params.
std::vector<int32_t> fresh(const FilterState& state, const FilterParams& params) {
	jcong = kSeed;
	Rig rig;
	return rig.run(state, params);
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
		expect(fresh({.lpf_mode = FilterMode::SVF_BAND}, params) == kInput).to_be_false();
	});
	it("runs the low-pass while its cutoff param is below fully open", _ {
		FilterParams params = kOpen;
		params.lpf_frequency_param = Filter::kLowPassOpen - 1;
		expect(fresh({}, params) == kInput).to_be_false();
	});
	it("counts a cutoff param within the open margin as fully open", _ {
		FilterParams params = kOpen;
		params.lpf_frequency_param = Filter::kLowPassOpen;
		expect(fresh({}, params)).to_equal(kInput);
	});
	it("runs the high-pass while its cutoff param is above minimum", _ {
		FilterParams params = kOpen;
		params.hpf_frequency_param = kMinimum + 1;
		params.hpf_frequency = 1 << 23;
		expect(fresh({}, params) == kInput).to_be_false();
	});
	it("runs the high-pass while its morph is above minimum", _ {
		FilterParams params = kOpen;
		params.hpf_morph_param = kMinimum + 1;
		params.hpf_morph = 1 << 26;
		expect(fresh({.hpf_mode = FilterMode::SVF_NOTCH}, params) == kInput).to_be_false();
	});
	it("runs the low-pass in the state's mode", _ {
		expect(fresh({.lpf_mode = FilterMode::TRANSISTOR_12DB}, kLowOnlyParams)
		       == fresh({.lpf_mode = FilterMode::TRANSISTOR_24DB}, kLowOnlyParams))
		    .to_be_false();
	});
	it("filters as the legacy chain did, the high-pass switched on in parallel fading in", _ {
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
