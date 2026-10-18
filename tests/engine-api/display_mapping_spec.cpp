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
#include "engine-api/types/rate_tables.h"
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
using deluge::engine::SettingDescriptorExtras;
using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::engine::SettingSentinel;
using deluge::engine::to_menu;
using deluge::engine::UserScale;

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

// A sound's MIDI note for a drum: unset is stored 255, outside the row, and the menu shows 60 there.
constexpr SettingDescriptorExtras kNoteForDrumExtras{
    .mapping = {.kind = DisplayMappingKind::CLAMP_HIGH, .clamp_to = 60}};
constexpr SettingInfo kNoteForDrum{
    .path = "midiOutput/noteForDrum",
    .min = 0,
    .max = 127,
    .user = {.user_min = 0, .user_max = 127, .stored_min = 0, .stored_max = 127},
    .extras = &kNoteForDrumExtras,
};

// The sidechain's own rate tables.
using deluge::engine::kSidechainAttackRates;
using deluge::engine::kSidechainReleaseRates;
constexpr SettingDescriptorExtras kSidechainAttackExtras{
    .mapping = {.kind = DisplayMappingKind::TABLE, .table_shift = 2, .table = kSidechainAttackRates}};
constexpr SettingInfo kSidechainAttack{
    .path = "sidechain/attack",
    .min = 0,
    .max = 2147483647,
    .extras = &kSidechainAttackExtras,
};
constexpr SettingDescriptorExtras kSidechainReleaseExtras{
    .mapping = {.kind = DisplayMappingKind::TABLE, .table_shift = 3, .table = kSidechainReleaseRates}};
constexpr SettingInfo kSidechainRelease{
    .path = "sidechain/release",
    .min = 0,
    .max = 2147483647,
    .extras = &kSidechainReleaseExtras,
};

constexpr SettingInfo kPlain{.path = "plain", .min = -5, .max = 5};

constexpr std::array<std::string_view, 5> kPolyphonies{"auto", "poly", "mono", "legato", "choke"};
constexpr SettingInfo kPolyphony{.path = "polyphonic", .kind = SettingKind::CHOICE, .choices = kPolyphonies};
constexpr SettingInfo kToggle{.path = "osc2/oscillatorSync", .kind = SettingKind::BOOL};

constexpr int32_t kQ31Min = -2147483647 - 1;
constexpr int32_t kQ31Max = 2147483647;
constexpr SettingDescriptorExtras kStandardExtras{.mapping = {.kind = DisplayMappingKind::STANDARD}};
constexpr SettingInfo kGate{.path = "arpeggiator/gate", .min = kQ31Min, .max = kQ31Max, .extras = &kStandardExtras};
constexpr SettingDescriptorExtras kUnsignedExtras{.mapping = {.kind = DisplayMappingKind::UNSIGNED}};
constexpr SettingInfo kRhythm{.path = "arpeggiator/rhythm", .min = kQ31Min, .max = kQ31Max, .extras = &kUnsignedExtras};
constexpr SettingDescriptorExtras kPanExtras{.mapping = {.kind = DisplayMappingKind::PAN}};
constexpr SettingInfo kPan{.path = "reverb/pan", .min = kQ31Min, .max = kQ31Max, .extras = &kPanExtras};
constexpr SettingDescriptorExtras kFiftiethsExtras{.mapping = {.kind = DisplayMappingKind::FIFTIETHS}};
constexpr SettingInfo kSend{.path = "sideChainSend", .min = 0, .max = kQ31Max, .extras = &kFiftiethsExtras};
constexpr SettingDescriptorExtras kKnobExtras{
    .mapping = {.kind = DisplayMappingKind::KNOB_POSITION, .clamp_to = 127 << 24}};
constexpr SettingInfo kKnob{.path = "audioCompressor/attack", .min = 0, .max = kQ31Max, .extras = &kKnobExtras};
constexpr SettingDescriptorExtras kBlendExtras{
    .mapping = {.kind = DisplayMappingKind::KNOB_POSITION, .clamp_to = kQ31Max}};
constexpr UserScale kFiftieths{.user_min = 0, .user_max = 50, .stored_min = 0, .stored_max = kQ31Max};
constexpr SettingDescriptorExtras kFloatFractionExtras{.mapping = {.kind = DisplayMappingKind::FLOAT_FRACTION}};
constexpr SettingInfo kRoomSize{
    .path = "reverb/roomSize", .min = 0, .max = kQ31Max, .user = kFiftieths, .extras = &kFloatFractionExtras};
constexpr SettingInfo kBlend{.path = "audioCompressor/compBlend", .min = 0, .max = kQ31Max, .extras = &kBlendExtras};

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

	it("shows an unset drum note (stored 255) as 60, and no menu value stores 255", _ {
		expect(to_menu(kNoteForDrum, 255)).to_equal(60);
		for (int32_t note = 0; note <= 127; ++note) {
			expect(to_menu(kNoteForDrum, note)).to_equal(note);
			expect(stored_of(kNoteForDrum, note)).to_equal(Pair{note, 0});
		}
		expect(menu_range(kNoteForDrum)).to_equal(Pair{0, 127});
		for (int32_t menu = -1; menu <= 256; ++menu) {
			const std::optional<Pair> stored = from_menu(kNoteForDrum, menu);
			expect(!stored.has_value() || stored->first != 255).to_be_true();
		}
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

	it("offers each kind's menu range", _ {
		using deluge::engine::menu_range;
		expect(menu_range(kPlain)).to_equal(Pair{-5, 5});
		expect(menu_range(kPolyphony)).to_equal(Pair{0, 4});
		expect(menu_range(kToggle)).to_equal(Pair{0, 1});
		expect(menu_range(kMaxVoices)).to_equal(Pair{0, 16});
		expect(menu_range(kMidiChannel)).to_equal(Pair{0, 16});
		expect(menu_range(kLinearInterpolation)).to_equal(Pair{0, 1});
		expect(menu_range(kSyncLevel)).to_equal(Pair{0, 27});
		expect(menu_range(kTranspose)).to_equal(Pair{-9600, 9600});
		expect(menu_range(kPhase)).to_equal(Pair{-1, 360});
		expect(menu_range(kSidechainAttack)).to_equal(Pair{0, 50});
		expect(menu_range(kGate)).to_equal(Pair{0, 50});
		expect(menu_range(kRhythm)).to_equal(Pair{0, 50});
		expect(menu_range(kSend)).to_equal(Pair{0, 50});
		expect(menu_range(kPan)).to_equal(Pair{-25, 25});
		expect(menu_range(kKnob)).to_equal(Pair{0, 128});
	});
	it("stores a choice's or a bool's menu value as itself, within its choices", _ {
		expect(stored_of(kPolyphony, 3)).to_equal(Pair{3, 0});
		expect(to_menu(kPolyphony, 4)).to_equal(4);
		expect(from_menu(kPolyphony, 5).has_value()).to_be_false();
		expect(stored_of(kToggle, 1)).to_equal(Pair{1, 0});
		expect(from_menu(kToggle, 2).has_value()).to_be_false();
	});

	it("shows a standard menu as computeCurrentValueForStandardMenuItem does, and stores as its final value", _ {
		expect(to_menu(kGate, kQ31Min)).to_equal(0);
		expect(to_menu(kGate, 0)).to_equal(25);
		expect(to_menu(kGate, kQ31Max)).to_equal(50);
		expect(stored_of(kGate, 0)).to_equal(Pair{kQ31Min, 0});
		expect(stored_of(kGate, 1)).to_equal(Pair{-2061584303, 0});
		expect(stored_of(kGate, 25)).to_equal(Pair{-23, 0});
		expect(stored_of(kGate, 50)).to_equal(Pair{kQ31Max, 0});
		expect(from_menu(kGate, 51).has_value()).to_be_false();
		expect(from_menu(kGate, -1).has_value()).to_be_false();
	});
	it("shows an unsigned menu over the 32 bits, and stores steps of 85899345, the top wrapping", _ {
		expect(to_menu(kRhythm, 0)).to_equal(0);
		expect(to_menu(kRhythm, -1)).to_equal(50);
		expect(to_menu(kRhythm, 85899345)).to_equal(1);
		expect(stored_of(kRhythm, 1)).to_equal(Pair{85899345, 0});
		expect(stored_of(kRhythm, 50)).to_equal(Pair{-46, 0});
		expect(from_menu(kRhythm, 51).has_value()).to_be_false();
	});
	it("shows a pan as computeCurrentValueForPan does, and stores as computeFinalValueForPan", _ {
		expect(to_menu(kPan, kQ31Min)).to_equal(-25);
		expect(to_menu(kPan, 0)).to_equal(0);
		expect(to_menu(kPan, kQ31Max)).to_equal(25);
		expect(stored_of(kPan, -25)).to_equal(Pair{kQ31Min, 0});
		expect(stored_of(kPan, -1)).to_equal(Pair{-85899344, 0});
		expect(stored_of(kPan, 1)).to_equal(Pair{85899344, 0});
		expect(stored_of(kPan, 25)).to_equal(Pair{kQ31Max, 0});
		expect(from_menu(kPan, 26).has_value()).to_be_false();
	});
	it("shows the sidechain send in fiftieths rounded to the nearest, as Send::readCurrentValue does", _ {
		expect(to_menu(kSend, 0)).to_equal(0);
		expect(to_menu(kSend, 21474836)).to_equal(0);
		expect(to_menu(kSend, 21474837)).to_equal(1);
		expect(to_menu(kSend, 42949673)).to_equal(1);
		expect(to_menu(kSend, kQ31Max)).to_equal(50);
		expect(stored_of(kSend, 25)).to_equal(Pair{1073741825, 0});
		expect(stored_of(kSend, 49)).to_equal(Pair{2104533977, 0});
		expect(stored_of(kSend, 50)).to_equal(Pair{kQ31Max, 0});
		expect(from_menu(kSend, 51).has_value()).to_be_false();
	});
	it("shows a compressor knob as its top seven bits, and stores the top step as its row says", _ {
		expect(to_menu(kKnob, (5 << 24) + 0xFFFFFF)).to_equal(5);
		expect(to_menu(kKnob, kQ31Max)).to_equal(127);
		expect(stored_of(kKnob, 64)).to_equal(Pair{1073741824, 0});
		expect(stored_of(kKnob, 128)).to_equal(Pair{127 << 24, 0});
		expect(stored_of(kBlend, 128)).to_equal(Pair{kQ31Max, 0});
		expect(from_menu(kKnob, 129).has_value()).to_be_false();
	});

	it("shows a fraction the record holds as a float as RoomSize::readCurrentValue rounds it, half away from zero", _ {
		expect(to_menu(kRoomSize, 0)).to_equal(0);
		expect(to_menu(kRoomSize, 21474836)).to_equal(1);    // 0.01f * 50 is 0.5: rounds up
		expect(to_menu(kRoomSize, 2083059072)).to_equal(49); // 48.5
		expect(to_menu(kRoomSize, 2083058944)).to_equal(48);
		expect(to_menu(kRoomSize, kQ31Max)).to_equal(50);
		expect(stored_of(kRoomSize, 25)).to_equal(Pair{1073741824, 0});
		expect(stored_of(kRoomSize, 50)).to_equal(Pair{kQ31Max, 0});
		expect(deluge::engine::menu_range(kRoomSize)).to_equal(Pair{0, 50});
		expect(from_menu(kRoomSize, 51).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(display_mapping)
