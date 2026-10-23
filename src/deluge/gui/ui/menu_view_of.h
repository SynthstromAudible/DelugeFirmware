#pragma once

/// @file
/// @brief What a menu opens over, as the view shows it, from the clip a legacy view has open.

#include "scenes/services/view_state.h"

class Clip;

namespace deluge::gui {

/// @brief @p clip as the menu's view: its address and track, what plays it, the kit row selected (and whether its note
///        row is in the clip), and the clip's affect-entire.
/// @param clip The clip the menu opens over, or null for a song-level or settings menu.
[[nodiscard]] scenes::services::MenuView menuViewOf(::Clip* clip);

} // namespace deluge::gui
