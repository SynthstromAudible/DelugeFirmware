#pragma once

/// @brief The only place the menu system reads and writes the model (spec E §4, amended by
///        `.superpowers/sdd/2026-08-27-ui-scenes-menus-1/task-4-brief.md`).
///
/// A `Node`'s `Binding` is plain data: either a `ParamRef` (which param on the model) or a
/// `get`/`set` pair. These five functions are what turns that data into an actual read, an actual
/// write, a range, a name list and a MIDI-learn descriptor. Every legacy `readCurrentValue` /
/// `writeCurrentValue` / `getFinalValue` / `getMaxValue` / `getLearningThing` override the menus
/// carry today collapses into them; see `bindings.cpp` for the line-by-line port table.
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
/// scratch[i])` -- the one sanctioned text path (`shell/profile.h:156`), which already implements
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

/// @brief What `write()` calls after a `ParamRef` write, in place of legacy's
///        `view.sendMidiFollowFeedback(modelStack, knobPos)` (`gui/views/view.cpp:1713`).
///
/// `src/scenes` may not include `gui/views/`, so the call is inverted into a hook the application
/// installs.
using FeedbackHook = void (*)(const MenuContext& ctx, const Node& node, int32_t knob_pos);

/// @brief Install (or, with `nullptr`, remove) the MIDI-follow feedback hook.
/// @param hook The hook to call after each `ParamRef` write, or `nullptr` for none.
void set_feedback_hook(FeedbackHook hook);

} // namespace deluge::scenes::menu::bindings
