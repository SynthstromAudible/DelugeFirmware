#pragma once

/// @file
/// @brief The columns a host has read of a waveform, kept so that a later read at the same window, or one scrolled by
///        whole columns, reads only what it lacks; and the few a host keeps, one per waveform.
///
/// Only the host's waveform reads and the legacy views use these. A scene asks the waveforms port for a window's
/// columns and keeps nothing (`scripts/layers.toml`, `scenes-no-peak-cache`).

#include "base/divide.h"
#include "ui/views/waveform/peaks.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace deluge::app::ports::waveforms {

/// @brief A set of columns, bit `c` for column `c`: the columns a read is asked for.
using ColumnMask = std::bitset<ui::views::waveform::kColumns>;

/// @brief The columns read of one waveform, at the window they were last read at.
class PeakCache {
public:
	/// @brief Forget every column and the window: the next read reads all of them.
	constexpr void forget() { *this = PeakCache{}; }
	/// @brief Bring the columns to @p at and read what they lack.
	/// @param at           The window to read at. A zoom below 1 covers no frames: every column is past the end, and
	///                     nothing is read.
	/// @param valid_length The frames a read sees now: a take's captured count while it records, else the length.
	/// @param growing      Whether the waveform is a take still being written, whose growth keeps the columns.
	/// @param read         A callable `PeakRead(ColumnMask wanted)` that reads the columns asked for. It is called on
	///                     every update, with an empty mask when every column is kept, so it can refresh what it owns
	///                     (a take's running range). Its answers for columns not asked for are ignored.
	/// @param first        The first column a read may be asked for; 0 <= @p first <= @p last.
	/// @param last         One past the last; @p last <= kColumns.
	/// @return The columns at @p at.
	// TODO(c++26): take std::function_ref<…> once the device's libstdc++ provides it (arm-none-eabi GCC 16; 15.2.1 does
	// not).
	template <typename Read>
	constexpr ui::views::waveform::PeakRead update(ui::views::waveform::Window at, int64_t valid_length, bool growing,
	                                               Read&& read, int32_t first = 0,
	                                               int32_t last = ui::views::waveform::kColumns);
	/// @brief The columns as the last update left them.
	[[nodiscard]] constexpr const ui::views::waveform::PeakRead& columns() const { return columns_; }
	/// @brief The window of the last update; none before the first or after a forget.
	[[nodiscard]] constexpr std::optional<ui::views::waveform::Window> window() const { return window_; }
	/// @brief The valid length of the last update, which a later update's length is compared with.
	/// @return The frames the last read saw; 0 before the first update or after a forget.
	[[nodiscard]] constexpr int64_t valid_length() const { return valid_length_; }
	bool operator==(const PeakCache&) const = default;

private:
	constexpr void retarget(ui::views::waveform::Window at, int64_t valid_length, bool growing);
	constexpr void slide(int64_t columns);
	ui::views::waveform::PeakRead columns_{};
	std::optional<ui::views::waveform::Window> window_{};
	int64_t valid_length_{0};
	bool growing_{false};
};

/// @brief A host's caches, one per waveform it has read lately, by the waveform's identity. A new waveform takes the
///        cache read least lately.
/// @tparam Capacity How many waveforms are kept.
template <size_t Capacity>
class PeakCacheSet {
public:
	/// @brief Read @p identity's columns at @p at through its cache (PeakCache::update), over every column.
	/// @param identity     The waveform's identity: another file, or a take, has another.
	/// @param at           The window to read at (PeakCache::update).
	/// @param valid_length The frames a read sees now (PeakCache::update).
	/// @param growing      Whether the waveform is a take still being written (PeakCache::update).
	/// @param read         The reader for the columns the cache lacks (PeakCache::update).
	/// @return The columns at @p at.
	// TODO(c++26): take std::function_ref<…> once the device's libstdc++ provides it (arm-none-eabi GCC 16; 15.2.1 does
	// not).
	template <typename Read>
	constexpr ui::views::waveform::PeakRead read(uint64_t identity, ui::views::waveform::Window at,
	                                             int64_t valid_length, bool growing, Read&& read);
	/// @brief How many waveforms the set holds.
	/// @return The count, at most @p Capacity.
	[[nodiscard]] constexpr size_t held() const;

private:
	struct Entry {
		std::optional<uint64_t> identity; ///< the waveform held; none for an unused entry
		uint64_t last_read{0};            ///< when it was last read, by the set's own count
		PeakCache cache;                  ///< its columns
	};
	std::array<Entry, Capacity> entries_{};
	uint64_t reads_{0};
};

constexpr void PeakCache::retarget(ui::views::waveform::Window at, int64_t valid_length, bool growing) {
	const bool length_changed = !growing && valid_length != valid_length_;
	const bool take_ended = growing_ && !growing; // the take may have been rewritten as it settled
	if (!window_ || at.zoom != window_->zoom || length_changed || take_ended) {
		columns_ = ui::views::waveform::PeakRead{};
	}
	else if (at.scroll != window_->scroll) {
		// Only a scroll of at most kColumns whole columns can slide. Two scrolls further apart than an int64_t holds
		// clear: no view's zoom (at most 2^58, WaveformView::open) puts kColumns columns that far apart.
		constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
		const int64_t from = window_->scroll;
		const bool fits = from < 0 ? at.scroll <= kMax + from : at.scroll >= std::numeric_limits<int64_t>::min() + from;
		const int64_t reach =
		    at.zoom > (kMax >> ui::views::waveform::kColumnsLog2) ? kMax : at.zoom << ui::views::waveform::kColumnsLog2;
		const std::optional<int64_t> delta = fits ? std::optional{at.scroll - from} : std::nullopt;
		const bool near = delta && *delta >= -reach && *delta <= reach;
		const std::optional<int64_t> columns = near ? base::exact_div(*delta, at.zoom) : std::nullopt;
		if (columns && *columns != 0) {
			slide(*columns);
		}
		else {
			columns_ = ui::views::waveform::PeakRead{};
		}
	}
	window_ = at;
	valid_length_ = valid_length;
	growing_ = growing;
}

constexpr void PeakCache::slide(int64_t columns) {
	if (columns <= -ui::views::waveform::kColumns || columns >= ui::views::waveform::kColumns) {
		columns_ = ui::views::waveform::PeakRead{};
		return;
	}
	const auto n = static_cast<int32_t>(columns);
	ui::views::waveform::PeakRead slid; // every column pending: those the slide exposes stay so
	for (int32_t c = 0; c < ui::views::waveform::kColumns; c++) {
		const int32_t from = c + n;
		if (from >= 0 && from < ui::views::waveform::kColumns) {
			slid.set(c, columns_.column(from));
		}
	}
	columns_ = slid;
}

template <typename Read>
constexpr ui::views::waveform::PeakRead PeakCache::update(ui::views::waveform::Window at, int64_t valid_length,
                                                          bool growing, Read&& read, int32_t first, int32_t last) {
	if (at.zoom < 1) {
		forget();
		return ui::views::waveform::PeakRead::all_past_end();
	}
	retarget(at, valid_length, growing);
	ColumnMask wanted;
	for (int32_t c = first; c < last; c++) {
		// Only a peak is kept: a pending column may be readable now, and a growing take's past-the-end ones may not
		// be past it any more.
		if (columns_.column(c).state() != ui::views::waveform::ColumnState::PEAK) {
			wanted.set(static_cast<size_t>(c));
		}
	}
	const ui::views::waveform::PeakRead answer = std::forward<Read>(read)(wanted);
	for (int32_t c = first; c < last; c++) {
		if (wanted.test(static_cast<size_t>(c))) {
			columns_.set(c, answer.column(c));
		}
	}
	return columns_;
}

template <size_t Capacity>
template <typename Read>
constexpr ui::views::waveform::PeakRead PeakCacheSet<Capacity>::read(uint64_t identity, ui::views::waveform::Window at,
                                                                     int64_t valid_length, bool growing, Read&& read) {
	Entry* entry = nullptr;
	for (Entry& e : entries_) {
		if (e.identity == identity) {
			entry = &e; // this waveform's own cache
			break;
		}
	}
	if (entry == nullptr) {
		for (Entry& e : entries_) {
			if (!e.identity) {
				entry = &e; // an unused one, before any is given up
				break;
			}
		}
	}
	if (entry == nullptr) {
		entry = &entries_[0];
		for (Entry& e : entries_) {
			if (e.last_read < entry->last_read) {
				entry = &e; // the one read least lately
			}
		}
	}
	if (entry->identity != identity) {
		entry->identity = identity;
		entry->cache.forget(); // a new waveform starts with every column unread
	}
	entry->last_read = ++reads_;
	return entry->cache.update(at, valid_length, growing, std::forward<Read>(read));
}

template <size_t Capacity>
constexpr size_t PeakCacheSet<Capacity>::held() const {
	size_t n = 0;
	for (const Entry& e : entries_) {
		n += e.identity ? 1 : 0;
	}
	return n;
}

} // namespace deluge::app::ports::waveforms
