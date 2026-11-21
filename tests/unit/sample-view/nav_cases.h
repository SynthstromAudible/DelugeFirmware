#pragma once

/// @file
/// @brief The navigator's comparison domain, shared by the unit spec (new types against the oracle) and the sim spec
///        (the oracle against the live legacy code).
///
/// Zooms run from 1 to 2^30: above that, the legacy zoom-in reads an uninitialised value and has no answer to
/// compare with.

#include "sample-view/view.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace deluge::sample_view::test {

/// @brief A remembered view a waveform opens at: the open's inputs.
struct NavOpen {
	uint64_t length{0};
	int64_t scroll{0};
	int64_t zoom{0}; ///< 0 for none remembered
};

/// @brief A view the new type can reach through its own operations, which each operation starts from: an open's
///        result, or one scrolled or moved on from it past the scroll limit.
struct NavStart {
	uint64_t length{0};
	int64_t scroll{0};
	int64_t zoom{1};
	bool past_limit{false}; ///< scrolled past the last whole column, as only extra scroll right reaches
	WaveformView view{};    ///< the view itself, made by the operations that reach it
};

/// @brief One zoom call: its start, its direction, its pins and its flags.
struct NavCase {
	uint64_t length{0};
	int64_t scroll{0};
	int64_t zoom{1};
	int32_t offset{0};
	std::optional<size_t> preferred;
	bool extra_right{false};
	std::array<MarkerPin, 4> pins{};
	WaveformView view{}; ///< the start's view
};

/// @brief The pins' zone order: start, loop start, loop end, end. The loop end and end pin by their right edge.
inline constexpr std::array<bool, 4> kRightEdge{false, false, true, true};

/// @brief @p items sorted by @p key, each key kept once.
template <typename T, typename Key>
constexpr void sort_unique(std::vector<T>& items, Key key) {
	std::ranges::sort(items, {}, key);
	const auto [first, last] = std::ranges::unique(items, {}, key);
	items.erase(first, last);
}

[[nodiscard]] constexpr std::vector<uint64_t> nav_lengths() {
	return {0,
	        1,
	        2,
	        15,
	        16,
	        17,
	        31,
	        32,
	        33,
	        240,
	        241,
	        1000,
	        (uint64_t{1} << 31) - 1,
	        uint64_t{1} << 31,
	        (uint64_t{1} << 31) + 1,
	        (uint64_t{1} << 32) - 1,
	        uint64_t{1} << 32,
	        (uint64_t{1} << 32) + 1};
}

[[nodiscard]] constexpr std::vector<int64_t> nav_zooms() {
	std::vector<int64_t> zooms;
	const auto add = [&zooms](int64_t z) { zooms.push_back(std::clamp<int64_t>(z, 1, int64_t{1} << 30)); };
	for (int32_t k = 0; k <= 28; k++) {
		const int64_t p = int64_t{1} << k;
		const auto q = static_cast<int64_t>(static_cast<double>(p) * 0.707);
		for (int64_t d = -1; d <= 1; d++) {
			add(p + d);
			add(q + d);
		}
	}
	sort_unique(zooms, [](int64_t z) { return z; });
	return zooms;
}

[[nodiscard]] constexpr std::vector<int64_t> nav_scrolls(uint64_t length, int64_t zoom) {
	const auto l = static_cast<int64_t>(length);
	return {-zoom, -1, 0, 1, 15, 16, zoom - 1, zoom, 7 * zoom, l - 16 * zoom, l - 1, l};
}

/// @brief Every open the domain makes: each length, each zoom (and none), each scroll.
[[nodiscard]] constexpr std::vector<NavOpen> nav_opens() {
	std::vector<NavOpen> opens;
	for (const uint64_t length : nav_lengths()) {
		opens.push_back({.length = length, .scroll = 0, .zoom = 0});
		for (const int64_t zoom : nav_zooms()) {
			for (const int64_t scroll : nav_scrolls(length, zoom)) {
				opens.push_back({.length = length, .scroll = scroll, .zoom = zoom});
			}
		}
	}
	return opens;
}

/// @brief The distinct views the opens reach, each an operation's start; and, from each open with a nonzero scroll
///        limit, the views extra scroll right reaches one, two and sixteen columns on, and the view moved sixteen
///        columns, which pass the limit from a start at or near it.
[[nodiscard]] constexpr std::vector<NavStart> nav_starts() {
	std::vector<NavStart> starts;
	const auto add = [&starts](const WaveformView& v) {
		starts.push_back({.length = v.length(),
		                  .scroll = v.scroll(),
		                  .zoom = v.zoom(),
		                  .past_limit = v.clamped(false).scroll() < v.scroll(),
		                  .view = v});
	};
	for (const NavOpen& o : nav_opens()) {
		const WaveformView v = WaveformView::open(
		    o.length, o.zoom == 0 ? std::nullopt : std::optional{Window{.scroll = o.scroll, .zoom = o.zoom}});
		add(v);
		if (!v.zoomed_in()) {
			continue; // the whole waveform fits: the limit is 0
		}
		WaveformView on = v;
		for (int32_t n = 1; n <= 16; n++) {
			const std::optional<WaveformView> next = on.scrolled(1, true, std::nullopt);
			if (!next) {
				break;
			}
			on = *next;
			if (n == 1 || n == 2 || n == 16) {
				add(on);
			}
		}
		add(v.moved(16));
	}
	sort_unique(starts, [](const NavStart& s) { return std::tuple{s.length, s.scroll, s.zoom}; });
	return starts;
}

/// @brief A pin set at @p column of @p start's view, or unset.
[[nodiscard]] constexpr MarkerPin nav_pin(const NavStart& start, size_t marker, std::optional<int64_t> column) {
	if (!column) {
		return {.position = 0, .column = std::nullopt, .right_edge = kRightEdge[marker]};
	}
	const int64_t position = start.scroll + *column * start.zoom + (kRightEdge[marker] ? start.zoom : 0);
	return {.position = position, .column = column, .right_edge = kRightEdge[marker]};
}

/// @brief The pin sets a zoom from @p start is tried with: each pin alone over the columns {unset, -1, 0, 7, 8, 15,
///        16}; all four on screen over {0, 7, 8, 15} in each rotation; and all four over {9, 7, -1, 16} in each
///        rotation, where two pins compete while others are off screen.
[[nodiscard]] constexpr std::vector<std::array<MarkerPin, 4>> nav_pin_sets(const NavStart& start) {
	std::vector<std::array<MarkerPin, 4>> sets;
	const std::array<std::optional<int64_t>, 7> alone{std::nullopt, -1, 0, 7, 8, 15, 16};
	for (size_t marker = 0; marker < 4; marker++) {
		for (const std::optional<int64_t> column : alone) {
			std::array<MarkerPin, 4> pins{};
			for (size_t m = 0; m < 4; m++) {
				pins[m] = nav_pin(start, m, m == marker ? column : std::nullopt);
			}
			sets.push_back(pins);
		}
	}
	const std::array<int64_t, 4> on_screen{0, 7, 8, 15};
	for (size_t rotation = 0; rotation < 4; rotation++) {
		std::array<MarkerPin, 4> pins{};
		for (size_t m = 0; m < 4; m++) {
			pins[m] = nav_pin(start, m, on_screen[(m + rotation) % 4]);
		}
		sets.push_back(pins);
	}
	const std::array<int64_t, 4> competing{9, 7, -1, 16};
	for (size_t rotation = 0; rotation < 4; rotation++) {
		std::array<MarkerPin, 4> pins{};
		for (size_t m = 0; m < 4; m++) {
			pins[m] = nav_pin(start, m, competing[(m + rotation) % 4]);
		}
		sets.push_back(pins);
	}
	return sets;
}

/// @brief Every zoom call the domain makes. Each start zooms both ways, with and without extra scroll right,
///        centre-pinned; the starts at the waveform's start, seven columns in, its end and past its end also zoom
///        with each preferred pin over each pin set. The calls are handed to @p visit one at a time, since there are
///        millions of them.
/// @param visit A callable `void(const NavCase&)`.
template <typename Visit>
constexpr void for_each_zoom_case(Visit&& visit) {
	for (const NavStart& s : nav_starts()) {
		const auto l = static_cast<int64_t>(s.length);
		const bool pinned = s.past_limit || s.scroll == 0 || s.scroll == 7 * s.zoom
		                    || s.scroll == std::max<int64_t>(0, l - 16 * s.zoom);
		for (const int32_t offset : {-1, 1}) {
			for (const bool extra : {false, true}) {
				NavCase base{.length = s.length,
				             .scroll = s.scroll,
				             .zoom = s.zoom,
				             .offset = offset,
				             .preferred = std::nullopt,
				             .extra_right = extra,
				             .pins = {},
				             .view = s.view};
				visit(std::as_const(base));
				if (!pinned) {
					continue;
				}
				for (const auto& pins : nav_pin_sets(s)) {
					for (size_t preferred = 0; preferred < 4; preferred++) {
						NavCase c = base;
						c.preferred = preferred;
						c.pins = pins;
						visit(std::as_const(c));
					}
				}
			}
		}
	}
}

} // namespace deluge::sample_view::test
