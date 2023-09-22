#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"

namespace deluge::l10n {
Language const* chosenLanguage = nullptr;

} // namespace deluge::l10n

extern "C" {
#include "deluge.h"
char const* l10n_get(l10n_string string) {
	return deluge::l10n::get(static_cast<deluge::l10n::String>(string));
}
}
