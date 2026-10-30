#pragma once

/// @brief The only place the menu system reads and writes the model (spec E §4, amended by
///        `.superpowers/sdd/2026-08-27-ui-scenes-menus-1/task-4-brief.md`).
///
/// A `Node`'s `Binding` is plain data: a target (a setting or a param, which `scenes/bind` reads and the menu's sink
/// writes) or a `get`/`set` pair. These functions turn that data into a read, a write, a range, a name list and a
/// MIDI-learn descriptor. (Items whose value has no target keep their own logic, as a `get`/`set` pair the tree
/// supplies -- nothing here scales or interprets what those hooks return.)
///
/// @note **`model/model_stack.h` and `modulation/automation/auto_param.h` are reached by
///       `bindings.cpp` only** -- it and `context.cpp` are the two files the
///       `scenes-model-stack-outside-context` ratchet (`scripts/check_layer_rules.py`) excludes.
///       This header pulls in neither: it needs no model type but `ParamDescriptor`, which is a
///       leaf wrapper around an `int32_t`.
///
/// @note **No `gui/menu_item/`.** The value scaling the `get`/`set` items reuse (`standard_to_menu` and friends)
///       is the param displays' (`scenes/bind/param_display.cpp`), which transcribes legacy's
///       `gui/menu_item/value_scaling.cpp`; `sim/scenes_spec/bindings_spec.cpp` compares it with the legacy functions.

#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "modulation/params/param_descriptor.h"
#include "scenes/menu/node.h"
#include "shell/profile.h" // ui::shell::TextScratch

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::bindings {

/// @brief Whether @p node is shown: a target node when its descriptor's conditions hold (and its target resolves), any
///        other node by its `relevant` hook (none: always).
/// @param node The node.
/// @param ctx  The menu's context.
/// @return Whether the node is shown.
[[nodiscard]] bool relevant(const Node& node, const MenuContext& ctx);

/// @brief @p node's children a menu lists, in order: `dynamic_children` resolved first, then each child `relevant`
///        admits. The one rule every list and column of the menu applies.
/// @param node The page.
/// @param ctx  The menu's context.
/// @param out  Where the listed children go.
/// @return How many were written.
size_t listed_children(const Node& node, const MenuContext& ctx, std::span<const Node*> out);

/// @brief The value @p target's record holds now, as its descriptor stores it (a CHOICE row: the choice's index), for a
///        select chain or a relevance rule that branches on a setting.
/// @param target The setting.
/// @param ctx    The menu's context.
/// @return The stored value, or nullopt where the target does not resolve.
[[nodiscard]] std::optional<int32_t> stored(const deluge::scenes::bind::Target& target, const MenuContext& ctx);

/// @brief Read what @p node currently shows, in menu units (0..50 for a standard param).
///
/// A target binding reads through `bind::read` (a PARAM target: the value its param plays, scaled to
/// menu units); a `get`/`set` binding just calls `get`. A binding with neither reads 0.
///
/// @param node The item to read.
/// @param ctx  What is being edited (`context_for`'s output).
/// @return The menu-unit value.
int32_t read(const Node& node, const MenuContext& ctx);

/// @brief Write @p value (menu units) into the model, exactly as legacy's `writeCurrentValue` did.
///
/// A target binding hands its write to the app's sink (a PARAM target: `SetParamValue`, or
/// `SetKitParamValue` when affect-entire is held on a kit row) and raises `Dirty::Oled`, plus
/// `Dirty::ParamAutomation`, `Dirty::ModTarget` and `Dirty::GridMain` for a param. A `get`/`set`
/// binding calls `set` and raises `Dirty::Oled`.
///
/// @param node  The item to write.
/// @param ctx   What is being edited.
/// @param value The new value, in menu units.
void write(const Node& node, const MenuContext& ctx, int32_t value);

/// @brief Commit an offset edit through `node.bind.adjust` (spec E-note-family §5.2).
///
/// Mirrors `write`'s `get`/`set` hook arm: call the hook, then raise `Dirty::Oled` -- the hook
/// itself performs whatever model write legacy's `adjustVelocity`/`adjustProbability`/
/// `adjustNoteIterance`/`adjustNoteFillWithOffset` did, so this layer knows only that something
/// the OLED shows has changed. A node with no `bind.adjust` does nothing (`validate_tree` reports
/// this arm being reached with `adjust == nullptr` as a table bug, not something to guess at here).
///
/// @param node   The item to write. `node.bind.adjust` must be set.
/// @param ctx    What is being edited.
/// @param offset The accumulated detents since the last commit.
void adjust(const Node& node, const MenuContext& ctx, int32_t offset);

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
/// @brief Which spelling of an option `names` writes.
enum class NameForm : uint8_t {
	Full,  ///< the option's name: a list row, a notification
	Short, ///< a horizontal column's: the short form where the node takes one (`Node::short_options`)
};

size_t names(const Node& node, const MenuContext& ctx, std::span<std::string_view> out,
             std::span<deluge::ui::shell::TextScratch> scratch, NameForm form = NameForm::Full);

/// @brief What a MIDI knob learned on @p node turns: the param, on the controls of the owner @p ctx edits --
///        `PatchedParam`/`UnpatchedParam::getLearningThing` (`gui/menu_item/patched_param.cpp:51-55`,
///        `unpatched_param.cpp:88-92`), by address.
/// @param node The item to learn.
/// @param ctx  What is being edited.
/// @return The target, or none for a node with no learnable param or a context that edits no owner.
std::optional<deluge::project::ParamTarget> learn_target(const Node& node, const MenuContext& ctx);

/// @brief The controls a learn on @p ctx's owner lands on: its sound's, or its chain's owner's.
std::optional<deluge::project::ControlsAddress> controls_owner(const MenuContext& ctx);

/// @brief Write the song's row @p path to @p value (its menu value) through @p ctx's sink: a SONG request. A
///        context with no sink -- a song the app does not play -- writes nothing.
/// @param ctx   What is being edited.
/// @param path  The row, which outlives the call.
/// @param value The menu value.
void write_song(const MenuContext& ctx, std::string_view path, int32_t value);

/// @brief Would a write from @p ctx sweep every `SoundDrum` in the kit?
///
/// Legacy's `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR &&
/// soundEditor.editingKitRow()` (`gui/menu_item/unpatched_param.cpp:52`): the request a hook makes then names the
/// whole kit (`Addresses::affect_entire`). Exposed because a couple of legacy items take a *different code
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

/// @brief Read the play direction `sequence::Direction` would show (`sequence/direction.h:65-76`).
///
/// The direction is a kit row's note row's, where a kit row the clip is not on affect-entire for has one, else the
/// clip's: read from that record.
///
/// @param ctx           What is being edited.
/// @param from_note_row Optional out-parameter: true when an individual note row supplied the
///                      value, which is also the condition under which legacy offers the fourth
///                      option, NONE (`direction.h:103-105`).
/// @return The `SequenceDirection` as an `int32_t`.
///
/// @note **The third arm is ported too, as of Task 6 -- for `Root::NoteRowEditor` only, fix round
///       1.** Legacy's `getIndividualNoteRow` has a non-kit arm reached only when
///       `soundEditor.selectedNoteRow` is set (`direction.h:50-60`), which reads
///       `instrumentClipView.lastAuditionedYDisplay` and creates a note row if none exists.
///       `soundEditor.selectedNoteRow` is itself a UI-state latch this layer may not read directly,
///       set only by `checkPermissionToBeginSession`'s else-if (`:117-119`,
///       `isUIModeActive(UI_MODE_AUDITIONING)`); `bindings.cpp`'s own comment on this function works
///       out, from every entry point this port has, that the latch is ALWAYS true under
///       `Root::NoteRowEditor` and never true from any of the eight gated `openShellMenu` sites --
///       **but that is narrower than "every other path opens with `UI_MODE_NONE`"**. Legacy also
///       reaches this same item from the regular sound-editor roots (`Root::Sound`/`Root::MidiOrCv`)
///       while an audition pad is held, which can latch the flag true there too and let the user
///       switch rows without leaving the menu (`bindings.cpp`'s divergence note, same comment). This
///       port does not build that case; `ctx.root == MenuContext::Root::NoteRowEditor` on a non-kit
///       clip is the equivalent condition for the case it DOES build. That arm goes through
///       `note_bridge()`'s `row_sequence_direction` (`scenes/menu/note_bridge.h`); a kit clip, or no
///       bridge answer, falls through to the kit-row arm and the "no note row, use the clip's own
///       mode" fallback below, transcribed exactly as before.
int32_t read_sequence_direction(const MenuContext& ctx, bool* from_note_row = nullptr);

/// @brief Commit a play-direction change (`sequence::Direction::writeCurrentValue`,
///        `sequence/direction.h:78-90`): a request to the sink on the record legacy chooses -- the kit row's note row,
///        or the clip.
///
/// @param ctx   What is being edited.
/// @param value The new `SequenceDirection`.
void write_sequence_direction(const MenuContext& ctx, int32_t value);

} // namespace deluge::scenes::menu::bindings
