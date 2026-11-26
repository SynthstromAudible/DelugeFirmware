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

#include "storage/audio/sidecar_policy.h"

#include "cppspec.hpp"

// When a sample's sidecar is written: after its lookup, when it completes, grows by a quarter, or learns a pitch.

using deluge::storage::shouldWriteSidecar;
using deluge::storage::SidecarProgress;

namespace {
/// A keyed, looked-up, final sample of 100 entries with @p complete of them complete and @p written at its last write.
SidecarProgress at(uint64_t complete, uint64_t written) {
	return {.keyed = true, .looked = true, .final = true, .complete = complete, .entries = 100, .written = written};
}
} // namespace

// clang-format off
describe sidecar_policy("when a sample's sidecar is written", $ {
	it("never before the sample is keyed, looked up and final, nor after two failures", _ {
		SidecarProgress p = at(100, 0);
		expect(shouldWriteSidecar(p)).to_equal(true);
		p.keyed = false;
		expect(shouldWriteSidecar(p)).to_equal(false);
		p = at(100, 0);
		p.looked = false;
		expect(shouldWriteSidecar(p)).to_equal(false);
		p = at(100, 0);
		p.final = false;
		expect(shouldWriteSidecar(p)).to_equal(false);
		p = at(100, 0);
		p.failures = 2;
		expect(shouldWriteSidecar(p)).to_equal(false);
		p.failures = 1;
		expect(shouldWriteSidecar(p)).to_equal(true);
	});
	it("once when complete", _ {
		expect(shouldWriteSidecar(at(100, 0))).to_equal(true);
		expect(shouldWriteSidecar(at(100, 100))).to_equal(false);
		expect(shouldWriteSidecar(at(100, 90))).to_equal(true);
	});
	it("a partial index only from a quarter covered, and again after a quarter's growth", _ {
		expect(shouldWriteSidecar(at(24, 0))).to_equal(false);
		expect(shouldWriteSidecar(at(25, 0))).to_equal(true);
		expect(shouldWriteSidecar(at(49, 40))).to_equal(false);
		expect(shouldWriteSidecar(at(50, 40))).to_equal(true);
		expect(shouldWriteSidecar(at(40, 40))).to_equal(false);
	});
	it("whenever a pitch was remembered, complete or not", _ {
		SidecarProgress p = at(3, 3);
		p.pitchUnwritten = true;
		expect(shouldWriteSidecar(p)).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(sidecar_policy)
