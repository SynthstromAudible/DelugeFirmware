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

// AnalogImpulseResponse: the analog delay's 26-tap impulse response. Exact reference values pin an impulse (each tap
// in order, halved and rounded) and a block of two sines through both channels.

#include "dsp-next/fixed/delay/analog_impulse_response.h"

#include <climits>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::AnalogImpulseResponse;

namespace {

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

std::vector<int32_t> run(AnalogImpulseResponse& ir, auto&& input, int count) {
	std::vector<int32_t> out;
	for (int i = 0; i < count; i++) {
		const StereoSample o = ir.process(input(i));
		out.push_back(o.l);
		out.push_back(o.r);
	}
	return out;
}

} // namespace

// clang-format off
describe analog_impulse_response("AnalogImpulseResponse", ${
	it("answers an impulse with its 26 taps in order, then silence", _ {
		AnalogImpulseResponse ir;
		const auto out = run(ir, [](int i) {
			return i == 0 ? StereoSample{.l = INT_MIN, .r = INT_MAX} : StereoSample{.l = 0, .r = 0};
		}, 30);
		expect(out).to_equal(kImpulse);
	});

	it("filters a block of two sines exactly, each channel on its own", _ {
		AnalogImpulseResponse ir;
		TwoSines sines;
		const auto out = run(ir, [&](int) { return sines.next(); }, 128);
		expect(out).to_equal(kTwoSine);
	});

	it("starts silent: a fresh response outputs nothing for silence", _ {
		AnalogImpulseResponse ir;
		const auto out = run(ir, [](int) { return StereoSample{.l = 0, .r = 0}; }, 26);
		expect(out).to_equal(std::vector<int32_t>(52, 0));
	});
});
// clang-format on

CPPSPEC_MAIN(analog_impulse_response);
