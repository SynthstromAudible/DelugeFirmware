/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

// clang-format off

#include "util/lookuptables/lookuptables.h"
#include "definitions_cxx.hpp"
// The l10n/iterance preset tables (chordNames, iterancePresets) live in preset_tables.cpp
// so this TU stays gui-free (numeric DSP tables only).

alignas(16) const uint16_t centAdjustTableSmall[257] = {
31835, 31842, 31850, 31857, 31864, 31871, 31878, 31885, 31893, 31900, 31907, 31914, 31921, 31929, 31936, 31943,
31950, 31958, 31965, 31972, 31979, 31986, 31994, 32001, 32008, 32015, 32022, 32030, 32037, 32044, 32051, 32059,
32066, 32073, 32080, 32088, 32095, 32102, 32109, 32117, 32124, 32131, 32138, 32146, 32153, 32160, 32167, 32175,
32182, 32189, 32196, 32204, 32211, 32218, 32225, 32233, 32240, 32247, 32255, 32262, 32269, 32276, 32284, 32291,
32298, 32306, 32313, 32320, 32327, 32335, 32342, 32349, 32357, 32364, 32371, 32378, 32386, 32393, 32400, 32408,
32415, 32422, 32430, 32437, 32444, 32452, 32459, 32466, 32474, 32481, 32488, 32496, 32503, 32510, 32518, 32525,
32532, 32540, 32547, 32554, 32562, 32569, 32576, 32584, 32591, 32598, 32606, 32613, 32620, 32628, 32635, 32643,
32650, 32657, 32665, 32672, 32679, 32687, 32694, 32702, 32709, 32716, 32724, 32731, 32738, 32746, 32753, 32761,
32768, 32775, 32783, 32790, 32798, 32805, 32812, 32820, 32827, 32835, 32842, 32849, 32857, 32864, 32872, 32879,
32887, 32894, 32901, 32909, 32916, 32924, 32931, 32938, 32946, 32953, 32961, 32968, 32976, 32983, 32991, 32998,
33005, 33013, 33020, 33028, 33035, 33043, 33050, 33058, 33065, 33073, 33080, 33087, 33095, 33102, 33110, 33117,
33125, 33132, 33140, 33147, 33155, 33162, 33170, 33177, 33185, 33192, 33200, 33207, 33215, 33222, 33230, 33237,
33245, 33252, 33260, 33267, 33275, 33282, 33290, 33297, 33305, 33312, 33320, 33327, 33335, 33342, 33350, 33357,
33365, 33372, 33380, 33387, 33395, 33403, 33410, 33418, 33425, 33433, 33440, 33448, 33455, 33463, 33470, 33478,
33486, 33493, 33501, 33508, 33516, 33523, 33531, 33538, 33546, 33554, 33561, 33569, 33576, 33584, 33591, 33599,
33607, 33614, 33622, 33629, 33637, 33645, 33652, 33660, 33667, 33675, 33683, 33690, 33698, 33705, 33713, 33721,
33728, };


// Begins at E (4 semitones above C). So that this octave contains the largest values (phase increments) possible without going over 22.05kHz (2147483648),
// even when shifted up a semitone (via osc-cents and unison combined)
alignas(16) const int32_t noteFrequencyTable[12] = {
1027294024, 1088380105, 1153098554, 1221665363, 1294309365, 1371273005,
1452813141, 1539201906, 1630727614, 1727695724, 1830429858, 1939272882, };


alignas(16) const int32_t noteIntervalTable[12] = {
1073741824, 1137589835, 1205234447, 1276901417, 1352829926, 1433273380,
1518500250, 1608794974, 1704458901, 1805811301, 1913190429, 2026954652, };





alignas(16) const int16_t hanningWindow[] = {
0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593, 705, 827, 958, 1098,
1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542, 2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518,
4799, 5086, 5381, 5682, 5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378, 14778, 15178, 15580, 15981,
16384, 16786, 17187, 17589, 17989, 18389, 18787, 19184, 19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280,
22653, 23023, 23388, 23750, 24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639, 30832, 31018, 31194, 31361,
31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369, 32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762,
32767, 32762, 32747, 32723, 32688, 32644, 32590, 32526, 32452, 32369, 32276, 32174, 32062, 31940, 31809, 31669,
31520, 31361, 31194, 31018, 30832, 30639, 30436, 30225, 30006, 29778, 29543, 29299, 29048, 28789, 28523, 28249,
27968, 27681, 27386, 27085, 26777, 26463, 26143, 25817, 25486, 25149, 24806, 24459, 24107, 23750, 23388, 23023,
22653, 22280, 21903, 21523, 21139, 20753, 20364, 19973, 19580, 19184, 18787, 18389, 17989, 17589, 17187, 16786,
16383, 15981, 15580, 15178, 14778, 14378, 13980, 13583, 13187, 12794, 12403, 12014, 11628, 11244, 10864, 10487,
10114, 9744, 9379, 9017, 8660, 8308, 7961, 7618, 7281, 6950, 6624, 6304, 5990, 5682, 5381, 5086,
4799, 4518, 4244, 3978, 3719, 3468, 3224, 2989, 2761, 2542, 2331, 2128, 1935, 1749, 1573, 1406,
1247, 1098, 958, 827, 705, 593, 491, 398, 315, 241, 177, 123, 79, 44, 20, 5,
0, };


alignas(16) const uint8_t noteCodeToNoteLetter[12] = {67, 67, 68, 68, 69, 70, 70, 71, 71, 65, 65, 66};
alignas(16) const uint8_t noteCodeToNoteLetterFlats[12] = {67, 68, 68, 69, 69, 70, 71, 71, 65, 65, 66, 66};
//This constant is used for both sharp and flat determination.
const bool noteCodeIsSharp[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

/**
 * @brief How iterance is encoded:
 *
 * The encoding is a uint16_t with the first 8 bits being the divisor and the next 8 bits being the iteration bits.
 * The 'divisor' is the total number of iterations
 * The 'iteration bits' is a byte where each of its 8 bits represents a boolean on whether the iteration step is active or not
 *
 * Examples:
 * - If the divisor is 8 (that is, 0x08) and the iteration bits are 0000 0001, then the iteration step 0 is active and other 7 are inactive.
 *   This iterance will read as "1of8". Iterance value is 0x0801.
 * - If the divisor is 4 (that is, 0x04) and the iteration bits are 0000 1001, then iteration steps first and forth are active, while others are inactive.
 *   This iterance is Custom and will read as "1 and 4, of 4". Iterance value is 0x0409
 * - If the divisor is 6 (that is, 0x06) and the iteration bits are 0011 0000, then iteration steps fifth and sixth are active, while others are inactive.
 *   This iterance is Custom and will read as "5 and 6, of 6". Iterance value is 0x0630
 */

// CHORD TYPES

uint8_t chordTypeSemitoneOffsets[MAX_CHORD_TYPES][MAX_CHORD_NOTES] = {
    /* NO_CHORD  */ {0, 0, 0, 0},
    /* FIFTH     */ {0, 7, 0, 0},
    /* SUS2      */ {0, 2, 7, 0},
    /* MINOR     */ {0, 3, 7, 0},
    /* MAJOR     */ {0, 4, 7, 0},
    /* SUS4      */ {0, 5, 7, 0},
    /* MINOR7    */ {0, 3, 7, 10},
    /* DOMINANT7 */ {0, 4, 7, 10},
    /* MAJOR7    */ {0, 4, 7, 11},
};

uint8_t chordTypeNoteCount[MAX_CHORD_TYPES] = {
    /* NO_CHORD  */ 1,
    /* FIFTH     */ 2,
    /* SUS2      */ 3,
    /* MINOR     */ 3,
    /* MAJOR     */ 3,
    /* SUS4      */ 3,
    /* MINOR7    */ 4,
    /* DOMINANT7 */ 4,
    /* MAJOR7    */ 4,
};

