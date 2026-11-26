#pragma once

/// @file
/// @brief What a waveform's columns show: each column's peak, or why it has none, and the bars a renderer draws from a
///        peak.

#include "base/divide.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace deluge::ui::views::waveform {

/// @brief How many columns a waveform view shows.
inline constexpr int32_t kColumns = 16;
/// @brief kColumns as a power of two: dividing by it is a shift.
inline constexpr int32_t kColumnsLog2 = 4;
static_assert(kColumns == 1 << kColumnsLog2);

/// @brief One pad's height in Q8.24 pad units.
inline constexpr int32_t kOnePad = 1 << 24;

/// @brief Which frames a view's columns cover: column `c` covers [scroll + c * zoom, scroll + (c + 1) * zoom).
struct Window {
	int64_t scroll{0}; ///< the first frame column 0 covers; negative before the waveform's start
	int64_t zoom{1};   ///< frames per column; a zoom below 1 covers no frames
	bool operator==(const Window&) const = default;
};

/// @brief One column's lowest and highest sample value, at full 32-bit scale.
struct Peak {
	int32_t min{0}; ///< the lowest value
	int32_t max{0}; ///< the highest value
	bool operator==(const Peak&) const = default;
};

/// @brief What one column's answer is. PENDING is 0, so a default `ColumnRead` is all zero bits and a `PeakRead` with
///        static storage is zero-initialised.
enum class ColumnState : uint8_t {
	PENDING,  ///< not known yet: a cluster under it could not be read now, so a later frame asks again
	PEAK,     ///< the column's peak is known
	PAST_END, ///< nothing to draw: outside the frames the waveform holds now, or no whole frame under it
};

/// @brief One column's answer: a peak, pending, or past the end.
class ColumnRead {
public:
	/// @brief A column not answered yet: pending.
	constexpr ColumnRead() = default;
	/// @brief A column whose peak is @p peak.
	/// @param peak The column's lowest and highest value.
	/// @return The answer.
	[[nodiscard]] static constexpr ColumnRead of(Peak peak) { return ColumnRead{ColumnState::PEAK, peak}; }
	/// @brief A column a later read may answer.
	/// @return The answer.
	[[nodiscard]] static constexpr ColumnRead pending() { return ColumnRead{}; }
	/// @brief A column with nothing to draw.
	/// @return The answer.
	[[nodiscard]] static constexpr ColumnRead past_end() { return ColumnRead{ColumnState::PAST_END, {}}; }
	/// @brief What the answer is.
	[[nodiscard]] constexpr ColumnState state() const { return state_; }
	/// @brief The column's peak; none unless the state is PEAK.
	[[nodiscard]] constexpr std::optional<Peak> peak() const {
		return state_ == ColumnState::PEAK ? std::optional{peak_} : std::nullopt;
	}
	bool operator==(const ColumnRead&) const = default;

private:
	constexpr ColumnRead(ColumnState state, Peak peak) : state_{state}, peak_{peak} {}
	ColumnState state_{ColumnState::PENDING};
	Peak peak_{};
};

/// @brief A column's bar on the pads, in Q8.24 pad units from the waveform's centre value.
struct Bar {
	int32_t bottom{0}; ///< Q8.24
	int32_t top{0};    ///< Q8.24
	bool operator==(const Bar&) const = default;
};

/// @brief The bar @p peak draws, scaled so that @p span is one pad, and at least one pad tall.
/// @param peak   The column's peak.
/// @param centre The waveform's centre value.
/// @param span   The waveform's value range per pad. 0 or less means the waveform is flat, or no peak has been
///               measured yet: the bar is then the one-pad bar at the centre.
/// @return The bar, each end saturated to the `int32_t` range of Q8.24.
[[nodiscard]] constexpr Bar bar_of(Peak peak, int32_t centre, int32_t span) {
	constexpr int32_t kHalfPad = kOnePad >> 1;
	if (span <= 0) {
		return {.bottom = -kHalfPad, .top = kHalfPad};
	}
	constexpr int64_t kLow = std::numeric_limits<int32_t>::min();
	constexpr int64_t kHigh = std::numeric_limits<int32_t>::max();
	const auto scaled = [centre, span](int32_t value) {
		const int64_t offset = int64_t{value} - centre; // 33 bits at most
		// An end whose quotient would pass the int32_t range saturates. Below that bound the quotient is under 2^31,
		// and offset * 2^24 is a 33-bit value shifted left, so trunc_div is exact.
		const int64_t limit = int64_t{span} << 7; // 2^31 / 2^24 spans
		if (offset >= limit) {
			return kHigh;
		}
		if (offset <= -limit) {
			return kLow;
		}
		return base::trunc_div(offset * kOnePad, span);
	};
	int64_t bottom = scaled(peak.min);
	int64_t top = scaled(peak.max);
	if (top - bottom < kOnePad) {
		// At least one pad tall, centred on the bar's own midpoint (halves first, so the sum cannot overflow).
		const int64_t mid = (top >> 1) + (bottom >> 1);
		top = mid + kHalfPad;
		bottom = mid - kHalfPad;
	}
	return {.bottom = static_cast<int32_t>(std::clamp(bottom, kLow, kHigh)),
	        .top = static_cast<int32_t>(std::clamp(top, kLow, kHigh))};
}

/// @brief A column's brightness on the single-row render, against the waveform's largest magnitude.
/// @param peak     The column's peak.
/// @param max_peak The waveform's largest magnitude. 0 or less is a silent waveform, which draws dark.
/// @return 0 to 256 inclusive: 256 at or past @p max_peak.
[[nodiscard]] constexpr int32_t single_row_brightness(Peak peak, int32_t max_peak) {
	if (max_peak <= 0) {
		return 0;
	}
	const int64_t low = peak.min < 0 ? -int64_t{peak.min} : int64_t{peak.min};
	const int64_t high = peak.max < 0 ? -int64_t{peak.max} : int64_t{peak.max};
	const int64_t magnitude = std::max(low, high);
	if (magnitude >= max_peak) {
		return 256;
	}
	// Below max_peak the Q16 ratio is under 2^16, and magnitude << 16 is under 2^47: exact.
	return static_cast<int32_t>(base::trunc_div(magnitude << 16, max_peak) >> 8);
}

/// @brief A window's columns as one read answers them: each a peak, pending or past the end.
class PeakRead {
public:
	/// @brief Every column pending.
	constexpr PeakRead() = default;
	/// @brief Every column past the end: nothing to draw.
	[[nodiscard]] static constexpr PeakRead all_past_end() {
		PeakRead read;
		read.columns_.fill(ColumnRead::past_end());
		return read;
	}
	/// @brief A column's answer.
	/// @param column The column, 0 to kColumns - 1.
	/// @return Its answer.
	[[nodiscard]] constexpr ColumnRead column(int32_t column) const { return columns_[static_cast<size_t>(column)]; }
	/// @brief Set a column's answer.
	/// @param column The column, 0 to kColumns - 1.
	/// @param read   Its answer.
	constexpr void set(int32_t column, ColumnRead read) { columns_[static_cast<size_t>(column)] = read; }
	/// @brief Whether a column in [@p first, @p last) is pending, so that a later frame should ask again.
	/// @param first The first column asked about; 0 <= @p first <= @p last.
	/// @param last  One past the last; @p last <= kColumns.
	/// @return Whether one is pending.
	[[nodiscard]] constexpr bool pending(int32_t first = 0, int32_t last = kColumns) const {
		for (int32_t c = first; c < last; c++) {
			if (column(c).state() == ColumnState::PENDING) {
				return true;
			}
		}
		return false;
	}
	/// @brief How many columns are answered: a peak or past the end.
	[[nodiscard]] constexpr int32_t answered() const {
		int32_t n = 0;
		for (const ColumnRead& c : columns_) {
			n += c.state() != ColumnState::PENDING ? 1 : 0;
		}
		return n;
	}
	/// @brief How many frames of the pending columns' spans their index has folded: it grows as the index fills, so a
	///        view waiting on a column many clusters wide sees its read progress before the column is answered.
	[[nodiscard]] constexpr uint64_t progress() const { return progress_; }
	/// @brief Count @p frames more of the pending columns' spans folded.
	constexpr void add_progress(uint64_t frames) { progress_ += frames; }
	/// @brief A column's bar.
	/// @param column The column, 0 to kColumns - 1.
	/// @param centre The waveform's centre value (bar_of).
	/// @param span   The waveform's value range per pad (bar_of).
	/// @return The bar (bar_of); none unless the column holds a peak.
	[[nodiscard]] constexpr std::optional<Bar> bar(int32_t column, int32_t centre, int32_t span) const {
		const std::optional<Peak> p = this->column(column).peak();
		return p ? std::optional{bar_of(*p, centre, span)} : std::nullopt;
	}
	bool operator==(const PeakRead&) const = default;

private:
	std::array<ColumnRead, kColumns> columns_{};
	uint64_t progress_{0};
};

} // namespace deluge::ui::views::waveform
