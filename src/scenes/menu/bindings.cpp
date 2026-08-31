/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The one place the menu system touches the model. See `bindings.h` for the interface.
///
/// **Port table** -- one entry per line (a markdown table here gets reflowed into an unreadable
/// run-on by clang-format; fix round 1, Minor 6). Every line below is transcribed, not called:
/// `gui/menu_item/` is behind the `scenes-includes-outside` wall (task-4 ruling 1).
///
///   * patched read -- `patched_param::Integer::readCurrentValue` (`gui/menu_item/patched_param/integer.cpp:33-36`)
///   * unpatched read -- `UnpatchedParam::readCurrentValue` (`gui/menu_item/unpatched_param.cpp:36-39`)
///   * pan read -- `patched_param::Pan::readCurrentValue` (`patched_param/pan.cpp:32-34`),
///     `unpatched_param::Pan::readCurrentValue` (`unpatched_param/pan.cpp:33-36`)
///   * half-precision read -- `osc::PulseWidth::readCurrentValue` (`osc/pulse_width.h:34-37`),
///     `audio_compressor::CompParam::readCurrentValue` (`audio_compressor/compressor_params.h:29-32`)
///   * patched stack -- `PatchedParam::getModelStack` (`gui/menu_item/patched_param.cpp:84-87`)
///   * unpatched stack -- `UnpatchedParam::getModelStack` (`gui/menu_item/unpatched_param.cpp:41-44`)
///   * write -- `patched_param::Integer::writeCurrentValue` (`integer.cpp:38-77`),
///     `UnpatchedParam::writeCurrentValue` (`unpatched_param.cpp:46-82`)
///   * standard scaling -- `computeCurrentValueForStandardMenuItem` / `computeFinalValueForStandardMenuItem`
///     (`gui/menu_item/value_scaling.cpp:6-8` / `:22-33`)
///   * half-precision scaling -- `computeCurrentValueForHalfPrecisionMenuItem` /
///     `computeFinalValueForHalfPrecisionMenuItem` (`value_scaling.cpp:10-12` / `:35-46`)
///   * pan scaling -- `computeCurrentValueForPan` / `computeFinalValueForPan` (`value_scaling.cpp:14-16` / `:48-58`)
///   * 0..50 default -- `Param::getMaxValue`/`getMinValue` (`gui/menu_item/param.h:34-35`), forwarded
///     by `patched_param/integer.h:35-36` and `unpatched_param.h:42-43`
///   * -25..25 default -- `patched_param::Pan::getMaxValue`/`getMinValue` (`patched_param/pan.h:26-27`),
///     `unpatched_param::Pan` (`unpatched_param/pan.h:28-29`)
///   * enum default -- `Enumeration::selectEncoderAction`'s `[0, numOptions)` clamp
///     (`gui/menu_item/enumeration.cpp:28`)
///   * toggle default -- `Toggle::selectEncoderAction` (`gui/menu_item/toggle.cpp:14-17`), a `Value<bool>`
///   * affect-entire loop -- `unpatched_param.cpp:52-67` and its patched sibling `integer.cpp:45-60`;
///     the per-drum stack is `getModelStackFromSoundDrum` (`model/model_stack.cpp:111-117`)
///   * feedback -- `view.sendMidiFollowFeedback(modelStack, knobPos)` (`integer.cpp:70-71`,
///     `unpatched_param.cpp:74-75`)
///   * automation refresh -- `automationView.possiblyRefreshAutomationEditorGrid` (`integer.cpp:73-77`,
///     `unpatched_param.cpp:77-81`)
///   * patched learn -- `PatchedParam::getLearningThing` (`gui/menu_item/patched_param.cpp:51-55`)
///   * unpatched learn -- `UnpatchedParam::getLearningThing` (`gui/menu_item/unpatched_param.cpp:88-92`)
///
/// **What did NOT come across.**
///
///   * The UI-mode half of the affect-entire condition. Legacy asks
///     `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()`
///     (`unpatched_param.cpp:52`). The second half is `getCurrentOutputType() == KIT &&
///     !setupKitGlobalFXMenu` (`sound_editor.cpp:169-171`), which `MenuContext` already carries as
///     `is_kit && !is_kit_global_fx`; the first half is a `currentUIMode` read, which this layer may
///     not do (task-3 ruling 2), so it arrives as `MenuContext::affect_entire_held` -- a field the
///     caller sets and `context_for` never does. It is deliberately NOT `MenuContext::affect_entire`:
///     that is the *clip's* `InstrumentClip::affectEntire` setting, which is true on the kit-row
///     grid-shortcut path (`sound_editor.cpp:1150-1158`) where legacy edits one row only.
///   * `MenuItem::usesAffectEntire()` (`gui/menu_item/menu_item.h:112`), the per-item gate legacy
///     uses to *enter* `UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR` at all
///     (`sound_editor.cpp:503`). It has no `Node` counterpart, and needs none today: every
///     `ParamRef`-backed legacy class returns true (`patched_param/integer.h:32`,
///     `unpatched_param.h:38`), so no `ParamRef` node loses the loop and none gains one legacy
///     refused. It becomes a real gap only if E2 gives `affect_entire_held` to a node that is not a
///     param, which `write()` would ignore anyway (fix round 1, Minor 5). Note also that the ~20
///     *non*-param items that loop over drums themselves (`filter/mode.h`, `lfo/type.h`,
///     `synth_mode.h`, `arpeggiator/*`, ...) get no help from `write_whole_kit`: each of those
///     becomes a `get`/`set` node in Tasks 6-8 whose `set` hook owns its own loop.
///   * `getRootUI() == &automationView` and the `lastSelectedParamID`/`Kind` match inside
///     `possiblyRefreshAutomationEditorGrid` (`integer.cpp:73`, `automation_view.cpp:611-628`). The
///     notify design replaces "am I in the automation view, and is this the param it is showing?"
///     with unconditional facts. See `write()`'s own comment for which two are raised and why one is
///     not enough.
///   * The double write at `integer.cpp:41`. `patched_param::Integer::writeCurrentValue` calls
///     `setCurrentValueInResponseToUserInput` once before its affect-entire branch and again inside
///     the `else`, so the ordinary path writes twice and the affect-entire path writes the selected
///     drum twice (once at `:41`, once through the loop). Its unpatched sibling
///     (`unpatched_param.cpp:46-71`) has the branch and no pre-write, which is plainly the intended
///     shape; the extra call is a bug, not behaviour, and is not transcribed.
///
/// **`currentSong` references in this file: 0** (task-3 ruling 3, ratcheted by Task 12). Legacy's
/// `getModelStackFromSoundDrum` (`model/model_stack.cpp:111-117`) reads `currentSong` and
/// `getCurrentInstrumentClip()`; the transcription below takes `ctx.song` / `ctx.clip` instead.

#include "scenes/menu/bindings.h"

#include "scenes/menu/context.h"
#include "scenes/profile.h"

#include "definitions_cxx.hpp"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h" // ruling 6 / the scenes-model-stack ratchet: this file and context.cpp only
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"             // the ALPHA_OR_BETA_VERSION diagnostic only
#include "processing/engines/audio_engine.h" // Binding::updates_reverb_params
#include "processing/sound/sound_drum.h"

#include <algorithm>
#include <cstdint>

namespace deluge::scenes::menu::bindings {

namespace {

namespace params = deluge::modulation::params;

/// @brief The installed MIDI-follow feedback hook, or null. A single function pointer, set once at
///        start-up by the application; no heap, no ownership.
FeedbackHook g_feedback_hook = nullptr;

// -------------------------------------------------------------------------------------------
// Value scaling, transcribed from `gui/menu_item/value_scaling.cpp` (ruling 1). The literals and
// the shift are reproduced exactly; `bindings_spec.cpp` asserts against the originals.
// -------------------------------------------------------------------------------------------

/// @brief `computeCurrentValueForStandardMenuItem` (`value_scaling.cpp:6-8`): INT32 range -> 0..50.
int32_t current_value_standard(int32_t value) {
	return (((int64_t)value + 2147483648) * kMaxMenuValue + 2147483648) >> 32;
}

/// @brief `computeFinalValueForStandardMenuItem` (`value_scaling.cpp:22-33`): 0..50 -> INT32 range.
int32_t final_value_standard(int32_t value) {
	if (value == kMaxMenuValue) {
		return 2147483647;
	}
	if (value == kMinMenuValue) {
		return -2147483648;
	}
	// (2147483648 / kMidMenuValue) == 85899345
	return (uint32_t)value * 85899345 - 2147483648;
}

/// @brief `computeCurrentValueForHalfPrecisionMenuItem` (`value_scaling.cpp:10-12`):
///        0..INT32_MAX -> 0..50.
int32_t current_value_half_precision(int32_t value) {
	return ((int64_t)value * (kMaxMenuValue * 2) + 2147483648) >> 32;
}

/// @brief `computeFinalValueForHalfPrecisionMenuItem` (`value_scaling.cpp:35-46`): 0..50 ->
///        0..INT32_MAX. Note the `0` (not `INT32_MIN`) floor -- "comp params and osc pulse width
///        aren't set up for negative inputs" (`value_scaling.cpp:36`).
int32_t final_value_half_precision(int32_t value) {
	if (value == kMaxMenuValue) {
		return 2147483647;
	}
	if (value == kMinMenuValue) {
		return 0;
	}
	return (uint32_t)value * (2147483648 / kMidMenuValue) >> 1;
}

/// @brief `computeCurrentValueForPan` (`value_scaling.cpp:14-16`): INT32 range -> -25..25.
int32_t current_value_pan(int32_t value) {
	return ((int64_t)value * (kMaxMenuRelativeValue * 2) + 2147483648) >> 32;
}

/// @brief `computeFinalValueForPan` (`value_scaling.cpp:48-58`): -25..25 -> INT32 range.
int32_t final_value_pan(int32_t value) {
	if (value == kMaxMenuRelativeValue) {
		return 2147483647;
	}
	if (value == kMinMenuRelativeValue) {
		return -2147483648;
	}
	return ((int32_t)value * (2147483648 / (kMaxMenuRelativeValue * 2)) * 2);
}

// -------------------------------------------------------------------------------------------

/// @brief Which scaling @p node's `ParamRef` is read and written through.
///
/// Normally just `bind.scaling`, which is what the tree declares (`node.h`'s `Scaling`, set through
/// `param()`'s trailing argument). The one addition is a backstop for `Scaling::Pan`: a node left at
/// the default that is *nonetheless* one of legacy's two `Pan` menu items is promoted, rather than
/// silently taking 0..50 arithmetic on a bipolar param. Two independent spellings say so --
/// `Hint::Pan`, the node-data port of `Pan::getRenderingStyle() == PAN` (`patched_param/pan.h:28`,
/// `unpatched_param/pan.h:30`), and `params::isParamPan` (`modulation/params/param.cpp:35-38`), the
/// model's own predicate, true for exactly the two params legacy declares with a `Pan` class
/// (`LOCAL_PAN`, `UNPATCHED_PAN`).
///
/// No such backstop exists (or could exist) for `Scaling::HalfPrecision`: legacy's two
/// half-precision items are picked out by *class*, not by param id or rendering style, so
/// `osc::PulseWidth` (`LOCAL_OSC_A_PHASE_WIDTH`/`_B_`) and `audio_compressor::CompParam`
/// (`UNPATCHED_COMPRESSOR_THRESHOLD`, and the shared comp params) are indistinguishable from any
/// other patched/unpatched param from here. Tasks 6-8 must name `Scaling::HalfPrecision` at those
/// call sites; `bindings.h` says so too.
Scaling scaling_for(const Node& node) {
	if (node.bind.scaling != Scaling::Standard) {
		return node.bind.scaling;
	}
	if (node.hint == Hint::Pan
	    || (node.bind.param.valid() && params::isParamPan(node.bind.param.kind, node.bind.param.index))) {
		return Scaling::Pan;
	}
	return Scaling::Standard;
}

/// @brief Stored `int32_t` -> menu units, per @p scaling.
int32_t to_menu_units(Scaling scaling, int32_t stored) {
	switch (scaling) {
	case Scaling::HalfPrecision:
		return current_value_half_precision(stored);
	case Scaling::Pan:
		return current_value_pan(stored);
	case Scaling::Standard:
		break;
	}
	return current_value_standard(stored);
}

/// @brief Menu units -> stored `int32_t`, per @p scaling -- legacy's `getFinalValue()`.
int32_t to_stored_value(Scaling scaling, int32_t menu_value) {
	switch (scaling) {
	case Scaling::HalfPrecision:
		return final_value_half_precision(menu_value);
	case Scaling::Pan:
		return final_value_pan(menu_value);
	case Scaling::Standard:
		break;
	}
	return final_value_standard(menu_value);
}

/// @brief Which of the two stored param collections a context's param manager really holds.
///
/// Both fields are **positive signals**: default-constructed is "serves nothing", which is the right
/// answer for a context this layer does not recognise.
struct Serves {
	bool unpatched = false;
	bool patched = false;
};

/// @brief Is the param manager @p ctx addresses one whose `summaries[0]` is an `UnpatchedParamSet`,
///        rather than a MIDI clip's `MIDIParamCollection`?
///
/// **There is no structural discriminator, so this is a fact about the model, threaded in.**
/// `ParamManager::setupMIDI` and `setupUnpatched` (`modulation/params/param_manager.cpp:60-82`)
/// both leave `expressionParamSetOffset == 1` and `summaries[0].paramCollection` non-null; the two
/// differ only in the *type* of the object placement-new'd into that slot, and `ParamManager`
/// exposes nothing that tells them apart. `getUnpatchedParamSet()` (`param_manager.h:110-117`) is a
/// bare `(UnpatchedParamSet*)` cast of that pointer, so asking a MIDI manager for it reinterprets a
/// `MIDIParamCollection` and reads whatever lands at the `params[]` offset.
///
/// So the answer comes from what the menu is *editing*, which `context_for` already resolved:
///
///   * no output at all -- the session / arranger arm (`context.cpp`'s `session_or_arranger_root`)
///     took the song's own param manager, which is `setupUnpatched`'d
///     (`deluge.cpp`'s `setupBlankSong`); the settings root has no param manager at all and is
///     caught by the null test in `serves_param` instead;
///   * `SYNTH` -- `SoundInstrument`, `setupWithPatching`'d (unpatched **and** patched);
///   * `AUDIO` -- `AudioClip`, `GlobalEffectableForClip::initParams` over `setupUnpatched`;
///   * `KIT` -- the kit's own clip param manager on the global-FX page, else the **selected drum's
///     note row**, which is unpatched only when that drum is a `SoundDrum`. `context_for` resolves
///     `ctx.sound` for exactly that case (`context.cpp`'s `DrumType::SOUND` arm) and leaves both
///     `sound` and `params` null for a MIDI or gate drum, so `ctx.sound != nullptr` is the signal;
///   * `MIDI_OUT` / `CV` -- `setupMIDI`'d. **No.**
///
/// Note the asymmetry: this returns false unless something positively says otherwise. A context
/// this function does not recognise reads as "not unpatched", which costs a menu item its value and
/// never costs the instrument its memory.
bool holds_unpatched_params(const MenuContext& ctx) {
	if (ctx.output == nullptr) {
		return true;
	}
	switch (ctx.output->type) {
	case OutputType::SYNTH:
	case OutputType::AUDIO:
		return true;
	case OutputType::KIT:
		return ctx.is_kit_global_fx || ctx.sound != nullptr;
	default:
		return false;
	}
}

/// @brief What @p ctx's own param manager serves.
///
/// The patched half is `ctx.sound != nullptr`, and that is the *whole* test: a `PatchedParamSet`
/// exists only after `ParamManager::setupWithPatching()` (`param_manager.cpp:85-...`), which only a
/// `Sound`'s manager ever gets -- a `SoundInstrument`'s clip manager, or a `SoundDrum`'s note-row
/// manager -- and `context_for` resolves `ctx.sound` in exactly those two cases and leaves it null
/// everywhere else (the kit's global-FX page, an audio clip, the song, a MIDI or CV clip, a
/// non-audio drum row, the settings root).
Serves serves_for(const MenuContext& ctx) {
	return Serves{.unpatched = holds_unpatched_params(ctx), .patched = ctx.sound != nullptr};
}

/// @brief Does @p params hold the collection a `ParamRef` of @p kind lives in?
///
/// Legacy reaches straight for `soundEditor.currentParamManager->getPatchedParamSet()`
/// (`integer.cpp:34`) / `getUnpatchedParamSet()` (`unpatched_param.cpp:38`). Those two accessors
/// `FREEZE_WITH_ERROR("E410"/"E411")` on a param manager that has no such collection
/// (`modulation/params/param_manager.h:110-135`) -- and only `#if ALPHA_OR_BETA_VERSION`, so a
/// release build casts a null pointer and crashes on the next member read. The same is true one
/// level up: `ModelStackWithThreeMainThings::getPatchedAutoParamFromId` (`model_stack.cpp:245-256`)
/// tests `containsAnyParamCollectionsIncludingExpression()` -- which only looks at summaries[0] --
/// and then dereferences `summary->paramCollection` unconditionally, so it too crashes on a MIDI or
/// unpatched-only param manager asked for a patched param.
///
/// The menu tree is supposed to make that unreachable (a patched param is only relevant where there
/// is a `Sound`), but "supposed to" is not a guarantee a library-level read or write should rest on.
/// This is the predicate those accessors assert about, tested up front by both `read()` and
/// `write()`.
///
/// @param serves Which collections this manager really holds -- `serves_for(ctx)` for a context's
///               own manager, or `{true, true}` for a `SoundDrum`'s note row (which the caller has
///               already filtered to `DrumType::SOUND`). **Neither half can be derived here**, so it
///               is an argument rather than another `param_manager` test, and a caller that cannot
///               answer must pass a default-constructed `Serves`.
bool serves_param(ParamManager* param_manager, deluge::modulation::params::Kind kind, Serves serves) {
	if (param_manager == nullptr || !param_manager->containsAnyParamCollectionsIncludingExpression()) {
		return false;
	}
	switch (kind) {
	case params::Kind::PATCHED:
		// `param_manager.h:128-135`: the patched set is summaries[1] -- and **that slot being
		// non-null is not proof it holds one** (fix round 1, I1). `setupMIDI` leaves
		// `expressionParamSetOffset == 1` (`param_manager.cpp:66-69`), so
		// `ensureExpressionParamSetExists` places an `ExpressionParamSet` into summaries[1]
		// (`:289-298`) -- and `bend_range::Main::writeCurrentValue` calls
		// `getOrCreateExpressionParamSet()` (`gui/menu_item/bend_range/main.h:30`) from `bendMenu`,
		// which IS a child of the MIDI/CV root. So once a user has touched bend range on a MIDI
		// clip, the structural test alone would cast a four-param `ExpressionParamSet` to a
		// `PatchedParamSet` and index past its end. The positive signal is `serves.patched`; the
		// structural test stays as the second half, for a `Sound` whose manager somehow has no
		// patched set yet.
		return serves.patched && param_manager->summaries[1].paramCollection != nullptr;
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		// `param_manager.h:110-117`: summaries[0], which the guard above already tested -- but that
		// slot holds a `MIDIParamCollection` on a MIDI or CV clip (`param_manager.cpp:60-71`) and
		// nothing about the manager says which. Batch 3's tables made that reachable: the MIDI/CV
		// root carries the randomizer's and the arpeggiator's unpatched params so their `midi_cv`
		// twins can be siblings (`nodes/roots.h`'s `arp_midi`, `nodes::randomizer`). Relevance keeps
		// the interpreter off them, but `read()`/`write()` are public and must not rest on that.
		return serves.unpatched;
	default:
		return false;
	}
}

#if ALPHA_OR_BETA_VERSION
/// @brief Say something when a node is bound to a param this context cannot serve.
///
/// The port replaces legacy's E410/E411 freeze (`param_manager.h:110-135`) with a silent no-op,
/// which is right for the shipping build -- a mis-declared node should not brick the instrument --
/// but leaves a tree-authoring bug with no signal at all in a debug build either (fix round 1,
/// Minor 4). One console line, alpha/beta only, on the discrete user action (a write) rather than on
/// the per-frame read.
void notice_unservable_param() {
	notify::notice_text("MENU PARAM UNBOUND", notify::Notice::Kind::Console);
}
#endif

/// @brief Which stored param set @p node's `ParamRef` lives in, or null when this context cannot
///        serve it (`serves_param` above).
ParamSet* param_set_for(const Node& node, const MenuContext& ctx) {
	if (!serves_param(ctx.params, node.bind.param.kind, serves_for(ctx))) {
		return nullptr;
	}
	return node.bind.param.kind == params::Kind::PATCHED ? static_cast<ParamSet*>(ctx.params->getPatchedParamSet())
	                                                     : static_cast<ParamSet*>(ctx.params->getUnpatchedParamSet());
}

/// @brief Add @p node's param to @p stack -- `PatchedParam::getModelStack` (`patched_param.cpp:86`)
///        / `UnpatchedParam::getModelStack` (`unpatched_param.cpp:43`), minus the `soundEditor`
///        singleton the caller has already replaced with `model_stack_for`.
/// @return The stack, or null when the param manager holds no such collection (both getters already
///         return null for that -- `model/model_stack.cpp:232-256`).
ModelStackWithAutoParam* add_param(ModelStackWithThreeMainThings* stack, const Node& node) {
	if (stack == nullptr) {
		return nullptr;
	}
	switch (node.bind.param.kind) {
	case params::Kind::PATCHED:
		return stack->getPatchedAutoParamFromId(node.bind.param.index);
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		return stack->getUnpatchedAutoParamFromId(node.bind.param.index);
	default:
		return nullptr;
	}
}

/// @brief Is @p ctx one `model_stack_for` will accept? Its precondition (`context.h:155-157`): a
///        resolved context that is either the song view or has a clip.
bool has_model_stack(const MenuContext& ctx) {
	return ctx.resolved && (ctx.is_song_view || ctx.clip != nullptr);
}

/// @brief Set @p node's param to @p final_value on every `SoundDrum` in the kit --
///        `unpatched_param.cpp:54-66` / `integer.cpp:47-59`.
///
/// The per-drum stack is `getModelStackFromSoundDrum` (`model/model_stack.cpp:111-117`) with
/// `currentSong` / `getCurrentInstrumentClip()` replaced by @p ctx's own song and clip. Legacy
/// dereferences the note row unconditionally at `model_stack.cpp:116`; a drum with no row in this
/// clip is skipped here instead.
void write_whole_kit(const Node& node, const MenuContext& ctx, int32_t final_value) {
	auto* kit = static_cast<Kit*>(ctx.output);
	auto* instrument_clip = static_cast<InstrumentClip*>(ctx.clip);

	for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) { // `unpatched_param.cpp:56`
		if (drum->type != DrumType::SOUND) {                                // `:57`
			continue;
		}
		auto* sound_drum = static_cast<SoundDrum*>(drum); // `:58`

		// Ruling 6: the model-stack scratch is a caller-side buffer in this frame, never a shared
		// static -- `write()` can be re-entered (a `set` hook is arbitrary code). Fix round 2, M4:
		// every such buffer in this layer is `alignas(ModelStackWithAutoParam)`, uniformly -- see
		// `model_stack_for`'s note in `context.h` for why.
		alignas(ModelStackWithAutoParam) char drum_memory[MODEL_STACK_MAX_SIZE]; // `:60`
		int32_t note_row_index = 0;
		NoteRow* note_row = instrument_clip->getNoteRowForDrum(sound_drum, &note_row_index);
		// Both halves are unconditionally true here: the loop above has already skipped every
		// `drum->type != DrumType::SOUND`, and a `SoundDrum`'s note row is `setupWithPatching`'d
		// (`song.cpp`'s `backUpParamManager` / `NoteRow::setDrum`), so summaries[0] is an
		// `UnpatchedParamSet` and summaries[1] a `PatchedParamSet`. MIDI and gate drums in the same
		// kit never reach this line.
		if (note_row == nullptr
		    || !serves_param(&note_row->paramManager, node.bind.param.kind,
		                     Serves{.unpatched = true, .patched = true})) {
			continue;
		}
		ModelStackWithThreeMainThings* drum_stack = setupModelStackWithThreeMainThingsIncludingNoteRow(
		    drum_memory, ctx.song, ctx.clip, note_row_index, note_row, sound_drum, &note_row->paramManager);

		ModelStackWithAutoParam* drum_param = add_param(drum_stack, node); // `:61-63`
		if (drum_param == nullptr || drum_param->autoParam == nullptr) {
			continue;
		}
		drum_param->autoParam->setCurrentValueInResponseToUserInput(final_value, drum_param); // `:64`
	}
}

/// @brief How many `Kind::Enum` options @p node has -- the input to its default range.
///
/// This is the *option* count, legacy's `Enumeration::size()`, not the number of names a particular
/// caller has room to render: `Enumeration::selectEncoderAction` clamps against `size()` on every
/// detent (`enumeration.cpp:19,28`), so running a `dynamic_names` hook to obtain it is exactly what
/// legacy does, at the same rate. That is why the window here is the full `kMaxDynamicNames`
/// (`node.h:97` -- the documented cap on what one call may produce) rather than whatever `out` span
/// `names()` was handed: a caller with room for four names still edits a ten-option enum over its
/// real 0..9 range (fix round 1, Minor 3). Cost is a `std::string_view[64]` -- 512 bytes on the
/// 32-bit target -- in this frame for the duration of the hook call.
size_t name_count(const Node& node, const MenuContext& ctx) {
	if (node.bind.dynamic_names != nullptr) {
		std::string_view window[kMaxDynamicNames]{};
		return std::min(node.bind.dynamic_names(ctx, std::span<std::string_view>{window}), kMaxDynamicNames);
	}
	return std::min(node.bind.names.size(), kMaxDynamicNames);
}

} // namespace

/// Legacy `unpatched_param.cpp:52` / `integer.cpp:45` -- write the whole kit rather than the
/// selected row? `editingKitRow()` (`sound_editor.cpp:169-171`) is `is_kit && !is_kit_global_fx`;
/// the held-button half arrives as `affect_entire_held` (see the file header). Declared in
/// `bindings.h` since Task 6 -- a couple of the tables' own `set` hooks branch on it.
bool writes_whole_kit(const MenuContext& ctx) {
	return ctx.affect_entire_held && ctx.is_kit && !ctx.is_kit_global_fx && ctx.output != nullptr
	       && ctx.clip != nullptr;
}

void apply_to_targets(const MenuContext& ctx, int32_t value, int32_t arg, ApplyFn fn) {
	if (fn == nullptr) {
		return;
	}

	if (writes_whole_kit(ctx)) {
		auto* kit = static_cast<Kit*>(ctx.output);
		auto* instrument_clip = static_cast<InstrumentClip*>(ctx.clip);
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) { // `filter/mode.h:49`
			if (drum->type != DrumType::SOUND) {                                // `:50`
				continue;
			}
			auto* sound_drum = static_cast<SoundDrum*>(drum);

			// `getModelStackFromSoundDrum` (`model/model_stack.cpp:111-117`) with `ctx`'s own song
			// and clip in place of the globals, and with legacy's unconditional note-row
			// dereference (`:116`) turned into a skip. A drum with no row in this clip still gets
			// `fn`, just with a null stack. Some setters tolerate that and some do not --
			// `Sound::setNumUnison` dereferences it whenever the sound has active voices
			// (`processing/sound/sound.cpp:3102-3105`) -- but the ones that do not are exactly the
			// ones legacy never reached at all: it built the same stack one line earlier and
			// dereferenced the null note row itself (`model_stack.cpp:116`). Skipping the drum is
			// strictly safer than legacy's crash, never a behaviour change on a well-formed clip.
			alignas(ModelStackWithAutoParam) char drum_memory[MODEL_STACK_MAX_SIZE];
			int32_t note_row_index = 0;
			NoteRow* note_row = instrument_clip->getNoteRowForDrum(sound_drum, &note_row_index);
			ModelStackWithSoundFlags* drum_stack = nullptr;
			if (note_row != nullptr) {
				drum_stack =
				    setupModelStackWithThreeMainThingsIncludingNoteRow(drum_memory, ctx.song, ctx.clip, note_row_index,
				                                                       note_row, sound_drum, &note_row->paramManager)
				        ->addSoundFlags();
			}
			fn(SoundTarget{sound_drum, sound_drum, drum_stack, /*whole_kit=*/true}, value, arg, ctx);
		}
		return;
	}

	if (ctx.mod == nullptr) {
		return;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* stack = nullptr;
	if (ctx.sound != nullptr && has_model_stack(ctx)) {
		stack = model_stack_for(ctx, memory)->addSoundFlags();
	}
	fn(SoundTarget{ctx.mod, ctx.sound, stack, /*whole_kit=*/false}, value, arg, ctx);
}

int32_t read(const Node& node, const MenuContext& ctx) {
	const Binding& bind = node.bind;

	// `get` first, then `param` -- `node.h`'s documented precedence. A node with both is one of
	// legacy's `menu_item::Transpose` derivatives, whose value lives outside the param set and
	// whose `ParamRef` exists only so `learn_target` below has something to bind.
	if (bind.get != nullptr) {
		return bind.get(ctx);
	}

	if (bind.param.valid()) {
		ParamSet* set = param_set_for(node, ctx);
		if (set == nullptr) {
			return 0;
		}
		// `integer.cpp:34` / `unpatched_param.cpp:38`: the *stored* value, then the menu scaling.
		return to_menu_units(scaling_for(node), set->getValue(bind.param.index));
	}
	return 0;
}

void write(const Node& node, const MenuContext& ctx, int32_t value) {
	const Binding& bind = node.bind;

	// A `get`/`set` (or `action`) node: the hook owns the model write, so all this layer knows is
	// that something the OLED shows has changed. Tested before `param` -- `node.h`'s documented
	// precedence, and the reason a `menu_item::Transpose` node can carry a learn-only `ParamRef`
	// without this function writing to it.
	if (bind.set != nullptr) {
		bind.set(ctx, value);
		notify::raise(notify::Dirty::Oled);
		return;
	}
	if (!bind.param.valid()) {
		return; // nothing to write, so nothing to repaint (fix round 1, Minor 7)
	}

	if (!has_model_stack(ctx)) {
		return;
	}

	// Before `model_stack_for`: `getPatchedAutoParamFromId` dereferences a summary this param
	// manager may not have (`model_stack.cpp:245-256`; see `serves_param`), so the same predicate
	// `read()` uses guards the write path too.
	if (!serves_param(ctx.params, bind.param.kind, serves_for(ctx))) {
#if ALPHA_OR_BETA_VERSION
		notice_unservable_param();
#endif
		return;
	}

	// `integer.cpp:39-40` / `unpatched_param.cpp:47-48`, with `soundEditor.getCurrentModelStack`
	// replaced by `model_stack_for` (context.h:159 -- the one sanctioned route). Ruling 6: the
	// buffer is this frame's, not a shared static.
	alignas(ModelStackWithAutoParam) char stack_memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = add_param(model_stack_for(ctx, stack_memory), node);
	if (stack == nullptr || stack->autoParam == nullptr) {
		return;
	}

	// `integer.cpp:41` / `unpatched_param.cpp:49`, via `getFinalValue()`
	// (`integer.cpp:79-81` / `unpatched_param.cpp:84-86`, or the `Pan` overrides at
	// `patched_param/pan.cpp:28-30` / `unpatched_param/pan.cpp:29-31`, or the half-precision ones at
	// `osc/pulse_width.h:32` / `audio_compressor/compressor_params.h:33`).
	const int32_t final_value = to_stored_value(scaling_for(node), value);

	if (writes_whole_kit(ctx)) {
		write_whole_kit(node, ctx, final_value);
	}
	else {
		// `unpatched_param.cpp:70` / `integer.cpp:63` -- the normal case of just one sound.
		stack->autoParam->setCurrentValueInResponseToUserInput(final_value, stack);
	}

	// `unpatched_param::UpdatingReverbParams::writeCurrentValue`
	// (`gui/menu_item/unpatched_param/updating_reverb_params.h:26-29`): the ordinary param write
	// above, then the engine flag. Two legacy items subclass it -- `sidechain::Shape`
	// (`sidechain/shape.h:6`) and `sidechain::GlobalVolume` (`sidechain/volume_global.h:6`) -- and
	// neither can express it as a `set` hook, because a hook would replace the param write rather
	// than follow it (review round 1, I2).
	if (bind.updates_reverb_params) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}

	// `integer.cpp:70-71` / `unpatched_param.cpp:74-75`. `view.sendMidiFollowFeedback` itself is
	// behind the `gui/` wall, so the knob position is computed here and handed to the hook.
	const int32_t knob_pos = stack->paramCollection->paramValueToKnobPos(final_value, stack);
	if (g_feedback_hook != nullptr) {
		g_feedback_hook(ctx, node, knob_pos);
	}

	// Three facts, replacing three legacy effects. Each is unconditional here where legacy was
	// gated; over-invalidation, never loss.
	//
	//  * `Dirty::ParamAutomation` and `Dirty::ModTarget` -- the OLED half. Legacy reaches it two
	//    ways: `possiblyRefreshAutomationEditorGrid` (`integer.cpp:73-77` /
	//    `unpatched_param.cpp:77-81`), and the knob-indicator refresh
	//    `setCurrentValueInResponseToUserInput` triggers through
	//    `ParamManager::notifyParamModifiedInSomeWay` -> `view.notifyParamAutomationOccurred`
	//    (`auto_param.cpp:1161`, `view.cpp:1691-1711`), whose deferred `displayAutomation()` calls
	//    `setKnobIndicatorLevels()` (`view.cpp:1741-1744`). `legacy_redraw_for` lists both
	//    enumerators under `r.oled` (`gui/ui/notify_drain_mapping.cpp:20`).
	//
	//  * `Dirty::GridMain` -- the PAD half, and the reason the pair above is not enough (fix round 1,
	//    Important 1). `possiblyRefreshAutomationEditorGrid` ends in
	//    `uiNeedsRendering(&automationView)` (`automation_view.cpp:624`), i.e. a main-pad re-render
	//    (`AutomationView::renderMainPads`, `:633`) -- and `ParamAutomation`/`ModTarget` appear
	//    *only* in `r.oled`, never in `r.main_rows` (`notify_drain_mapping.cpp:13-14`). Without this
	//    third raise, editing the param the automation view is showing would stop repainting its
	//    grid the moment `MenuScene` replaces the legacy items. `Dirty::GridMain` is the enumerator
	//    `r.main_rows` carries for exactly this; the mapping file itself is left alone.
	//
	//    Coarser than legacy on purpose: `possiblyRefreshAutomationEditorGrid` repaints only when the
	//    edited param *is* the view's `lastSelectedParamID`/`Kind` (`automation_view.cpp:611-622`),
	//    a comparison this layer cannot make -- it may not read `automationView` or the root UI. So
	//    every param write asks for one grid repaint the drain already coalesces, rather than
	//    silently dropping the one that mattered.
	notify::raise(notify::Dirty::ParamAutomation);
	notify::raise(notify::Dirty::ModTarget);
	notify::raise(notify::Dirty::GridMain);
}

IntRange range(const Node& node, const MenuContext& ctx) {
	// node.h:110-116's documented order: a dynamic hook, then the literal bounds a builder was
	// given, then the legacy per-kind default.
	if (node.bind.range != nullptr) {
		return node.bind.range(ctx);
	}
	if (node.bind.fixed_range.has_value()) {
		return *node.bind.fixed_range;
	}

	switch (node.kind) {
	case Kind::Toggle:
		// `Toggle` is a `Value<bool>` (`gui/menu_item/toggle.h:8`) whose encoder action stores
		// `offset > 0` (`toggle.cpp:14-17`).
		return IntRange{0, 1, 1};

	case Kind::Enum: {
		// `Enumeration::selectEncoderAction` clamps to `[0, numOptions)` (`enumeration.cpp:28`).
		const size_t count = name_count(node, ctx);
		return IntRange{0, count > 0 ? static_cast<int32_t>(count) - 1 : 0, 1};
	}

	default:
		if (scaling_for(node) == Scaling::Pan) {
			// `patched_param::Pan::getMaxValue`/`getMinValue` (`patched_param/pan.h:26-27`),
			// `unpatched_param::Pan` (`unpatched_param/pan.h:28-29`). `Scaling::HalfPrecision` does
			// NOT get its own range: neither `osc::PulseWidth` nor `audio_compressor::CompParam`
			// overrides `getMaxValue`/`getMinValue`, so both keep the 0..50 default below.
			return IntRange{kMinMenuRelativeValue, kMaxMenuRelativeValue, 1};
		}
		// `param.h:34-35` (`kMaxMenuValue` / `kMinMenuValue`), forwarded by
		// `patched_param/integer.h:35-36` and `unpatched_param.h:42-43`.
		//
		// This arm is also where `Kind::Bipolar`, `Range`, `MultiRange`, `Colour` and `SyncLevel`
		// land (fix round 1, Minor 2). Their interpreters are E2's, and each will want its own
		// default -- legacy `menu_item::Bipolar`, `menu_item::Range`, `multi_range.h`,
		// `menu_item::Colour`, `sync_level.h`. Until then 0..50 is a placeholder, not a claim:
		// nothing in E1's tables declares one of those kinds, and every one of them will arrive
		// with either a `fixed_range` from its builder or a `range` hook, both of which are
		// consulted above before this switch is ever reached. Deliberately not a hard failure --
		// a `FREEZE` in a library this layer means to keep pure would be worse than a wrong number.
		return IntRange{kMinMenuValue, kMaxMenuValue, 1};
	}
}

size_t names(const Node& node, const MenuContext& ctx, std::span<std::string_view> out,
             std::span<deluge::ui::shell::TextScratch> scratch) {
	// The two spans are documented as the same length; the shorter one wins if a caller gets it
	// wrong, so a mismatch truncates rather than writing past either end.
	const size_t limit = std::min({out.size(), scratch.size(), kMaxDynamicNames});
	if (limit == 0) {
		return 0;
	}

	if (node.bind.dynamic_names != nullptr) {
		// The hook writes views it owns (static strings, or storage of its own); `scratch` is for
		// the id path only, and `DynamicNamesFn`'s signature (node.h:89) has nowhere to pass it.
		return std::min(node.bind.dynamic_names(ctx, out.first(limit)), limit);
	}

	// Ruling 2: ids resolve through the profile's `text_or_digits`, never `text_lookup` directly --
	// `shell/profile.h:156` already implements the digits fallback for an id with no entry.
	const deluge::ui::shell::BoardProfile& profile = scenes::deluge_profile();
	const size_t count = std::min(node.bind.names.size(), limit);
	for (size_t i = 0; i < count; ++i) {
		out[i] = profile.text_or_digits(node.bind.names[i], scratch[i]);
	}
	return count;
}

ParamDescriptor learn_target(const Node& node, const MenuContext& /*ctx*/) {
	ParamDescriptor descriptor;
	descriptor.setToNull();

	if (!node.bind.param.valid()) {
		return descriptor;
	}

	switch (node.bind.param.kind) {
	case params::Kind::PATCHED:
		// `PatchedParam::getLearningThing` (`gui/menu_item/patched_param.cpp:51-55`).
		descriptor.setToHaveParamOnly(node.bind.param.index);
		break;
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		// `UnpatchedParam::getLearningThing` (`gui/menu_item/unpatched_param.cpp:88-92`) -- the
		// unpatched ids are offset into the shared numbering by `UNPATCHED_START`
		// (`modulation/params/param.h:171`).
		descriptor.setToHaveParamOnly(node.bind.param.index + params::UNPATCHED_START);
		break;
	default:
		break;
	}
	return descriptor;
}

void set_feedback_hook(FeedbackHook hook) {
	g_feedback_hook = hook;
}

// ---------------------------------------------------------------------------------------------
// The value scaling `get`/`set` items reuse (`bindings.h`'s note)

int32_t standard_to_menu(int32_t stored) {
	return current_value_standard(stored);
}
int32_t menu_to_standard(int32_t menu_value) {
	return final_value_standard(menu_value);
}
int32_t pan_to_menu(int32_t stored) {
	return current_value_pan(stored);
}
int32_t menu_to_pan(int32_t menu_value) {
	return final_value_pan(menu_value);
}
int32_t unsigned_to_menu(uint32_t stored) {
	// `computeCurrentValueForUnsignedMenuItem` (`value_scaling.cpp:18-20`).
	return ((int64_t)stored * kMaxMenuValue + 2147483648) >> 32;
}
uint32_t menu_to_unsigned(int32_t menu_value) {
	// `computeFinalValueForUnsignedMenuItem` (`value_scaling.cpp:60-62`).
	return (uint32_t)menu_value * 85899345;
}

// ---------------------------------------------------------------------------------------------
// `SoundEditor::allowsNoteTails` (`gui/ui/sound_editor.cpp:1811-1816`)

bool allows_note_tails(const MenuContext& ctx) {
	if (ctx.sound == nullptr) {
		return true; // `:1811` -- the default, kept when there is no sound to ask
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* stack = model_stack_for(ctx, memory);
	if (stack == nullptr) {
		return true;
	}
	return ctx.sound->allowNoteTails(stack->addSoundFlags(), /*disregardSampleLoop=*/true); // `:1815`
}

bool drum_allows_note_tails(const MenuContext& /*ctx*/, Drum* drum) {
	if (drum == nullptr || drum->type != DrumType::SOUND) {
		return true; // `include_in_kit_arp.h:33` -- the initialiser, left alone for a non-sound drum
	}
	auto* sound_drum = static_cast<SoundDrum*>(drum);
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	// `include_in_kit_arp.h:37-40`, verbatim.
	ModelStackWithSoundFlags* stack = getModelStackFromSoundDrum(memory, sound_drum)->addSoundFlags();
	return sound_drum->allowNoteTails(stack, /*disregardSampleLoop=*/true);
}

// ---------------------------------------------------------------------------------------------
// The play direction (`gui/menu_item/sequence/direction.h`)

namespace {

/// @brief `sequence::Direction::getIndividualNoteRow` (`sequence/direction.h:29-52`), minus the
///        audition arm `bindings.h` documents as belonging to `NoteClipScene`.
///
/// @return A stack whose note row may be null -- legacy's own "no individual row" answer, which it
///         produces the same way (`:51`, `modelStack->addNoteRow(0, nullptr)`).
ModelStackWithNoteRow* individual_note_row(const MenuContext& ctx, void* memory) {
	ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, ctx.song, ctx.clip);
	auto* clip = static_cast<InstrumentClip*>(ctx.clip);
	// `:31-37` -- a kit row (the clip is not on affect-entire) with a selected drum.
	if (!ctx.affect_entire && ctx.is_kit && ctx.output != nullptr) {
		Drum* drum = static_cast<Kit*>(ctx.output)->selectedDrum;
		if (drum != nullptr) {
			return clip->getNoteRowForDrum(stack, drum); // still might be null, exactly as legacy says
		}
	}
	return stack->addNoteRow(0, nullptr); // `:51`
}

/// @brief Is @p ctx one this pair of functions can work on at all?
bool has_instrument_clip(const MenuContext& ctx) {
	return ctx.song != nullptr && ctx.clip != nullptr && !ctx.is_audio_clip;
}

} // namespace

int32_t read_sequence_direction(const MenuContext& ctx, bool* from_note_row) {
	if (from_note_row != nullptr) {
		*from_note_row = false;
	}
	if (!has_instrument_clip(ctx)) {
		return 0;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* stack = individual_note_row(ctx, memory);
	if (stack->getNoteRowAllowNull() != nullptr) { // `:57-59`
		if (from_note_row != nullptr) {
			*from_note_row = true;
		}
		return static_cast<int32_t>(stack->getNoteRow()->sequenceDirectionMode);
	}
	return static_cast<int32_t>(static_cast<InstrumentClip*>(ctx.clip)->sequenceDirectionMode); // `:60-62`
}

void write_sequence_direction(const MenuContext& ctx, int32_t value) {
	if (!has_instrument_clip(ctx)) {
		return;
	}
	const auto mode = static_cast<SequenceDirection>(value);
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* stack = individual_note_row(ctx, memory);
	if (stack->getNoteRowAllowNull() != nullptr) { // `:71-73`
		stack->getNoteRow()->setSequenceDirectionMode(stack, mode);
	}
	else { // `:74-76`
		static_cast<InstrumentClip*>(ctx.clip)->setSequenceDirectionMode(stack->toWithTimelineCounter(), mode);
	}
	deluge::notify::raise(deluge::notify::Dirty::Oled);
}

bool patch_cable_write(const MenuContext& ctx, ParamDescriptor learning_thing, int32_t value) {
	if (!has_model_stack(ctx) || ctx.params == nullptr) {
		return false;
	}

	// `PatchCableStrength::getModelStack` (`patch_cable_strength.cpp:213-229`), with
	// `soundEditor.getCurrentModelStack` replaced by `model_stack_for` -- the one sanctioned route.
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* stack = model_stack_for(ctx, memory);
	if (stack == nullptr) {
		return false;
	}
	// The null test comes FIRST: `getPatchCableSetSummary()` itself `FREEZE_WITH_ERROR("E412")`s on
	// a manager with no patch-cable collection (`modulation/params/param_manager.h:146-153`), and
	// `freezeWithError` dereferences the null `display` global in the host build. `AllowJibberish` is
	// the same read with no assertion (`:166-168`).
	if (stack->paramManager->getPatchCableSetAllowJibberish() == nullptr) {
		return false;
	}
	ParamCollectionSummary* summary = stack->paramManager->getPatchCableSetSummary();
	if (summary == nullptr || summary->paramCollection == nullptr) {
		return false;
	}
	ModelStackWithParamId* with_id = stack->addParamCollectionSummary(summary)->addParamId(learning_thing.data);
	// `allowCreation = true`: legacy's write path is the only caller that passes it, and it is what
	// makes editing a not-yet-existing cable create it (`:233`).
	ModelStackWithAutoParam* with_param = summary->paramCollection->getAutoParamFromId(with_id, /*allowCreation=*/true);
	if (with_param == nullptr || with_param->autoParam == nullptr) {
		return false; // `:234-236`
	}

	// `:238-241`, verbatim: "rescale from 5000 to 2^30. The magic constant is ((2^30)/5000), shifted
	// 32 bits for precision ((1<<(30+32))/5000)".
	const int64_t magic_constant = (INT64_C(922337203685477) * 5000) / kMaxMenuPatchCableValue;
	const auto final_value = static_cast<int32_t>((magic_constant * value) >> 32);
	with_param->autoParam->setCurrentValueInResponseToUserInput(final_value, with_param);

	// `:243-247`'s `automationView.possiblyRefreshAutomationEditorGrid` is a `gui/views/` call this
	// layer may not make; the same two facts every other param write raises stand in for it, and the
	// application's own listeners repaint what they own.
	deluge::notify::raise(deluge::notify::Dirty::ParamAutomation);
	deluge::notify::raise(deluge::notify::Dirty::ModTarget);
	deluge::notify::raise(deluge::notify::Dirty::GridMain);
	return true;
}

} // namespace deluge::scenes::menu::bindings
