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
	std::array<char, kStateLineBytes> buf{};
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

/// @brief `.state` category order (controller ruling on spec G §3): a fixed category order,
/// `scene` first, then `led`, `pad`, `indicator`, `lock`, `notice`; anything with an
/// unrecognized first word sorts last. `scene` keeps insertion order within its category;
/// every other category (including `kOther`) is ASCII-sorted within itself.
enum Category : int { kScene = 0, kLed, kPad, kIndicator, kLock, kNotice, kOther, kCategoryCount };

Category category_of(std::string_view s) {
	if (starts_with_word(s, "scene")) {
		return kScene;
	}
	if (starts_with_word(s, "led")) {
		return kLed;
	}
	if (starts_with_word(s, "pad")) {
		return kPad;
	}
	if (starts_with_word(s, "indicator")) {
		return kIndicator;
	}
	if (starts_with_word(s, "lock")) {
		return kLock;
	}
	if (starts_with_word(s, "notice")) {
		return kNotice;
	}
	return kOther;
}

} // namespace

std::string StateLines::sorted_text() const {
	std::vector<std::string_view> buckets[kCategoryCount];
	for (const auto& buf : lines) {
		std::string_view sv(buf.data());
		buckets[category_of(sv)].push_back(sv);
	}
	// scene (bucket 0) keeps insertion order; every other category is ASCII-sorted within itself.
	for (int c = kLed; c < kCategoryCount; c++) {
		std::sort(buckets[c].begin(), buckets[c].end());
	}

	std::string out;
	auto emit = [&](std::string_view sv) {
		out.append(sv);
		out.push_back('\n');
	};
	for (const auto& bucket : buckets) {
		for (auto sv : bucket) {
			emit(sv);
		}
	}
	return out;
}

void add_pad_line(StateLines& lines, uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b) {
	if (r == 0 && g == 0 && b == 0) {
		return; // black: skipped -- only non-black pads are ever shown
	}
	char buf[kStateLineBytes];
	std::snprintf(buf, sizeof buf, "pad %u %u %u %u %u", static_cast<unsigned>(x), static_cast<unsigned>(y),
	              static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b));
	lines.add(buf);
}

void add_led_line(StateLines& lines, std::string_view name, bool blink, uint16_t period_ms) {
	char buf[kStateLineBytes];
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
	char buf[kStateLineBytes];
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
