#include "snapshot.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace deluge::harness {

std::string oled_pbm(std::span<const uint8_t> page_major, uint16_t w, uint16_t h) {
	std::string out = "P1\n" + std::to_string(w) + " " + std::to_string(h) + "\n";
	for (uint16_t y = 0; y < h; y++) {
		for (uint16_t x = 0; x < w; x++) {
			bool on = (page_major[(y >> 3) * w + x] & (1u << (y & 7))) != 0;
			out += on ? '1' : '0';
			if (x + 1 < w) {
				out += ' ';
			}
		}
		out += '\n';
	}
	return out;
}

void StateLines::add(std::string_view line) {
	std::array<char, 48> buf{};
	size_t len = line.size() < buf.size() - 1 ? line.size() : buf.size() - 1;
	std::memcpy(buf.data(), line.data(), len);
	buf[len] = '\0';
	lines.push_back(buf);
}

namespace {

/// @brief True if \p s is exactly \p word followed by a space (i.e. \p word is its first
/// whitespace-delimited token).
bool starts_with_word(std::string_view s, std::string_view word) {
	return s.size() > word.size() && s.compare(0, word.size(), word) == 0 && s[word.size()] == ' ';
}

} // namespace

std::string StateLines::sorted_text() const {
	// Three tiers, per spec G §3's display order: `scene` always leads (insertion order, so a
	// script's nested-scene breadcrumb reads top-down); the bulk of the state (pad/led/anything
	// else `add()`ed directly) is ASCII-sorted so diffs are stable regardless of write order;
	// `indicator`/`lock`/`notice` are singleton-ish status lines that always trail, in the order
	// they were written, rather than competing alphabetically with the sorted bulk.
	std::vector<std::string_view> scenes, general, trailing;
	for (const auto& buf : lines) {
		std::string_view sv(buf.data());
		if (starts_with_word(sv, "scene")) {
			scenes.push_back(sv);
		}
		else if (starts_with_word(sv, "indicator") || starts_with_word(sv, "lock") || starts_with_word(sv, "notice")) {
			trailing.push_back(sv);
		}
		else {
			general.push_back(sv);
		}
	}
	std::sort(general.begin(), general.end());

	std::string out;
	auto emit = [&](std::string_view sv) {
		out.append(sv);
		out.push_back('\n');
	};
	for (auto sv : scenes) {
		emit(sv);
	}
	for (auto sv : general) {
		emit(sv);
	}
	for (auto sv : trailing) {
		emit(sv);
	}
	return out;
}

void add_pad_line(StateLines& lines, uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
	if (r == 0 && g == 0 && b == 0) {
		return; // black: skipped -- only non-black pads are ever shown
	}
	char buf[48];
	std::snprintf(buf, sizeof buf, "pad %u %u %u %u %u", static_cast<unsigned>(x), static_cast<unsigned>(y),
	              static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b));
	lines.add(buf);
}

void add_led_line(StateLines& lines, std::string_view name, bool blink, uint16_t period_ms) {
	char buf[48];
	if (blink) {
		std::snprintf(buf, sizeof buf, "led %.*s blink %u", static_cast<int>(name.size()), name.data(),
		              static_cast<unsigned>(period_ms));
	}
	else {
		std::snprintf(buf, sizeof buf, "led %.*s on", static_cast<int>(name.size()), name.data());
	}
	lines.add(buf);
}

void add_indicator_line(StateLines& lines, bool upper, const uint8_t levels[4]) {
	char buf[48];
	std::snprintf(buf, sizeof buf, "indicator %s %u %u %u %u", upper ? "upper" : "lower",
	              static_cast<unsigned>(levels[0]), static_cast<unsigned>(levels[1]), static_cast<unsigned>(levels[2]),
	              static_cast<unsigned>(levels[3]));
	lines.add(buf);
}

bool write_file(std::string_view dir, std::string_view file, std::string_view contents) {
	// mkdir -p dir, POSIX-only (host tests only -- never runs on device).
	std::string d(dir);
	size_t pos = 0;
	while (pos < d.size()) {
		size_t slash = d.find('/', pos);
		size_t end = (slash == std::string::npos) ? d.size() : slash;
		std::string prefix = d.substr(0, end);
		if (!prefix.empty() && ::mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST) {
			return false;
		}
		if (slash == std::string::npos) {
			break;
		}
		pos = slash + 1;
	}

	std::string path = d;
	if (!path.empty() && path.back() != '/') {
		path += '/';
	}
	path += std::string(file);

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}
	out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	return out.good();
}

} // namespace deluge::harness
