/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// model_equality <song or directory>... — the model-equality gate over songs on disk, built from the model and
// serialization libraries only. Prints one line per song; exits 0 when every song survives the JSON round trip (files
// that are not songs are skipped), 1 when one does not, 77 when there are no songs.

#include "equality/equality.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool is_song(const fs::path& path) {
	std::string extension = path.extension().string();
	std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
	return extension == ".xml" || extension == ".json";
}

} // namespace

int main(int argc, char** argv) {
	std::vector<fs::path> songs;
	for (int i = 1; i < argc; i++) {
		const fs::path root = argv[i];
		if (fs::is_directory(root)) {
			for (const auto& entry : fs::recursive_directory_iterator(root)) {
				if (entry.is_regular_file() && is_song(entry.path())) {
					songs.push_back(entry.path());
				}
			}
		}
		else if (fs::is_regular_file(root)) {
			songs.push_back(root);
		}
	}
	std::ranges::sort(songs);
	if (songs.empty()) {
		std::printf("no songs\n");
		return 77;
	}
	int passed = 0;
	int failed = 0;
	int skipped = 0;
	for (const fs::path& song : songs) {
		std::ifstream in(song, std::ios::binary);
		std::stringstream text;
		text << in.rdbuf();
		const bool isJson = song.extension() == ".Json" || song.extension() == ".json";
		auto result = deluge::project::check_equality(text.str(), isJson);
		if (result) {
			passed++;
			std::printf("OK      %s\n", song.c_str());
		}
		else if (result.error() == "does not parse" || result.error() == "has no song") {
			skipped++; // not a song: a preset in the folder, or a file that is not a document
			std::printf("skip    %s (%s)\n", song.c_str(), result.error().c_str());
		}
		else {
			failed++;
			std::printf("FAIL    %s: %s\n", song.c_str(), result.error().c_str());
		}
	}
	std::printf("OK=%d FAIL=%d SKIP=%d\n", passed, failed, skipped);
	return failed == 0 ? 0 : 1;
}
