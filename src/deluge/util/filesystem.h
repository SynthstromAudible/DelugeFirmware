#pragma once
#include "util/filesystem.h"
#include "util/filesystem/basic_path.h"
#include "util/string.h"

namespace deluge::filesystem {
/// @brief Deluge's filesystem path type
/// @note this is case insensitive to match FAT32 filesystems
using Path = BasicPath<ci_string>;

/// @brief Tests whether a filename is an AIFF via the extension
static constexpr bool isAIFF(const char* filename) {
	ci_string_view ci_filename = ci_string_view{filename};
	return ci_filename.ends_with(".aiff") || ci_filename.ends_with(".aif");
}

/// @brief Tests whether a filename is a WAV file via the extension
static constexpr bool isWAV(const char* filename) {
	return ci_string_view{filename}.ends_with(".wav");
}

/// @brief Deluge's audio filetype filter
static constexpr bool isAudioFile(const char* filename) {
	if (filename[0] == '.') {
		return false; // macOS invisible files
	}
	return isWAV(filename) || isAIFF(filename);
}

} // namespace deluge::filesystem
