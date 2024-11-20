#pragma once

#include "deluge/util/misc.h"
#include <array>
#include <cstdint>

namespace deluge::dsp {
class SineOsc {
	static constexpr size_t kSineTableSizeMagnitude = 8;
	static constexpr std::array<int16_t, 512> sineWaveDiff __attribute__((aligned(CACHE_LINE_SIZE))) = {
	    0,      804,  804,    804,  1608,   802,  2410,   802,  3212,   799,  4011,   797,  4808,   794,  5602,   791,
	    6393,   786,  7179,   783,  7962,   777,  8739,   773,  9512,   766,  10278,  761,  11039,  754,  11793,  746,
	    12539,  740,  13279,  731,  14010,  722,  14732,  714,  15446,  705,  16151,  695,  16846,  684,  17530,  674,
	    18204,  664,  18868,  651,  19519,  640,  20159,  628,  20787,  616,  21403,  602,  22005,  589,  22594,  576,
	    23170,  561,  23731,  548,  24279,  532,  24811,  518,  25329,  503,  25832,  487,  26319,  471,  26790,  455,
	    27245,  438,  27683,  422,  28105,  405,  28510,  388,  28898,  370,  29268,  353,  29621,  335,  29956,  317,
	    30273,  298,  30571,  281,  30852,  261,  31113,  243,  31356,  224,  31580,  205,  31785,  186,  31971,  166,
	    32137,  148,  32285,  127,  32412,  109,  32521,  88,   32609,  69,   32678,  50,   32728,  29,   32757,  10,
	    32767,  -10,  32757,  -29,  32728,  -50,  32678,  -69,  32609,  -88,  32521,  -109, 32412,  -127, 32285,  -148,
	    32137,  -166, 31971,  -186, 31785,  -205, 31580,  -224, 31356,  -243, 31113,  -261, 30852,  -281, 30571,  -298,
	    30273,  -317, 29956,  -335, 29621,  -353, 29268,  -370, 28898,  -388, 28510,  -405, 28105,  -422, 27683,  -438,
	    27245,  -455, 26790,  -471, 26319,  -487, 25832,  -503, 25329,  -518, 24811,  -532, 24279,  -548, 23731,  -561,
	    23170,  -576, 22594,  -589, 22005,  -602, 21403,  -616, 20787,  -628, 20159,  -640, 19519,  -651, 18868,  -664,
	    18204,  -674, 17530,  -684, 16846,  -695, 16151,  -705, 15446,  -714, 14732,  -722, 14010,  -731, 13279,  -740,
	    12539,  -746, 11793,  -754, 11039,  -761, 10278,  -766, 9512,   -773, 8739,   -777, 7962,   -783, 7179,   -786,
	    6393,   -791, 5602,   -794, 4808,   -797, 4011,   -799, 3212,   -802, 2410,   -802, 1608,   -804, 804,    -804,
	    0,      -804, -804,   -804, -1608,  -802, -2410,  -802, -3212,  -799, -4011,  -797, -4808,  -794, -5602,  -791,
	    -6393,  -786, -7179,  -783, -7962,  -777, -8739,  -773, -9512,  -766, -10278, -761, -11039, -754, -11793, -746,
	    -12539, -740, -13279, -731, -14010, -722, -14732, -714, -15446, -705, -16151, -695, -16846, -684, -17530, -674,
	    -18204, -664, -18868, -651, -19519, -640, -20159, -628, -20787, -616, -21403, -602, -22005, -589, -22594, -576,
	    -23170, -561, -23731, -548, -24279, -532, -24811, -518, -25329, -503, -25832, -487, -26319, -471, -26790, -455,
	    -27245, -438, -27683, -422, -28105, -405, -28510, -388, -28898, -370, -29268, -353, -29621, -335, -29956, -317,
	    -30273, -298, -30571, -281, -30852, -261, -31113, -243, -31356, -224, -31580, -205, -31785, -186, -31971, -166,
	    -32137, -148, -32285, -127, -32412, -109, -32521, -88,  -32609, -69,  -32678, -50,  -32728, -29,  -32757, -10,
	    -32767, 10,   -32757, 29,   -32728, 50,   -32678, 69,   -32609, 88,   -32521, 109,  -32412, 127,  -32285, 148,
	    -32137, 166,  -31971, 186,  -31785, 205,  -31580, 224,  -31356, 243,  -31113, 261,  -30852, 281,  -30571, 298,
	    -30273, 317,  -29956, 335,  -29621, 353,  -29268, 370,  -28898, 388,  -28510, 405,  -28105, 422,  -27683, 438,
	    -27245, 455,  -26790, 471,  -26319, 487,  -25832, 503,  -25329, 518,  -24811, 532,  -24279, 548,  -23731, 561,
	    -23170, 576,  -22594, 589,  -22005, 602,  -21403, 616,  -20787, 628,  -20159, 640,  -19519, 651,  -18868, 664,
	    -18204, 674,  -17530, 684,  -16846, 695,  -16151, 705,  -15446, 714,  -14732, 722,  -14010, 731,  -13279, 740,
	    -12539, 746,  -11793, 754,  -11039, 761,  -10278, 766,  -9512,  773,  -8739,  777,  -7962,  783,  -7179,  786,
	    -6393,  791,  -5602,  794,  -4808,  797,  -4011,  799,  -3212,  802,  -2410,  802,  -1608,  804,  -804,   804,
	};

public:
	static int32_t doFMNew(uint32_t carrierPhase, uint32_t phaseShift) {
		// return getSineNew((((*carrierPhase += carrierPhaseIncrement) >> 8) + phaseShift) & 16777215, 24);

		uint32_t phaseSmall = (carrierPhase >> 8) + phaseShift;
		int32_t strength2 = phaseSmall & 65535;

		uint32_t readOffset = (phaseSmall >> (24 - 8 - 2)) & 0b1111111100;

		uint32_t readValue = *(uint32_t*)&sineWaveDiff[readOffset];
		int32_t value = readValue << 16;
		int32_t diff = (int32_t)readValue >> 16;
		return value + diff * strength2;
	}

	static inline int32x4_t getSineVector(uint32_t* thisPhase, uint32_t phaseIncrement) {

		int16x4_t strength2;
		uint32x4_t readValue;

		for (int32_t i = 0; i < 4; i++) {
			*thisPhase += phaseIncrement;
			int32_t whichValue = *thisPhase >> (32 - kSineTableSizeMagnitude);
			strength2[i] = (*thisPhase >> (32 - 16 - kSineTableSizeMagnitude + 1)) & 32767;

			uint32_t readOffset = whichValue << 2;

			readValue[i] = *(uint32_t*)&sineWaveDiff[readOffset];
		}

		int32x4_t enlargedValue1 = vreinterpretq_s32_u32(vshlq_n_u32(readValue, 16));
		int16x4_t diffValue = vshrn_n_s32(vreinterpretq_s32_u32(readValue), 16);

		return vqdmlal_s16(enlargedValue1, strength2, diffValue);
	}

	static inline int32x4_t doFMVector(uint32x4_t phaseVector, uint32x4_t phaseShift) {

		uint32x4_t finalPhase = vaddq_u32(phaseVector, vshlq_n_u32(phaseShift, 8));

		uint32x4_t readValue{0};
		util::constexpr_for<0, 4, 1>([&]<int i>() {
			uint32_t readOffsetNow = (vgetq_lane_u32(finalPhase, i) >> (32 - kSineTableSizeMagnitude)) << 2;
			uint32_t* thisReadAddress = (uint32_t*)&sineWaveDiff[readOffsetNow];
			readValue = vld1q_lane_u32(thisReadAddress, readValue, i);
		});

		int16x4_t strength2 =
		    vreinterpret_s16_u16(vshr_n_u16(vshrn_n_u32(finalPhase, (32 - 16 - kSineTableSizeMagnitude)), 1));

		int32x4_t enlargedValue1 = vreinterpretq_s32_u32(vshlq_n_u32(readValue, 16));
		int16x4_t diffValue = vshrn_n_s32(vreinterpretq_s32_u32(readValue), 16);

		return vqdmlal_s16(enlargedValue1, strength2, diffValue);
	}
}; // namespace deluge::dsp
} // namespace deluge::dsp
