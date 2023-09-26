#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "util/misc.h"

namespace deluge::l10n {
Language const* chosenLanguage = nullptr;

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
