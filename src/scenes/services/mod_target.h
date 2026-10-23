#pragma once

/// @file
/// @brief The plain results `ModKnobs` computes for a gold-knob turn and a knob's indicator, over the knobs' focus.

#include "definitions_cxx.hpp" // ActionResult (a root-level header; see scenes/CMakeLists.txt)
#include "modulation/params/param.h"
#include "scenes/bind/knob_focus.h"

#include <cstdint>

namespace deluge::scenes::services {

/// @brief Whether the gold knobs are on a clip rather than the song: `View::isClipContext`
///        (`view.cpp:1749-1756`) -- an owner, in a clip.
/// @param focus What the knobs point at.
/// @return True for a clip's owner; false for the song, an arranger row with no clip, or no focus.
bool is_clip_context(const bind::KnobFocus& focus);

/// @brief What one gold-knob turn on a param did, and what finishing it needs (the popup, the MIDI feedback, the
///        indicator blink, the repaint) -- `View::modEncoderAction_existentParam` (`view.cpp:853-948`).
struct ModTurn {
	/// The knobs have an owner (`view.cpp:841`).
	bool has_controllable = false;
	/// The knob names a param (`view.cpp:847`, negated).
	bool param_exists = false;
	/// The non-existent-param arm changed something (`view.cpp:865`).
	bool caused_change = false;

	/// `modelStackWithParam->paramCollection->getParamKind()` (`view.cpp:876`).
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	/// `modelStackWithParam->paramId` (`view.cpp:910`).
	int32_t param_id = 0;
	/// The knob position before the turn (`view.cpp:879`).
	int32_t old_knob_pos = 0;
	/// `calculateKnobPosForModEncoderTurn`'s output (`view.cpp:880`) -- what the popup shows, and
	/// what was written, whether or not it differed.
	int32_t new_knob_pos = 0;
	/// `new_knob_pos != old_knob_pos` -- legacy's `:902` early return, which happens *after* the
	/// popup and before everything else.
	bool value_changed = false;

	/// `noteTailsAllowedBefore != noteTailsAllowedAfter` (`view.cpp:946`) -- the pad re-render.
	bool note_tails_changed = false;
	/// `paramCollection->shouldParamIndicateMiddleValue(…)` (`view.cpp:1085-1086`), for
	/// `potentiallyMakeItHarderToTurnKnob`.
	bool should_indicate_middle_value = false;
};

/// @brief The model reads `View::setKnobIndicatorLevel` (`view.cpp:1420-1485`) makes, before any
///        of the arithmetic it then does with them.
///
/// Split exactly as `ModTurn` is: the transaction resolves the param stack and reads it, and every
/// clamp, bucket and offset stays in `mod_knobs.cpp` where a spec can mutate it as a pure function.
///
/// The three booleans below are legacy's own three-way branch, and they are not exclusive by
/// accident: `has_auto_param` is `:1413`, and when it is false legacy tests `paramId == 255`
/// (`:1454`) and then `!((paramId & 0x0000FF00) == 0x0000FF00)` (`:1460`).
///
/// @warning **When none of those three holds, legacy reads an uninitialised `int32_t knobPos`**
///          (declared `view.cpp:1427`, consumed `:1485`) and lights a gold knob's indicator column
///          from it. Legacy defect 5 of 5 in `src/scenes/README.md`'s "Defects this port found in
///          shipping firmware"; `ModKnobs::indicator_level` carries the full note and declares
///          nothing for that knob rather than inventing a value.
struct ModIndicatorInfo {
	/// `modelStackWithParam->autoParam` is non-null (`view.cpp:1430`).
	bool has_auto_param = false;
	/// `paramCollection->getParamKind()` (`:1416`).
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	/// `modelStackWithParam->paramId` (`:1417`).
	int32_t param_id = 0;
	/// `paramCollection->paramValueToKnobPos(autoParam->getValuePossiblyAtPos(modPos, …), …)`
	/// (`:1414-1418`).
	int32_t knob_pos = 0;
	/// `isParamBipolar(kind, paramId)` (`:1417`).
	bool bipolar = false;
	/// `isParamQuantizedStutter(kind, paramId, (ModControllableAudio*)modControllable)` (`:1428-1429`).
	/// Legacy ands this with `!isUIModeActive(UI_MODE_STUTTERING)`, which is a UI read this layer
	/// cannot make; only the model half is reported.
	bool quantized_stutter = false;

	/// The no-`autoParam` arm's `paramId == 255` (`:1454`).
	bool non_existent_param = false;
	/// `modControllable->getKnobPosForNonExistentParam(whichModEncoder, …)` (`:1455-1456`), valid
	/// iff `non_existent_param`.
	int32_t non_existent_knob_pos = 0;
	/// The no-`autoParam` arm's `!((paramId & 0x0000FF00) == 0x0000FF00)` (`:1460`) -- "not just a
	/// param, so it is a patch cable", which legacy shows at its 0 midpoint.
	bool patch_cable_default = false;
};

} // namespace deluge::scenes::services
