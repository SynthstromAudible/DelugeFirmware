#pragma once
#include <utility>
#include <fmt/core.h>

#include "util/string.h"


// Mixin for a formatted title
class FormattedTitle {
public:
	FormattedTitle(deluge::string format_str) : format_str_(std::move(format_str)) {}

	template <class... Args>
	void format(Args&&... args) {
		title_ = fmt::format(fmt::runtime(format_str_), args...);
	}

	[[nodiscard]] const deluge::string& title() const { return title_; }

private:
	deluge::string format_str_;
	deluge::string title_;
};
