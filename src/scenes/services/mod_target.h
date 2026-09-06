#pragma once

/// @file
/// @brief `ModTarget` -- what the two gold knobs currently edit -- and the plain-data results of
///        the three model transactions `ModKnobs` runs against it.
///
/// This header is the *data* half of the mod-knob model boundary. The *code* half lives in
/// `scenes/menu/context.cpp`, which is one of the two files the
/// `scenes-model-stack-outside-context` ratchet (`scripts/check_layer_rules.py`) lets include
/// `model/model_stack.h`; the declarations are in `scenes/menu/context.h`, beside `model_stack_for`
/// and `marker_written`, and `marker_written`'s own doc records why a non-menu concern lands there
/// rather than on a third allow-listed file. Everything in this header is POD over forward-declared
/// pointers, so `mod_knobs.{h,cpp}` -- and any later reader -- can carry the target around without
/// ever naming a `ModelStack`.

#include "definitions_cxx.hpp" // ActionResult (a root-level header; see scenes/CMakeLists.txt)
#include "modulation/params/param.h"

#include <cstdint>

class ModControllable;
class NoteRow;
class ParamManager;
class Song;
class TimelineCounter;

namespace deluge::scenes::services {

/// @brief The port of `View::activeModControllableModelStack` (`gui/views/view.h:120-121`) plus the
///        three mod-region globals beside it (`view.h:130-135`).
///
/// Legacy keeps a live `ModelStackWithThreeMainThings` (and `MODEL_STACK_MAX_SIZE` bytes of slack
/// behind it, so a `ModelStackWithAutoParam` can be built over the top in place) as a `View`
/// member, and every gold-knob path reads it directly. A model stack is six pointers and two ints
/// wrapped in a nest of reinterpret-casting wrapper classes; this struct is the six pointers and
/// two ints, and `menu::mod_*` rebuild the stack over caller memory whenever one is actually
/// needed. See the task-2 report's state table for the per-field citation.
struct ModTarget {
	/// `setupModelStackWithSong(&activeModControllableModelStack, currentSong)`'s song
	/// (`view.cpp:1873`, `:1905`). Held rather than read from the `currentSong` global, which the
	/// `scenes-currentSong` ratchet forbids this layer -- `ModKnobs::set_song` is what fills it.
	Song* song = nullptr;
	/// `ModelStackWithTimelineCounter::timelineCounter` (`model_stack.h:145`, `timelineCounterIsSet`
	/// is exactly `timelineCounter != nullptr`). Null for
	/// `setActiveModControllableWithoutTimelineCounter` (`view.cpp:1900-1922`).
	TimelineCounter* timeline_counter = nullptr;
	/// `ModelStackWithNoteRow::noteRow` (`model_stack.h:222`); very often null.
	NoteRow* note_row = nullptr;
	/// `ModelStackWithNoteRowId::noteRowId` (`model_stack.h:186`) -- valid iff `note_row` is set.
	int32_t note_row_id = 0;
	/// `ModelStackWithModControllable::modControllable` (`model_stack.h:227`). Null means "no target
	/// at all": `View::modEncoderAction`'s entire body is inside `if (…modControllable)`
	/// (`view.cpp:824`).
	ModControllable* mod_controllable = nullptr;
	/// `ModelStackWithThreeMainThings::paramManager` (`model_stack.h:233`).
	ParamManager* param_manager = nullptr;

	/// `View::modPos` (`view.h:131`) -- the position automation is read and written at.
	/// `0xFFFFFFFF` is legacy's own default (`View::setModRegion`'s default argument, `view.h:81`).
	uint32_t mod_pos = 0xFFFFFFFF;
	/// `View::modLength` (`view.h:135`) -- 0 when not editing a region / holding a note.
	uint32_t mod_length = 0;
	/// `View::modNoteRowId` (`view.h:130`).
	int32_t mod_note_row_id = 0;

	/// @brief `ModelStackWithTimelineCounter::timelineCounterIsSet()` (`model_stack.h:145`).
	bool timeline_counter_set() const { return timeline_counter != nullptr; }
};

/// @brief `View::isClipContext()` (`view.cpp:1732-1740`): a timeline counter that is set and is not
///        the song itself.
///
/// A free function rather than a `ModTarget` member because the comparison needs `Song` to be a
/// complete type (it is the `TimelineCounter` being compared against) and this header deliberately
/// forward declares it. Defined in `mod_knobs.cpp`.
/// @param target The target to ask about.
/// @return True when the gold knobs are pointed at a clip rather than at the song.
bool is_clip_context(const ModTarget& target);

/// @brief How `ModKnobs` turns a knob position into the next one -- the port-owned policy
///        `menu::mod_encoder_turn` calls between reading the model and writing it.
///
/// A plain function pointer so the whole model transaction stays one call (legacy resolves the
/// param stack exactly once, and `possiblyCloneForArrangementRecording` must not run twice) while
/// `View::calculateKnobPosForModEncoderTurn` (`view.cpp:949-970`) stays in `mod_knobs.cpp`, where
/// it is a pure function a spec can mutate on its own.
using NextKnobPosFn = int32_t (*)(deluge::modulation::params::Kind kind, int32_t knob_pos, int32_t offset);

/// @brief Everything one gold-knob turn did to the model, and everything the caller needs to
///        finish the turn (the popup, the MIDI feedback, the indicator blink, the repaint).
///
/// The port of `View::modEncoderAction` (`view.cpp:818-842`) and its two arms
/// (`modEncoderAction_nonExistentParam`, `:843-852`; `modEncoderAction_existentParam`, `:853-948`)
/// with every UI read and every side effect that is not a model write lifted out to `ModKnobs`.
struct ModTurn {
	/// False when `ModTarget::mod_controllable` is null: `view.cpp:824`'s test, which skips the
	/// whole body (but not `instrumentBeenEdited()`, which is outside it at `:840`).
	bool has_controllable = false;
	/// True on the `modEncoderAction_existentParam` arm -- the param stack resolved *and* has an
	/// `autoParam` (`view.cpp:830`, negated).
	bool param_exists = false;
	/// The non-existent-param arm's `ActionResult::ACTIONED_AND_CAUSED_CHANGE` (`view.cpp:848`),
	/// which is what re-levels that knob's indicator.
	bool caused_change = false;

	/// `modelStackWithParam->paramCollection->getParamKind()` (`view.cpp:859`).
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	/// `modelStackWithParam->paramId` (`view.cpp:893`).
	int32_t param_id = 0;
	/// The knob position before the turn (`view.cpp:862`).
	int32_t old_knob_pos = 0;
	/// `calculateKnobPosForModEncoderTurn`'s output (`view.cpp:863`) -- what the popup shows, and
	/// what was written, whether or not it differed.
	int32_t new_knob_pos = 0;
	/// `new_knob_pos != old_knob_pos` -- legacy's `:902` early return, which happens *after* the
	/// popup and before everything else.
	bool value_changed = false;

	/// `noteTailsAllowedBefore != noteTailsAllowedAfter` (`view.cpp:929`) -- the pad re-render.
	bool note_tails_changed = false;
	/// `paramCollection->shouldParamIndicateMiddleValue(…)` (`view.cpp:1068-1069`), for
	/// `potentiallyMakeItHarderToTurnKnob`.
	bool should_indicate_middle_value = false;
};

/// @brief What param a gold knob addresses right now, without turning it.
///
/// `View::getParameterNameFromModEncoder`'s first three lines (`view.cpp:1013-1016`); the name
/// building itself is pure and stays in `mod_knobs.cpp`.
struct ModParamInfo {
	bool param_exists = false;
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	int32_t param_id = 0;
};

} // namespace deluge::scenes::services
