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

#pragma once

#include "engine-api/types/display_mapping.h"
#include "engine-api/types/setting_units.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace deluge::engine {

/// @brief What a setting holds: one of an enum's names, an integer in a range, a bool, free text (a name), or a
/// structured value only its own Edit changes.
enum class SettingKind : uint8_t { CHOICE, INTEGER, BOOL, STRUCTURED, TEXT };

/// @brief A menu value that stands for a stored value outside the linear mapping: max voices' "off" is user 0, stored
/// 127.
struct SettingSentinel {
	int32_t user{0};   ///< the value a menu shows
	int32_t stored{0}; ///< the value the setting stores
};

/// @brief How a menu's value maps onto the stored one: linearly from [user_min, user_max] onto [stored_min,
/// stored_max], except for the sentinels. All zero, with no sentinels, means the menu shows the stored value itself.
struct UserScale {
	int32_t user_min{0};
	int32_t user_max{0};
	int32_t stored_min{0};
	int32_t stored_max{0};
	std::span<const SettingSentinel> sentinels{};

	/// @brief Whether a menu shows the stored value itself, its sentinels aside (no user range of its own).
	[[nodiscard]] constexpr bool identity() const { return user_min == 0 && user_max == 0; }
};

/// @brief Who a setting is shown for: the facts about the owner that no setting of it holds.
///
/// Owner and Instrument overlap but say different things: Owner is the scope a menu edits (one kit lane, the whole
/// kit, a clip, the song), Instrument what plays the notes there. A condition that turns on what plays the notes uses
/// INSTRUMENT, so it holds whichever scope a host passes; one that turns on the scope uses OWNER. Each setting group
/// says which of each its host passes (deluge::project::SettingGroupId).
struct SettingContext {
	/// @brief The menu's editing scope: a track (a synth's sound, a track's own settings), one kit lane, the whole kit
	/// (its own effects), a clip (its values and arp, the whole kit's arp included) or the song.
	enum class Owner : uint8_t { TRACK, KIT_LANE, KIT, CLIP, SONG };
	/// @brief What plays the owner's notes: its track's instrument, or on a kit lane, the lane's drum. NONE for the
	/// song.
	enum class Instrument : uint8_t { NONE, SYNTH, KIT, SOUND_DRUM, MIDI_DRUM, GATE_DRUM, MIDI, CV, AUDIO };
	/// @brief How many Owner values there are: SONG is the last.
	static constexpr int32_t kOwnerCount = static_cast<int32_t>(Owner::SONG) + 1;
	/// @brief How many Instrument values there are: AUDIO is the last.
	static constexpr int32_t kInstrumentCount = static_cast<int32_t>(Instrument::AUDIO) + 1;
	Owner owner{Owner::TRACK};
	Instrument instrument{Instrument::NONE};
	bool audio_inputs_present{false}; ///< any audio input is plugged in
	/// The owner's sound sources that hold a loaded audio file, a bit each (bit 0 the first source); 0 off a sound.
	uint8_t loaded_sources{0};
};

/// @brief The sources a sound has, and so the bits SettingContext::loaded_sources uses.
inline constexpr std::size_t kMaxSoundSources = 2;

/// @brief One fact a setting's menu depends on. A condition holds when its subject's value is one of `values` (IS),
/// none of them (IS_NOT), or at least its one value (AT_LEAST, a SETTING's alone). Build one with setting_is,
/// setting_is_not, setting_at_least, owner_is, owner_is_not, instrument_is, instrument_is_not, inputs_are,
/// source_loaded or never.
struct SettingCondition {
	/// @brief What the condition reads. NEVER reads nothing and holds for no one.
	enum class Subject : uint8_t { SETTING, OWNER, INSTRUMENT, INPUTS_PRESENT, LOADED_SOURCES, NEVER };
	enum class Test : uint8_t { IS, IS_NOT, AT_LEAST };
	Subject subject{Subject::SETTING};
	Test test{Test::IS};
	std::string_view path; ///< SETTING: a row of the same owner (its stored value; a CHOICE row's index)
	/// SETTING: stored values; OWNER: SettingContext::Owner values; INSTRUMENT: SettingContext::Instrument values;
	/// INPUTS_PRESENT: {1} or {0}; LOADED_SOURCES: SettingContext::loaded_sources masks. Never empty but for NEVER.
	std::span<const int32_t> values;
};

/// @brief The values inputs_are tests against.
inline constexpr std::array<int32_t, 1> kInputsPresentValue{1};
inline constexpr std::array<int32_t, 1> kInputsAbsentValue{0};
/// @brief For each source (row i), the loaded_sources masks in which it holds a loaded file: every mask of
/// kMaxSoundSources bits with bit i set.
inline constexpr auto kSourceLoadedMasks = [] {
	constexpr std::size_t kMasks = std::size_t{1} << kMaxSoundSources;
	std::array<std::array<int32_t, kMasks / 2>, kMaxSoundSources> masks{};
	for (std::size_t source = 0; source < kMaxSoundSources; ++source) {
		std::size_t n = 0;
		for (std::size_t mask = 0; mask < kMasks; ++mask) {
			if ((mask & (std::size_t{1} << source)) != 0) {
				masks[source][n++] = static_cast<int32_t>(mask);
			}
		}
	}
	return masks;
}();

/// @brief A condition that holds when the owner's setting @p path stores one of @p values.
[[nodiscard]] constexpr SettingCondition setting_is(std::string_view path, std::span<const int32_t> values) {
	return {SettingCondition::Subject::SETTING, SettingCondition::Test::IS, path, values};
}
/// @brief A condition that holds when the owner's setting @p path stores at least @p least, a span of one value.
[[nodiscard]] constexpr SettingCondition setting_at_least(std::string_view path, std::span<const int32_t> least) {
	return {SettingCondition::Subject::SETTING, SettingCondition::Test::AT_LEAST, path, least};
}
/// @brief A condition that holds when the owner's setting @p path stores none of @p values.
[[nodiscard]] constexpr SettingCondition setting_is_not(std::string_view path, std::span<const int32_t> values) {
	return {SettingCondition::Subject::SETTING, SettingCondition::Test::IS_NOT, path, values};
}
/// @brief A condition that holds when the owner is one of @p values (SettingContext::Owner values).
[[nodiscard]] constexpr SettingCondition owner_is(std::span<const int32_t> values) {
	return {SettingCondition::Subject::OWNER, SettingCondition::Test::IS, {}, values};
}
/// @brief A condition that holds when the owner is none of @p values (SettingContext::Owner values).
[[nodiscard]] constexpr SettingCondition owner_is_not(std::span<const int32_t> values) {
	return {SettingCondition::Subject::OWNER, SettingCondition::Test::IS_NOT, {}, values};
}
/// @brief A condition that holds when what plays the owner's notes is one of @p values (SettingContext::Instrument
/// values).
[[nodiscard]] constexpr SettingCondition instrument_is(std::span<const int32_t> values) {
	return {SettingCondition::Subject::INSTRUMENT, SettingCondition::Test::IS, {}, values};
}
/// @brief A condition that holds when what plays the owner's notes is none of @p values (SettingContext::Instrument
/// values).
[[nodiscard]] constexpr SettingCondition instrument_is_not(std::span<const int32_t> values) {
	return {SettingCondition::Subject::INSTRUMENT, SettingCondition::Test::IS_NOT, {}, values};
}
/// @brief A condition that holds when the owner's sound source @p source (0 the first) holds a loaded audio file.
/// @pre @p source < kMaxSoundSources.
[[nodiscard]] constexpr SettingCondition source_loaded(std::size_t source) {
	return {SettingCondition::Subject::LOADED_SOURCES, SettingCondition::Test::IS, {}, kSourceLoadedMasks[source]};
}
/// @brief A condition that holds when audio inputs are present (@p present true) or absent (false).
[[nodiscard]] constexpr SettingCondition inputs_are(bool present) {
	return {SettingCondition::Subject::INPUTS_PRESENT,
	        SettingCondition::Test::IS,
	        {},
	        present ? std::span<const int32_t>{kInputsPresentValue} : std::span<const int32_t>{kInputsAbsentValue}};
}
/// @brief A condition that holds for no owner, no setting and no context: a choice or row a menu never offers.
[[nodiscard]] constexpr SettingCondition never() {
	return {SettingCondition::Subject::NEVER, SettingCondition::Test::IS, {}, {}};
}

/// @brief The conditions under which one choice of a CHOICE row applies; every condition must hold.
struct ChoiceCondition {
	std::size_t choice;
	std::span<const SettingCondition> when;
};

/// @brief Reads the owner's settings by path, for evaluating SETTING conditions. A function pointer plus its object;
/// host-side only, never part of a descriptor.
/// A value the reader cannot give (a path its owner does not hold, or no `read` at all) makes a SETTING condition fail
/// closed: the condition does not hold, whichever its test.
struct SettingReader {
	/// The stored value at a path (a CHOICE row's index), or nullopt where the owner holds none.
	std::optional<int32_t> (*read)(const void* self, std::string_view path){nullptr};
	const void* self{nullptr}; ///< passed to read
	/// @param path The slash path of a setting.
	/// @return Its stored value, or nullopt where the owner holds none or the reader has no `read`.
	[[nodiscard]] constexpr std::optional<int32_t> operator()(std::string_view path) const {
		return read != nullptr ? read(self, path) : std::nullopt;
	}
};

/// @brief What only a few rows set: a display mapping other than LINEAR, and when the setting or its choices apply.
/// A row points at a static one, so the rows that need none pay one pointer.
struct SettingDescriptorExtras {
	DisplayMapping mapping{}; ///< how a menu's value maps onto what the row (and a partner) stores
	/// The conditions under which the setting applies; all must hold. Empty applies everywhere.
	std::span<const SettingCondition> applies_when{};
	/// Alternatives, one of which must hold besides applies_when: the setting applies where every condition of some
	/// alternative holds. Empty adds nothing.
	std::span<const std::span<const SettingCondition>> applies_any{};
	/// The conditions under which a CHOICE row's choices apply; a choice not listed applies everywhere.
	std::span<const ChoiceCondition> choice_when{};
};

/// @brief One non-param setting of an engine or a model record, named by its slash path in the owner's state
/// element, with what a menu needs to show and edit it.
struct SettingInfo {
	std::string_view path;
	SettingKind kind{SettingKind::INTEGER};
	std::span<const std::string_view> choices; ///< CHOICE: the names a file writes
	int32_t min{0};                            ///< INTEGER: the least stored value
	int32_t max{0};                            ///< INTEGER: the greatest stored value
	UserScale user{};                          ///< INTEGER: how a menu's value maps onto the stored one
	int32_t default_value{0};                  ///< the stored value a new owner starts with
	SettingUnits units{SettingUnits::NONE};    ///< what a menu's value counts
	/// The row's mapping and applicability; nullptr is LINEAR, applying everywhere.
	const SettingDescriptorExtras* extras{nullptr};
};

/// @brief The LINEAR mapping a row without extras has.
inline constexpr DisplayMapping kLinearMapping{};

/// @brief @p row's display mapping.
/// @param row The setting's row.
/// @return Its extras' mapping, or kLinearMapping when it has none.
[[nodiscard]] constexpr const DisplayMapping& mapping_of(const SettingInfo& row) {
	return row.extras != nullptr ? row.extras->mapping : kLinearMapping;
}

/// @brief Whether @p condition holds.
/// @param condition What to test.
/// @param context   Who the setting is shown for.
/// @param reader    The owner's settings, read for a SETTING condition.
/// @return Whether the subject's value is one of the condition's values (IS), none of them (IS_NOT) or at least its
///         value (AT_LEAST). A SETTING the reader gives no value for does not hold; NEVER never holds.
[[nodiscard]] constexpr bool condition_holds(const SettingCondition& condition, const SettingContext& context,
                                             SettingReader reader) {
	int32_t subject = 0;
	switch (condition.subject) {
	case SettingCondition::Subject::SETTING: {
		const std::optional<int32_t> value = reader(condition.path);
		if (!value) {
			return false;
		}
		subject = *value;
		break;
	}
	case SettingCondition::Subject::OWNER:
		subject = static_cast<int32_t>(context.owner);
		break;
	case SettingCondition::Subject::INSTRUMENT:
		subject = static_cast<int32_t>(context.instrument);
		break;
	case SettingCondition::Subject::INPUTS_PRESENT:
		subject = context.audio_inputs_present ? 1 : 0;
		break;
	case SettingCondition::Subject::LOADED_SOURCES:
		subject = context.loaded_sources;
		break;
	case SettingCondition::Subject::NEVER:
		return false;
	}
	if (condition.test == SettingCondition::Test::AT_LEAST) {
		return !condition.values.empty() && subject >= condition.values.front();
	}
	const bool among = std::ranges::find(condition.values, subject) != condition.values.end();
	return among == (condition.test == SettingCondition::Test::IS);
}

/// @brief Whether every condition of @p conditions holds.
/// @param conditions The conditions; none holds vacuously.
/// @param context    Who the setting is shown for.
/// @param reader     The owner's settings.
/// @return Whether all hold.
[[nodiscard]] constexpr bool conditions_hold(std::span<const SettingCondition> conditions,
                                             const SettingContext& context, SettingReader reader) {
	return std::ranges::all_of(
	    conditions, [&](const SettingCondition& condition) { return condition_holds(condition, context, reader); });
}

/// @brief Whether @p row's setting applies in @p context.
/// @param row     The setting's row.
/// @param context Who the setting is shown for.
/// @param reader  The owner's settings.
/// @return Whether its applies_when conditions all hold and, where it lists alternatives (applies_any), every
///         condition of one of them; true when it has neither. A SETTING condition the reader cannot read does not
///         hold.
[[nodiscard]] constexpr bool setting_applies(const SettingInfo& row, const SettingContext& context,
                                             SettingReader reader) {
	if (row.extras == nullptr) {
		return true;
	}
	const auto holds = [&](std::span<const SettingCondition> conditions) {
		return conditions_hold(conditions, context, reader);
	};
	return holds(row.extras->applies_when)
	       && (row.extras->applies_any.empty() || std::ranges::any_of(row.extras->applies_any, holds));
}

/// @brief Whether @p row's choice @p choice applies in @p context.
/// @param row     The setting's row.
/// @param context Who the setting is shown for.
/// @param reader  The owner's settings.
/// @param choice  The choice's index.
/// @return Whether the conditions listed for the choice all hold; true when none are listed.
[[nodiscard]] constexpr bool setting_choice_applies(const SettingInfo& row, const SettingContext& context,
                                                    SettingReader reader, std::size_t choice) {
	if (row.extras == nullptr) {
		return true;
	}
	return std::ranges::all_of(row.extras->choice_when, [&](const ChoiceCondition& entry) {
		return entry.choice != choice || conditions_hold(entry.when, context, reader);
	});
}

/// @brief How many of @p row's choices apply in @p context.
/// @param row     A CHOICE row.
/// @param context Who the setting is shown for.
/// @param reader  The owner's settings.
/// @return The count of applicable choices.
[[nodiscard]] constexpr int32_t applicable_choice_count(const SettingInfo& row, const SettingContext& context,
                                                        SettingReader reader) {
	int32_t count = 0;
	for (std::size_t i = 0; i < row.choices.size(); ++i) {
		count += setting_choice_applies(row, context, reader, i) ? 1 : 0;
	}
	return count;
}

/// @brief A CHOICE row's menu value: the position of choice @p stored among the choices that apply.
/// @param row     A CHOICE row.
/// @param context Who the setting is shown for.
/// @param reader  The owner's settings.
/// @param stored  The stored choice index.
/// @return Its position, or nullopt where @p stored is out of range or does not apply.
[[nodiscard]] constexpr std::optional<int32_t> choice_position(const SettingInfo& row, const SettingContext& context,
                                                               SettingReader reader, int32_t stored) {
	if (stored < 0 || static_cast<std::size_t>(stored) >= row.choices.size()
	    || !setting_choice_applies(row, context, reader, static_cast<std::size_t>(stored))) {
		return std::nullopt;
	}
	int32_t position = 0;
	for (int32_t i = 0; i < stored; ++i) {
		position += setting_choice_applies(row, context, reader, static_cast<std::size_t>(i)) ? 1 : 0;
	}
	return position;
}

/// @brief The stored choice index a CHOICE row's menu @p position stands for.
/// @param row      A CHOICE row.
/// @param context  Who the setting is shown for.
/// @param reader   The owner's settings.
/// @param position A position among the choices that apply.
/// @return The choice index, or nullopt where @p position is outside the applicable choices.
[[nodiscard]] constexpr std::optional<int32_t> choice_at_position(const SettingInfo& row, const SettingContext& context,
                                                                  SettingReader reader, int32_t position) {
	if (position < 0) {
		return std::nullopt;
	}
	int32_t seen = 0;
	for (std::size_t i = 0; i < row.choices.size(); ++i) {
		if (setting_choice_applies(row, context, reader, i)) {
			if (seen == position) {
				return static_cast<int32_t>(i);
			}
			++seen;
		}
	}
	return std::nullopt;
}

/// @brief The row of @p table for @p path, or nullptr.
/// @param table The rows to search.
/// @param path  The slash path of the setting.
/// @return The matching row, or `nullptr` when none has that path.
[[nodiscard]] constexpr const SettingInfo* find_setting(std::span<const SettingInfo> table, std::string_view path) {
	for (const SettingInfo& row : table) {
		if (row.path == path) {
			return &row;
		}
	}
	return nullptr;
}

/// @brief Whether @p value is one @p row can store.
/// @param row   The setting's row.
/// @param value A stored value.
/// @return Whether it is a choice index (CHOICE), within min..max (INTEGER) or 0 or 1 (BOOL); false for a TEXT or
///         STRUCTURED row.
[[nodiscard]] constexpr bool stores_value(const SettingInfo& row, int32_t value) {
	switch (row.kind) {
	case SettingKind::CHOICE:
		return value >= 0 && static_cast<std::size_t>(value) < row.choices.size();
	case SettingKind::INTEGER:
		return value >= row.min && value <= row.max;
	case SettingKind::BOOL:
		return value == 0 || value == 1;
	case SettingKind::STRUCTURED:
	case SettingKind::TEXT:
		break;
	}
	return false;
}

/// @brief Whether @p condition is well formed against the table @p rows.
/// @param rows      The owner's table.
/// @param condition The condition.
/// @return For every subject but NEVER, at least one value (an empty IS holds for no one, which is NEVER's to say). For
///         SETTING: its path names a row of @p rows that is not STRUCTURED or TEXT, and each value is one that row can
///         store. For OWNER and INSTRUMENT: no path, and each value is one of the enum's. For INPUTS_PRESENT: IS, no
///         path, and each value is 0 or 1. For LOADED_SOURCES: no path, and each value is a mask of kMaxSoundSources
///         bits. For NEVER: IS, no path and no values. AT_LEAST only on a SETTING whose row is INTEGER, with exactly
///         one value.
[[nodiscard]] constexpr bool condition_is_valid(std::span<const SettingInfo> rows, const SettingCondition& condition) {
	const auto values_within = [&](int32_t least, int32_t greatest) {
		return condition.path.empty() && std::ranges::all_of(condition.values, [&](int32_t value) {
			       return value >= least && value <= greatest;
		       });
	};
	if (condition.subject != SettingCondition::Subject::NEVER && condition.values.empty()) {
		return false;
	}
	if (condition.test == SettingCondition::Test::AT_LEAST) {
		const SettingInfo* target =
		    condition.subject == SettingCondition::Subject::SETTING ? find_setting(rows, condition.path) : nullptr;
		return target != nullptr && target->kind == SettingKind::INTEGER && condition.values.size() == 1
		       && stores_value(*target, condition.values.front());
	}
	switch (condition.subject) {
	case SettingCondition::Subject::SETTING: {
		const SettingInfo* target = find_setting(rows, condition.path);
		return target != nullptr
		       && std::ranges::all_of(condition.values, [&](int32_t value) { return stores_value(*target, value); });
	}
	case SettingCondition::Subject::OWNER:
		return values_within(0, SettingContext::kOwnerCount - 1);
	case SettingCondition::Subject::INSTRUMENT:
		return values_within(0, SettingContext::kInstrumentCount - 1);
	case SettingCondition::Subject::INPUTS_PRESENT:
		return condition.test == SettingCondition::Test::IS && values_within(0, 1);
	case SettingCondition::Subject::LOADED_SOURCES:
		return values_within(0, (1 << kMaxSoundSources) - 1);
	case SettingCondition::Subject::NEVER:
		return condition.test == SettingCondition::Test::IS && condition.path.empty() && condition.values.empty();
	}
	return false;
}

/// @brief Whether every condition of every row in @p rows is well formed (condition_is_valid), its applies_any
/// alternatives included (none of them empty), every choice entry names a choice of its row, and no choice is listed
/// twice; for a static_assert per table, so a misspelt path or an out-of-range value fails to compile.
/// @param rows The table.
/// @return True when all hold.
[[nodiscard]] constexpr bool conditions_are_valid(std::span<const SettingInfo> rows) {
	const auto all_valid = [&](std::span<const SettingCondition> conditions) {
		return std::ranges::all_of(
		    conditions, [&](const SettingCondition& condition) { return condition_is_valid(rows, condition); });
	};
	for (const SettingInfo& row : rows) {
		if (row.extras == nullptr) {
			continue;
		}
		if (!all_valid(row.extras->applies_when)) {
			return false;
		}
		for (const std::span<const SettingCondition> alternative : row.extras->applies_any) {
			if (alternative.empty() || !all_valid(alternative)) {
				return false;
			}
		}
		const std::span<const ChoiceCondition> entries = row.extras->choice_when;
		for (std::size_t i = 0; i < entries.size(); ++i) {
			if (entries[i].choice >= row.choices.size() || !all_valid(entries[i].when)) {
				return false;
			}
			for (std::size_t j = i + 1; j < entries.size(); ++j) {
				if (entries[j].choice == entries[i].choice) {
					return false;
				}
			}
		}
	}
	return true;
}

/// @brief @p text as a whole decimal integer, or nothing.
/// @param text The text to parse; it must be entirely a decimal integer.
/// @return The parsed integer, or std::nullopt if @p text is not wholly one.
[[nodiscard]] inline std::optional<int64_t> whole_number(std::string_view text) {
	int64_t n = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return n;
}

/// @brief Whether @p text is a value @p table's row for @p path allows: a choice's name, an integer in the row's
/// range, 0 or 1 for a bool, or any text for a text row. False for a structured row or a path the table does not hold.
/// @param table The setting table.
/// @param path  The setting's slash path.
/// @param text  The candidate value, as text.
/// @return Whether @p text is allowed.
[[nodiscard]] inline bool check_setting_text(std::span<const SettingInfo> table, std::string_view path,
                                             std::string_view text) {
	const SettingInfo* row = find_setting(table, path);
	if (row == nullptr) {
		return false;
	}
	switch (row->kind) {
	case SettingKind::CHOICE:
		return std::ranges::find(row->choices, text) != row->choices.end();
	case SettingKind::INTEGER: {
		const std::optional<int64_t> n = whole_number(text);
		return n && *n >= row->min && *n <= row->max;
	}
	case SettingKind::BOOL:
		return text == "0" || text == "1";
	case SettingKind::TEXT:
		return true; // any text; the owner checks what it allows (a duplicate name)
	case SettingKind::STRUCTURED:
		break;
	}
	return false;
}

/// @brief The least and greatest value @p row stores: a CHOICE's indices, a BOOL's 0 and 1, an INTEGER's range.
/// @param row The setting's row.
/// @return {least, greatest}.
[[nodiscard]] constexpr std::pair<int32_t, int32_t> stored_bounds(const SettingInfo& row) {
	switch (row.kind) {
	case SettingKind::CHOICE:
		return {0, static_cast<int32_t>(row.choices.size()) - 1};
	case SettingKind::BOOL:
		return {0, 1};
	case SettingKind::INTEGER:
	case SettingKind::STRUCTURED:
	case SettingKind::TEXT:
		break;
	}
	return {row.min, row.max};
}

/// @brief @p stored as a menu shows it.
/// @param row    The setting's row.
/// @param stored A value within the row's stored range.
/// @return The menu's value: a sentinel's user value, the linear mapping's, or @p stored itself with no user scale.
[[nodiscard]] constexpr int32_t to_user(const SettingInfo& row, int32_t stored) {
	for (const SettingSentinel& sentinel : row.user.sentinels) {
		if (sentinel.stored == stored) {
			return sentinel.user;
		}
	}
	const int64_t span_stored = int64_t{row.user.stored_max} - row.user.stored_min;
	if (row.user.identity() || span_stored == 0) {
		return stored;
	}
	const int64_t span_user = int64_t{row.user.user_max} - row.user.user_min;
	const int64_t offset = int64_t{stored} - row.user.stored_min;
	return static_cast<int32_t>(row.user.user_min + (offset * span_user + span_stored / 2) / span_stored);
}

/// @brief A menu's @p user value as the row stores it.
/// @param row  The setting's row.
/// @param user The menu's value.
/// @return The stored value, or nullopt where @p user is outside the row's user range (with no user scale, its stored
///         range).
[[nodiscard]] constexpr std::optional<int32_t> to_stored(const SettingInfo& row, int32_t user) {
	for (const SettingSentinel& sentinel : row.user.sentinels) {
		if (sentinel.user == user) {
			return sentinel.stored;
		}
	}
	if (row.user.identity()) {
		const auto [least, greatest] = stored_bounds(row);
		return (user >= least && user <= greatest) ? std::optional<int32_t>{user} : std::nullopt;
	}
	if (user < row.user.user_min || user > row.user.user_max) {
		return std::nullopt;
	}
	const int64_t span_user = int64_t{row.user.user_max} - row.user.user_min;
	if (span_user == 0) {
		return row.user.stored_min;
	}
	const int64_t span_stored = int64_t{row.user.stored_max} - row.user.stored_min;
	return static_cast<int32_t>(row.user.stored_min
	                            + (int64_t{user - row.user.user_min} * span_stored + span_user / 2) / span_user);
}

/// @brief @p stored as the row's menu shows it, through the row's display mapping.
/// @param row            The setting's row. A CHOICE row's stored value is its choice index.
/// @param stored         The row's stored value.
/// @param partner_stored The partner row's stored value, for COMBINED_SYNC (the sync type's choice index) and
///                       TRANSPOSE_CENTS (the cents); ignored by the other kinds.
/// @return The menu's value. For a CHOICE row it is the choice index; a menu that filters choices uses
///         choice_position. CHOICE_ORDER gives 0 for a choice its order does not list.
[[nodiscard]] constexpr int32_t to_menu(const SettingInfo& row, int32_t stored, int32_t partner_stored = 0) {
	const DisplayMapping& mapping = mapping_of(row);
	switch (mapping.kind) {
	case DisplayMappingKind::LINEAR:
		break;
	case DisplayMappingKind::CLAMP_HIGH:
		for (const SettingSentinel& sentinel : row.user.sentinels) {
			if (sentinel.stored == stored) {
				return sentinel.user;
			}
		}
		if (stored > row.user.stored_max) {
			return mapping.clamp_to;
		}
		break;
	case DisplayMappingKind::CHOICE_ORDER:
		for (std::size_t i = 0; i < mapping.order.size(); ++i) {
			if (mapping.order[i] == stored) {
				return static_cast<int32_t>(i);
			}
		}
		return 0;
	case DisplayMappingKind::INVERTED_BOOL:
		return stored != 0 ? 0 : 1;
	case DisplayMappingKind::COMBINED_SYNC:
		return combined_sync_menu(stored, partner_stored);
	case DisplayMappingKind::TRANSPOSE_CENTS:
		return transpose_cents_menu(stored, partner_stored);
	case DisplayMappingKind::PHASE_DEGREES:
		return phase_degrees_menu(stored);
	case DisplayMappingKind::TABLE:
		return table_menu(stored, mapping.table, mapping.table_shift);
	case DisplayMappingKind::STANDARD:
		return standard_menu(stored);
	case DisplayMappingKind::UNSIGNED:
		return unsigned_menu(stored);
	case DisplayMappingKind::PAN:
		return pan_menu(stored);
	case DisplayMappingKind::FIFTIETHS:
		return fiftieths_menu(stored);
	case DisplayMappingKind::KNOB_POSITION:
		return knob_position_menu(stored);
	case DisplayMappingKind::FLOAT_FRACTION:
		return float_fraction_menu(stored, row.user.user_max);
	}
	return to_user(row, stored);
}

/// @brief The least and greatest value @p row's menu offers, through its display mapping. For a CHOICE row they bound
/// the choice index; a menu that filters choices counts them with applicable_choice_count.
/// @param row The setting's row.
/// @return {least, greatest}: the user range and its sentinels (LINEAR, CLAMP_HIGH, FLOAT_FRACTION; with no user
///         scale, the stored bounds), the order's or table's indices, 0..1 (INVERTED_BOOL), 0..kMaxCombinedSync or the
///         row's own user range within it (COMBINED_SYNC), the row's range ×100 (TRANSPOSE_CENTS), kPhaseFreeMenu to
///         kMaxPhaseDegrees (PHASE_DEGREES), 0..kMaxMenuSteps (STANDARD, UNSIGNED, FIFTIETHS), ±kMaxPanSteps (PAN) or
///         0..kMaxKnobPosition (KNOB_POSITION).
[[nodiscard]] constexpr std::pair<int32_t, int32_t> menu_range(const SettingInfo& row) {
	const DisplayMapping& mapping = mapping_of(row);
	const auto last_index = [](std::size_t size) {
		return std::pair<int32_t, int32_t>{0, static_cast<int32_t>(size) - 1};
	};
	switch (mapping.kind) {
	case DisplayMappingKind::LINEAR:
	case DisplayMappingKind::CLAMP_HIGH:
	case DisplayMappingKind::FLOAT_FRACTION:
		break;
	case DisplayMappingKind::CHOICE_ORDER:
		return last_index(mapping.order.size());
	case DisplayMappingKind::INVERTED_BOOL:
		return {0, 1};
	case DisplayMappingKind::COMBINED_SYNC:
		if (row.user.identity()) {
			return {0, kMaxCombinedSync};
		}
		return {std::max(row.user.user_min, 0), std::min(row.user.user_max, kMaxCombinedSync)};
	case DisplayMappingKind::TRANSPOSE_CENTS:
		return {row.min * 100, row.max * 100};
	case DisplayMappingKind::PHASE_DEGREES:
		return {kPhaseFreeMenu, kMaxPhaseDegrees};
	case DisplayMappingKind::TABLE:
		return last_index(mapping.table.size());
	case DisplayMappingKind::STANDARD:
	case DisplayMappingKind::UNSIGNED:
	case DisplayMappingKind::FIFTIETHS:
		return {0, kMaxMenuSteps};
	case DisplayMappingKind::PAN:
		return {-kMaxPanSteps, kMaxPanSteps};
	case DisplayMappingKind::KNOB_POSITION:
		return {0, kMaxKnobPosition};
	}
	std::pair<int32_t, int32_t> range =
	    row.user.identity() ? stored_bounds(row) : std::pair<int32_t, int32_t>{row.user.user_min, row.user.user_max};
	for (const SettingSentinel& sentinel : row.user.sentinels) {
		range = {std::min(range.first, sentinel.user), std::max(range.second, sentinel.user)};
	}
	return range;
}

/// @brief What the row (and its partner) store for menu value @p menu, through the row's display mapping.
/// @param row  The setting's row.
/// @param menu The menu's value.
/// @return {stored, partner_stored}, the partner's 0 for a one-row kind; nullopt when @p menu is outside the menu's
///         range (menu_range).
[[nodiscard]] constexpr std::optional<std::pair<int32_t, int32_t>> from_menu(const SettingInfo& row, int32_t menu) {
	using Stored = std::pair<int32_t, int32_t>;
	const DisplayMapping& mapping = mapping_of(row);
	const auto index_within = [menu](std::size_t size) { return menu >= 0 && static_cast<std::size_t>(menu) < size; };
	switch (mapping.kind) {
	case DisplayMappingKind::LINEAR:
	case DisplayMappingKind::CLAMP_HIGH:
	case DisplayMappingKind::FLOAT_FRACTION:
		break;
	case DisplayMappingKind::CHOICE_ORDER:
		if (!index_within(mapping.order.size())) {
			return std::nullopt;
		}
		return Stored{mapping.order[static_cast<std::size_t>(menu)], 0};
	case DisplayMappingKind::INVERTED_BOOL:
		if (menu != 0 && menu != 1) {
			return std::nullopt;
		}
		return Stored{1 - menu, 0};
	case DisplayMappingKind::COMBINED_SYNC: {
		// A row's own user range narrows its menu (the sidechain's offers off and the nine levels, with no triplet or
		// dotted).
		const auto [least, greatest] = menu_range(row);
		if (menu < least || menu > greatest) {
			return std::nullopt;
		}
		return combined_sync_stored(menu);
	}
	case DisplayMappingKind::TRANSPOSE_CENTS:
		if (int64_t{menu} < int64_t{row.min} * 100 || int64_t{menu} > int64_t{row.max} * 100) {
			return std::nullopt;
		}
		return transpose_cents_stored(menu);
	case DisplayMappingKind::PHASE_DEGREES:
		if (menu < kPhaseFreeMenu || menu > kMaxPhaseDegrees) {
			return std::nullopt;
		}
		return Stored{phase_degrees_stored(menu), 0};
	case DisplayMappingKind::TABLE:
		if (!index_within(mapping.table.size())) {
			return std::nullopt;
		}
		return Stored{mapping.table[static_cast<std::size_t>(menu)] << mapping.table_shift, 0};
	case DisplayMappingKind::STANDARD:
	case DisplayMappingKind::UNSIGNED:
	case DisplayMappingKind::PAN:
	case DisplayMappingKind::FIFTIETHS:
	case DisplayMappingKind::KNOB_POSITION: {
		const auto [least, greatest] = menu_range(row);
		if (menu < least || menu > greatest) {
			return std::nullopt;
		}
		const int32_t stored = mapping.kind == DisplayMappingKind::STANDARD   ? standard_stored(menu)
		                       : mapping.kind == DisplayMappingKind::UNSIGNED ? unsigned_stored(menu)
		                       : mapping.kind == DisplayMappingKind::PAN      ? pan_stored(menu)
		                       : mapping.kind == DisplayMappingKind::FIFTIETHS
		                           ? fiftieths_stored(menu)
		                           : knob_position_stored(menu, mapping.clamp_to);
		return Stored{stored, 0};
	}
	}
	const std::optional<int32_t> stored = to_stored(row, menu);
	if (!stored) {
		return std::nullopt;
	}
	return Stored{*stored, 0};
}

} // namespace deluge::engine
