#pragma once
#include "gui/l10n/english.hpp"
#include "gui/l10n/l10n.hpp"
#include "gui/l10n/seven_segment.hpp"
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <initializer_list>

namespace deluge::l10n {

// Available languages
enum class Language { SevenSegment, English };

extern Language chosenLanguage;

constexpr const char* get(Language language, l10n::Strings string) {
	const auto s_idx = util::to_underlying(string);
	switch (language) {
		// add other languages using switches here
	case Language::SevenSegment:
		return language_map::seven_segment[s_idx];
	default:
		return language_map::english[s_idx];
	}
}

inline const char* get(l10n::Strings string) {
	return get(chosenLanguage, string);
}
} // namespace deluge::l10n
