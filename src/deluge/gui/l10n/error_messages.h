#pragma once

/// @file
/// @brief The text the display shows for an error code, in the chosen language.

#include "base/error.h"

#include <string_view>

namespace deluge::l10n {

/// @brief The text shown for @p error.
/// @param error The code to describe.
/// @return The chosen language's text for it; the generic error text for a code with no text of its own.
[[nodiscard]] std::string_view error_message(Error error);

} // namespace deluge::l10n
