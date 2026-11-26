/*
 * Specs for the iterance migration helpers (iterance_migration.h).
 *
 * deluge::project::kIterancePresets[] opens with FIRST and LAST, so the 1of2…8of8 presets sit at indices 2-36. Song
 * files in the legacy encodings index those presets without FIRST and LAST, at 0-34, so the migration helpers apply a
 * +2 correction for those files to load correctly.
 *
 * These specs exercise the real deluge::project::kIterancePresets[] from preset_tables.cpp, linked into this driver,
 * not a local copy, so they can't drift out of sync with production data.
 */

#include "definitions_cxx.hpp"
#include "project/iterance.h"
#include "project/note_probability.h"
#include "sequence/iterance_migration.h"

#include <array>

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::project::Iterance;

// ---------------------------------------------------------------------------
// iteranceFromLegacyProbabilityByte — C1.2 / pre-lift noteHexLength==22 path
//
// Legacy encoding: probability byte = deluge::project::kProbabilityValueCount + legacyPresetIndex
//   legacyPresetIndex 1 = "1 of 2", 2 = "2 of 2", ..., 35 = "8 of 8"
// ---------------------------------------------------------------------------

// clang-format off
describe iterance_legacy_probability_group("iterance legacy probability", ${
	it("byte21 decodes to 1of2", _ {
		// probability 21 = deluge::project::kProbabilityValueCount(20) + 1 = legacy "1 of 2"
		Iterance result = iteranceFromLegacyProbabilityByte(21);
		expect(result.divisor).to_equal(2u);
		expect((unsigned)result.steps.to_ulong()).to_equal(0b1u);
	});

	it("byte22 decodes to 2of2", _ {
		Iterance result = iteranceFromLegacyProbabilityByte(22);
		expect(result.divisor).to_equal(2u);
		expect((unsigned)result.steps.to_ulong()).to_equal(0b10u);
	});

	it("byte23 decodes to 1of3", _ {
		Iterance result = iteranceFromLegacyProbabilityByte(23);
		expect(result.divisor).to_equal(3u);
		expect((unsigned)result.steps.to_ulong()).to_equal(0b1u);
	});

	it("byte55 decodes to 8of8", _ {
		// probability 55 = deluge::project::kProbabilityValueCount(20) + 35 = legacy "8 of 8" (last preset)
		Iterance result = iteranceFromLegacyProbabilityByte(55);
		expect(result.divisor).to_equal(8u);
		expect((unsigned)result.steps.to_ulong()).to_equal(0b10000000u);
	});

	it("result is never FIRST", _ {
		// No legacy probability value maps to FIRST, which the legacy encodings could not express.
		for (int32_t prob = 21; prob <= 55; ++prob) {
			Iterance result = iteranceFromLegacyProbabilityByte(prob);
			expect(result.packed()).not_().to_equal(deluge::project::kIteranceFirst.packed());
		}
	});

	it("result is never LAST", _ {
		for (int32_t prob = 21; prob <= 55; ++prob) {
			Iterance result = iteranceFromLegacyProbabilityByte(prob);
			expect(result.packed()).not_().to_equal(deluge::project::kIteranceLast.packed());
		}
	});

	it("byte20 below range decodes to off", _ {
		// probability 20 = deluge::project::kProbabilityValueCount(20) + 0: below the legacy iterance
		// range (which starts at legacyPresetIndex 1), should fall back to default.
		Iterance result = iteranceFromLegacyProbabilityByte(20);
		expect(result.packed()).to_equal(deluge::project::kIteranceOff.packed());
	});

	it("byte56 above range decodes to off", _ {
		// probability 56 = deluge::project::kProbabilityValueCount(20) + 36: one past the last legacy
		// preset (35 = "8 of 8"), should fall back to default rather than reading
		// out of bounds.
		Iterance result = iteranceFromLegacyProbabilityByte(56);
		expect(result.packed()).to_equal(deluge::project::kIteranceOff.packed());
	});
});

// ---------------------------------------------------------------------------
// iteranceFromLegacyPresetIndex — early-1.3-nightly noteHexLength==26 path
//
// Legacy encoding: preset index stored directly; 0=OFF, 1=1of2, ..., 35=8of8, 36=custom
// ---------------------------------------------------------------------------

describe iterance_legacy_preset_index_group("iterance legacy preset index", ${
	it("index0 decodes to off", _ {
		Iterance result = iteranceFromLegacyPresetIndex(0);
		expect(result.packed()).to_equal(deluge::project::kIteranceOff.packed());
	});

	it("index1 decodes to 1of2", _ {
		// Legacy index 1 is "1 of 2", at preset index 3 (deluge::project::kIterancePresets[2]) after the +2 offset.
		Iterance result = iteranceFromLegacyPresetIndex(1);
		expect(result.divisor).to_equal(2u);
		expect((unsigned)result.steps.to_ulong()).to_equal(0b1u);
	});

	it("index2 decodes to 2of2", _ {
		Iterance result = iteranceFromLegacyPresetIndex(2);
		expect(result.divisor).to_equal(2u);
		expect((unsigned)result.steps.to_ulong()).to_equal(0b10u);
	});

	it("index35 decodes to 8of8", _ {
		Iterance result = iteranceFromLegacyPresetIndex(35);
		expect(result.divisor).to_equal(8u);
		expect((unsigned)result.steps.to_ulong()).to_equal(0b10000000u);
	});

	it("index36 decodes to custom", _ {
		// Legacy "custom" index 36 maps to deluge::project::kCustomIterancePreset (38) → deluge::project::kIteranceCustom.
		Iterance result = iteranceFromLegacyPresetIndex(36);
		expect(result.packed()).to_equal(deluge::project::kIteranceCustom.packed());
	});

	it("no index decodes to FIRST", _ {
		for (int32_t idx = 0; idx <= 36; ++idx) {
			Iterance result = iteranceFromLegacyPresetIndex(idx);
			expect(result.packed()).not_().to_equal(deluge::project::kIteranceFirst.packed());
		}
	});

	it("no index decodes to LAST", _ {
		for (int32_t idx = 0; idx <= 36; ++idx) {
			Iterance result = iteranceFromLegacyPresetIndex(idx);
			expect(result.packed()).not_().to_equal(deluge::project::kIteranceLast.packed());
		}
	});
});

DELUGE_SPEC(iterance_migration_spec, iterance_legacy_probability_group, iterance_legacy_preset_index_group)
