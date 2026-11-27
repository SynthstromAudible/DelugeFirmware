#pragma once

/// @file
/// @brief The gold knobs' focus, as addresses, from the engine stack a legacy view points them at.

#include "ui/controls/knob_focus.h"

class Clip;
class ModelStackWithThreeMainThings;
class Song;

namespace deluge::gui {

/// @brief What @p stack's owner is, as the knobs' focus: the song's chain, a synth or a kit row (with its
///        clip and note row), a kit's or an audio track's chain, a MIDI track; none for a CV track or no target. A take
///        the arrangement records is named by the session clip it records, as the focus keeps.
/// @param song  The song the stack is in.
/// @param stack The view's stack: its timeline counter, owner and note row.
/// @return The focus's owner, with no region, compressor view or VU meter: those are the knobs' own view state.
[[nodiscard]] deluge::ui::controls::KnobFocus knobFocusFromStack(::Song& song,
                                                                 const ModelStackWithThreeMainThings& stack);

/// @brief What @p clip's owner is, as the focus MIDI follow's CCs move params of: a synth, a kit with affect-entire, an
///        audio track, or a kit's selected sound row; none for a kit whose selected drum is not a sound with a note row
///        in @p clip, a clip with no output, or an output with no id.
/// @param clip The clip follow is on: the selected or active clip.
[[nodiscard]] deluge::ui::controls::KnobFocus knobFocusOfClip(::Clip& clip);

} // namespace deluge::gui
