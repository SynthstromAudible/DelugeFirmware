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

/// A setting's display mapping: each kind's menu value against the stored value(s), with the expected numbers
/// transcribed from the legacy menu items' own conversions.
#include "engine-api/types/display_mapping.h"
#include "engine-api/types/setting.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace {
using deluge::engine::DisplayMappingKind;
using deluge::engine::from_menu;
using deluge::engine::mapping_of;
using deluge::engine::SettingContext;
using deluge::engine::SettingDescriptorExtras;
using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::engine::SettingSentinel;
using deluge::engine::to_menu;

using Pair = std::pair<int32_t, int32_t>;

// Local rows, one per mapping kind, each shaped as legacy's menu item for that setting.

constexpr std::array<std::string_view, 3> kSyncTypes{"0", "10", "19"};
constexpr SettingDescriptorExtras kSyncLevelExtras{
    .mapping = {.kind = DisplayMappingKind::COMBINED_SYNC, .partner = "lfo1/syncType"}};
constexpr SettingInfo kSyncLevel{
    .path = "lfo1/syncLevel",
    .min = 0,
    .max = 9,
    .extras = &kSyncLevelExtras,
};

constexpr SettingDescriptorExtras kTransposeExtras{
    .mapping = {.kind = DisplayMappingKind::TRANSPOSE_CENTS, .partner = "cents"}};
constexpr SettingInfo kTranspose{
    .path = "transpose",
    .min = -96,
    .max = 96,
    .extras = &kTransposeExtras,
};

constexpr SettingDescriptorExtras kPhaseExtras{.mapping = {.kind = DisplayMappingKind::PHASE_DEGREES}};
constexpr SettingInfo kPhase{
    .path = "osc1/retrigPhase",
    .min = -2147483647 - 1,
    .max = 2147483647,
    .extras = &kPhaseExtras,
};

constexpr SettingDescriptorExtras kLinearInterpolationExtras{.mapping = {.kind = DisplayMappingKind::INVERTED_BOOL}};
constexpr SettingInfo kLinearInterpolation{
    .path = "osc1/linearInterpolation",
    .kind = SettingKind::BOOL,
    .extras = &kLinearInterpolationExtras,
};

constexpr std::array<std::string_view, 17> kMidiChannels{"0", "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",
                                                         "9", "10", "11", "12", "13", "14", "15", "255"};
constexpr std::array<uint8_t, 17> kMidiChannelOrder{16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
constexpr SettingDescriptorExtras kMidiChannelExtras{
    .mapping = {.kind = DisplayMappingKind::CHOICE_ORDER, .order = kMidiChannelOrder}};
constexpr SettingInfo kMidiChannel{
    .path = "midiOutput/channel",
    .kind = SettingKind::CHOICE,
    .choices = kMidiChannels,
    .extras = &kMidiChannelExtras,
};

constexpr SettingSentinel kOff[] = {{.user = 0, .stored = 127}};
constexpr SettingDescriptorExtras kMaxVoicesExtras{.mapping = {.kind = DisplayMappingKind::CLAMP_HIGH, .clamp_to = 0}};
constexpr SettingInfo kMaxVoices{
    .path = "maxVoices",
    .min = 1,
    .max = 127,
    .user = {.user_min = 0, .user_max = 16, .stored_min = 0, .stored_max = 16, .sentinels = kOff},
    .extras = &kMaxVoicesExtras,
};

// `attackRateTable` and `releaseRateTable` (util/lookuptables/lookuptables.cpp), as the sidechain's owner supplies
// them.
constexpr std::array<int32_t, 51> kAttackRates{
    262144, 221969, 187951, 159147, 134757, 114105, 96618, 81811, 69273, 58656, 49667, 42055, 35610,
    30153,  25532,  21619,  18306,  15500,  13125,  11113, 9410,  7968,  6747,  5713,  4837,  4096,
    3468,   2937,   2487,   2106,   1783,   1510,   1278,  1082,  917,   776,   657,   556,   471,
    399,    338,    286,    242,    205,    174,    147,   124,   105,   89,    76,    64,
};
constexpr std::array<int32_t, 51> kReleaseRates{
    32691, 4604, 2444, 1648, 1234, 980, 809, 685, 592, 519, 460, 412, 372, 338, 309, 283, 261,
    241,   224,  208,  194,  181,  169, 159, 149, 140, 132, 124, 117, 110, 104, 98,  93,  88,
    83,    78,   74,   70,   66,   62,  59,  56,  53,  50,  47,  44,  41,  39,  36,  34,  32,
};
constexpr SettingDescriptorExtras kSidechainAttackExtras{
    .mapping = {.kind = DisplayMappingKind::TABLE, .table = kAttackRates, .table_shift = 2}};
constexpr SettingInfo kSidechainAttack{
    .path = "sidechain/attack",
    .min = 0,
    .max = 2147483647,
    .extras = &kSidechainAttackExtras,
};
constexpr SettingDescriptorExtras kSidechainReleaseExtras{
    .mapping = {.kind = DisplayMappingKind::TABLE, .table = kReleaseRates, .table_shift = 3}};
constexpr SettingInfo kSidechainRelease{
    .path = "sidechain/release",
    .min = 0,
    .max = 2147483647,
    .extras = &kSidechainReleaseExtras,
};

constexpr SettingInfo kPlain{.path = "plain", .min = -5, .max = 5};

/// `SyncLevel::syncTypeAndLevelToMenuOption` over every (type, level) pair: row = the partner's choice index (even,
/// triplet, dotted), column = the level.
constexpr int32_t kSyncMenuOf[3][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {9, 10, 11, 12, 13, 14, 15, 16, 17, 18},
    {18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
};
/// `syncValueToSyncLevel` and `syncValueToSyncType` for each menu value 0..27: {level, the type's choice index}.
constexpr Pair kSyncStoredOf[28] = {
    {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {1, 1}, {2, 1}, {3, 1}, {4, 1},
    {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 2},
};

Pair stored_of(const SettingInfo& row, int32_t menu) {
	const std::optional<Pair> stored = from_menu(row, menu);
	return stored.value_or(Pair{-12345, -12345});
}

constexpr bool is_kit_lane(const SettingContext& where) {
	return where.owner == SettingContext::Owner::KIT_LANE;
}
/// Choice 4 (choke) applies only on a kit lane.
constexpr bool choke_on_kit_lane(const SettingContext& where, std::size_t choice) {
	return choice != 4 || is_kit_lane(where);
}
constexpr SettingDescriptorExtras kKitLaneOnlyExtras{.applies = is_kit_lane, .choice_applies = choke_on_kit_lane};
constexpr SettingInfo kKitLaneOnly{.path = "polyphonic", .kind = SettingKind::CHOICE, .extras = &kKitLaneOnlyExtras};

// The mappings are constant expressions, so a row's menu values can be computed at compile time.
static_assert(to_menu(kSyncLevel, 1, 2) == 19);
static_assert(from_menu(kSyncLevel, 19) == Pair{1, 2});
static_assert(to_menu(kMaxVoices, 126) == 0);
static_assert(from_menu(kTranspose, -51) == Pair{-1, 49});
static_assert(from_menu(kSidechainRelease, 50) == Pair{256, 0});
static_assert(to_menu(kPlain, 3) == 3 && !from_menu(kPlain, 6));
} // namespace

// clang-format off
describe display_mapping("A setting's display mapping", $ {
	it("leaves a row with no mapping linear, as to_user and to_stored", _ {
		expect(to_menu(kPlain, 3)).to_equal(3);
		expect(stored_of(kPlain, -5)).to_equal(Pair{-5, 0});
		expect(from_menu(kPlain, 6).has_value()).to_be_false();
	});

	it("maps every stored sync pair to legacy's combined menu value", _ {
		for (int32_t type = 0; type < 3; ++type) {
			for (int32_t level = 0; level <= 9; ++level) {
				expect(to_menu(kSyncLevel, level, type)).to_equal(kSyncMenuOf[type][level]);
			}
		}
	});
	it("splits every combined sync value 0..27 as legacy does, and back", _ {
		for (int32_t menu = 0; menu < 28; ++menu) {
			expect(stored_of(kSyncLevel, menu)).to_equal(kSyncStoredOf[menu]);
			const Pair stored = stored_of(kSyncLevel, menu);
			expect(to_menu(kSyncLevel, stored.first, stored.second)).to_equal(menu);
		}
		expect(from_menu(kSyncLevel, -1).has_value()).to_be_false();
		expect(from_menu(kSyncLevel, 28).has_value()).to_be_false();
	});
	it("names the sync partner row", _ {
		expect(mapping_of(kSyncLevel).partner == std::string_view{"lfo1/syncType"}).to_be_true();
		expect(kSyncTypes[static_cast<std::size_t>(stored_of(kSyncLevel, 19).second)] == std::string_view{"19"}).to_be_true();
	});

	it("combines transpose and cents as transpose*100+cents", _ {
		expect(to_menu(kTranspose, 96, 0)).to_equal(9600);
		expect(to_menu(kTranspose, -96, 0)).to_equal(-9600);
		expect(to_menu(kTranspose, 2, -50)).to_equal(150);
		expect(to_menu(kTranspose, -1, 49)).to_equal(-51);
		expect(to_menu(kTranspose, 1, -50)).to_equal(50);
	});
	it("splits a transpose menu value as computeFinalValuesForTranspose does", _ {
		expect(stored_of(kTranspose, 9600)).to_equal(Pair{96, 0});
		expect(stored_of(kTranspose, -9600)).to_equal(Pair{-96, 0});
		expect(stored_of(kTranspose, 0)).to_equal(Pair{0, 0});
		expect(stored_of(kTranspose, 150)).to_equal(Pair{2, -50});
		expect(stored_of(kTranspose, 149)).to_equal(Pair{1, 49});
		expect(stored_of(kTranspose, 50)).to_equal(Pair{1, -50});
		expect(stored_of(kTranspose, 49)).to_equal(Pair{0, 49});
		expect(stored_of(kTranspose, -50)).to_equal(Pair{0, -50});
		expect(stored_of(kTranspose, -51)).to_equal(Pair{-1, 49});
		expect(stored_of(kTranspose, -149)).to_equal(Pair{-1, -49});
		expect(stored_of(kTranspose, -150)).to_equal(Pair{-1, -50});
		expect(from_menu(kTranspose, 9601).has_value()).to_be_false();
		expect(from_menu(kTranspose, -9601).has_value()).to_be_false();
	});

	it("shows a retrigger phase in degrees, and the free-running sentinel as -1", _ {
		expect(to_menu(kPhase, -1)).to_equal(-1);
		expect(to_menu(kPhase, 0)).to_equal(0);
		expect(to_menu(kPhase, 11930463)).to_equal(0);
		expect(to_menu(kPhase, 11930464)).to_equal(1);
		expect(to_menu(kPhase, 2135553056)).to_equal(179);
		expect(to_menu(kPhase, 2147483520)).to_equal(180);
		expect(to_menu(kPhase, -11930720)).to_equal(359);
		expect(to_menu(kPhase, -256)).to_equal(360);
	});
	it("stores a retrigger phase's degrees as RetriggerPhase::writeCurrentValue does", _ {
		expect(stored_of(kPhase, -1)).to_equal(Pair{-1, 0});
		expect(stored_of(kPhase, 0)).to_equal(Pair{0, 0});
		expect(stored_of(kPhase, 1)).to_equal(Pair{11930464, 0});
		expect(stored_of(kPhase, 179)).to_equal(Pair{2135553056, 0});
		expect(stored_of(kPhase, 180)).to_equal(Pair{2147483520, 0});
		expect(stored_of(kPhase, 359)).to_equal(Pair{-11930720, 0});
		expect(stored_of(kPhase, 360)).to_equal(Pair{-256, 0});
		expect(from_menu(kPhase, -2).has_value()).to_be_false();
		expect(from_menu(kPhase, 361).has_value()).to_be_false();
	});

	it("inverts the interpolation bool: menu 0 is LINEAR, stored 1", _ {
		expect(to_menu(kLinearInterpolation, 1)).to_equal(0);
		expect(to_menu(kLinearInterpolation, 0)).to_equal(1);
		expect(stored_of(kLinearInterpolation, 0)).to_equal(Pair{1, 0});
		expect(stored_of(kLinearInterpolation, 1)).to_equal(Pair{0, 0});
		expect(from_menu(kLinearInterpolation, 2).has_value()).to_be_false();
		expect(from_menu(kLinearInterpolation, -1).has_value()).to_be_false();
	});

	it("lists the MIDI channel's none first: menu 0 is \"255\", menu n is channel n-1", _ {
		expect(kMidiChannels[static_cast<std::size_t>(stored_of(kMidiChannel, 0).first)] == std::string_view{"255"}).to_be_true();
		for (int32_t menu = 1; menu <= 16; ++menu) {
			expect(stored_of(kMidiChannel, menu)).to_equal(Pair{menu - 1, 0});
		}
		for (int32_t menu = 0; menu <= 16; ++menu) {
			expect(to_menu(kMidiChannel, stored_of(kMidiChannel, menu).first)).to_equal(menu);
		}
		expect(to_menu(kMidiChannel, 16)).to_equal(0);
		expect(from_menu(kMidiChannel, 17).has_value()).to_be_false();
		expect(from_menu(kMidiChannel, -1).has_value()).to_be_false();
	});

	it("shows max voices above 16 as 0, as VoiceCount::readCurrentValue does", _ {
		expect(to_menu(kMaxVoices, 1)).to_equal(1);
		expect(to_menu(kMaxVoices, 16)).to_equal(16);
		expect(to_menu(kMaxVoices, 17)).to_equal(0);
		expect(to_menu(kMaxVoices, 126)).to_equal(0);
		expect(to_menu(kMaxVoices, 127)).to_equal(0);
		expect(stored_of(kMaxVoices, 0)).to_equal(Pair{127, 0});
		expect(stored_of(kMaxVoices, 16)).to_equal(Pair{16, 0});
		expect(from_menu(kMaxVoices, 17).has_value()).to_be_false();
	});

	it("stores a sidechain attack or release as its rate table's entry, shifted", _ {
		expect(stored_of(kSidechainAttack, 0)).to_equal(Pair{1048576, 0});
		expect(stored_of(kSidechainAttack, 50)).to_equal(Pair{256, 0});
		expect(stored_of(kSidechainRelease, 0)).to_equal(Pair{261528, 0});
		expect(stored_of(kSidechainRelease, 50)).to_equal(Pair{256, 0});
		expect(from_menu(kSidechainAttack, 51).has_value()).to_be_false();
		expect(from_menu(kSidechainRelease, -1).has_value()).to_be_false();
	});
	it("shows a stored rate as the closest table entry, the first on a tie", _ {
		for (int32_t menu = 0; menu <= 50; ++menu) {
			expect(to_menu(kSidechainAttack, stored_of(kSidechainAttack, menu).first)).to_equal(menu);
			expect(to_menu(kSidechainRelease, stored_of(kSidechainRelease, menu).first)).to_equal(menu);
		}
		expect(to_menu(kSidechainAttack, 0)).to_equal(50);
		expect(to_menu(kSidechainAttack, 2147483647)).to_equal(0);
		expect(to_menu(kSidechainRelease, 0)).to_equal(50);
		expect(to_menu(kSidechainRelease, 264)).to_equal(49); // 33: 34 and 32 tie; legacy keeps the first
	});

	it("applies a row with no predicate everywhere, and a predicate by its context", _ {
		using deluge::engine::setting_applies;
		using deluge::engine::setting_choice_applies;
		expect(kPlain.extras == nullptr).to_be_true();
		expect(setting_applies(kPlain, SettingContext{})).to_be_true();
		expect(setting_choice_applies(kPlain, SettingContext{}, 0)).to_be_true();
		expect(setting_applies(kKitLaneOnly, SettingContext{.owner = SettingContext::Owner::KIT_LANE})).to_be_true();
		expect(setting_applies(kKitLaneOnly, SettingContext{})).to_be_false();
		expect(setting_choice_applies(kKitLaneOnly, SettingContext{}, 4)).to_be_false();
		expect(setting_choice_applies(kKitLaneOnly, SettingContext{}, 0)).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(display_mapping)
