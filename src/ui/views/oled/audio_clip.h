#pragma once
#include "ui/widgets/core/state.h"
#include "ui/widgets/oled/frame.h"

#include <string_view>

/// @file
/// @brief The audio clip view's screen: its output's type and name, and the clip's line under it.
///
/// Every word arrives finished (formatted and localised by the scene); a view formats nothing.

namespace deluge::ui::views::oled {

/// @brief What the audio clip view shows.
struct AudioClipName {
	std::string_view type_text; ///< The output's type, as `track_kind_name` gives it.
	std::string_view name;      ///< The output's name; empty for an unnamed output.
	std::string_view info;      ///< The clip's line: "Section N" for an unnamed clip, "N: name" for a named one.
};

/// @brief `View::drawOutputNameFromDetails`' AUDIO arm with no blink: `output_name` with `name_empty` false (an
///        unnamed audio output draws its type and an empty name, matching none of legacy's slot substitutions); then,
///        under a named output only, the clip's line 14 rows below the name, centred. A name or a line too wide for
///        the panel side-scrolls (legacy's side-scrollers 0 and 1), which reads the frame's clock and arms its gate.
/// @param f           The frame.
/// @param d           What to show.
/// @param name_scroll The name's side-scroller: the scene's.
/// @param info_scroll The clip line's side-scroller: the scene's.
void audio_clip_name(widgets::oled::OledFrame& f, const AudioClipName& d, widgets::core::ScrollText& name_scroll,
                     widgets::core::ScrollText& info_scroll);

} // namespace deluge::ui::views::oled
