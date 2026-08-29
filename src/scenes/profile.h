#pragma once
#include "shell/profile.h"

namespace deluge::scenes {

/// @brief The board profile `deluge_scenes` and every scenes spec build a `Shell` /
///        `RunOptions::profile` from.
///
/// `ui::shell::deluge_profile()` field-for-field (ruling Q3), with `.text = text_lookup` and
/// `.error_text = error_lookup` (`l10n_bridge.h`) filled in. The shell's own profile stays
/// text-less on purpose (`src/shell/profiles/deluge.cpp:143-144`: "no l10n bridge... yet") --
/// wiring it to the firmware's `l10n::String` table is the scenes layer's job, since `src/shell`
/// may not include `gui/l10n/`. `BoardProfile` is spans and function pointers, so this copy is
/// cheap and owns nothing.
/// @return The profile, viewing static storage.
const ui::shell::BoardProfile& deluge_profile();

} // namespace deluge::scenes
