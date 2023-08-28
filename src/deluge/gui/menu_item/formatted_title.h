#pragma once
#include "gui/l10n/l10n.h"
#include <utility>

namespace deluge::gui::menu_item {

// Mixin for a formatted title
class FormattedTitle {
public:
	FormattedTitle(l10n::String format_str) : format_str_(format_str) {}

	void format(int32_t arg) {
		title_ = l10n::get(format_str_);
		std::replace(title_.begin(), title_.end(), '*', (char)('0' + arg));
	}

	[[nodiscard]] std::string_view title() const { return title_; }

private:
	l10n::String format_str_;
	std::string title_;
};
} // namespace deluge::gui::menu_item
