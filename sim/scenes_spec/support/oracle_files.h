#pragma once

/// @file
/// @brief The panels and pad images beside the menu reference tables: `pixels/<name>.pbm` and `pads/<name>.txt` under
///        `tests/golden/ui/menu_oracle/`, read by name.
///
/// A pad image is eight lines, one per pad row from the top (y = 7) to the bottom (y = 0), each sixteen `r,g,b`
/// triples separated by spaces, one per main-grid column from x = 0. Like the table rows, the files are reference
/// data: a file changes only to follow upstream `main`'s source, and the commit names the source it follows.
///
/// Every file is a claim, so a spec that reads them also checks it read every file under its own prefix (`unread`):
/// a panel the spec no longer compares fails as loudly as a wrong one.

#include "support/oracle_table.h"

#include "board_layout.hpp"
#include "gfx/grid/rgb.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace deluge::scenes::spec {

/// @brief The main grid's pads, indexed `[y][x]` with y = 0 the bottom row.
using OraclePads = std::array<std::array<RGB, kDisplayWidth>, kDisplayHeight>;

/// @brief @p pads as a pad-image file holds it.
inline std::string oracle_pads_text(const OraclePads& pads) {
	std::string out;
	for (int32_t y = kDisplayHeight - 1; y >= 0; y--) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			const RGB& c = pads[y][x];
			out += (x == 0 ? "" : " ") + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b);
		}
		out += "\n";
	}
	return out;
}

/// @brief The pads @p text holds, or nothing when it is not eight lines of sixteen `r,g,b` triples in 0..255.
inline std::optional<OraclePads> oracle_pads_parse(std::string_view text) {
	OraclePads pads{};
	std::istringstream in{std::string{text}};
	std::string line;
	for (int32_t y = kDisplayHeight - 1; y >= 0; y--) {
		if (!std::getline(in, line)) {
			return std::nullopt;
		}
		std::istringstream cells{line};
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			int32_t r = -1;
			int32_t g = -1;
			int32_t b = -1;
			char comma1 = 0;
			char comma2 = 0;
			if (!(cells >> r >> comma1 >> g >> comma2 >> b) || comma1 != ',' || comma2 != ',' || r < 0 || r > 255
			    || g < 0 || g > 255 || b < 0 || b > 255) {
				return std::nullopt;
			}
			pads[y][x] = RGB{.r = static_cast<uint8_t>(r), .g = static_cast<uint8_t>(g), .b = static_cast<uint8_t>(b)};
		}
		std::string rest;
		if (cells >> rest) {
			return std::nullopt;
		}
	}
	if (std::getline(in, line) && !line.empty()) {
		return std::nullopt;
	}
	return pads;
}

/// @brief The panels and pad images, and which of them a spec has read.
class OracleFiles {
public:
	/// @brief `pixels/<name>.pbm`; empty when there is no such file.
	static std::string pixels(std::string_view name) { return read_file("pixels", std::string{name} + ".pbm"); }

	/// @brief `pads/<name>.txt`; nothing when there is no such file or it is not a pad image.
	static std::optional<OraclePads> pads(std::string_view name) {
		const std::string text = read_file("pads", std::string{name} + ".txt");
		return text.empty() ? std::nullopt : oracle_pads_parse(text);
	}

	/// @brief "<dir>/<file>" of every file in `pixels/` and `pads/` whose name starts with @p prefix that nothing read,
	///        one per line, sorted.
	static std::string unread(std::string_view prefix) {
		std::set<std::string> on_disk;
		for (const char* dir : {"pixels", "pads"}) {
			const std::filesystem::path path = std::filesystem::path{oracle_dir()} / dir;
			if (!std::filesystem::is_directory(path)) {
				continue;
			}
			for (const auto& entry : std::filesystem::directory_iterator{path}) {
				const std::string file = entry.path().filename().string();
				if (file.starts_with(prefix)) {
					on_disk.insert(std::string{dir} + "/" + file);
				}
			}
		}
		std::string out;
		for (const std::string& f : on_disk) {
			if (!read_names().contains(f)) {
				out += f + "\n";
			}
		}
		return out;
	}

private:
	static std::set<std::string>& read_names() {
		static std::set<std::string> names;
		return names;
	}

	static std::string read_file(std::string_view dir, const std::string& file) {
		const std::string relative = std::string{dir} + "/" + file;
		std::ifstream in(oracle_dir() + "/" + relative);
		if (!in) {
			return {};
		}
		read_names().insert(relative);
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}
};

} // namespace deluge::scenes::spec
