#include "ui/scenes/l10n_bridge.h"

#include "base/error.h"
#include "gui/l10n/error_messages.h"
#include "gui/l10n/l10n.h"

namespace deluge::ui::scenes {

std::string_view text_lookup(uint16_t string_id) {
	return l10n::getView(static_cast<l10n::String>(string_id));
}

std::string_view error_lookup(int16_t error_code) {
	return l10n::error_message(static_cast<Error>(error_code));
}

} // namespace deluge::ui::scenes
