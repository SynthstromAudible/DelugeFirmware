#pragma once

/// @file
/// @brief The gold knobs' focus, as addresses, from the engine stack a legacy view points them at.

#include "deluge-project/knob_focus.h"

class ModelStackWithThreeMainThings;
class Song;

namespace deluge::gui {

/// @brief What @p stack's mod controllable is, as the knobs' focus: the song's chain, a synth or a kit row (with its
///        clip and note row), a kit's or an audio track's chain, a MIDI track; none for a CV track or no target. A take
///        the arrangement records is named by the session clip it records, as the focus keeps.
/// @param song  The song the stack is in.
/// @param stack The view's stack: its timeline counter, mod controllable and note row.
/// @return The focus's owner, with no region, compressor view or VU meter: those are the knobs' own view state.
[[nodiscard]] project::KnobFocus knobFocusFromStack(::Song& song, const ModelStackWithThreeMainThings& stack);

} // namespace deluge::gui
