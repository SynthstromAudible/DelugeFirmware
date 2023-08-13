#pragma once
#include "gui/l10n/english.h"
#include "gui/l10n/language.h"
#include "gui/l10n/seven_segment.h"
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <initializer_list>

namespace deluge::l10n {
constexpr std::string_view getView(size_t language_idx, l10n::String string) {
	return languages.at(language_idx)->get(string);
}

inline std::string_view getView(l10n::String string) {
	return chosenLanguage->get(string);
}

constexpr const char* get(size_t language_idx, l10n::String string) {
	return languages.at(language_idx)->get(string).data();
}

inline const char* get(l10n::String string) {
	return chosenLanguage->get(string).data();
}
} // namespace deluge::l10n
