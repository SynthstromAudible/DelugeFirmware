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

/// The menu-facing half of a setting's row: how a menu's value maps onto the stored one.
#include "engine-api/types/setting.h"

#include <cppspec.hpp>

namespace {
using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::engine::SettingSentinel;
using deluge::engine::UserScale;

constexpr SettingSentinel kOff[] = {{.user = 0, .stored = 127}};
constexpr SettingInfo kMaxVoices{
    .path = "maxVoices",
    .kind = SettingKind::INTEGER,
    .min = 1,
    .max = 127,
    .user = {.user_min = 0, .user_max = 16, .stored_min = 0, .stored_max = 16, .sentinels = kOff},
};
constexpr SettingInfo kRoom{
    .path = "reverb/roomSize",
    .kind = SettingKind::INTEGER,
    .min = 0,
    .max = 2147483647,
    .user = {.user_min = 0, .user_max = 50, .stored_min = 0, .stored_max = 2147483647},
};
constexpr SettingSentinel kAuto[] = {{.user = -1, .stored = -21474836}};
constexpr SettingInfo kSentinelOnly{
    .path = "volume", .kind = SettingKind::INTEGER, .min = -21474836, .max = 1073741800, .user = {.sentinels = kAuto}};
constexpr SettingInfo kPlain{.path = "plain", .kind = SettingKind::INTEGER, .min = -5, .max = 5};
} // namespace

// clang-format off
describe setting_descriptor("SettingInfo's user mapping", $ {
	it("maps a sentinel both ways and leaves other values linear", _ {
		expect(*deluge::engine::to_stored(kMaxVoices, 0)).to_equal(127);
		expect(deluge::engine::to_user(kMaxVoices, 127)).to_equal(0);
		expect(*deluge::engine::to_stored(kMaxVoices, 8)).to_equal(8);
		expect(deluge::engine::to_user(kMaxVoices, 8)).to_equal(8);
	});
	it("scales a fraction's user range onto the stored range and back", _ {
		expect(*deluge::engine::to_stored(kRoom, 50)).to_equal(2147483647);
		expect(*deluge::engine::to_stored(kRoom, 0)).to_equal(0);
		expect(deluge::engine::to_user(kRoom, *deluge::engine::to_stored(kRoom, 25))).to_equal(25);
	});
	it("passes values through a scale that has only a sentinel, mapping the sentinel", _ {
		expect(*deluge::engine::to_stored(kSentinelOnly, -1)).to_equal(-21474836);
		expect(deluge::engine::to_user(kSentinelOnly, -21474836)).to_equal(-1);
		expect(*deluge::engine::to_stored(kSentinelOnly, 1000)).to_equal(1000);
		expect(deluge::engine::to_user(kSentinelOnly, 1000)).to_equal(1000);
	});
	it("refuses a user value outside the user range", _ {
		expect(deluge::engine::to_stored(kMaxVoices, 17).has_value()).to_be_false();
		expect(deluge::engine::to_stored(kMaxVoices, -1).has_value()).to_be_false();
	});
	it("treats a row with no user scale as user == stored within the stored range", _ {
		expect(*deluge::engine::to_stored(kPlain, -5)).to_equal(-5);
		expect(deluge::engine::to_stored(kPlain, 6).has_value()).to_be_false();
		expect(deluge::engine::to_user(kPlain, 3)).to_equal(3);
	});
});
// clang-format on

CPPSPEC_SPEC(setting_descriptor)
