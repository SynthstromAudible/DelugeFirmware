// clean_roundtrip <song or directory>... — the clean-roundtrip gate over songs on disk, built from the model and
// serialization libraries only. Prints one line per song (and one per field its clean load clamped); exits 0 when
// every song passes (files that hold no song are skipped), 1 when one does not, 77 when there are no songs.
#include "clean/clean_check.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string extension_of(const fs::path& path) {
	std::string extension = path.extension().string();
	std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
	return extension;
}
std::string read(const fs::path& path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}
} // namespace

int main(int argc, char** argv) {
	std::vector<fs::path> songs;
	for (int i = 1; i < argc; i++) {
		const fs::path root = argv[i];
		if (fs::is_directory(root)) {
			for (const auto& entry : fs::recursive_directory_iterator(root)) {
				const std::string extension = extension_of(entry.path());
				if (entry.is_regular_file() && (extension == ".xml" || extension == ".json")) {
					songs.push_back(entry.path());
				}
			}
		}
		else {
			songs.push_back(root);
		}
	}
	std::ranges::sort(songs);
	int checked = 0;
	int failed = 0;
	for (const fs::path& song : songs) {
		auto result = deluge::project::check_clean(read(song), extension_of(song) == ".json");
		if (!result && result.error() == "has no song") {
			std::printf("SKIP    %s\n", song.c_str());
			continue;
		}
		++checked;
		if (!result) {
			++failed;
			std::printf("FAIL    %s: %s\n", song.c_str(), result.error().c_str());
			continue;
		}
		std::printf("OK      %s (%zu presets, %zu clamped)\n", song.c_str(), result->presets, result->clamped.size());
		for (const std::string& path : result->clamped) {
			std::printf("CLAMPED %s: %s\n", song.c_str(), path.c_str());
		}
	}
	std::printf("%s\nOK=%d FAIL=%d\n", std::string(60, '=').c_str(), checked - failed, failed);
	if (checked == 0) {
		return 77;
	}
	return failed == 0 ? 0 : 1;
}
