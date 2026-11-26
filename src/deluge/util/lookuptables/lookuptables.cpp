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
// The l10n preset name table (chordNames) lives in preset_tables.cpp
// so this TU stays gui-free (numeric DSP tables only).



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

