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

/// Final review I-3: `src/bsp/host/host_snapshot.{h,c}` (the C port of `tests/harness/snapshot.h`,
/// used by level 3) has no automated cross-check against the C++ original -- only a comment holds
/// them together ("MUST stay byte-identical"). This spec is that check, made permanent: it feeds
/// ONE fact set through BOTH writers -- `deluge::harness` (C++) and the C API -- and asserts the
/// `.pbm` and `.state` bytes are identical. This is the review's own by-hand verification (it fed
/// the boot golden's fact set through both writers in different insertion order and diffed the
/// bytes), turned into a test that runs every time either side changes.
///
/// The fact set: a 128x48 page-major framebuffer with a known (non-trivial, non-symmetric)
/// pattern; several pads, including black ones (which both writers must skip identically); two
/// lit LEDs; both knob indicators; a `lock` line; a `notice` line -- enough lines, and added in a
/// DIFFERENT order on each side, to exercise the category sort rather than just insertion order.

#include "bsp/host/host_snapshot.h"
#include "harness/snapshot.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../support/spec_main.h"

namespace {

constexpr uint16_t kFbWidth = 128;
constexpr uint16_t kFbHeight = 48;

/// @brief A 128x48 page-major framebuffer with bit (x, y) set when `(x + 2*y) % 5 == 0` -- a
/// pattern that lights a non-trivial, non-symmetric fraction of pixels (not all-on/all-off, which
/// a transposed-loop bug could pass by accident).
std::vector<uint8_t> make_framebuffer() {
	std::vector<uint8_t> fb(static_cast<size_t>(kFbWidth) * ((kFbHeight + 7) / 8), 0);
	for (uint16_t y = 0; y < kFbHeight; y++) {
		for (uint16_t x = 0; x < kFbWidth; x++) {
			if ((x + 2 * y) % 5 == 0) {
				fb[(y >> 3) * kFbWidth + x] |= static_cast<uint8_t>(1u << (y & 7));
			}
		}
	}
	return fb;
}

/// @brief Everything `f` has had written to it so far, read back from the start. `f` must be
/// seekable (a `std::tmpfile()`); flushes before rewinding so buffered writes are visible.
std::string read_all(FILE* f) {
	std::fflush(f);
	std::rewind(f);
	std::string out;
	char buf[4096];
	size_t n = 0;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
		out.append(buf, n);
	}
	return out;
}

std::string write_pbm_cpp(const std::vector<uint8_t>& fb) {
	return deluge::harness::oled_pbm(fb, kFbWidth, kFbHeight);
}

std::string write_pbm_c(const std::vector<uint8_t>& fb) {
	FILE* f = std::tmpfile();
	host_snapshot_write_pbm(f, fb.data(), kFbWidth, kFbHeight);
	std::string out = read_all(f);
	std::fclose(f);
	return out;
}

/// @brief The C++ side of the shared `.state` fact set, added via `deluge::harness`'s own
/// builders in one insertion order.
std::string write_state_cpp() {
	deluge::harness::StateLines lines;
	deluge::harness::add_pad_line(lines, 3, 5, 10, 20, 30);
	deluge::harness::add_pad_line(lines, 0, 0, 0, 0, 0); // black: must be skipped
	deluge::harness::add_pad_line(lines, 1, 1, 0, 0, 0); // black: must be skipped
	deluge::harness::add_pad_line(lines, 7, 2, 255, 0, 0);
	deluge::harness::add_led_line(lines, "Play", false, 0);
	deluge::harness::add_led_line(lines, "Record", false, 0);
	const uint8_t lower[4] = {1, 2, 3, 4};
	const uint8_t upper[4] = {4, 3, 2, 1};
	deluge::harness::add_indicator_line(lines, false, lower);
	deluge::harness::add_indicator_line(lines, true, upper);
	lines.add("lock loading");
	lines.add("notice Error 248");
	return lines.sorted_text();
}

/// @brief The SAME fact set as `write_state_cpp`, built through the C API, deliberately added in a
/// DIFFERENT insertion order -- the whole point of the category sort is that insertion order
/// (other than `scene`'s, which this fact set does not use) must not matter to either writer.
std::string write_state_c() {
	HostSnapshotLines lines;
	host_snapshot_reset(&lines);
	const uint8_t lower[4] = {1, 2, 3, 4};
	const uint8_t upper[4] = {4, 3, 2, 1};
	host_snapshot_add_indicator(&lines, 1, upper);
	host_snapshot_add_led(&lines, "Record", 0, 0);
	host_snapshot_add_pad(&lines, 0, 0, 0, 0, 0); // black: must be skipped
	host_snapshot_add_pad(&lines, 3, 5, 10, 20, 30);
	host_snapshot_add_indicator(&lines, 0, lower);
	host_snapshot_add_pad(&lines, 1, 1, 0, 0, 0); // black: must be skipped
	host_snapshot_addf(&lines, "notice Error 248");
	host_snapshot_add_led(&lines, "Play", 0, 0);
	host_snapshot_addf(&lines, "lock loading");
	host_snapshot_add_pad(&lines, 7, 2, 255, 0, 0);

	FILE* f = std::tmpfile();
	host_snapshot_write_state(f, &lines);
	std::string out = read_all(f);
	std::fclose(f);
	return out;
}

} // namespace

// clang-format off
describe identity("snapshot writer identity: C++ vs C (final review I-3)", $ {
	it("writes a byte-identical .pbm from the same framebuffer", _ {
		const std::vector<uint8_t> fb = make_framebuffer();
		expect(write_pbm_c(fb)).to_equal(write_pbm_cpp(fb));
	});

	it("writes a byte-identical .state from the same facts added in a different order", _ {
		expect(write_state_c()).to_equal(write_state_cpp());
	});

	it("shares one .state line-width constant between the two writers", _ {
		expect(static_cast<size_t>(UI_SCRIPT_STATE_LINE_BYTES)).to_equal(deluge::harness::kStateLineBytes);
	});
});
// clang-format on

DELUGE_SPEC(harness_identity_spec, identity)
