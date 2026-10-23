#pragma once

/// @file
/// @brief The plain results `ModKnobs` computes for a gold-knob turn and a knob's indicator, over the knobs' focus.

#include "definitions_cxx.hpp" // ActionResult (a root-level header; see scenes/CMakeLists.txt)
#include "modulation/params/param.h"
#include "scenes/bind/knob_focus.h"

#include <cstdint>

namespace deluge::scenes::services {

/// @brief Whether the gold knobs are on a clip rather than the song: `View::knobsOnClipContext`
///        (`view.cpp:1749-1756`) -- an owner, in a clip.
/// @param focus What the knobs point at.
/// @return True for a clip's owner; false for the song, an arranger row with no clip, or no focus.
bool is_clip_context(const bind::KnobFocus& focus);

/// @brief What one gold-knob turn on a param did, and what finishing it needs (the popup, the MIDI feedback, the
///        indicator blink, the repaint) -- `View::modEncoderAction_existentParam` (`view.cpp:853-948`).
struct ModTurn {
	/// The param's kind, as the popup and MIDI follow name it.
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	/// The param's id in its kind's numbering (a cable's packed word, a CC number).
	int32_t param_id = 0;
	/// The knob position before the turn.
	int32_t old_knob_pos = 0;
	/// The position after it -- what the popup shows, and what was written where it differs.
	int32_t new_knob_pos = 0;
	/// The turn wrote a new value (legacy's `:902` early return is the other case).
	bool value_changed = false;
	/// The sound's note tails changed with the value (`view.cpp:946`): the pads repaint.
	bool note_tails_changed = false;
	/// The param shows its middle (`potentiallyMakeItHarderToTurnKnob`'s detent).
	bool should_indicate_middle_value = false;
};

/// @brief What a gold knob's indicator reads -- the reads `View::setKnobIndicatorLevel` (`view.cpp:1420-1485`)
///        makes, from the records and the runtime facts; `ModKnobs::indicator_level` does the arithmetic.
///
/// The three booleans are legacy's own three-way branch: a param the owner holds, a knob with no param (its
/// non-existent arm), and a cable not made yet, shown at its midpoint.
///
/// @warning **When none of those three holds, legacy reads an uninitialised `int32_t knobPos`** (declared
///          `view.cpp:1427`, consumed `:1485`); `ModKnobs::indicator_level` declares nothing for that knob.
struct ModIndicatorInfo {
	/// The knob names a param (or a MIDI lane, shown at 0 until it is made).
	bool has_auto_param = false;
	/// The param's kind.
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	/// The param's id in its kind's numbering.
	int32_t param_id = 0;
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

} // namespace deluge::scenes::services
