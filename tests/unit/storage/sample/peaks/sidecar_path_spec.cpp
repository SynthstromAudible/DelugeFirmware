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

#include "storage/audio/sidecar_path.h"

#include "cppspec.hpp"

#include <string>

// A sidecar's card path: its shard is the key's top byte, its name the key in eight hex digits.

using deluge::storage::SidecarFile;
using deluge::storage::sidecarPath;

namespace {
std::string pathOf(uint32_t cluster, SidecarFile file) {
	return sidecarPath(cluster, file).data();
}
} // namespace

// clang-format off
describe sidecar_path("a sidecar's card path", $ {
	it("shards by the key's top byte and names the file by the whole key", _ {
		expect(pathOf(0x9A12C4E0, SidecarFile::PK)).to_equal(std::string{"DELUGE/PEAKS/9A/9A12C4E0.PK"});
		expect(pathOf(0x9A12C4E0, SidecarFile::TMP)).to_equal(std::string{"DELUGE/PEAKS/9A/9A12C4E0.TMP"});
	});
	it("keeps leading zeros", _ {
		expect(pathOf(0x0000003F, SidecarFile::PK)).to_equal(std::string{"DELUGE/PEAKS/00/0000003F.PK"});
	});
	it("fits its buffer with the terminator", _ {
		expect(pathOf(0xFFFFFFFF, SidecarFile::TMP).size()).to_be_less_than(size_t{32});
	});
});
// clang-format on

CPPSPEC_SPEC(sidecar_path)
