#pragma once

/// @file
/// @brief The Fixed-quirks rows the waveform view specs skip in their comparisons with the oracle, and the predicates
///        that say which row an input falls in.
///
/// The new types are correct on every input; legacy was not. Each predicate answers, from an input alone, the row
/// under which legacy's answer differs, written from the row's own statement; where a row turns on a choice, it
/// computes legacy's choice and the correct one and answers the row when they differ. A domain loop compares the new
/// types with the oracle wherever its predicate answers none, and counts the inputs it skips per row; each row's own
/// named cases pin the correct value.

#include "nav_cases.h"
#include "ui/views/waveform/peaks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

namespace deluge::ui::views::waveform::test {

/// @brief The library's Fixed-quirks rows.
enum class Quirk : uint8_t { Q1, Q3, Q4, Q5, Q6, Q7, Q8, Q9, Q10, Q11, Q12, Q13, Q14, Q15, Q16, Q29 };

/// @brief How many rows there are: Q29 is the last.
inline constexpr size_t kQuirkCount = static_cast<size_t>(Quirk::Q29) + 1;

/// @brief The row's id, as the Fixed-quirks table names it.
[[nodiscard]] constexpr std::string_view quirk_name(Quirk q) {
	constexpr std::array<std::string_view, kQuirkCount> kNames{"Q1",  "Q3",  "Q4",  "Q5",  "Q6",  "Q7",  "Q8",  "Q9",
	                                                           "Q10", "Q11", "Q12", "Q13", "Q14", "Q15", "Q16", "Q29"};
	return kNames[static_cast<size_t>(q)];
}

/// @brief Per-row counts of the inputs a domain loop skipped, the inputs it compared, and its failures.
struct Skips {
	std::array<int64_t, kQuirkCount> per_row{};
	int64_t compared = 0;
	int64_t failures = 0;
	void skip(Quirk q) { per_row[static_cast<size_t>(q)]++; }
	[[nodiscard]] int64_t of(Quirk q) const { return per_row[static_cast<size_t>(q)]; }
	/// @brief Count a failure; true for the first ten, which the loop prints.
	bool fail() { return failures++ < 10; }
	void print(const char* domain) const {
		std::cout << domain << ": " << compared << " compared";
		for (size_t q = 0; q < kQuirkCount; q++) {
			if (per_row[q] != 0) {
				std::cout << ", " << quirk_name(static_cast<Quirk>(q)) << " skipped " << per_row[q];
			}
		}
		std::cout << "\n";
	}
};

namespace detail {

[[nodiscard]] constexpr bool outside_int32(int64_t v) {
	return v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max();
}

/// @brief The new code's whole zoom: `ceil(length / kColumns)`, at least 1.
[[nodiscard]] constexpr int64_t whole_zoom(uint64_t length) {
	return std::max<int64_t>(1, static_cast<int64_t>((length + 15) >> 4));
}

} // namespace detail

/// @brief The row under which legacy's scroll clamp differs: from a scroll of @p scroll at @p zoom on a waveform of
///        @p length frames.
[[nodiscard]] constexpr std::optional<Quirk> clamp_quirk(uint64_t length, int64_t scroll, int64_t zoom, bool extra) {
	if (scroll < 0 || extra) {
		return std::nullopt;
	}
	// Legacy's limit: the length narrowed to 32 bits (where 0 - 1 wraps), and the limit narrowed to 32 bits.
	const auto narrowed = static_cast<uint32_t>(length);
	const int64_t legacy_columns = int64_t{static_cast<uint32_t>(narrowed - 1)} / zoom + 1;
	const auto legacy_limit = static_cast<int32_t>((legacy_columns - 16) * zoom);
	// The correct one: the last whole column's scroll, and never below 0.
	const auto l = static_cast<int64_t>(length);
	const int64_t columns = l == 0 ? 0 : (l - 1) / zoom + 1;
	const int64_t limit = (columns - 16) * zoom;
	if (std::min<int64_t>(scroll, legacy_limit) == std::min<int64_t>(scroll, std::max<int64_t>(0, limit))) {
		return std::nullopt;
	}
	if (length == 0 || length > std::numeric_limits<uint32_t>::max() || detail::outside_int32(limit)) {
		return Quirk::Q9; // the length or the limit narrowed to 32 bits, or 0 - 1 wrapped
	}
	return Quirk::Q10; // a narrow waveform's negative limit
}

/// @brief The row under which legacy's open differs.
[[nodiscard]] constexpr std::optional<Quirk> open_quirk(const NavOpen& o) {
	if (o.length == 0) {
		return Quirk::Q4; // legacy's whole zoom of an empty waveform is 0
	}
	if (o.zoom == 0) {
		return std::nullopt;
	}
	if (o.zoom < 1 || o.zoom > detail::whole_zoom(o.length)) {
		return Quirk::Q13; // the remembered zoom adopted unclamped
	}
	return clamp_quirk(o.length, o.scroll, o.zoom, false);
}

/// @brief The row under which legacy's whole zoom, or its zoomed-in test, differs at @p start.
[[nodiscard]] constexpr std::optional<Quirk> state_quirk(const NavStart& start) {
	return start.length == 0 ? std::optional{Quirk::Q4} : std::nullopt;
}

/// @brief The row under which legacy's one-column scroll differs from @p start.
[[nodiscard]] constexpr std::optional<Quirk> scroll_quirk(const NavStart& start, int32_t offset, bool extra) {
	if (offset < 0) {
		return start.scroll > 0 && start.scroll < start.zoom ? std::optional{Quirk::Q12} : std::nullopt;
	}
	if (!extra) {
		return std::nullopt;
	}
	// Legacy refuses on a wrapped 32-bit sum, and keeps the new scroll in 32 bits.
	const auto wrapped = static_cast<int32_t>(static_cast<uint32_t>(start.scroll + start.zoom * 16));
	if (wrapped < start.scroll || start.scroll + start.zoom > std::numeric_limits<int32_t>::max()) {
		return Quirk::Q11;
	}
	return std::nullopt;
}

namespace detail {

/// @brief The pin a zoom pins, by legacy's measure (@p legacy) or the correct one: the preferred pin when it is on
///        screen, else the on-screen pin nearest the centre; none for the centre.
[[nodiscard]] constexpr std::optional<size_t> pin_choice(const NavCase& c, bool legacy) {
	if (!c.preferred) {
		return std::nullopt;
	}
	std::optional<size_t> chosen;
	int64_t chosen_point = -1; // legacy's first comparison measures from column -1
	const auto from_centre = [](int64_t point) { return point < 8 ? 8 - point : point - 8; };
	for (size_t m = 0; m < c.pins.size(); m++) {
		const MarkerPin& pin = c.pins[m];
		if (!pin.column || *pin.column < 0 || *pin.column >= 16) {
			continue;
		}
		const int64_t point = *pin.column + (pin.right_edge ? 1 : 0);
		const int64_t challenger = legacy ? *pin.column : point;
		const bool take = m == *c.preferred
		                  || (legacy ? from_centre(challenger) < from_centre(chosen_point)
		                             : (!chosen || from_centre(challenger) < from_centre(chosen_point)));
		if (take) {
			chosen = m;
			chosen_point = point;
		}
		if (m == *c.preferred) {
			break;
		}
	}
	return chosen;
}

/// @brief The zoom one step from @p c's start, which legacy and the new code agree on across the domain.
[[nodiscard]] constexpr std::optional<int64_t> stepped_zoom(const NavCase& c) {
	if (c.offset >= 0) {
		if (c.zoom < 2) {
			return std::nullopt;
		}
		const auto z = static_cast<uint64_t>(c.zoom);
		if ((z & (z - 1)) == 0) {
			return c.zoom >> 1;
		}
		int64_t next = 1;
		while (next < c.zoom) {
			next <<= 1;
		}
		return static_cast<double>(c.zoom) >= static_cast<double>(next) * 0.707 ? next >> 1 : next >> 2;
	}
	const int64_t limit = whole_zoom(c.length);
	if (c.zoom >= limit) {
		return std::nullopt;
	}
	int64_t z = c.zoom << 1;
	if (z >= limit || static_cast<double>(z << 1) * 0.707 >= static_cast<double>(limit)) {
		z = limit;
	}
	return z;
}

} // namespace detail

/// @brief The row under which legacy's zoom differs for @p c.
[[nodiscard]] constexpr std::optional<Quirk> zoom_quirk(const NavCase& c) {
	const std::optional<int64_t> new_zoom = detail::stepped_zoom(c);
	if (!new_zoom) {
		return std::nullopt; // both refuse
	}
	const std::optional<size_t> legacy_pin = detail::pin_choice(c, true);
	const std::optional<size_t> pin = detail::pin_choice(c, false);
	if (legacy_pin != pin) {
		return Quirk::Q7;
	}
	const int64_t frame = pin ? c.pins[*pin].position : c.scroll + c.zoom * 8;
	const int64_t point = pin ? *c.pins[*pin].column + (c.pins[*pin].right_edge ? 1 : 0) : 8;
	if (detail::outside_int32(frame) || detail::outside_int32(*new_zoom * point)
	    || detail::outside_int32(frame - *new_zoom * point)) {
		return Quirk::Q6; // the pin frame, the product or the new scroll in 32 bits
	}
	const int64_t unaligned = frame - *new_zoom * point;
	// Legacy aligns with truncating division; the new code rounds a right-edge pin up and any other down. Both then
	// clamp a negative scroll to 0, so the results differ only where they do after that clamp.
	int64_t legacy_aligned = (unaligned / *new_zoom) * *new_zoom;
	if (pin && c.pins[*pin].right_edge) {
		legacy_aligned = ((unaligned - 1) / *new_zoom + 1) * *new_zoom;
		const int64_t q = unaligned / *new_zoom;
		const int64_t ceiling = (q * *new_zoom < unaligned ? q + 1 : q) * *new_zoom;
		if (std::max<int64_t>(0, legacy_aligned) != std::max<int64_t>(0, ceiling)) {
			return Quirk::Q8; // the alignment's truncating division
		}
	}
	// Legacy clamps its own aligned scroll; a negative one goes to 0 with no right-hand check.
	return clamp_quirk(c.length, legacy_aligned, *new_zoom, c.extra_right);
}

/// @brief The row under which legacy's bar for @p peak differs.
[[nodiscard]] constexpr std::optional<Quirk> bar_quirk(Peak peak, int32_t centre, int32_t span) {
	if (span <= 0) {
		return Quirk::Q15;
	}
	const int64_t low = int64_t{peak.min} - centre;
	const int64_t high = int64_t{peak.max} - centre;
	if (detail::outside_int32(low) || detail::outside_int32(high)) {
		return Quirk::Q14; // the subtraction in 32 bits
	}
	const int64_t bottom = (low << 24) / span;
	const int64_t top = (high << 24) / span;
	if (detail::outside_int32(bottom) || detail::outside_int32(top) || detail::outside_int32(top - bottom)) {
		return Quirk::Q14; // the quotient, or the bar's height, narrowed to 32 bits
	}
	if (top - bottom < (1 << 24)) {
		const int64_t mid = (top >> 1) + (bottom >> 1);
		if (detail::outside_int32(mid + (1 << 23)) || detail::outside_int32(mid - (1 << 23))) {
			return Quirk::Q14; // the widened ends in 32 bits
		}
	}
	return std::nullopt;
}

/// @brief The row under which legacy's single-row brightness for @p peak differs.
[[nodiscard]] constexpr std::optional<Quirk> brightness_quirk(Peak peak, int32_t max_peak) {
	constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
	if (max_peak <= 0 || peak.min == kMin || peak.max == kMin) {
		return Quirk::Q16; // a silent waveform, or abs of the most negative value
	}
	const int64_t magnitude = std::max(peak.min < 0 ? -int64_t{peak.min} : int64_t{peak.min},
	                                   peak.max < 0 ? -int64_t{peak.max} : int64_t{peak.max});
	if ((magnitude << 16) / max_peak > std::numeric_limits<uint32_t>::max()) {
		return Quirk::Q16; // the ratio narrowed to 32 bits before the clamp
	}
	return std::nullopt;
}

} // namespace deluge::ui::views::waveform::test
