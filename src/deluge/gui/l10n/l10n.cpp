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

extern "C" {
#include "deluge.h"

const size_t l10n_STRING_FOR_USB_DEVICES_MAX = util::to_underlying(deluge::l10n::String::STRING_FOR_USB_DEVICES_MAX);
const size_t l10n_STRING_FOR_USB_DEVICE_DETACHED =
    util::to_underlying(deluge::l10n::String::STRING_FOR_USB_DEVICE_DETACHED);
const size_t l10n_STRING_FOR_USB_HUB_ATTACHED = util::to_underlying(deluge::l10n::String::STRING_FOR_USB_HUB_ATTACHED);
const size_t l10n_STRING_FOR_USB_DEVICE_NOT_RECOGNIZED =
    util::to_underlying(deluge::l10n::String::STRING_FOR_USB_DEVICE_NOT_RECOGNIZED);

char const* l10n_get(size_t string) {
	return deluge::l10n::get(static_cast<deluge::l10n::String>(string));
}
}
