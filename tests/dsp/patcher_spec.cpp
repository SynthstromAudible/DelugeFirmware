/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// Today's patching arithmetic, site by site, with values worked from the formulas; the random cases the matrix specs
// draw from; and the frozen reference patcher (tests/dsp/reference_patcher.*) the matrix is compared against.

#include "alloc_seams.h"
#include "patch_cases.h"

#include "synth/modulation/cable_depth.h"
#include "util/fixedpoint.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <memory>

using namespace deluge::synth;
using namespace deluge::synth::test;
namespace params = deluge::modulation::params;

// clang-format off
describe patcher_spec("today's patching", ${
	context("polarity", _{
		it("halves a bipolar source to unipolar with C's truncating division", _{
			expect(to_polarity(PatchSource::ENVELOPE_0, CablePolarity::UNIPOLAR, -3)).to_equal(1073741822);
			expect(to_polarity(PatchSource::ENVELOPE_0, CablePolarity::UNIPOLAR, 3)).to_equal(1073741824);
		});
		it("reads aftertouch bipolar by recentring and doubling", _{
			expect(to_polarity(PatchSource::AFTERTOUCH, CablePolarity::BIPOLAR, 1073741823)).to_equal(0);
			expect(to_polarity(PatchSource::AFTERTOUCH, CablePolarity::UNIPOLAR, 5)).to_equal(5);
		});
		it("never converts Y", _{
			expect(to_polarity(PatchSource::Y, CablePolarity::UNIPOLAR, -5)).to_equal(-5);
		});
	});

	context("depth modes", _{
		it("squares a pitch depth, keeping its sign", _{
			expect(modified_depth(-(1 << 30), PatchSource::LFO_LOCAL_1, params::LOCAL_OSC_A_PITCH_ADJUST))
			    .to_equal(-(1 << 29));
			expect(modified_depth(1 << 30, PatchSource::LFO_LOCAL_1, params::GLOBAL_DELAY_RATE)).to_equal(1 << 29);
		});
		it("leaves other params' depths alone", _{
			expect(modified_depth(12345, PatchSource::LFO_LOCAL_1, params::LOCAL_LPF_FREQ)).to_equal(12345);
		});
	});

	context("range", _{
		it("floor-shifts and clamps every cable through a range of 1", _{
			expect(apply_range(7, 536870912)).to_equal(0);                 // (7 * 2^29) >> 32 = 0
			expect(apply_range(1 << 30, 536870912)).to_equal(536870904);   // 2^27, clamped to 2^26 - 1, << 3
			expect(apply_range(-(1 << 30), 536870912)).to_equal(-(1 << 29)); // -2^27, clamped to -2^26, << 3
		});
	});

	context("the random cases", _{
		it("route cables in both scopes, depth cables among them", _{
			int32_t with_voice = 0;
			int32_t with_sound = 0;
			int32_t with_depth = 0;
			for (uint32_t seed = 1; seed <= 2000; ++seed) {
				const PatchCase c = random_case(seed);
				auto compiled = std::make_unique<CompiledCase>();
				compile_case(c, *compiled);
				with_voice += compiled->build.num_destinations[kLocal] != 0 ? 1 : 0;
				with_sound += compiled->build.num_destinations[kGlobal] != 0 ? 1 : 0;
				bool depth = false;
				for (int32_t i = 0; i < compiled->build.num_cables; ++i) {
					depth = depth || compiled->build.cables[i].range_index >= 0;
				}
				with_depth += depth ? 1 : 0;
			}
			expect(with_voice).to_be_greater_than(1500);
			expect(with_sound).to_be_greater_than(1000);
			expect(with_depth).to_be_greater_than(500);
		});
	});

	context("the reference patcher", _{
		// The Patcher's depth results are one array for every pass: a pass that skips a depth destination reads the
		// last value any pass wrote there. The matrix runtime keeps that until the audible window fixes it.
		it("shares its depth results across passes, which per-pass results change", _{
			int32_t leak = 0;
			for (uint32_t seed = 1; seed <= 2000; ++seed) {
				const PatchCase c = random_case(seed);
				auto compiled = std::make_unique<CompiledCase>();
				compile_case(c, *compiled);
				const ReferenceResult a = reference_results(c, *compiled, Ranges::shared);
				const ReferenceResult b = reference_results(c, *compiled, Ranges::per_pass);
				leak += (a.voice != b.voice || a.sound != b.sound) ? 1 : 0;
			}
			expect(leak).to_be_greater_than(0);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(patcher_spec);
