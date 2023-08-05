#include "gui/l10n/l10n.h"
#include "gui/l10n/english.h"
#include "gui/l10n/seven_segment.h"
#include "gui/l10n/strings.h"

namespace deluge::l10n {

static_vector<Language const*, kMaxNumLanguages> languages = {&built_in::english};
Language const* chosenLanguage = &built_in::seven_segment;

} // namespace deluge::l10n

extern "C" {
#include "deluge.h"
char const* l10n_get(l10n_string string) {
	return deluge::l10n::get(static_cast<deluge::l10n::Strings>(string));
}
}
