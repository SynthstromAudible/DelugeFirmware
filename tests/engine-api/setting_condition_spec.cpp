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

/// A setting's applicability as data: conditions over the owner's other settings, evaluated generically.
#include "engine-api/types/setting.h"

#include <array>
#include <cppspec.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using deluge::engine::ChoiceCondition;
using deluge::engine::SettingCondition;
using deluge::engine::SettingContext;
using deluge::engine::SettingDescriptorExtras;
using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::engine::SettingReader;
using Subject = SettingCondition::Subject;
using Test = SettingCondition::Test;
using Owner = SettingContext::Owner;

// CppSpec defines a `subject` macro, so a condition is built positionally rather than by designated initialisers.
constexpr SettingCondition setting_is(std::string_view path, std::span<const int32_t> values) {
	return {Subject::SETTING, Test::IS, path, values};
}
constexpr SettingCondition setting_is_not(std::string_view path, std::span<const int32_t> values) {
	return {Subject::SETTING, Test::IS_NOT, path, values};
}
constexpr SettingCondition owner_is(std::span<const int32_t> values) {
	return {Subject::OWNER, Test::IS, {}, values};
}
constexpr SettingCondition owner_is_not(std::span<const int32_t> values) {
	return {Subject::OWNER, Test::IS_NOT, {}, values};
}
constexpr SettingCondition inputs_are(std::span<const int32_t> values) {
	return {Subject::INPUTS_PRESENT, Test::IS, {}, values};
}

/// An owner holding named values; a path it does not hold is recorded.
struct Held {
	std::vector<std::pair<std::string_view, int32_t>> values;
	mutable std::vector<std::string> unknown{};

	[[nodiscard]] SettingReader reader() const {
		return {.read =
		            [](const void* self, std::string_view path) {
			            const auto& held = *static_cast<const Held*>(self);
			            for (const auto& [name, value] : held.values) {
				            if (name == path) {
					            return value;
				            }
			            }
			            held.unknown.emplace_back(path);
			            return 0;
		            },
		        .self = this};
	}
};

constexpr std::array<std::string_view, 12> kTypes{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l"};

constexpr std::array<int32_t, 1> kRing{2};
constexpr std::array<int32_t, 2> kFmOrRing{1, 2};
constexpr std::array<int32_t, 1> kKitLane{static_cast<int32_t>(Owner::KIT_LANE)};
constexpr std::array<int32_t, 2> kTrackOrClip{static_cast<int32_t>(Owner::TRACK), static_cast<int32_t>(Owner::CLIP)};
constexpr std::array<int32_t, 1> kYes{1};
constexpr std::array<int32_t, 1> kNo{0};

constexpr std::array kNotRing{setting_is_not("mode", kRing)};
constexpr std::array kNotKitLaneAndNotRing{
    setting_is_not("mode", kRing),
    owner_is_not(kKitLane),
};
constexpr std::array kWithInputs{
    setting_is_not("mode", kRing),
    inputs_are(kYes),
};
constexpr std::array kNever{owner_is({})};

/// Types: 0-6 always; 7 not under ring mod; 8 (DX7-like) off a kit lane, under no ring mod; 9 not under ring mod;
/// 10 and 11 not under ring mod, with inputs plugged in.
constexpr std::array kTypeWhen{
    ChoiceCondition{7, kNotRing},     ChoiceCondition{8, kNotKitLaneAndNotRing}, ChoiceCondition{9, kNotRing},
    ChoiceCondition{10, kWithInputs}, ChoiceCondition{11, kWithInputs},
};
constexpr SettingDescriptorExtras kTypeExtras{.choice_when = kTypeWhen};
/// The same menu on a row that never offers choice 8.
constexpr std::array kTypeWhenNoEight{
    ChoiceCondition{7, kNotRing},     ChoiceCondition{8, kNever},       ChoiceCondition{9, kNotRing},
    ChoiceCondition{10, kWithInputs}, ChoiceCondition{11, kWithInputs},
};
constexpr SettingDescriptorExtras kTypeExtrasNoEight{.choice_when = kTypeWhenNoEight};
constexpr SettingInfo kType{.path = "type", .kind = SettingKind::CHOICE, .choices = kTypes, .extras = &kTypeExtras};
constexpr SettingInfo kTypeNoEight{
    .path = "type2", .kind = SettingKind::CHOICE, .choices = kTypes, .extras = &kTypeExtrasNoEight};

constexpr SettingDescriptorExtras kRowIs{.applies_when = kNotRing};
constexpr SettingInfo kGatedRow{.path = "gated", .extras = &kRowIs};
constexpr std::array kInSet{setting_is("mode", kFmOrRing)};
constexpr SettingDescriptorExtras kRowSet{.applies_when = kInSet};

constexpr std::array kMissing{setting_is("moed", kRing)};
constexpr SettingDescriptorExtras kMisspeltExtras{.applies_when = kMissing};
constexpr std::array kGoodRows{
    SettingInfo{.path = "mode", .kind = SettingKind::CHOICE, .choices = kTypes},
    kGatedRow,
};
constexpr std::array kBadRows{
    SettingInfo{.path = "mode", .kind = SettingKind::CHOICE, .choices = kTypes},
    SettingInfo{.path = "gated", .extras = &kMisspeltExtras},
};
/// A row gated on "mode" in a table that holds no "mode".
constexpr std::array kUnheldRows{
    SettingInfo{.path = "lone", .extras = &kRowIs},
};
static_assert(deluge::engine::conditions_name_rows(kGoodRows));

std::vector<int32_t> offered(const SettingInfo& row, const SettingContext& shown, const SettingReader& reader) {
	std::vector<int32_t> out;
	const int32_t count = deluge::engine::applicable_choice_count(row, shown, reader);
	for (int32_t position = 0; position < count; ++position) {
		out.push_back(deluge::engine::choice_at_position(row, shown, reader, position).value_or(-1));
	}
	return out;
}
} // namespace

// clang-format off
describe setting_condition("A setting's applicability as data", $ {
	it("applies a row with no conditions everywhere", _ {
		const Held none{};
		const SettingInfo plain{.path = "plain"};
		expect(deluge::engine::setting_applies(plain, SettingContext{}, none.reader())).to_be_true();
		expect(deluge::engine::setting_choice_applies(plain, SettingContext{}, none.reader(), 3)).to_be_true();
		expect(none.unknown.empty()).to_be_true();
	});

	it("tests a sibling setting's stored value with IS_NOT", _ {
		const Held ring{{{"mode", 2}}};
		const Held fm{{{"mode", 1}}};
		expect(deluge::engine::setting_applies(kGatedRow, SettingContext{}, ring.reader())).to_be_false();
		expect(deluge::engine::setting_applies(kGatedRow, SettingContext{}, fm.reader())).to_be_true();
	});

	it("tests membership in a set of values with IS", _ {
		const SettingInfo row{.path = "set", .extras = &kRowSet};
		for (const auto& [mode, want] : {std::pair{0, false}, std::pair{1, true}, std::pair{2, true}, std::pair{3, false}}) {
			const Held held{{{"mode", mode}}};
			expect(deluge::engine::setting_applies(row, SettingContext{}, held.reader())).to_equal(want);
		}
	});

	it("requires every condition of a list to hold", _ {
		const Held fm{{{"mode", 1}}};
		const Held ring{{{"mode", 2}}};
		const SettingContext lane{.owner = Owner::KIT_LANE};
		const SettingContext track{.owner = Owner::TRACK};
		expect(deluge::engine::setting_choice_applies(kType, track, fm.reader(), 8)).to_be_true();
		expect(deluge::engine::setting_choice_applies(kType, lane, fm.reader(), 8)).to_be_false();
		expect(deluge::engine::setting_choice_applies(kType, track, ring.reader(), 8)).to_be_false();
		expect(deluge::engine::setting_choice_applies(kType, lane, ring.reader(), 8)).to_be_false();
	});

	it("tests the owner against a set of owners", _ {
		const std::array only{owner_is(kTrackOrClip)};
		const SettingDescriptorExtras extras{.applies_when = only};
		const SettingInfo row{.path = "owned", .extras = &extras};
		const Held none{};
		expect(deluge::engine::setting_applies(row, {.owner = Owner::TRACK}, none.reader())).to_be_true();
		expect(deluge::engine::setting_applies(row, {.owner = Owner::CLIP}, none.reader())).to_be_true();
		expect(deluge::engine::setting_applies(row, {.owner = Owner::KIT_LANE}, none.reader())).to_be_false();
		expect(deluge::engine::setting_applies(row, {.owner = Owner::SONG}, none.reader())).to_be_false();
		expect(none.unknown.empty()).to_be_true();
	});

	it("tests whether audio inputs are present", _ {
		const std::array present{inputs_are(kYes)};
		const std::array absent{inputs_are(kNo)};
		const SettingDescriptorExtras with{.applies_when = present};
		const SettingDescriptorExtras without{.applies_when = absent};
		const SettingInfo needs{.path = "needs", .extras = &with};
		const SettingInfo lacks{.path = "lacks", .extras = &without};
		const Held none{};
		expect(deluge::engine::setting_applies(needs, {.audio_inputs_present = true}, none.reader())).to_be_true();
		expect(deluge::engine::setting_applies(needs, {.audio_inputs_present = false}, none.reader())).to_be_false();
		expect(deluge::engine::setting_applies(lacks, {.audio_inputs_present = true}, none.reader())).to_be_false();
		expect(deluge::engine::setting_applies(lacks, {.audio_inputs_present = false}, none.reader())).to_be_true();
	});

	it("leaves a choice with no entry applying everywhere", _ {
		const Held ring{{{"mode", 2}}};
		for (std::size_t choice = 0; choice < 7; ++choice) {
			expect(deluge::engine::setting_choice_applies(kType, {.owner = Owner::KIT_LANE}, ring.reader(), choice)).to_be_true();
		}
	});

	it("lists a filtered choice row's applicable choices in order", _ {
		const Held fm{{{"mode", 1}}};
		const Held ring{{{"mode", 2}}};
		expect(offered(kType, {.owner = Owner::TRACK}, fm.reader()))
		    .to_equal(std::vector<int32_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
		expect(offered(kType, {.owner = Owner::TRACK, .audio_inputs_present = true}, fm.reader()))
		    .to_equal(std::vector<int32_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
		expect(offered(kType, {.owner = Owner::KIT_LANE}, fm.reader()))
		    .to_equal(std::vector<int32_t>{0, 1, 2, 3, 4, 5, 6, 7, 9});
		expect(offered(kType, {.owner = Owner::TRACK, .audio_inputs_present = true}, ring.reader()))
		    .to_equal(std::vector<int32_t>{0, 1, 2, 3, 4, 5, 6});
	});

	it("maps a choice to its position among the applicable ones and back", _ {
		const Held fm{{{"mode", 1}}};
		const SettingContext track{.owner = Owner::TRACK};
		// A row that never offers choice 8: choice 9 is position 8
		expect(deluge::engine::choice_position(kTypeNoEight, track, fm.reader(), 9).value_or(-1)).to_equal(8);
		expect(deluge::engine::choice_at_position(kTypeNoEight, track, fm.reader(), 8).value_or(-1)).to_equal(9);
		expect(deluge::engine::choice_position(kTypeNoEight, track, fm.reader(), 8)).not_().to_have_value();
		expect(deluge::engine::choice_at_position(kTypeNoEight, track, fm.reader(), 9)).not_().to_have_value();
		expect(deluge::engine::choice_at_position(kTypeNoEight, track, fm.reader(), -1)).not_().to_have_value();
		expect(deluge::engine::choice_position(kTypeNoEight, track, fm.reader(), 12)).not_().to_have_value();
		expect(deluge::engine::choice_position(kTypeNoEight, track, fm.reader(), -1)).not_().to_have_value();
		expect(deluge::engine::applicable_choice_count(kTypeNoEight, track, fm.reader())).to_equal(9);
		// Every position round-trips
		for (int32_t position = 0; position < 9; ++position) {
			const std::optional<int32_t> stored = deluge::engine::choice_at_position(kTypeNoEight, track, fm.reader(), position);
			expect(stored).to_have_value();
			expect(deluge::engine::choice_position(kTypeNoEight, track, fm.reader(), *stored).value_or(-1)).to_equal(position);
		}
	});

	it("accepts a table whose conditions name its rows, and refuses a misspelt path", _ {
		expect(deluge::engine::conditions_name_rows(kGoodRows)).to_be_true();
		expect(deluge::engine::conditions_name_rows(kBadRows)).to_be_false();
		expect(deluge::engine::conditions_name_rows(kUnheldRows)).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(setting_condition)
