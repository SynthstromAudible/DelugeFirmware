#pragma once
#include "gui/l10n/l10n.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

// Mixin for a formatted title
class FormattedTitle {
public:
	FormattedTitle(l10n::String format_str, std::optional<uint8_t> arg = std::nullopt)
	    : format_str_(format_str), arg_{arg} {}

	void format(int32_t arg) const {
		title_ = l10n::get(format_str_);
		asterixToInt(title_.data(), arg);
	}

	[[nodiscard]] std::string_view title() const {
		if (arg_.has_value()) {
			format(arg_.value());
			arg_ = std::nullopt;
		}
		return title_;
	}

private:
	l10n::String format_str_;
	mutable std::string title_;
	mutable std::optional<uint8_t> arg_;
};
} // namespace deluge::gui::menu_item
