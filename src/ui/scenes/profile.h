#pragma once
#include "ui/shell/profile.h"

namespace deluge::ui::scenes {

/// @brief The board profile `deluge_ui_scenes` and every scenes spec build a `Shell` /
///        `RunOptions::profile` from.
///
/// `ui::shell::deluge_profile()` field-for-field, with `.text = text_lookup` and
/// `.error_text = error_lookup` (`l10n_bridge.h`) filled in. The shell's own profile stays
/// text-less on purpose (`src/ui/shell/profiles/deluge.cpp`: "no l10n bridge... yet") --
/// wiring it to the firmware's `l10n::String` table is the scenes layer's job, since `src/ui/shell`
/// may not include `gui/l10n/`. `BoardProfile` is spans and function pointers, so this copy is
/// cheap and owns nothing.
/// @return The profile, viewing static storage.
const ui::shell::BoardProfile& deluge_profile();

} // namespace deluge::ui::scenes
