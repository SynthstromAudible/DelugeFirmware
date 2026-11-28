#pragma once

/// @file
/// @brief The freezer for legacy `AudioClipView`'s answers: `LEGACY_ONLY` holds the legacy half of an example, and
///        `keep` either writes what legacy answered (with `AUDIO_CLIP_ORACLE_OUT` naming a directory) or checks it
///        against the frozen row.

#include "support/audio_clip_answers.h"

#include "cppspec.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

/// @brief The legacy half of an example.
#define LEGACY_ONLY(...)                                                                                               \
	do {                                                                                                               \
		__VA_ARGS__                                                                                                    \
	} while (false)

namespace deluge::ui::scenes::spec {

/// @brief The directory a freeze writes to, or null when this run checks instead.
inline const char* audio_clip_freeze_dir() {
	return std::getenv("AUDIO_CLIP_ORACLE_OUT");
}

/// @brief Add a row to @p spec's table in the freeze directory, rewriting the file.
inline void freeze_row(std::string_view spec, std::string_view path, std::string_view column, std::string_view value) {
	static std::map<std::string, std::string, std::less<>> tables;
	std::string& text = tables[std::string{spec}];
	text += oracle_escape(path) + '\t' + oracle_escape(column) + '\t' + oracle_escape(value) + '\n';
	std::ofstream out(std::filesystem::path{audio_clip_freeze_dir()} / (std::string{spec} + ".tsv"), std::ios::binary);
	out << text;
}

/// @brief Freeze legacy's @p value as (@p path, @p column) of @p spec's table, or check it against the frozen row.
template <typename Self>
void keep(Self& self, std::string_view spec, std::string_view path, std::string_view column, const std::string& value) {
	if (audio_clip_freeze_dir() != nullptr) {
		freeze_row(spec, path, column, value);
		return;
	}
	expect(audio_clip_answers(spec).value(path, column)).to_equal(value);
}

/// @brief Freeze legacy's OLED page @p pbm as `pixels/<name>.pbm`, or check it against the frozen page.
template <typename Self>
void keep_page(Self& self, std::string_view name, const std::string& pbm) {
	if (audio_clip_freeze_dir() != nullptr) {
		const std::filesystem::path dir = std::filesystem::path{audio_clip_freeze_dir()} / "pixels";
		std::filesystem::create_directories(dir);
		std::ofstream out(dir / (std::string{name} + ".pbm"), std::ios::binary);
		out << pbm;
		return;
	}
	expect(AudioClipPages::page(name)).to_equal(pbm);
}

} // namespace deluge::ui::scenes::spec
