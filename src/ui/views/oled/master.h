#pragma once
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The master output as a screen: a triggered scope, and L against R as an X/Y figure.

namespace deluge::ui::views::oled {

/// @brief The master scope.
struct MasterScope {
	std::string_view title;          ///< "MASTER".
	std::span<const int8_t> samples; ///< Triggered and decimated by the caller, one per column.
};

/// @brief The title over `scope` across the body.
void master_scope(widgets::oled::OledFrame& f, const MasterScope& d);

/// @brief The master's stereo as an X/Y figure.
struct MasterScopeXy {
	std::string_view title;        ///< "MASTER".
	std::string_view note;         ///< "X/Y": the title's note.
	std::span<const int8_t> left;  ///< L, on x ...
	std::span<const int8_t> right; ///< ... against R, on y.
};

/// @brief `title_note(title, note)` over `scope_xy` in the body.
void master_scope_xy(widgets::oled::OledFrame& f, const MasterScopeXy& d);

} // namespace deluge::ui::views::oled
