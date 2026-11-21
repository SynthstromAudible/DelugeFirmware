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
#include <set>
#include <tuple>
#include <vector>

namespace deluge::sample_view::test {

/// @brief A remembered view a waveform opens at: the open's inputs.
struct NavOpen {
	uint64_t length{0};
	int64_t scroll{0};
	int64_t zoom{0}; ///< 0 for none remembered
};

/// @brief A view the new type can be in (an open's result), which each operation starts from.
struct NavStart {
	uint64_t length{0};
	int64_t scroll{0};
	int64_t zoom{1};
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
};

/// @brief The pins' zone order: start, loop start, loop end, end. The loop end and end pin by their right edge.
inline constexpr std::array<bool, 4> kRightEdge{false, false, true, true};

[[nodiscard]] inline std::vector<uint64_t> nav_lengths() {
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

[[nodiscard]] inline std::vector<int64_t> nav_zooms() {
	std::set<int64_t> zooms;
	const auto add = [&zooms](int64_t z) { zooms.insert(std::clamp<int64_t>(z, 1, int64_t{1} << 30)); };
	for (int32_t k = 0; k <= 28; k++) {
		const int64_t p = int64_t{1} << k;
		const auto q = static_cast<int64_t>(static_cast<double>(p) * 0.707);
		for (int64_t d = -1; d <= 1; d++) {
			add(p + d);
			add(q + d);
		}
	}
	return {zooms.begin(), zooms.end()};
}

[[nodiscard]] inline std::vector<int64_t> nav_scrolls(uint64_t length, int64_t zoom) {
	const auto l = static_cast<int64_t>(length);
	return {-zoom, -1, 0, 1, 15, 16, zoom - 1, zoom, 7 * zoom, l - 16 * zoom, l - 1, l};
}

/// @brief Every open the domain makes: each length, each zoom (and none), each scroll.
[[nodiscard]] inline std::vector<NavOpen> nav_opens() {
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

/// @brief The distinct views the opens reach, each an operation's start.
[[nodiscard]] inline std::vector<NavStart> nav_starts() {
	std::set<std::tuple<uint64_t, int64_t, int64_t>> seen;
	std::vector<NavStart> starts;
	for (const NavOpen& o : nav_opens()) {
		const WaveformView v = WaveformView::open(
		    o.length, o.zoom == 0 ? std::nullopt : std::optional{Window{.scroll = o.scroll, .zoom = o.zoom}});
		if (seen.insert({v.length(), v.scroll(), v.zoom()}).second) {
			starts.push_back({.length = v.length(), .scroll = v.scroll(), .zoom = v.zoom()});
		}
	}
	return starts;
}

/// @brief A pin set at @p column of @p start's view, or unset.
[[nodiscard]] inline MarkerPin nav_pin(const NavStart& start, size_t marker, std::optional<int64_t> column) {
	if (!column) {
		return {.position = 0, .column = std::nullopt, .right_edge = kRightEdge[marker]};
	}
	const int64_t position = start.scroll + *column * start.zoom + (kRightEdge[marker] ? start.zoom : 0);
	return {.position = position, .column = column, .right_edge = kRightEdge[marker]};
}

/// @brief The pin sets a zoom from @p start is tried with: each pin alone over the columns {unset, -1, 0, 7, 8, 15,
///        16}; all four on screen over {0, 7, 8, 15} in each rotation; and all four over {9, 7, -1, 16} in each
///        rotation, where two pins compete while others are off screen.
[[nodiscard]] inline std::vector<std::array<MarkerPin, 4>> nav_pin_sets(const NavStart& start) {
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
///        centre-pinned; the starts at the waveform's start, seven columns in and its end also zoom with each
///        preferred pin over each pin set.
[[nodiscard]] inline std::vector<NavCase> nav_zoom_cases() {
	std::vector<NavCase> cases;
	for (const NavStart& s : nav_starts()) {
		const auto l = static_cast<int64_t>(s.length);
		const bool pinned =
		    s.scroll == 0 || s.scroll == 7 * s.zoom || s.scroll == std::max<int64_t>(0, l - 16 * s.zoom);
		for (const int32_t offset : {-1, 1}) {
			for (const bool extra : {false, true}) {
				NavCase base{.length = s.length,
				             .scroll = s.scroll,
				             .zoom = s.zoom,
				             .offset = offset,
				             .preferred = std::nullopt,
				             .extra_right = extra,
				             .pins = {}};
				cases.push_back(base);
				if (!pinned) {
					continue;
				}
				for (const auto& pins : nav_pin_sets(s)) {
					for (size_t preferred = 0; preferred < 4; preferred++) {
						NavCase c = base;
						c.preferred = preferred;
						c.pins = pins;
						cases.push_back(c);
					}
				}
			}
		}
	}
	return cases;
}

} // namespace deluge::sample_view::test
