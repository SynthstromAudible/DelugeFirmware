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
/// **Port table.** Every line below is transcribed, not called -- `gui/menu_item/` is behind the
/// `scenes-includes-outside` wall (task-4 ruling 1).
///
/// | What | Legacy |
/// |---|---|
/// | patched read | `patched_param::Integer::readCurrentValue` (`gui/menu_item/patched_param/integer.cpp:33-36`) |
/// | unpatched read | `UnpatchedParam::readCurrentValue` (`gui/menu_item/unpatched_param.cpp:36-39`) |
/// | pan read | `patched_param::Pan::readCurrentValue` (`patched_param/pan.cpp:32-34`),
/// `unpatched_param::Pan::readCurrentValue` (`unpatched_param/pan.cpp:67-70`) | | patched stack |
/// `PatchedParam::getModelStack` (`gui/menu_item/patched_param.cpp:84-87`) | | unpatched stack |
/// `UnpatchedParam::getModelStack` (`gui/menu_item/unpatched_param.cpp:41-44`) | | write |
/// `patched_param::Integer::writeCurrentValue` (`integer.cpp:38-77`), `UnpatchedParam::writeCurrentValue`
/// (`unpatched_param.cpp:46-82`) | | standard scaling | `computeCurrentValueForStandardMenuItem` /
/// `computeFinalValueForStandardMenuItem` (`gui/menu_item/value_scaling.cpp:6-8` / `:22-33`) | | pan scaling |
/// `computeCurrentValueForPan` / `computeFinalValueForPan` (`value_scaling.cpp:14-16` / `:48-58`) | | 0..50 default |
/// `Param::getMaxValue`/`getMinValue` (`gui/menu_item/param.h:34-35`), forwarded by `patched_param/integer.h:35-36` and
/// `unpatched_param.h:42-43` | | -25..25 default | `patched_param::Pan::getMaxValue`/`getMinValue`
/// (`patched_param/pan.h:26-27`), `unpatched_param::Pan` (`unpatched_param/pan.h:28-29`) | | enum default |
/// `Enumeration::selectEncoderAction`'s `[0, numOptions)` clamp (`gui/menu_item/enumeration.cpp:28`) | | toggle default
/// | `Toggle::selectEncoderAction` (`gui/menu_item/toggle.cpp:14-17`), a `Value<bool>` | | affect-entire loop |
/// `unpatched_param.cpp:52-67` and its patched sibling `integer.cpp:45-60`; the per-drum stack is
/// `getModelStackFromSoundDrum` (`model/model_stack.cpp:111-117`) | | feedback |
/// `view.sendMidiFollowFeedback(modelStack, knobPos)` (`integer.cpp:70-71`, `unpatched_param.cpp:74-75`) | | automation
/// refresh | `automationView.possiblyRefreshAutomationEditorGrid` (`integer.cpp:73-77`, `unpatched_param.cpp:77-81`) |
/// | patched learn | `PatchedParam::getLearningThing` (`gui/menu_item/patched_param.cpp:51-55`) |
/// | unpatched learn | `UnpatchedParam::getLearningThing` (`gui/menu_item/unpatched_param.cpp:88-92`) |
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
///   * `getRootUI() == &automationView` (`integer.cpp:73`). The notify design replaces "am I in the
///     automation view?" with an unconditional fact: `Dirty::ParamAutomation` is raised either way,
///     and `legacy_redraw_for` (`gui/ui/notify_drain_mapping.cpp:20`) decides what repaints.
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

/// @brief Does @p node take the pan scaling and the pan range, i.e. is it one of legacy's two `Pan`
///        menu-item classes rather than an `Integer`?
///
/// Two spellings, either of which is enough. `Hint::Pan` is the node-data port of
/// `Pan::getRenderingStyle() == PAN` (`patched_param/pan.h:28`, `unpatched_param/pan.h:30`) and is
/// what a `get`/`set` pan item would carry; `params::isParamPan` (`modulation/params/param.cpp:35-38`)
/// is the model's own predicate and covers exactly the two params legacy declares with a `Pan`
/// class (`LOCAL_PAN`, `UNPATCHED_PAN`), so a tree author who omits the hint still gets the legacy
/// arithmetic rather than a silently wrong 0..50 read.
bool is_pan(const Node& node) {
	if (node.hint == Hint::Pan) {
		return true;
	}
	return node.bind.param.valid() && params::isParamPan(node.bind.param.kind, node.bind.param.index);
}

/// @brief Which stored param set @p node's `ParamRef` lives in, or null when this context cannot
///        serve it.
///
/// Legacy reaches straight for `soundEditor.currentParamManager->getPatchedParamSet()`
/// (`integer.cpp:34`) / `getUnpatchedParamSet()` (`unpatched_param.cpp:38`). Those two accessors
/// `FREEZE_WITH_ERROR("E410"/"E411")` on a param manager that has no such collection
/// (`modulation/params/param_manager.h:110-135`) and, in a release build, cast a null pointer and
/// crash on the next member read. The menu tree is supposed to make that unreachable (a patched
/// param is only relevant where there is a `Sound`), but "supposed to" is not a guarantee a
/// library-level read should rest on, so the two summaries those accessors assert about are tested
/// here and a context that cannot serve the param reads 0 instead.
ParamSet* param_set_for(const Node& node, const MenuContext& ctx) {
	if (ctx.params == nullptr || !ctx.params->containsAnyParamCollectionsIncludingExpression()) {
		return nullptr;
	}
	switch (node.bind.param.kind) {
	case params::Kind::PATCHED:
		// `param_manager.h:128-135`: the patched set is summaries[1], present only after
		// `setupWithPatching()` (`param_manager.cpp:104-108`).
		return ctx.params->summaries[1].paramCollection != nullptr ? ctx.params->getPatchedParamSet() : nullptr;
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		// `param_manager.h:110-117`: summaries[0], which the guard above already tested.
		return ctx.params->getUnpatchedParamSet();
	default:
		return nullptr;
	}
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

/// @brief Legacy `unpatched_param.cpp:52` / `integer.cpp:45` -- write the whole kit rather than the
///        selected row?
///
/// `editingKitRow()` (`sound_editor.cpp:169-171`) is `is_kit && !is_kit_global_fx`; the held-button
/// half arrives as `affect_entire_held` (see the file header).
bool writes_whole_kit(const MenuContext& ctx) {
	return ctx.affect_entire_held && ctx.is_kit && !ctx.is_kit_global_fx && ctx.output != nullptr
	       && ctx.clip != nullptr;
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
		// static -- `write()` can be re-entered (a `set` hook is arbitrary code).
		alignas(ModelStackWithAutoParam) char drum_memory[MODEL_STACK_MAX_SIZE]; // `:60`
		int32_t note_row_index = 0;
		NoteRow* note_row = instrument_clip->getNoteRowForDrum(sound_drum, &note_row_index);
		if (note_row == nullptr) {
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
size_t name_count(const Node& node, const MenuContext& ctx) {
	if (node.bind.dynamic_names != nullptr) {
		std::string_view window[kMaxDynamicNames]{};
		return std::min(node.bind.dynamic_names(ctx, std::span<std::string_view>{window}), kMaxDynamicNames);
	}
	return std::min(node.bind.names.size(), kMaxDynamicNames);
}

} // namespace

int32_t read(const Node& node, const MenuContext& ctx) {
	const Binding& bind = node.bind;

	if (bind.param.valid()) {
		ParamSet* set = param_set_for(node, ctx);
		if (set == nullptr) {
			return 0;
		}
		// `integer.cpp:34` / `unpatched_param.cpp:38`: the *stored* value, then the menu scaling.
		const int32_t stored = set->getValue(bind.param.index);
		return is_pan(node) ? current_value_pan(stored) : current_value_standard(stored);
	}

	if (bind.get != nullptr) {
		return bind.get(ctx);
	}
	return 0;
}

void write(const Node& node, const MenuContext& ctx, int32_t value) {
	const Binding& bind = node.bind;

	// A `get`/`set` (or `action`) node: the hook owns the model write, so all this layer knows is
	// that something the OLED shows has changed.
	if (!bind.param.valid()) {
		if (bind.set != nullptr) {
			bind.set(ctx, value);
		}
		notify::raise(notify::Dirty::Oled);
		return;
	}

	if (!has_model_stack(ctx)) {
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
	// `patched_param/pan.cpp:28-30` / `unpatched_param/pan.cpp:63-65`).
	const int32_t final_value = is_pan(node) ? final_value_pan(value) : final_value_standard(value);

	if (writes_whole_kit(ctx)) {
		write_whole_kit(node, ctx, final_value);
	}
	else {
		// `unpatched_param.cpp:70` / `integer.cpp:64` -- the normal case of just one sound.
		stack->autoParam->setCurrentValueInResponseToUserInput(final_value, stack);
	}

	// `integer.cpp:70-71` / `unpatched_param.cpp:74-75`. `view.sendMidiFollowFeedback` itself is
	// behind the `gui/` wall, so the knob position is computed here and handed to the hook.
	const int32_t knob_pos = stack->paramCollection->paramValueToKnobPos(final_value, stack);
	if (g_feedback_hook != nullptr) {
		g_feedback_hook(ctx, node, knob_pos);
	}

	// `integer.cpp:73-77` / `unpatched_param.cpp:77-81` -- legacy's root-UI-gated
	// `possiblyRefreshAutomationEditorGrid`, plus the knob-indicator refresh
	// `setCurrentValueInResponseToUserInput` reaches through
	// `ParamManager::notifyParamModifiedInSomeWay` -> `view.notifyParamAutomationOccurred`
	// (`modulation/automation/auto_param.cpp:1161`, `gui/views/view.cpp:1691-1711`), whose
	// deferred `displayAutomation()` calls `setKnobIndicatorLevels()` (`view.cpp:1741-1744`).
	// Both are unconditional facts here; `legacy_redraw_for` (`gui/ui/notify_drain_mapping.cpp:20`)
	// decides what repaints, and the notifications spec maps this pair to exactly those two effects
	// ("ModKnobs knob indicators re-derived; automation views repaint").
	notify::raise(notify::Dirty::ParamAutomation);
	notify::raise(notify::Dirty::ModTarget);
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
		if (is_pan(node)) {
			// `patched_param/pan.h:26-27`, `unpatched_param/pan.h:28-29`.
			return IntRange{kMinMenuRelativeValue, kMaxMenuRelativeValue, 1};
		}
		// `param.h:34-35` (`kMaxMenuValue` / `kMinMenuValue`), forwarded by
		// `patched_param/integer.h:35-36` and `unpatched_param.h:42-43`.
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

} // namespace deluge::scenes::menu::bindings
