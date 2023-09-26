#pragma once
#include "gui/l10n/language.h"
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>

namespace deluge::l10n {
constexpr std::string_view getView(const Language& language, l10n::String string) {
	auto string_opt = language.get(string);
	if (string_opt.has_value()) {
		return string_opt.value();
	}

	if (language.hasFallback()) {
		return getView(language.fallback(), string);
	}

	return built_in::english.get(String::EMPTY_STRING).value(); // EMPTY_STRING
}

inline std::string_view getView(l10n::String string) {
	return getView(*chosenLanguage, string);
}

constexpr const char* get(const Language& language, l10n::String string) {
	return getView(language, string).data();
}

inline const char* get(l10n::String string) {
	return getView(string).data();
}
} // namespace deluge::l10n
