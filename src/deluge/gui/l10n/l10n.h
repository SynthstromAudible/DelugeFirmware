#pragma once
#include "gui/l10n/english.h"
#include "gui/l10n/language.h"
#include "gui/l10n/seven_segment.h"
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <initializer_list>

namespace deluge::l10n {
constexpr const char* get(size_t language_idx, l10n::Strings string) {
	return languages.at(language_idx)->get(string);
}

inline const char* get(l10n::Strings string) {
	return chosenLanguage->get(string);
}
} // namespace deluge::l10n
