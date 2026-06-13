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
#include "sine.h"
#include <array>
#include <limits>

namespace {
/// A full-circle phase is a q32 value: 2^32 represents one cycle.
constexpr int kPhaseBits = 32;
/// The table holds 2^8 == 256 interpolation entries (one per 1/256 of a cycle).
constexpr int kTableSizeMagnitude = 8;
constexpr size_t kNumEntries = size_t{1} << kTableSizeMagnitude; // 256
/// Each entry is a {value, diff} int16 pair, stored interleaved, so the backing array
/// is twice the entry count.
constexpr size_t kTableSize = 2 * kNumEntries; // 512

/// The top kTableSizeMagnitude bits of the phase select the table entry; the next
/// kInterpWeightBits bits are the interpolation weight.
constexpr int kIndexShift = kPhaseBits - kTableSizeMagnitude; // 24
/// The interpolation weight is a Q15 fraction — INT16_MAX (0x7FFF) masks it to 15 bits,
/// so we shift the phase down to leave those 15 bits at the bottom.
constexpr int kInterpWeightBits = 15;
constexpr int kFractionShift = kIndexShift - kInterpWeightBits; // 9

/// Cache-line alignment for the gather; placement only, so it is golden-neutral. Kept
/// local to avoid pulling the board config header into L0.
constexpr size_t kTableAlignment = 32;

/// Interleaved {value, diff} sine table; sine(x) ≈ value + diff * fraction.
alignas(kTableAlignment) std::array<int16_t, kTableSize> sine_wave_diff = {
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
} // namespace

namespace deluge::dsp::fixed {

[[gnu::hot]] Argon<int32_t> sine(Argon<uint32_t> phase) {
	// Q15 interpolation weight: the kInterpWeightBits phase bits just below the index.
	// ShiftRightNarrow drops to a 16-bit lane; the mask clears the index bit that leaks
	// into the top of that lane, leaving a 15-bit fraction.
	ArgonHalf<int16_t> fraction = phase.ShiftRightNarrow<kFractionShift>()
	                                  .BitwiseAnd(std::numeric_limits<int16_t>::max())
	                                  .As<int16_t>();

	// The table entry index is the top kTableSizeMagnitude bits of the phase. The gather
	// scales the offset by its stride (2 int16 per entry) internally since argon 90fde9a
	// "Fix load bugs", so pass the entry index directly — do NOT pre-multiply by 2.
	Argon<uint32_t> index = phase >> kIndexShift;

	auto [value, diff] =
	    ArgonHalf<int16_t>::LoadGatherOffsetIndexInterleaved<2>(sine_wave_diff.data(), index.Narrow());

	// Linear interpolation, value + diff*fraction, kept in the high half of a q31 lane:
	// promote the int16 value to bit 16, then qdmlal adds the (doubled, saturating)
	// diff*fraction term without reducing back to q31.
	return value.ShiftLeftLong<16>().MultiplyDoubleAddSaturateLong(diff, fraction);
}

[[gnu::hot]] int32_t sine_fm_operator(uint32_t carrier_phase, uint32_t phase_shift) {
	// The operator works in a 24-bit phase: drop the carrier's low 8 bits and add the
	// (already 24-bit) modulation.
	constexpr int kOperatorPhaseBits = 24;
	const uint32_t phase = (carrier_phase >> (kPhaseBits - kOperatorPhaseBits)) + phase_shift;

	// Interpolation weight: the low 16 bits of the 24-bit phase, a Q16 fraction.
	const int32_t fraction = phase & 0xFFFF;

	// int16 offset of the entry's {value, diff} pair: the entry index (top
	// kTableSizeMagnitude bits of the 24-bit phase) times 2. Folding the ×2 into the
	// shift and masking with 0b0111111110 yields (index << 1) clamped to the table.
	const uint32_t entry_offset = (phase >> (kOperatorPhaseBits - kTableSizeMagnitude - 1)) & 0b0111111110;

	// Read the {value (low 16), diff (high 16)} pair as one word.
	const uint32_t pair = *reinterpret_cast<uint32_t*>(&sine_wave_diff[entry_offset]);
	const int32_t value = pair << 16;                       // sine value promoted to the q31 high half
	const int32_t diff = static_cast<int32_t>(pair) >> 16;  // sign-extended diff
	return value + (diff * fraction);
}

} // namespace deluge::dsp::fixed
