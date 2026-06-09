#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "util/misc.h"

namespace deluge::l10n {
Language const* chosenLanguage = nullptr;

std::string_view getView(const deluge::l10n::Language& language, deluge::l10n::String string) {
	auto string_opt = language.get(string);
	if (string_opt.has_value()) {
		return string_opt.value();
	}

	if (language.hasFallback()) {
		return getView(language.fallback(), string);
	}

	return built_in::english.get(deluge::l10n::String::EMPTY_STRING).value(); // EMPTY_STRING
}

std::string_view getView(deluge::l10n::String string) {
	return getView(*chosenLanguage, string);
}

const char* get(const Language& language, deluge::l10n::String string) {
	return getView(language, string).data();
}

const char* get(deluge::l10n::String string) {
	return getView(string).data();
}

} // namespace deluge::l10n
