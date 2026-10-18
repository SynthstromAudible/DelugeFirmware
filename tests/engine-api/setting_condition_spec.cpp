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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {
using deluge::engine::ChoiceCondition;
using deluge::engine::inputs_are;
using deluge::engine::instrument_is;
using deluge::engine::instrument_is_not;
using deluge::engine::never;
using deluge::engine::owner_is;
using deluge::engine::owner_is_not;
using deluge::engine::setting_at_least;
using deluge::engine::setting_is;
using deluge::engine::setting_is_not;
using deluge::engine::SettingCondition;
using deluge::engine::SettingContext;
using deluge::engine::SettingDescriptorExtras;
using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::engine::SettingReader;
using deluge::engine::source_loaded;
using Instrument = SettingContext::Instrument;
using Owner = SettingContext::Owner;

/// An owner holding named values; a path it does not hold is recorded.
struct Held {
	std::vector<std::pair<std::string_view, int32_t>> values;
	mutable std::vector<std::string> unknown{};

	[[nodiscard]] SettingReader reader() const {
		return {.read = [](const void* self, std::string_view path) -> std::optional<int32_t> {
			        const auto& held = *static_cast<const Held*>(self);
			        for (const auto& [name, value] : held.values) {
				        if (name == path) {
					        return value;
				        }
			        }
			        held.unknown.emplace_back(path);
			        return std::nullopt;
		        },
		        .self = this};
	}
};

constexpr std::array<std::string_view, 12> kTypes{"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l"};

constexpr std::array<int32_t, 1> kRing{2};
constexpr std::array<int32_t, 2> kFmOrRing{1, 2};
constexpr std::array<int32_t, 1> kKitLane{static_cast<int32_t>(Owner::KIT_LANE)};
constexpr std::array<int32_t, 2> kTrackOrClip{static_cast<int32_t>(Owner::TRACK), static_cast<int32_t>(Owner::CLIP)};

constexpr std::array kNotRing{setting_is_not("mode", kRing)};
constexpr std::array kNotKitLaneAndNotRing{
    setting_is_not("mode", kRing),
    owner_is_not(kKitLane),
};
constexpr std::array kWithInputs{
    setting_is_not("mode", kRing),
    inputs_are(true),
};
constexpr std::array kNever{never()};

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

// Each bad table breaks one rule of conditions_are_valid, against a "mode" CHOICE row of twelve choices and a "name"
// TEXT row.
constexpr SettingInfo kMode{.path = "mode", .kind = SettingKind::CHOICE, .choices = kTypes};
constexpr SettingInfo kName{.path = "name", .kind = SettingKind::TEXT};
constexpr std::array<int32_t, 1> kPastChoices{12};
constexpr std::array<int32_t, 1> kNotAnOwner{5};
constexpr std::array<int32_t, 1> kNotABool{2};
constexpr std::array<int32_t, 1> kSomeValue{1};

constexpr std::array kOutOfRange{setting_is("mode", kPastChoices)};
constexpr std::array kOnText{setting_is("name", kSomeValue)};
constexpr std::array kBadOwner{owner_is(kNotAnOwner)};
constexpr std::array kBadInputs{
    SettingCondition{SettingCondition::Subject::INPUTS_PRESENT, SettingCondition::Test::IS, {}, kNotABool}};
constexpr std::array kNeverWithValues{
    SettingCondition{SettingCondition::Subject::NEVER, SettingCondition::Test::IS, {}, kSomeValue}};
constexpr std::array kNeverWithPath{
    SettingCondition{SettingCondition::Subject::NEVER, SettingCondition::Test::IS, "mode", {}}};
constexpr std::array<int32_t, 1> kNotAnInstrument{9};
constexpr std::array<int32_t, 1> kPastTheMasks{4};
constexpr std::array kBadInstrument{instrument_is(kNotAnInstrument)};
constexpr std::array kBadMask{
    SettingCondition{SettingCondition::Subject::LOADED_SOURCES, SettingCondition::Test::IS, {}, kPastTheMasks}};
// An empty IS holds for no one: NEVER says that, so an empty list is refused on every other subject.
constexpr std::array kEmptyOwners{owner_is({})};
constexpr std::array kEmptySetting{setting_is("mode", {})};
constexpr std::array kEmptyInstruments{instrument_is_not({})};

// Alternatives and AT_LEAST, against "mode" and an INTEGER "level" of -5..100.
constexpr SettingInfo kLevel{.path = "level", .min = -5, .max = 100};
constexpr std::array<int32_t, 1> kZero{0};
constexpr std::array<int32_t, 2> kZeroAndOne{0, 1};
constexpr std::array<int32_t, 1> kPastLevel{101};
constexpr std::array<int32_t, 1> kModeOne{1};
constexpr std::array<int32_t, 1> kModeThree{3};
constexpr std::array kLevelAudible{setting_at_least("level", kZero)};
constexpr std::array kModeIsOne{setting_is("mode", kModeOne)};
constexpr std::array kModeIsThree{setting_is("mode", kModeThree)};
constexpr std::array<std::span<const SettingCondition>, 2> kOneOrThree{kModeIsOne, kModeIsThree};
constexpr SettingDescriptorExtras kAnyExtras{.applies_when = kLevelAudible, .applies_any = kOneOrThree};
constexpr SettingInfo kAnyRow{.path = "any", .extras = &kAnyExtras};
constexpr std::array kAnyRows{kMode, kLevel, kAnyRow};
static_assert(deluge::engine::conditions_are_valid(kAnyRows));

constexpr std::array<std::span<const SettingCondition>, 2> kWithEmpty{kModeIsOne, std::span<const SettingCondition>{}};
constexpr SettingDescriptorExtras kEmptyAlternativeExtras{.applies_any = kWithEmpty};
constexpr std::array kEmptyAlternativeRows{kMode, SettingInfo{.path = "x", .extras = &kEmptyAlternativeExtras}};
constexpr std::array<std::span<const SettingCondition>, 1> kMisspeltAlternative{kMissing};
constexpr SettingDescriptorExtras kMisspeltAlternativeExtras{.applies_any = kMisspeltAlternative};
constexpr std::array kMisspeltAlternativeRows{kMode, SettingInfo{.path = "x", .extras = &kMisspeltAlternativeExtras}};

constexpr std::array kAtLeastTwo{setting_at_least("level", kZeroAndOne)};
constexpr std::array kAtLeastPast{setting_at_least("level", kPastLevel)};
constexpr std::array kAtLeastChoice{setting_at_least("mode", kZero)};
constexpr std::array kAtLeastOwner{
    SettingCondition{SettingCondition::Subject::OWNER, SettingCondition::Test::AT_LEAST, {}, kZero}};
constexpr SettingDescriptorExtras kAtLeastTwoExtras{.applies_when = kAtLeastTwo};
constexpr SettingDescriptorExtras kAtLeastPastExtras{.applies_when = kAtLeastPast};
constexpr SettingDescriptorExtras kAtLeastChoiceExtras{.applies_when = kAtLeastChoice};
constexpr SettingDescriptorExtras kAtLeastOwnerExtras{.applies_when = kAtLeastOwner};
constexpr std::array kAtLeastTwoRows{kMode, kLevel, SettingInfo{.path = "x", .extras = &kAtLeastTwoExtras}};
constexpr std::array kAtLeastPastRows{kMode, kLevel, SettingInfo{.path = "x", .extras = &kAtLeastPastExtras}};
constexpr std::array kAtLeastChoiceRows{kMode, kLevel, SettingInfo{.path = "x", .extras = &kAtLeastChoiceExtras}};
constexpr std::array kAtLeastOwnerRows{kMode, kLevel, SettingInfo{.path = "x", .extras = &kAtLeastOwnerExtras}};
static_assert(!deluge::engine::conditions_are_valid(kEmptyAlternativeRows));    // an empty alternative
static_assert(!deluge::engine::conditions_are_valid(kMisspeltAlternativeRows)); // a misspelt path in an alternative
static_assert(!deluge::engine::conditions_are_valid(kAtLeastTwoRows));          // AT_LEAST takes one value
static_assert(!deluge::engine::conditions_are_valid(kAtLeastPastRows));         // a value the row cannot store
static_assert(!deluge::engine::conditions_are_valid(kAtLeastChoiceRows));       // AT_LEAST on a CHOICE row
static_assert(!deluge::engine::conditions_are_valid(kAtLeastOwnerRows));        // AT_LEAST off a SETTING

constexpr SettingDescriptorExtras kOutOfRangeExtras{.applies_when = kOutOfRange};
constexpr SettingDescriptorExtras kOnTextExtras{.applies_when = kOnText};
constexpr SettingDescriptorExtras kBadOwnerExtras{.applies_when = kBadOwner};
constexpr SettingDescriptorExtras kBadInputsExtras{.applies_when = kBadInputs};
constexpr SettingDescriptorExtras kNeverWithValuesExtras{.applies_when = kNeverWithValues};
constexpr SettingDescriptorExtras kNeverWithPathExtras{.applies_when = kNeverWithPath};
constexpr SettingDescriptorExtras kBadInstrumentExtras{.applies_when = kBadInstrument};
constexpr SettingDescriptorExtras kBadMaskExtras{.applies_when = kBadMask};
constexpr SettingDescriptorExtras kEmptyOwnersExtras{.applies_when = kEmptyOwners};
constexpr SettingDescriptorExtras kEmptySettingExtras{.applies_when = kEmptySetting};
constexpr SettingDescriptorExtras kEmptyInstrumentsExtras{.applies_when = kEmptyInstruments};

constexpr std::array kPastEnd{ChoiceCondition{12, kNotRing}};
constexpr std::array kTwice{ChoiceCondition{7, kNotRing}, ChoiceCondition{7, kNever}};
constexpr SettingDescriptorExtras kPastEndExtras{.choice_when = kPastEnd};
constexpr SettingDescriptorExtras kTwiceExtras{.choice_when = kTwice};

constexpr std::array kOutOfRangeRows{kMode, SettingInfo{.path = "x", .extras = &kOutOfRangeExtras}};
constexpr std::array kOnTextRows{kMode, kName, SettingInfo{.path = "x", .extras = &kOnTextExtras}};
constexpr std::array kBadOwnerRows{SettingInfo{.path = "x", .extras = &kBadOwnerExtras}};
constexpr std::array kBadInputsRows{SettingInfo{.path = "x", .extras = &kBadInputsExtras}};
constexpr std::array kNeverWithValuesRows{SettingInfo{.path = "x", .extras = &kNeverWithValuesExtras}};
constexpr std::array kNeverWithPathRows{kMode, SettingInfo{.path = "x", .extras = &kNeverWithPathExtras}};
constexpr std::array kBadInstrumentRows{SettingInfo{.path = "x", .extras = &kBadInstrumentExtras}};
constexpr std::array kBadMaskRows{SettingInfo{.path = "x", .extras = &kBadMaskExtras}};
constexpr std::array kEmptyOwnersRows{SettingInfo{.path = "x", .extras = &kEmptyOwnersExtras}};
constexpr std::array kEmptySettingRows{kMode, SettingInfo{.path = "x", .extras = &kEmptySettingExtras}};
constexpr std::array kEmptyInstrumentsRows{SettingInfo{.path = "x", .extras = &kEmptyInstrumentsExtras}};
constexpr std::array kPastEndRows{
    kMode, SettingInfo{.path = "type", .kind = SettingKind::CHOICE, .choices = kTypes, .extras = &kPastEndExtras}};
constexpr std::array kTwiceRows{
    kMode, SettingInfo{.path = "type", .kind = SettingKind::CHOICE, .choices = kTypes, .extras = &kTwiceExtras}};
constexpr std::array kChoicesOnPlainRow{SettingInfo{.path = "plain", .extras = &kPastEndExtras}};
constexpr std::array kGoodChoiceRows{
    kMode, SettingInfo{.path = "type", .kind = SettingKind::CHOICE, .choices = kTypes, .extras = &kTypeExtras}};

static_assert(deluge::engine::conditions_are_valid(kGoodRows));
static_assert(deluge::engine::conditions_are_valid(kGoodChoiceRows));
static_assert(!deluge::engine::conditions_are_valid(kBadRows));        // a misspelt path
static_assert(!deluge::engine::conditions_are_valid(kUnheldRows));     // a path the table does not hold
static_assert(!deluge::engine::conditions_are_valid(kOutOfRangeRows)); // a value past the CHOICE row's choices
static_assert(!deluge::engine::conditions_are_valid(kOnTextRows));     // a TEXT target
static_assert(!deluge::engine::conditions_are_valid(kBadOwnerRows));   // not an Owner
static_assert(!deluge::engine::conditions_are_valid(kBadInputsRows));  // not 0 or 1
static_assert(!deluge::engine::conditions_are_valid(kNeverWithValuesRows));
static_assert(!deluge::engine::conditions_are_valid(kNeverWithPathRows));
static_assert(!deluge::engine::conditions_are_valid(kBadInstrumentRows)); // not an Instrument
static_assert(!deluge::engine::conditions_are_valid(kBadMaskRows));       // a bit past the sound's sources
static_assert(!deluge::engine::conditions_are_valid(kEmptyOwnersRows));   // owner_is({}): NEVER's to say
static_assert(!deluge::engine::conditions_are_valid(kEmptySettingRows));
static_assert(!deluge::engine::conditions_are_valid(kEmptyInstrumentsRows));
static_assert(!deluge::engine::conditions_are_valid(kPastEndRows));       // a choice past the row's choices
static_assert(!deluge::engine::conditions_are_valid(kTwiceRows));         // a choice listed twice
static_assert(!deluge::engine::conditions_are_valid(kChoicesOnPlainRow)); // choice conditions on a row with no choices

// NEVER and INPUTS_PRESENT are spelled with IS alone, as never() and inputs_are() build them.
constexpr std::array kNeverIsNot{SettingCondition{SettingCondition::Subject::NEVER, SettingCondition::Test::IS_NOT}};
constexpr std::array kInputsIsNot{
    SettingCondition{SettingCondition::Subject::INPUTS_PRESENT, SettingCondition::Test::IS_NOT, {}, kSomeValue}};
constexpr SettingDescriptorExtras kNeverIsNotExtras{.applies_when = kNeverIsNot};
constexpr SettingDescriptorExtras kInputsIsNotExtras{.applies_when = kInputsIsNot};
constexpr std::array kNeverIsNotRows{SettingInfo{.path = "x", .extras = &kNeverIsNotExtras}};
constexpr std::array kInputsIsNotRows{SettingInfo{.path = "x", .extras = &kInputsIsNotExtras}};
static_assert(!deluge::engine::conditions_are_valid(kNeverIsNotRows));
static_assert(!deluge::engine::conditions_are_valid(kInputsIsNotRows));

/// A constexpr reader that holds "mode" at 2 (ring) and nothing else.
constexpr std::optional<int32_t> ring_mode(const void* /*self*/, std::string_view path) {
	return path == "mode" ? std::optional<int32_t>{2} : std::nullopt;
}
constexpr deluge::engine::SettingReader kRingReader{.read = ring_mode};
// A SETTING condition reads its value through a non-null reader at compile time: IS and IS_NOT on the held value, and
// a path the reader does not hold failing closed.
static_assert(deluge::engine::condition_holds(setting_is("mode", kRing), SettingContext{}, kRingReader));
static_assert(!deluge::engine::condition_holds(setting_is_not("mode", kRing), SettingContext{}, kRingReader));
static_assert(!deluge::engine::setting_applies(kGatedRow, SettingContext{}, kRingReader));
static_assert(!deluge::engine::condition_holds(setting_is_not("moed", kRing), SettingContext{}, kRingReader));

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
		const std::array present{inputs_are(true)};
		const std::array absent{inputs_are(false)};
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

	it("tests what plays the owner's notes against a set of instruments", _ {
		constexpr std::array<int32_t, 4> kNonSound{
		    static_cast<int32_t>(Instrument::MIDI), static_cast<int32_t>(Instrument::CV),
		    static_cast<int32_t>(Instrument::MIDI_DRUM), static_cast<int32_t>(Instrument::GATE_DRUM)};
		constexpr std::array<int32_t, 1> kGate{static_cast<int32_t>(Instrument::GATE_DRUM)};
		const std::array conditions{instrument_is(kNonSound), instrument_is_not(kGate)};
		const SettingDescriptorExtras extras{.applies_when = conditions};
		const SettingInfo row{.path = "played", .extras = &extras};
		const Held none{};
		const auto applies = [&](Instrument instrument) {
			return deluge::engine::setting_applies(row, {.instrument = instrument}, none.reader());
		};
		expect(applies(Instrument::MIDI)).to_equal(true);
		expect(applies(Instrument::CV)).to_equal(true);
		expect(applies(Instrument::MIDI_DRUM)).to_equal(true);
		expect(applies(Instrument::GATE_DRUM)).to_equal(false);
		expect(applies(Instrument::SYNTH)).to_equal(false);
		expect(applies(Instrument::SOUND_DRUM)).to_equal(false);
		expect(applies(Instrument::NONE)).to_equal(false);
		expect(none.unknown.empty()).to_equal(true);
	});

	it("tests whether a sound source holds a loaded file", _ {
		const std::array first{source_loaded(0)};
		const std::array second{source_loaded(1)};
		const SettingDescriptorExtras first_extras{.applies_when = first};
		const SettingDescriptorExtras second_extras{.applies_when = second};
		const SettingInfo one{.path = "one", .extras = &first_extras};
		const SettingInfo two{.path = "two", .extras = &second_extras};
		const Held none{};
		for (const auto& [mask, want_one, want_two] :
		     {std::tuple{0, false, false}, std::tuple{1, true, false}, std::tuple{2, false, true}, std::tuple{3, true, true}}) {
			const SettingContext shown{.loaded_sources = static_cast<uint8_t>(mask)};
			expect(deluge::engine::setting_applies(one, shown, none.reader())).to_equal(want_one);
			expect(deluge::engine::setting_applies(two, shown, none.reader())).to_equal(want_two);
		}
	});

	it("applies a row with alternatives where its conditions hold and one alternative's all do", _ {
		for (const auto& [mode, level, want] : {std::tuple{1, 0, true}, std::tuple{3, 50, true}, std::tuple{2, 50, false},
		                                        std::tuple{1, -1, false}, std::tuple{3, -5, false}, std::tuple{0, 100, false}}) {
			const Held held{{{"mode", mode}, {"level", level}}};
			expect(std::to_string(mode) + "/" + std::to_string(level) + " " +
			       (deluge::engine::setting_applies(kAnyRow, SettingContext{}, held.reader()) ? "applies" : "does not"))
			    .to_equal(std::to_string(mode) + "/" + std::to_string(level) + " " + (want ? "applies" : "does not"));
		}
		const Held no_level{{{"mode", 1}}};
		expect(deluge::engine::setting_applies(kAnyRow, SettingContext{}, no_level.reader())).to_equal(false);
		static_assert(!deluge::engine::setting_applies(kAnyRow, SettingContext{}, SettingReader{}));
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

	it("accepts a well-formed table, and refuses a misspelt path, a bad value or a bad choice (also static_asserted)", _ {
		expect(deluge::engine::conditions_are_valid(kGoodRows)).to_be_true();
		expect(deluge::engine::conditions_are_valid(kBadRows)).to_be_false();
		expect(deluge::engine::conditions_are_valid(kOutOfRangeRows)).to_be_false();
		expect(deluge::engine::conditions_are_valid(kPastEndRows)).to_be_false();
	});

	it("never holds a NEVER condition, whatever the context", _ {
		const Held fm{{{"mode", 1}}};
		const SettingDescriptorExtras extras{.applies_when = kNever};
		const SettingInfo row{.path = "never", .extras = &extras};
		expect(deluge::engine::setting_applies(row, {.owner = Owner::TRACK, .audio_inputs_present = true}, fm.reader())).to_be_false();
		expect(deluge::engine::setting_choice_applies(kTypeNoEight, {.owner = Owner::TRACK}, fm.reader(), 8)).to_be_false();
		expect(fm.unknown.empty()).to_be_true();
	});

	it("fails a SETTING condition closed where the reader has no value, IS_NOT included", _ {
		const Held empty{};
		const SettingReader unset{};
		expect(deluge::engine::setting_applies(kGatedRow, SettingContext{}, empty.reader())).to_be_false();
		expect(deluge::engine::setting_applies(kGatedRow, SettingContext{}, unset)).to_be_false();
		expect(deluge::engine::setting_choice_applies(kType, SettingContext{}, unset, 7)).to_be_false();
		// a choice with no SETTING condition, and a context-only condition, still evaluate
		expect(deluge::engine::setting_choice_applies(kType, SettingContext{}, unset, 0)).to_be_true();
		const SettingInfo plain{.path = "plain"};
		expect(deluge::engine::setting_applies(plain, SettingContext{}, unset)).to_be_true();
	});

	it("calls a context coherent when its owner and instrument pair as the host contract lists", _ {
		using deluge::engine::context_is_coherent;
		const auto coherent = [](Owner owner, Instrument instrument) {
			return context_is_coherent({.owner = owner, .instrument = instrument});
		};
		expect(coherent(Owner::SONG, Instrument::NONE)).to_be_true();
		expect(coherent(Owner::TRACK, Instrument::SYNTH)).to_be_true();
		expect(coherent(Owner::TRACK, Instrument::AUDIO)).to_be_true();
		expect(coherent(Owner::CLIP, Instrument::KIT)).to_be_true();
		expect(coherent(Owner::KIT, Instrument::KIT)).to_be_true();
		expect(coherent(Owner::KIT_LANE, Instrument::SOUND_DRUM)).to_be_true();
		expect(coherent(Owner::KIT_LANE, Instrument::GATE_DRUM)).to_be_true();
		// A kit lane with no drum fails open: NONE passes the kit gates as if no kit played.
		expect(coherent(Owner::KIT_LANE, Instrument::NONE)).to_be_false();
		expect(coherent(Owner::TRACK, Instrument::NONE)).to_be_false();
		expect(coherent(Owner::CLIP, Instrument::NONE)).to_be_false();
		expect(coherent(Owner::SONG, Instrument::SYNTH)).to_be_false();
		expect(coherent(Owner::KIT, Instrument::SOUND_DRUM)).to_be_false();
		expect(coherent(Owner::KIT_LANE, Instrument::SYNTH)).to_be_false();
		expect(coherent(Owner::TRACK, Instrument::MIDI_DRUM)).to_be_false();
		// A bare context is the track's, with nothing playing.
		expect(context_is_coherent(SettingContext{})).to_be_false();
	});

	it("evaluates in constant expressions", _ {
		static_assert(deluge::engine::setting_choice_applies(kTypeNoEight, SettingContext{}, SettingReader{}, 0));
		static_assert(!deluge::engine::setting_choice_applies(kTypeNoEight, SettingContext{}, SettingReader{}, 8));
		static_assert(deluge::engine::choice_position(kTypeNoEight, SettingContext{}, SettingReader{}, 3) == 3);
		static_assert(deluge::engine::applicable_choice_count(kTypeNoEight, SettingContext{}, SettingReader{}) == 7);
	});
});
// clang-format on

CPPSPEC_SPEC(setting_condition)
