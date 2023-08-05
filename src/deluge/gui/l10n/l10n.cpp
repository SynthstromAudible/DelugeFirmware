#include "gui/l10n/l10n.hpp"
#include "gui/l10n/seven_segment.hpp"
#include "gui/l10n/strings.hpp"

namespace deluge::l10n {

Language chosenLanguage = deluge::l10n::Language::SevenSegment;

}

extern "C" {
#include "gui/l10n/l10n.h"

char const* l10n_get(l10n_string string) {
	return deluge::l10n::get(static_cast<deluge::l10n::Strings>(string));
}
}
