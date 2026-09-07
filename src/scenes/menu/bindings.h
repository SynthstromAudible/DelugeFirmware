#pragma once

/// @brief The only place the menu system reads and writes the model (spec E §4, amended by
///        `.superpowers/sdd/2026-08-27-ui-scenes-menus-1/task-4-brief.md`).
///
/// A `Node`'s `Binding` is plain data: either a `ParamRef` (which param on the model) or a
/// `get`/`set` pair. These five functions are what turns that data into an actual read, an actual
/// write, a range, a name list and a MIDI-learn descriptor. Every legacy `readCurrentValue` /
/// `writeCurrentValue` / `getFinalValue` / `getMaxValue` / `getLearningThing` override on a
/// **`ParamRef`-backed** item collapses into them; see `bindings.cpp` for the line-by-line port
/// table. (Items whose value does not live in a param set keep their own logic, as a `get`/`set`
/// pair the tree supplies -- that is what `Binding::get`/`set` are for, and nothing here scales or
/// interprets what those hooks return.)
///
/// @note **Which scaling a param uses is declared, not inferred** (`node.h`'s `Scaling`). The
///       default, `Scaling::Standard`, is legacy's `patched_param::Integer` /
///       `menu_item::UnpatchedParam` arithmetic. Two exceptions exist and Tasks 6-8 must name them
///       at the call site, via `param()`'s trailing `scaling` argument:
///       `Scaling::HalfPrecision` for **osc pulse width** (`gui/menu_item/osc/pulse_width.h:32,34-37`
///       -- `LOCAL_OSC_A_PHASE_WIDTH` / `LOCAL_OSC_B_PHASE_WIDTH`) and for the **compressor params**
///       (`gui/menu_item/audio_compressor/compressor_params.h:29-33`), which cannot take the
///       negative stored values `Scaling::Standard` produces (`value_scaling.cpp:36`); and
///       `Scaling::Pan` for the two pan params, which `bindings.cpp` additionally infers as a
///       backstop (`scaling_for`) since `params::isParamPan` can recognise them and no equivalent
///       predicate exists for the half-precision pair.
///
/// @note **`model/model_stack.h` and `modulation/automation/auto_param.h` are reached by
///       `bindings.cpp` only** -- it and `context.cpp` are the two files the
///       `scenes-model-stack-outside-context` ratchet (`scripts/check_layer_rules.py`) excludes.
///       This header pulls in neither: it needs no model type but `ParamDescriptor`, which is a
///       leaf wrapper around an `int32_t`.
///
/// @note **No `gui/menu_item/`.** The value scaling this ports (`computeCurrentValueForStandardMenuItem`
///       and friends, `gui/menu_item/value_scaling.cpp`) lives behind the `gui/` wall the
///       `scenes-includes-outside` ratchet enforces, so `bindings.cpp` transcribes the arithmetic
///       with the legacy line cited on each function. `sim/scenes_spec/bindings_spec.cpp` compares
///       the transcription against the legacy functions directly -- the spec may include `gui/`,
///       the library may not.

#include "modulation/params/param_descriptor.h"
#include "scenes/menu/node.h"
#include "shell/profile.h" // ui::shell::TextScratch

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

class Drum;
class ModControllableAudio;
class ModelStackWithSoundFlags;
class Sound;

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::bindings {

/// @brief Read what @p node currently shows, in menu units (0..50 for a standard param).
///
/// A `ParamRef` binding reads the param set's stored value and scales it the way legacy's
/// `readCurrentValue` did; a `get`/`set` binding just calls `get`. A binding with neither, or a
/// context whose param manager cannot serve the param, reads 0.
///
/// @param node The item to read.
/// @param ctx  What is being edited (`context_for`'s output).
/// @return The menu-unit value.
int32_t read(const Node& node, const MenuContext& ctx);

/// @brief Write @p value (menu units) into the model, exactly as legacy's `writeCurrentValue` did.
///
/// A `ParamRef` binding goes through `model_stack_for` -> `get{Patched,Unpatched}AutoParamFromId`
/// -> `AutoParam::setCurrentValueInResponseToUserInput`, extends the write to every `SoundDrum` in
/// the kit when affect-entire is held on a kit row, calls the feedback hook with the resulting knob
/// position, and raises `Dirty::ParamAutomation` + `Dirty::ModTarget`. A `get`/`set` binding calls
/// `set` and raises `Dirty::Oled`.
///
/// @param node  The item to write.
/// @param ctx   What is being edited.
/// @param value The new value, in menu units.
void write(const Node& node, const MenuContext& ctx, int32_t value);

/// @brief The bounds and coarse step @p node is edited inside.
///
/// `bind.range(ctx)` if the node has one, else `bind.fixed_range` if it has one, else the legacy
/// default for its kind. @see `bindings.cpp` for each default's legacy citation.
///
/// @param node The item to bound.
/// @param ctx  What is being edited (a dynamic `range` hook reads it).
/// @return The range.
IntRange range(const Node& node, const MenuContext& ctx);

/// @brief Fill @p out with @p node's `Kind::Enum` option names.
///
/// Static `bind.names` ids are resolved through `scenes::deluge_profile().text_or_digits(id,
/// scratch[i])` -- the one sanctioned text path (`shell/profile.h:147`), which already implements
/// the digits fallback for an unresolvable id. A `bind.dynamic_names` hook fills `out` itself and
/// never touches @p scratch.
///
/// @param node    The item to name.
/// @param ctx     What is being edited.
/// @param out     Where the names land; written up to `kMaxDynamicNames` entries.
/// @param scratch Digit storage backing @p out, one entry per @p out entry, living in the caller's
///                frame for at least as long as @p out is read. Must be the same length as @p out;
///                the shorter of the two bounds the result if they differ.
/// @return How many names were written.
size_t names(const Node& node, const MenuContext& ctx, std::span<std::string_view> out,
             std::span<deluge::ui::shell::TextScratch> scratch);

/// @brief The `ParamDescriptor` MIDI/CC learn (E3) binds to @p node.
///
/// The port of `PatchedParam::getLearningThing` (`gui/menu_item/patched_param.cpp:51-55`) and
/// `UnpatchedParam::getLearningThing` (`gui/menu_item/unpatched_param.cpp:88-92`).
///
/// @param node The item to learn.
/// @param ctx  What is being edited.
/// @return The descriptor, or a null one (`ParamDescriptor::isNull()`) for a node with no learnable
///         param.
ParamDescriptor learn_target(const Node& node, const MenuContext& ctx);

/// @brief One thing a non-`ParamRef` `Binding::set` hook writes to.
///
/// Legacy's ~20 non-param items each spelled out the same affect-entire loop in their own
/// `writeCurrentValue` (`filter/mode.h:44-59`, `lfo/type.h:51-73`, `unison/count.h:38-63`, ...):
/// "if the AFFECT-ENTIRE button is held on a kit row, do every `SoundDrum` in the kit, else do the
/// selected one". `apply_to_targets` below is that loop, once, and a `SoundTarget` is one
/// iteration of it.
struct SoundTarget {
	/// @brief What legacy called `soundEditor.currentModControllable` (or `soundDrum`, in the kit
	///        loop). Never null.
	ModControllableAudio* mod = nullptr;
	/// @brief The same object as a `Sound`, or null when it is not one -- a song-level or
	///        audio-clip `GlobalEffectable` has filters and a compressor but no oscillators.
	Sound* sound = nullptr;
	/// @brief A stack over @p sound with sound flags added, or null when @p sound is null.
	///
	/// This is why the helper exists at all: `model/model_stack.h` may be included by
	/// `context.cpp` and this file only (the `scenes-model-stack-outside-context` ratchet), so the
	/// node tables cannot assemble their own stack for `Sound::setNumUnison`,
	/// `setModulatorTranspose`, `recalculateAllVoicePhaseIncrements` and friends.
	ModelStackWithSoundFlags* stack = nullptr;
	/// @brief True when this target came from the affect-entire kit sweep rather than being the
	///        one selected thing. Several legacy items filter *inside* that loop and not outside it
	///        (`filter_route.h:45`, `osc/sync.h:42`, `modulator/transpose.h:56`,
	///        `modulator/destination.h:46`), so a hook needs to know which it is looking at.
	bool whole_kit = false;
};

/// @brief What `apply_to_targets` calls per target.
/// @param target The thing to write.
/// @param value  The menu value being committed.
/// @param arg    The item's own baked-in index -- an oscillator's `source_id_`, an LFO's `lfoId_`,
///               whatever the legacy class held as a member. A `Binding::set` is a plain function
///               pointer with no room for one, and `apply_to_targets`' hook is too, so the tables
///               pass it through here rather than writing one hook per index.
/// @param ctx    The context the write is happening in (for a hook that needs the song or clip).
using ApplyFn = void (*)(const SoundTarget& target, int32_t value, int32_t arg, const MenuContext& ctx);

/// @brief Run @p fn over every `ModControllableAudio` a write from @p ctx should reach.
///
/// One target normally; every `SoundDrum` in the kit when affect-entire is held on a kit row
/// (`writes_whole_kit`, the same condition `write()` uses for a `ParamRef`). A legacy item that
/// additionally filtered *inside* its loop -- `filter_route.h:45`'s "both filters on",
/// `osc/sync.h:42`'s "not FM and neither source is a sample", `modulator/transpose.h:56`'s "FM
/// only" -- applies that test inside @p fn, exactly where legacy applied it.
///
/// @param ctx   What is being edited.
/// @param value The menu value to commit.
/// @param arg   Passed straight through to @p fn -- see `ApplyFn`. 0 when the hook needs none.
/// @param fn    The per-target write.
void apply_to_targets(const MenuContext& ctx, int32_t value, int32_t arg, ApplyFn fn);

/// @brief Would a write from @p ctx sweep every `SoundDrum` in the kit?
///
/// Legacy's `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR &&
/// soundEditor.editingKitRow()` (`gui/menu_item/unpatched_param.cpp:52`), i.e. exactly the condition
/// `apply_to_targets` branches on. Exposed because a couple of legacy items take a *different code
/// path* rather than a filtered one when it is false -- `sample::PitchSpeed` and
/// `sample::Interpolation` write `getCurrentSampleControls()` (which is the audio clip's, on an
/// audio clip) instead of a source's (`sample/pitch_speed.h:44-63`, `sample/interpolation.h:37-59`).
bool writes_whole_kit(const MenuContext& ctx);

/// @name Value scaling for `get`/`set` items that hold a full-range integer of their own
///
/// A `ParamRef` node's scaling is declared on its `Binding` and applied by `read`/`write`. But a
/// dozen batch-2 items are `get`/`set` items that nonetheless store the same INT32-range value a
/// param would (the MIDI/CV arpeggiator and randomizer settings live on `ArpeggiatorSettings`, not
/// in a param set) and call the same `value_scaling.cpp` helpers themselves
/// (`arpeggiator/midi_cv/rate.h:28-33`, `randomizer/midi_cv/spread_gate.h:27-32`, ...). Rather than
/// re-transcribing the arithmetic in each table file, the transcription this file already owns is
/// exposed. The `unsigned` pair is new -- batch 1 needed only the standard, half-precision and pan
/// ones.
/// @{

/// @brief `computeCurrentValueForStandardMenuItem` (`gui/menu_item/value_scaling.cpp:6-8`):
///        INT32 range -> 0..50.
int32_t standard_to_menu(int32_t stored);
/// @brief `computeFinalValueForStandardMenuItem` (`value_scaling.cpp:22-33`): 0..50 -> INT32 range.
int32_t menu_to_standard(int32_t menu_value);
/// @brief `computeCurrentValueForPan` (`value_scaling.cpp:14-16`): INT32 range -> -25..25.
int32_t pan_to_menu(int32_t stored);
/// @brief `computeFinalValueForPan` (`value_scaling.cpp:48-58`): -25..25 -> INT32 range.
int32_t menu_to_pan(int32_t menu_value);
/// @brief `computeCurrentValueForUnsignedMenuItem` (`value_scaling.cpp:18-20`): 0..UINT32_MAX ->
///        0..50.
int32_t unsigned_to_menu(uint32_t stored);
/// @brief `computeFinalValueForUnsignedMenuItem` (`value_scaling.cpp:60-62`): 0..50 ->
///        0..UINT32_MAX. Note it has no `kMaxMenuValue` special case, unlike its signed sibling.
uint32_t menu_to_unsigned(int32_t menu_value);
/// @}

/// @brief `SoundEditor::allowsNoteTails` (`gui/ui/sound_editor.h:63`), recomputed from @p ctx.
///
/// Legacy caches it once per menu entry (`sound_editor.cpp:1811-1816`: true when there is no sound,
/// else `Sound::allowNoteTails(stack, /*disregardSampleLoop=*/true)`), and
/// `arpeggiator::IncludeInKitArp` reads the cached copy (`include_in_kit_arp.h:28-33,50-52`). It
/// lives here rather than on `MenuContext` because computing it needs a `ModelStackWithSoundFlags`,
/// and `model/model_stack.h` may be included by `context.cpp` and this file only.
bool allows_note_tails(const MenuContext& ctx);

/// @brief `Sound::allowNoteTails` for one `Drum` of @p ctx's kit, on that drum's own model stack.
///
/// The per-target half of the test above, and the one legacy's affect-entire sweep in
/// `arpeggiator::IncludeInKitArp::writeCurrentValue` (`include_in_kit_arp.h:33-45`) actually
/// applies: it builds a `ModelStackWithSoundFlags` from each `SoundDrum` and asks that drum, rather
/// than reusing the context's answer. A non-sound drum (MIDI, gate) has no `Sound` to ask and is
/// always allowed, exactly as legacy's `bool allowsNoteTails = true;` initialiser says (`:33`).
///
/// Lives here for the same reason `allows_note_tails` does: `getModelStackFromSoundDrum`
/// (`model/model_stack.h:297`) needs `model/model_stack.h`.
///
/// @param ctx  What is being edited -- only its kit membership matters.
/// @param drum The drum to ask. Null answers true (nothing to refuse).
bool drum_allows_note_tails(const MenuContext& ctx, Drum* drum);

/// @brief Read the play direction `sequence::Direction` would show (`sequence/direction.h:54-64`).
///
/// Lives here, not in the node table, for the reason `SoundTarget::stack` does: resolving the
/// individual note row a clip's play direction may live on needs a `ModelStackWithNoteRow`, and
/// `model/model_stack.h` may be included by `context.cpp` and this file only (the
/// `scenes-model-stack-outside-context` ratchet).
///
/// @param ctx           What is being edited.
/// @param from_note_row Optional out-parameter: true when an individual note row supplied the
///                      value, which is also the condition under which legacy offers the fourth
///                      option, NONE (`direction.h:83-86`).
/// @return The `SequenceDirection` as an `int32_t`.
///
/// @note **Partial, deliberately.** Legacy's `getIndividualNoteRow` (`direction.h:29-52`) has a
///       third arm for a non-kit clip, reached only when `soundEditor.selectedNoteRow` is set, that
///       reads `instrumentClipView.lastAuditionedYDisplay` and creates a note row if none exists.
///       Both are UI state `src/scenes` may not read; that arm belongs to spec E's `NoteClipScene`.
///       Everything else -- the kit-row arm and the "no note row, use the clip's own mode"
///       fallback -- is transcribed exactly.
int32_t read_sequence_direction(const MenuContext& ctx, bool* from_note_row = nullptr);

/// @brief Commit a play-direction change (`sequence::Direction::writeCurrentValue`,
///        `sequence/direction.h:66-77`), through `NoteRow::setSequenceDirectionMode` or
///        `InstrumentClip::setSequenceDirectionMode` as legacy chooses between them.
///
/// @param ctx   What is being edited.
/// @param value The new `SequenceDirection`.
void write_sequence_direction(const MenuContext& ctx, int32_t value);

/// @brief What `write()` calls after a `ParamRef` write, in place of legacy's
///        `view.sendMidiFollowFeedback(modelStack, knobPos)` (`gui/views/view.cpp:1713`).
///
/// `src/scenes` may not include `gui/views/`, so the call is inverted into a hook the application
/// installs.
using FeedbackHook = void (*)(const MenuContext& ctx, const Node& node, int32_t knob_pos);

/// @brief Install (or, with `nullptr`, remove) the MIDI-follow feedback hook.
/// @param hook The hook to call after each `ParamRef` write, or `nullptr` for none.
void set_feedback_hook(FeedbackHook hook);

/// @brief Write a **patch cable's** strength, creating the cable if it does not exist yet.
///
/// The port of `PatchCableStrength::writeCurrentValue` (`gui/menu_item/patch_cable_strength.cpp:231-248`)
/// and the `getModelStack(memory, /*allowCreation=*/true)` it runs on (`:203-220`). It lives here,
/// not in `nodes/patch_cables.cpp`, for exactly the reason `SoundTarget::stack` does: reaching a
/// patch cable's `AutoParam` means `paramManager->getPatchCableSetSummary()` ->
/// `addParamCollectionSummary` -> `addParamId` -> `getAutoParamFromId`, and `model/model_stack.h`
/// may be included by this file and `context.cpp` only.
///
/// A patch cable has no `ParamRef` spelling (`params::Kind::PATCH_CABLE` addresses a
/// `ParamDescriptor`, not an 8-bit index), which is why this is a free function taking the
/// descriptor rather than another `Binding` arm.
///
/// @param ctx           What is being edited.
/// @param learning_thing The cable's own id -- `getLearningThing()`: the destination param plus the
///                      cable's source, plus the range source for a range cable
///                      (`patch_cable_strength/regular.cpp:30-34`, `range.cpp:25-30`).
/// @param value         The new strength, in menu units
///                      (`kMinMenuPatchCableValue`..`kMaxMenuPatchCableValue`).
/// @return Whether a cable was reached (false when the param manager holds no patch-cable set, or
///         when creation failed).
bool patch_cable_write(const MenuContext& ctx, ParamDescriptor learning_thing, int32_t value);

} // namespace deluge::scenes::menu::bindings
