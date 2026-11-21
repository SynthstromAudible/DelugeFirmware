#pragma once

/// @file
/// @brief A patch source's names as the UI shows them: `sourceToStringShort` and `getSourceDisplayName`
///        (`util/functions.cpp`), which `src/scenes` may not include.

#include "gui/l10n/strings.h"
#include "synth/modulation/patch_source.h"

#include <string_view>

namespace deluge::scenes::services {

/// @brief @p source's four-character name, as the mod matrix's columns and a cable's popup show it.
/// @return Its short name, a string literal, or `"----"` for anything without one.
[[nodiscard]] const char* source_short_name(deluge::synth::PatchSource source);

/// @brief The string @p source's full name is, as a lone cable's name shows it.
/// @return Its string id, or the unknown source's.
[[nodiscard]] deluge::l10n::String source_display_string(deluge::synth::PatchSource source);

} // namespace deluge::scenes::services
