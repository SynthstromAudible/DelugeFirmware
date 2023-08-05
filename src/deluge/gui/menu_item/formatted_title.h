#pragma once
#include "gui/l10n/l10n.hpp"
#include <fmt/core.h>
#include <utility>

namespace deluge::gui::menu_item {

// Mixin for a formatted title
class FormattedTitle {
public:
	FormattedTitle(const fmt::format_string<int32_t>& format_str) : format_str_(format_str) {}

	void format(int32_t arg) { title_ = fmt::vformat(format_str_.get(), fmt::make_format_args(arg)); }

	[[nodiscard]] std::string_view title() const { return title_; }

private:
	fmt::format_string<int32_t> format_str_;
	std::string title_;
};
}
