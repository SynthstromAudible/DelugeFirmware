#pragma once

/// @file
/// @brief A note's iterance: which repeats of its clip it plays. The packed form is what a song stores
///        (`project::DocNote::iterance`), and the presets are what the note menus step through.

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::project {

/// @brief Which repeats of its clip a note plays: the first, the last, or chosen steps of every `divisor` repeats.
struct Iterance {
	/// @brief How `format_preset` names the first and last presets.
	enum class Label : uint8_t {
		SHORT, ///< "1ST" and "LAST"
		LONG,  ///< "FIRST" and "LAST"
	};

	/// The cycle's length, 1 to 8. Zero means `steps` names the first repeat (bit 0) or the last (bit 1) instead.
	uint8_t divisor{};
	/// The repeats of the cycle that play, one bit per repeat.
	std::bitset<8> steps;

	constexpr bool operator==(const Iterance&) const = default;

	/// @brief Whether a note with this iterance plays on a repeat.
	/// @param repeat Which repeat of the clip this is, counting from 0.
	/// @param ending Whether this repeat is the clip's last.
	/// @return For a first or last iterance, whether @p repeat is the first or the repeat is @p ending; otherwise
	///         whether the cycle's step for @p repeat plays.
	[[nodiscard]] bool passes(uint32_t repeat, bool ending) const;

	/// @brief The packed form a song stores.
	/// @return The divisor in the high byte and the steps in the low byte.
	[[nodiscard]] constexpr uint16_t packed() const {
		return static_cast<uint16_t>((divisor << 8) | (steps.to_ulong() & 0xFF));
	}

	/// @brief The iterance a packed value names.
	/// @param value A packed iterance; only its low 16 bits are read.
	/// @return The iterance; OFF for a divisor above 8, or a divisor of 0 with steps other than first or last.
	[[nodiscard]] static constexpr Iterance from_packed(int32_t value);

	/// @brief The menu's preset index for this iterance.
	/// @return 0 for OFF, 1 to `kIterancePresetCount` for a preset, `kCustomIterancePreset` for any other.
	[[nodiscard]] constexpr int32_t preset_index() const;

	/// @brief The iterance a preset index names.
	/// @param index A preset index, as `preset_index` gives.
	/// @return The preset; 1of1 for `kCustomIterancePreset`; OFF for 0 or an index past the presets.
	[[nodiscard]] static constexpr Iterance from_preset_index(int32_t index);
};

// Notes and note rows hold an Iterance by value, so its layout is part of theirs.
static_assert(sizeof(Iterance) == 2 * sizeof(unsigned long), "Iterance is a divisor byte and a bitset<8>, padded");

/// Plays every repeat: no iterance.
inline constexpr Iterance kIteranceOff{.divisor = 0, .steps = 0};
/// What choosing CUSTOM starts from: 1of1.
inline constexpr Iterance kIteranceCustom{.divisor = 1, .steps = 1};
/// Plays the first repeat only.
inline constexpr Iterance kIteranceFirst{.divisor = 0, .steps = 1};
/// Plays the last repeat only.
inline constexpr Iterance kIteranceLast{.divisor = 0, .steps = 2};

/// How many presets the menus step through, OFF and CUSTOM aside.
inline constexpr int32_t kIterancePresetCount = 37;
/// The preset index of OFF.
inline constexpr int32_t kIterancePresetOff = 0;
/// The preset index of the first-repeat preset.
inline constexpr int32_t kIterancePresetFirst = 1;
/// The preset index of the last-repeat preset.
inline constexpr int32_t kIterancePresetLast = 2;
/// The preset index of CUSTOM, straight after the last preset.
inline constexpr int32_t kCustomIterancePreset = kIterancePresetCount + 1;

/// The presets, in menu order: first, last, then each step of 2 to 8 repeats. Preset index `i` is entry `i - 1`.
inline constexpr std::array<Iterance, kIterancePresetCount> kIterancePresets{{
    {0, 0b1},    {0, 0b10},    {2, 0b1},      {2, 0b10},      {3, 0b1},        {3, 0b10},     {3, 0b100},  {4, 0b1},
    {4, 0b10},   {4, 0b100},   {4, 0b1000},   {5, 0b1},       {5, 0b10},       {5, 0b100},    {5, 0b1000}, {5, 0b10000},
    {6, 0b1},    {6, 0b10},    {6, 0b100},    {6, 0b1000},    {6, 0b10000},    {6, 0b100000}, {7, 0b1},    {7, 0b10},
    {7, 0b100},  {7, 0b1000},  {7, 0b10000},  {7, 0b100000},  {7, 0b1000000},  {8, 0b1},      {8, 0b10},   {8, 0b100},
    {8, 0b1000}, {8, 0b10000}, {8, 0b100000}, {8, 0b1000000}, {8, 0b10000000},
}};

constexpr Iterance Iterance::from_packed(int32_t value) {
	const auto divisor = static_cast<uint8_t>(value >> 8);
	const auto steps = static_cast<uint8_t>(value & 0xFF);
	if ((divisor == 0 && steps != 1 && steps != 2) || divisor > 8) {
		return kIteranceOff;
	}
	return Iterance{.divisor = divisor, .steps = steps};
}

constexpr int32_t Iterance::preset_index() const {
	if (steps.none() && divisor == 0) {
		return kIterancePresetOff;
	}
	for (std::size_t i = 0; i < kIterancePresets.size(); ++i) {
		if (kIterancePresets[i] == *this) {
			return static_cast<int32_t>(i) + 1;
		}
	}
	return kCustomIterancePreset;
}

constexpr Iterance Iterance::from_preset_index(int32_t index) {
	if (index > 0 && index <= kIterancePresetCount) {
		return kIterancePresets[static_cast<std::size_t>(index - 1)];
	}
	if (index == kCustomIterancePreset) {
		return kIteranceCustom;
	}
	return kIteranceOff;
}

/// @brief Writes the menu's label for a preset, truncated to @p out and always terminated when @p out is not empty.
/// @param index The preset index (`Iterance::preset_index`).
/// @param out Where the label goes, with its terminator.
/// @param prefix Text written before the label.
/// @param step_separator What goes between an x-of-y preset's active step and its divisor (":" or " of ").
/// @param label How the first and last presets are named.
/// @return The label's length, without the terminator.
std::size_t format_preset(int32_t index, std::span<char> out, std::string_view prefix, std::string_view step_separator,
                          Iterance::Label label);

} // namespace deluge::project
