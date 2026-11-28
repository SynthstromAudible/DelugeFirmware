#pragma once

/// @file
/// @brief Legacy `AudioClipView`'s answers, frozen under `tests/golden/ui/audio_clip_oracle/` before the view was
///        deleted: one table per spec (`<spec>.tsv`, rows as `support/oracle_table.h` reads them) and OLED pages
///        (`pixels/<name>.pbm`).
///
/// Every row and page is a claim, so a spec that reads them checks, in its last example, that it asked every row of its
/// table and read every page under its prefix. The files are reference data: they change only to follow upstream
/// `main`, and the commit names the source it follows.

#include "support/oracle_table.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::spec {

/// @brief Where the audio clip view's frozen answers are.
inline std::filesystem::path audio_clip_oracle_dir() {
	return std::filesystem::path{GOLDEN_ROOT} / "ui" / "audio_clip_oracle";
}

/// @brief `<spec>.tsv`, read once per process; a missing file is an empty table, whose every value is `<no row>`.
inline OracleTable& audio_clip_answers(std::string_view spec) {
	static std::map<std::string, std::unique_ptr<OracleTable>, std::less<>> loaded;
	if (const auto it = loaded.find(spec); it != loaded.end()) {
		return *it->second;
	}
	std::ifstream in(audio_clip_oracle_dir() / (std::string{spec} + ".tsv"));
	std::stringstream text;
	text << in.rdbuf();
	auto table = std::make_unique<OracleTable>(OracleTable::parse(text.str()));
	return *loaded.emplace(std::string{spec}, std::move(table)).first->second;
}

/// @brief The OLED pages, and which of them were read.
class AudioClipPages {
public:
	/// @brief `pixels/<name>.pbm`; empty when there is no such file.
	static std::string page(std::string_view name) {
		read().insert(std::string{name});
		std::ifstream in(audio_clip_oracle_dir() / "pixels" / (std::string{name} + ".pbm"), std::ios::binary);
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}

	/// @brief Every page whose name starts with @p prefix that nothing read, one per line.
	static std::string unread(std::string_view prefix) {
		std::string out;
		std::set<std::string> names;
		std::error_code ec;
		for (const auto& e : std::filesystem::directory_iterator(audio_clip_oracle_dir() / "pixels", ec)) {
			const std::string stem = e.path().stem().string();
			if (e.path().extension() == ".pbm" && stem.starts_with(prefix)) {
				names.insert(stem);
			}
		}
		for (const std::string& n : names) {
			if (!read().contains(n)) {
				out += "pixels/" + n + ".pbm\n";
			}
		}
		return out;
	}

private:
	static std::set<std::string>& read() {
		static std::set<std::string> names;
		return names;
	}
};

} // namespace deluge::ui::scenes::spec
