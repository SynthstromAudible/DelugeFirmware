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

#include <cstdint>
#include <limits>

namespace deluge::plugin {

/// @brief How a param's stored word (a signed 32-bit word, Q31 for the Deluge's params) becomes the positions a host
/// shows: a knob position (−64..+64) and a menu number. Each kind is one arithmetic; a host converts only through
/// these functions and never switches on a param.
enum class MappingKind : uint8_t {
	/// Most params: the knob is the word's top 7 bits around zero, the menu 0..50 spans every word.
	STANDARD,
	/// Phase widths and the compressor threshold, which use only the non-negative words: the knob is the word's top
	/// 8 bits, offset to −64..+64, and the menu 0..50 spans the non-negative words.
	UNIPOLAR,
	/// Pan: STANDARD's knob, and a menu −25..25 centred on zero.
	PAN,
	/// A cable's depth, stored −2^30..2^30 (Q30: 2^30 is a depth of 1): the knob is the word's top bits in steps of
	/// 2^23 (+64 at 2^30), the menu −5000..5000 hundredths.
	CABLE_DEPTH,
};

/// @brief One named property of a param.
enum class ParamFlag : uint16_t {
	CENTRED = 1 << 0,             ///< shown around zero (pan, pitch)
	MIDDLE_DETENT = 1 << 1,       ///< the UI marks the middle position
	NO_UNDO = 1 << 2,             ///< changes to its unautomated value aren't kept for undo
	FLOAT_INTERPOLATION = 1 << 3, ///< automation interpolates in float
	SMOOTHED = 1 << 4,            ///< value changes pass the param smoother, from automation too
	SMOOTHED_USER_ONLY = 1 << 5,  ///< only a user's changes pass the smoother; automation steps land at once
};

/// @brief A set of ParamFlag.
struct ParamFlags {
	uint16_t bits{0};
	constexpr ParamFlags() = default;
	// Implicit: a single flag is a set of one, so a table row can name one flag as its set
	constexpr ParamFlags(ParamFlag flag) : bits{static_cast<uint16_t>(flag)} {} // NOLINT(*-explicit-constructor)
	/// @brief Whether @p flag is in the set.
	[[nodiscard]] constexpr bool has(ParamFlag flag) const { return (bits & static_cast<uint16_t>(flag)) != 0; }
	[[nodiscard]] constexpr ParamFlags operator|(ParamFlags other) const {
		ParamFlags out;
		out.bits = bits | other.bits;
		return out;
	}
	[[nodiscard]] constexpr bool operator==(const ParamFlags&) const = default;
};

[[nodiscard]] constexpr ParamFlags operator|(ParamFlag a, ParamFlag b) {
	return ParamFlags{a} | ParamFlags{b};
}

namespace mapping_detail {
constexpr int32_t kKnobMax = 64;
constexpr int32_t kMenuMax = 50;
constexpr int32_t kPanMenuMax = 25;
constexpr int32_t kCableMenuMax = 5000;
constexpr int32_t kWordMax = std::numeric_limits<int32_t>::max();
constexpr int32_t kWordMin = std::numeric_limits<int32_t>::min();
constexpr int64_t kHalfWordSpan = int64_t{1} << 31;
/// 2^32 / 50, rounded down: one STANDARD menu step in words.
constexpr uint32_t kMenuStep = 85899345;
constexpr int32_t kCableDepthOne = 1 << 30;
} // namespace mapping_detail

/// @brief The knob position (−64..+64) that shows @p word.
/// @param kind The param's mapping kind.
/// @param word The stored word.
/// @return The knob position.
[[nodiscard]] constexpr int32_t to_knob(MappingKind kind, int32_t word) {
	using namespace mapping_detail;
	switch (kind) {
	case MappingKind::UNIPOLAR:
		return (word >> 24) - kKnobMax;
	case MappingKind::CABLE_DEPTH:
		return (word >> 23) - kKnobMax;
	case MappingKind::STANDARD:
	case MappingKind::PAN:
	default:
		// The top position rounds every word within half a step of the top to +64.
		if (word >= kWordMax - (1 << 24) + 1) {
			return kKnobMax;
		}
		return (word + (1 << 24)) >> 25;
	}
}

/// @brief The word a knob at @p knob sets; +64 is the kind's top word.
/// @param kind The param's mapping kind.
/// @param knob The knob position, −64..+64.
/// @return The stored word.
[[nodiscard]] constexpr int32_t from_knob(MappingKind kind, int32_t knob) {
	using namespace mapping_detail;
	switch (kind) {
	case MappingKind::UNIPOLAR:
		return knob < kKnobMax ? (knob + kKnobMax) << 24 : kWordMax;
	case MappingKind::CABLE_DEPTH:
		return knob < kKnobMax ? (knob + kKnobMax) << 23 : kCableDepthOne;
	case MappingKind::STANDARD:
	case MappingKind::PAN:
	default:
		return knob < kKnobMax ? knob << 25 : kWordMax;
	}
}

/// @brief The menu number that shows @p word, rounded to nearest.
/// @param kind The param's mapping kind.
/// @param word The stored word.
/// @return The menu number, menu_min(kind)..menu_max(kind) for words in the kind's range.
[[nodiscard]] constexpr int32_t to_menu(MappingKind kind, int32_t word) {
	using namespace mapping_detail;
	switch (kind) {
	case MappingKind::UNIPOLAR:
		return static_cast<int32_t>((int64_t{word} * (int64_t{kMenuMax} * 2) + kHalfWordSpan) >> 32);
	case MappingKind::PAN:
		return static_cast<int32_t>((int64_t{word} * (int64_t{kPanMenuMax} * 2) + kHalfWordSpan) >> 32);
	case MappingKind::CABLE_DEPTH:
		return static_cast<int32_t>((int64_t{word} * kCableMenuMax + (1 << 29)) >> 30);
	case MappingKind::STANDARD:
	default:
		return static_cast<int32_t>(((int64_t{word} + kHalfWordSpan) * kMenuMax + kHalfWordSpan) >> 32);
	}
}

/// @brief The word a menu number sets; the ends snap to the kind's extreme words.
/// @param kind The param's mapping kind.
/// @param menu The menu number, menu_min(kind)..menu_max(kind).
/// @return The stored word.
[[nodiscard]] constexpr int32_t from_menu(MappingKind kind, int32_t menu) {
	using namespace mapping_detail;
	switch (kind) {
	case MappingKind::UNIPOLAR:
		if (menu == kMenuMax) {
			return kWordMax;
		}
		if (menu == 0) {
			return 0;
		}
		return static_cast<int32_t>((int64_t{static_cast<uint32_t>(menu)} * kMenuStep) >> 1);
	case MappingKind::PAN:
		if (menu == kPanMenuMax) {
			return kWordMax;
		}
		if (menu == -kPanMenuMax) {
			return kWordMin;
		}
		return static_cast<int32_t>(int64_t{menu} * (kHalfWordSpan / (int64_t{kPanMenuMax} * 2)) * 2);
	case MappingKind::CABLE_DEPTH:
		// 2^62 / 5000: one hundredth of a depth in words, with 32 fraction bits.
		return static_cast<int32_t>((int64_t{922337203685477} * menu) >> 32);
	case MappingKind::STANDARD:
	default:
		if (menu == kMenuMax) {
			return kWordMax;
		}
		if (menu == 0) {
			return kWordMin;
		}
		return static_cast<int32_t>(static_cast<uint32_t>(menu) * kMenuStep - 0x80000000u);
	}
}

/// @brief The lowest menu number of @p kind.
[[nodiscard]] constexpr int32_t menu_min(MappingKind kind) {
	using namespace mapping_detail;
	switch (kind) {
	case MappingKind::PAN:
		return -kPanMenuMax;
	case MappingKind::CABLE_DEPTH:
		return -kCableMenuMax;
	default:
		return 0;
	}
}

/// @brief The highest menu number of @p kind.
[[nodiscard]] constexpr int32_t menu_max(MappingKind kind) {
	using namespace mapping_detail;
	switch (kind) {
	case MappingKind::PAN:
		return kPanMenuMax;
	case MappingKind::CABLE_DEPTH:
		return kCableMenuMax;
	default:
		return kMenuMax;
	}
}

/// @brief The number a host shows for a knob: the menu's 0..50 scale, or −25..25 for a centred param, rounded half away
/// from zero.
/// @param flags The param's flags (CENTRED moves the scale).
/// @param knob  The knob position offset to 0..128 (a cable's reaches past both ends).
/// @return The number shown.
[[nodiscard]] constexpr int32_t knob_display_number(ParamFlags flags, int32_t knob) {
	using namespace mapping_detail;
	constexpr int32_t kKnobSpan = kKnobMax * 2;
	const int32_t scaled = (knob * kMenuMax) - (flags.has(ParamFlag::CENTRED) ? kPanMenuMax * kKnobSpan : 0);
	return scaled >= 0 ? (scaled + (kKnobSpan / 2)) / kKnobSpan : -((-scaled + (kKnobSpan / 2)) / kKnobSpan);
}

/// @brief The word a preset's 0..50 number gives a STANDARD param. Unlike from_menu it doesn't snap the ends: 50 is
/// 2147483602, not INT32_MAX, and new sounds' defaults depend on that.
/// @param number The preset number.
/// @return The stored word.
[[nodiscard]] constexpr int32_t preset_word(int8_t number) {
	return static_cast<int32_t>(static_cast<uint32_t>(number) * mapping_detail::kMenuStep - 0x80000000u);
}

} // namespace deluge::plugin
