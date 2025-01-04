#pragma once
#include <string>

struct case_insensitive_char_traits : public std::char_traits<char> {
	static bool eq(char c1, char c2) { return std::tolower(c1) == std::tolower(c2); }
	static bool ne(char c1, char c2) { return std::tolower(c1) != std::tolower(c2); }
	static bool lt(char c1, char c2) { return std::tolower(c1) < std::tolower(c2); }
	static int compare(const char* str1, const char* str2, size_t n) {
		for (size_t i = 0; i < n; ++i) {
			if (std::tolower(str1[i]) < std::tolower(str2[i])) {
				return -1;
			}
			if (std::tolower(str1[i]) > std::tolower(str2[i])) {
				return 1;
			}
		}
		return 0;
	}
	static const char* find(const char* s, int n, char a) {
		for (size_t i = 0; i < n; ++i) {
			if (std::tolower(s[i]) != std::tolower(a)) {
				break;
			}
		}
		return s;
	}
};

using path_view = std::basic_string_view<char, case_insensitive_char_traits>;

struct Path {
	static constexpr bool isAudioFile(path_view filename) {
		if (filename[0] == '.') {
			return false; // macOS invisible files
		}
		return filename.ends_with(".wav") || isAIFF(filename);
	}

	static constexpr bool isAIFF(path_view filename) {
		return filename.ends_with(".aiff") || filename.ends_with(".aif");
	}
};
