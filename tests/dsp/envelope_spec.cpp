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

// Exercises the Envelope as a freestanding ADSR building block — constructed and driven with zero
// Sound/Voice/ParamManager/engine context, which is the whole point of the first-principles
// refactor. Asserts the stage-machine contract (the audio bit-exactness itself is pinned end-to-end
// by the golden masters). Runs on x86 (SIMDe) and ARM/QEMU via CTest.

#include "synth/envelope.h"
#include "util/lookuptables/lookuptables.h" // decayTableSmall8 (the release curve the voice passes)
#include <cppspec.hpp>
#include <cstdint>

namespace {
using Stage = Envelope::Stage;

constexpr int32_t kFull = 2147483647; // q31 max == a fully-open envelope level.
constexpr int32_t kOff = -2147483648; // the centred output of a silent envelope.
constexpr uint32_t kBlock = 16;       // samples per render block.
constexpr uint32_t kRate = 65536;     // a moderate per-sample rate (a stage spans ~8 blocks at this).

// Render one block; the release curve is decayTableSmall8, exactly as the voice supplies.
int32_t render(Envelope& env, uint32_t attack, uint32_t decay, uint32_t sustain, uint32_t release) {
	return env.render(kBlock, attack, decay, sustain, release, decayTableSmall8);
}

// Drive the envelope until it reaches @p target (or give up), returning the blocks taken.
int32_t driveUntil(Envelope& env, Stage target, uint32_t attack, uint32_t decay, uint32_t sustain, uint32_t release) {
	for (int32_t i = 0; i < 1000; ++i) {
		if (env.stage() == target) {
			return i;
		}
		render(env, attack, decay, sustain, release);
	}
	return -1;
}
} // namespace

// clang-format off
describe envelope_spec("Envelope", ${
	it("starts a note at the bottom of the attack stage", _ {
		Envelope env;
		env.noteOn(false);
		expect(env.stage() == Stage::ATTACK).to_be_truthy();
		expect(env.isAtAttackStart()).to_be_truthy();
		expect(env.outputLevel()).to_equal(0);
	});

	it("can start straight in decay at full level", _ {
		Envelope env;
		env.noteOn(true, kFull);
		expect(env.stage() == Stage::DECAY).to_be_truthy();
		expect(env.outputLevel()).to_equal(kFull);
	});

	it("rises monotonically through the attack", _ {
		Envelope env;
		env.noteOn(false);
		int32_t first = render(env, kRate, kRate, kFull, kRate);
		int32_t second = render(env, kRate, kRate, kFull, kRate);
		expect(env.stage() == Stage::ATTACK).to_be_truthy(); // still attacking at this rate
		expect(first).to_be_less_than(second);
		expect(env.isAtAttackStart()).to_equal(false); // it has advanced
	});

	it("settles in sustain near the sustain level", _ {
		Envelope env;
		env.noteOn(false);
		expect(driveUntil(env, Stage::SUSTAIN, kRate, kRate, kFull, kRate)).to_be_greater_than(0);
		// Hold a while to let the smoother settle, then the output should be near full (positive).
		int32_t level = 0;
		for (int32_t i = 0; i < 50; ++i) {
			level = render(env, kRate, kRate, kFull, kRate);
		}
		expect(env.stage() == Stage::SUSTAIN).to_be_truthy();
		expect(level).to_be_greater_than(0);
	});

	it("releases down to silence and turns off", _ {
		Envelope env;
		env.noteOn(false);
		driveUntil(env, Stage::SUSTAIN, kRate, kRate, kFull, kRate);
		env.noteOff(/*has_sustain=*/true);
		expect(env.stage() == Stage::RELEASE).to_be_truthy();
		expect(driveUntil(env, Stage::OFF, kRate, kRate, kFull, kRate)).to_be_greater_than(0);
		expect(render(env, kRate, kRate, kFull, kRate)).to_equal(kOff); // stays silent once off
	});

	it("ignores a note-off when there is no sustain to release", _ {
		Envelope env;
		env.noteOn(false);
		env.noteOff(/*has_sustain=*/false);
		// The note-off is deferred, not acted on — we're still in the attack, not releasing.
		expect(env.stage() == Stage::ATTACK).to_be_truthy();
	});

	it("reports a stage change exactly once", _ {
		Envelope env;
		env.noteOn(false);
		expect(env.takeStageChanged()).to_be_truthy();
		expect(env.takeStageChanged()).to_equal(false);
	});

	it("enters the requested stage at the requested fast-release rate", _ {
		Envelope env;
		env.noteOn(false);
		env.unconditionalRelease(Stage::FAST_RELEASE, 5000);
		expect(env.stage() == Stage::FAST_RELEASE).to_be_truthy();
		expect(env.fastReleaseRate()).to_equal(5000u);
	});
});
// clang-format on

CPPSPEC_MAIN(envelope_spec);
