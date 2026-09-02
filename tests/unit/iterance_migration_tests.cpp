/*
 * Tests for iterance migration helpers (iterance_migration.h).
 *
 * Background: commit 3ce3d41d prepended FIRST and LAST to iterancePresets[], shifting
 * the existing 1of2…8of8 presets from indices 0-34 to indices 2-36.  Song files saved
 * before that commit encode iterance using the old (pre-shift) indices.  The migration
 * helpers must apply the +2 correction so those files load correctly.
 *
 * This file also provides a stub definition of iterancePresets so that
 * iterance_migration.cpp can be linked without pulling in all of lookuptables.cpp.
 */

#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "model/iterance/iterance.h"
#include "model/iterance/iterance_migration.h"
#include <array>

// ---------------------------------------------------------------------------
// Stub: mirrors the real definition in lookuptables.cpp exactly.
// Keep in sync with src/deluge/util/lookuptables/lookuptables.cpp.
// ---------------------------------------------------------------------------
const std::array<Iterance, kNumIterancePresets> iterancePresets = {
    Iterance{0, 1}, // FIRST  (index 0)
    Iterance{0, 2}, // LAST   (index 1)
    // 1of2 … 8of8 (indices 2-36)
    Iterance{2, 0b1},        Iterance{2, 0b10},
    Iterance{3, 0b1},        Iterance{3, 0b10},        Iterance{3, 0b100},
    Iterance{4, 0b1},        Iterance{4, 0b10},        Iterance{4, 0b100},        Iterance{4, 0b1000},
    Iterance{5, 0b1},        Iterance{5, 0b10},        Iterance{5, 0b100},        Iterance{5, 0b1000},
    Iterance{5, 0b10000},
    Iterance{6, 0b1},        Iterance{6, 0b10},        Iterance{6, 0b100},        Iterance{6, 0b1000},
    Iterance{6, 0b10000},    Iterance{6, 0b100000},
    Iterance{7, 0b1},        Iterance{7, 0b10},        Iterance{7, 0b100},        Iterance{7, 0b1000},
    Iterance{7, 0b10000},    Iterance{7, 0b100000},    Iterance{7, 0b1000000},
    Iterance{8, 0b1},        Iterance{8, 0b10},        Iterance{8, 0b100},        Iterance{8, 0b1000},
    Iterance{8, 0b10000},    Iterance{8, 0b100000},    Iterance{8, 0b1000000},    Iterance{8, 0b10000000},
};

// ---------------------------------------------------------------------------
// iteranceFromLegacyProbabilityByte — C1.2 / pre-lift noteHexLength==22 path
//
// Old encoding: probability byte = kNumProbabilityValues + legacyPresetIndex
//   legacyPresetIndex 1 = "1 of 2", 2 = "2 of 2", ..., 35 = "8 of 8"
// ---------------------------------------------------------------------------
TEST_GROUP(IteranceLegacyProbability){};

TEST(IteranceLegacyProbability, byte21_decodes_to_1of2) {
    // probability 21 = kNumProbabilityValues(20) + 1 = old "1 of 2"
    Iterance result = iteranceFromLegacyProbabilityByte(21);
    CHECK_EQUAL(2u, result.divisor);
    CHECK_EQUAL(0b1u, (unsigned)result.iteranceStep.to_ulong());
}

TEST(IteranceLegacyProbability, byte22_decodes_to_2of2) {
    Iterance result = iteranceFromLegacyProbabilityByte(22);
    CHECK_EQUAL(2u, result.divisor);
    CHECK_EQUAL(0b10u, (unsigned)result.iteranceStep.to_ulong());
}

TEST(IteranceLegacyProbability, byte23_decodes_to_1of3) {
    Iterance result = iteranceFromLegacyProbabilityByte(23);
    CHECK_EQUAL(3u, result.divisor);
    CHECK_EQUAL(0b1u, (unsigned)result.iteranceStep.to_ulong());
}

TEST(IteranceLegacyProbability, byte55_decodes_to_8of8) {
    // probability 55 = kNumProbabilityValues(20) + 35 = old "8 of 8" (last preset)
    Iterance result = iteranceFromLegacyProbabilityByte(55);
    CHECK_EQUAL(8u, result.divisor);
    CHECK_EQUAL(0b10000000u, (unsigned)result.iteranceStep.to_ulong());
}

TEST(IteranceLegacyProbability, result_is_never_FIRST) {
    // The whole point of the fix: no legacy probability value should map to FIRST.
    for (int32_t prob = 21; prob <= 55; ++prob) {
        Iterance result = iteranceFromLegacyProbabilityByte(prob);
        CHECK_FALSE_TEXT(result == kFirstIteranceValue, "legacy byte mapped to FIRST");
    }
}

TEST(IteranceLegacyProbability, result_is_never_LAST) {
    for (int32_t prob = 21; prob <= 55; ++prob) {
        Iterance result = iteranceFromLegacyProbabilityByte(prob);
        CHECK_FALSE_TEXT(result == kLastIteranceValue, "legacy byte mapped to LAST");
    }
}

// ---------------------------------------------------------------------------
// iteranceFromLegacyPresetIndex — early-1.3-nightly noteHexLength==26 path
//
// Old encoding: preset index stored directly; 0=OFF, 1=1of2, ..., 35=8of8, 36=custom
// ---------------------------------------------------------------------------
TEST_GROUP(IteranceLegacyPresetIndex){};

TEST(IteranceLegacyPresetIndex, index0_decodes_to_off) {
    Iterance result = iteranceFromLegacyPresetIndex(0);
    CHECK(result == kDefaultIteranceValue);
}

TEST(IteranceLegacyPresetIndex, index1_decodes_to_1of2) {
    // Old index 1 was "1 of 2"; after the +2 shift it lives at new index 3.
    Iterance result = iteranceFromLegacyPresetIndex(1);
    CHECK_EQUAL(2u, result.divisor);
    CHECK_EQUAL(0b1u, (unsigned)result.iteranceStep.to_ulong());
}

TEST(IteranceLegacyPresetIndex, index2_decodes_to_2of2) {
    Iterance result = iteranceFromLegacyPresetIndex(2);
    CHECK_EQUAL(2u, result.divisor);
    CHECK_EQUAL(0b10u, (unsigned)result.iteranceStep.to_ulong());
}

TEST(IteranceLegacyPresetIndex, index35_decodes_to_8of8) {
    Iterance result = iteranceFromLegacyPresetIndex(35);
    CHECK_EQUAL(8u, result.divisor);
    CHECK_EQUAL(0b10000000u, (unsigned)result.iteranceStep.to_ulong());
}

TEST(IteranceLegacyPresetIndex, index36_decodes_to_custom) {
    // Old "custom" index 36 maps to kCustomIterancePreset (38) → kCustomIteranceValue.
    Iterance result = iteranceFromLegacyPresetIndex(36);
    CHECK(result == kCustomIteranceValue);
}

TEST(IteranceLegacyPresetIndex, no_index_decodes_to_FIRST) {
    for (int32_t idx = 0; idx <= 36; ++idx) {
        Iterance result = iteranceFromLegacyPresetIndex(idx);
        CHECK_FALSE_TEXT(result == kFirstIteranceValue, "legacy index mapped to FIRST");
    }
}

TEST(IteranceLegacyPresetIndex, no_index_decodes_to_LAST) {
    for (int32_t idx = 0; idx <= 36; ++idx) {
        Iterance result = iteranceFromLegacyPresetIndex(idx);
        CHECK_FALSE_TEXT(result == kLastIteranceValue, "legacy index mapped to LAST");
    }
}
