#pragma once

/// @file
/// @brief The plain results `ModKnobs` computes for a gold-knob turn and a knob's indicator, over the knobs' focus.

#include "project/owner_plugin.h"
#include "ui/scenes/bind/knob_focus.h"

#include <cstdint>

namespace deluge::ui::scenes::services {

/// @brief Whether the gold knobs are on a clip rather than the song: `View::knobsOnClipContext`:
///        an owner, in a clip.
/// @param focus What the knobs point at.
/// @return True for a clip's owner; false for the song, an arranger row with no clip, or no focus.
bool is_clip_context(const bind::KnobFocus& focus);

/// @brief What one gold-knob turn on a param did, and what finishing it needs (the popup, the MIDI feedback, the
///        indicator blink, the repaint) -- `View::modEncoderAction_existentParam`
///        (`origin/main:src/deluge/gui/views/view.cpp`).
struct ModTurn {
	/// The param the knob turned, in its plugin, as the popup and MIDI follow name it.
	deluge::project::PluginParam param{};
	/// The knob position before the turn.
	int32_t old_knob_pos = 0;
	/// The position after it -- what the popup shows, and what was written where it differs.
	int32_t new_knob_pos = 0;
	/// The turn wrote a new value (the `newKnobPos == knobPos` early return of main's
	/// `View::modEncoderAction_existentParam` (`origin/main:src/deluge/gui/views/view.cpp`) is the other case).
	bool value_changed = false;
	/// The sound's note tails changed with the value (`View::modEncoderAction_existentParam`
	/// (`origin/main:src/deluge/gui/views/view.cpp`)): the pads repaint.
	bool note_tails_changed = false;
	/// The param shows its middle (`potentiallyMakeItHarderToTurnKnob`'s centre blink).
	bool should_indicate_middle_value = false;
};

/// @brief What a gold knob's indicator reads -- the reads `View::setKnobIndicatorLevel`
///        (`origin/main:src/deluge/gui/views/view.cpp`) makes, from the records and the runtime facts;
///        `ModKnobs::indicator_level` does the arithmetic.
///
/// The three booleans are legacy's own three-way branch: a param the owner holds, a knob with no param (its
/// non-existent arm), and a cable not made yet, shown at its midpoint.
///
/// @warning **When none of those three holds, legacy reads an uninitialised `int32_t knobPos`** (declared in
///          `View::setKnobIndicatorLevel` (`origin/main:src/deluge/gui/views/view.cpp`)); `ModKnobs::indicator_level`
///          declares nothing for that knob.
struct ModIndicatorInfo {
	/// The knob names a param (or a MIDI lane, shown at 0 until it is made).
	bool has_auto_param = false;
	/// The param, in its plugin.
	deluge::project::PluginParam param{};
	/// The value's knob position, at the held region or the playhead.
	int32_t knob_pos = 0;
	/// The param is bipolar.
	bool bipolar = false;
	/// A stutter rate that is quantized, whose position snaps to its buckets while no stutter plays.
	bool quantized_stutter = false;

	/// The knob has no param on this button (the song's compressor knobs, a chain's empty knob).
	bool non_existent_param = false;
	/// Its position, valid iff `non_existent_param`.
	int32_t non_existent_knob_pos = 0;
	/// A cable the sound does not hold yet, or a MIDI knob that sends nothing, shown at its midpoint.
	bool patch_cable_default = false;
};

} // namespace deluge::ui::scenes::services
