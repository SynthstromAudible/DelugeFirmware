#pragma once

/// @brief The note editor and note-row editor as data: the two horizontal roots, their four value
///        leaves apiece (minus the row family's velocity, which legacy never gave a row), and the
///        two custom-iterance pages a select press on either family's iterance leaf can chain to.
///
/// Spec E-note-family §5.5, each row citing its legacy object in `gui/ui/menus.cpp`. All state
/// lives behind `instrumentClipView`, reached through `NoteBridge` (`scenes/menu/note_bridge.h`,
/// Task 4) rather than any model read here -- every hook below calls `note_bridge()` and nothing
/// else.
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `note_editor_root` | `noteEditorRootMenu` (`:1880`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `note_velocity` | `noteVelocityMenu` | `note::Velocity` (`note/velocity.h:31`) | Int |
/// | `note_editor_probability` | `noteProbabilityMenu` | `note::Probability` (`note/probability.h:32`) | Int |
/// | `note_iterance` | `noteIteranceMenu` | `note::IterancePreset` (`note/iterance_preset.h:32`) | Int |
/// | `note_fill` | `noteFillMenu` | `note::Fill` (`note/fill.h:32`) | Int |
/// | `note_custom_iterance_root` | `noteCustomIteranceRootMenu` (`:1859`) | `Submenu` | Submenu (Vertical) |
/// | `note_iterance_divisor` | `noteCustomIteranceDivisor` | `note::IteranceDivisor` (`note/iterance_divisor.h:28`) | Int |
/// | `note_iterance_step_1..8` | `noteCustomIteranceStep1..8` | `note::IteranceStepToggle` (`note/iterance_step_toggle.h:26`) | Toggle |
/// | `note_row_editor_root` | `noteRowEditorRootMenu` (`:1927`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `note_row_probability` | `noteRowProbabilityMenu` | `note_row::Probability` (`note_row/probability.h:31`) | Int |
/// | `note_row_iterance` | `noteRowIteranceMenu` | `note_row::IterancePreset` (`note_row/iterance_preset.h:36`) | Int |
/// | `note_row_fill` | `noteRowFillMenu` | `note_row::Fill` (`note_row/fill.h:32`) | Int |
/// | `note_row_custom_iterance_root` | `noteRowCustomIteranceRootMenu` (`:1907`) | `Submenu` | Submenu (Vertical) |
/// | `note_row_iterance_divisor` | `noteRowCustomIteranceDivisor` | `note_row::IteranceDivisor` (`note_row/iterance_divisor.h:29`) | Int |
/// | `note_row_iterance_step_1..8` | `noteRowCustomIteranceStep1..8` | `note_row::IteranceStepToggle` (`note_row/iterance_step_toggle.h:26`) | Toggle |
// clang-format on
///
/// @note **No absolute `set` on velocity, probability, iterance or fill.** Each moves every note
///       under a zoomed-out square relative to itself, so there is no single value to write --
///       `Binding::adjust` is legacy's `adjustVelocity`/`adjustNoteProbabilityWithOffset`/
///       `adjustNoteIteranceWithOffset`/`adjustNoteFillWithOffset` (note family) and their row
///       siblings, exactly as `node.h`'s `AdjustFn` doc names them. `get` stays the ordinary read
///       so the column still shows a real value between turns.
///
/// @note **The two guards.** `permitted_note`/`permitted_row` port
///       `note::SelectedNote::selectEncoderActionIsPermitted()`
///       (`note/selected_note.h:29-42`) and `note_row::SelectedNoteRow`'s twin
///       (`note_row/selected_note_row.h:34-43`): each tests the bridge's own selection predicate
///       and, on refusal, raises legacy's own popup text -- `"Select Note"` / `"Select Row"`,
///       literally legacy's (`note/selected_note.h:37`, `note_row/selected_note_row.h:36`). Every
///       leaf of its family carries the guard **except** the step toggles: `IteranceStepToggle`
///       derives from `Toggle`, not `SelectedNote`/`SelectedNoteRow`, and overrides no permission
///       hook (`note/iterance_step_toggle.h:26`, `note_row/iterance_step_toggle.h:26`).
///
/// @note **The iterance chain.** `IterancePreset::selectButtonPress()`
///       (`note/iterance_preset.h:59-67`, `note_row/iterance_preset.h:72-80`) opens the matching
///       custom-iterance root only when the current value is `kCustomIterancePreset`; every other
///       value ascends. `chain_note_iterance`/`chain_note_row_iterance` port that, and are the one
///       place in this file a hook is defined in the header rather than the `.cpp` -- the same
///       choice `nodes/sound_arp.h` makes for `arp_preset_chain`, because the function needs the
///       address of a `Node` declared alongside it.
///
/// @note **The "OFF becomes 1of1" conversion.** `IteranceDivisor::writeCurrentValue` and
///       `IteranceStepToggle::readCurrentValue`/`writeCurrentValue`/`isRelevant` all convert a note
///       or row's `kDefaultIteranceValue` ("OFF") to `kCustomIteranceValue` ("1of1") before editing
///       from it, so the custom-iterance page always has a concrete divisor and step set to edit
///       (`note/iterance_divisor.h:45-49`, `note/iterance_step_toggle.h:39-43,52-56`, and their row
///       twins). `IteranceStepToggle::isRelevant` alone reads the **unconverted** value, because it
///       tests for `kDefaultIteranceValue` directly (`note/iterance_step_toggle.h:71-80`).
///
/// @note **`note_editor_probability`, not `note_probability`.** `nodes::note_probability` already
///       names `sound_randomizer.h`'s `randomizerNoteProbabilityMenu` (the *randomizer's* per-note
///       trigger chance, an unrelated `RandomizerUnpatchedParam`); the two would collide as C++
///       identifiers in the same `nodes` namespace. Every other leaf here keeps the plain name the
///       design spec gives it -- this is the one substitution a real naming clash forced.
///
/// @note **`note_editor_root` has no velocity counterpart in the row family.** Legacy's own
///       `noteRowEditorRootMenu` (`gui/ui/menus.cpp:1927-1933`) lists `sequenceDirectionMenu`,
///       `noteRowProbabilityMenu`, `noteRowIteranceMenu`, `noteRowFillMenu` -- four children, no
///       row-velocity item exists anywhere in `gui/menu_item/note_row/`. `sequence_direction` is
///       `nodes/sound_actions.h`'s existing node, reused here unchanged: its own note-row bridge
///       path is a later task's (spec §5.5's resolution 2), not this file's to edit.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/sound_actions.h" // &sequence_direction, note_row_editor_root's first child

#include "definitions_cxx.hpp" // kNumProbabilityValues, kNumIterancePresets, kCustomIterancePreset, FillMode

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @name The two "Select Note"/"Select Row" guards. Bodies in the `.cpp`.
/// @{
bool permitted_note(const MenuContext& ctx);
bool permitted_row(const MenuContext& ctx);
/// @}

/// @name The three bespoke value texts, shared by the note and note-row families (spec §5.3):
///       neither depends on which bridge half produced the value, only on the value and which
///       caller is asking. Bodies in the `.cpp`.
/// @{
ValueText value_text_probability(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText value_text_iterance(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText value_text_fill(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// @}

// -------------------------------------------------------------------------------------------
// The note editor. Hook bodies in the `.cpp`, except the two select-chain functions (see the
// file-level note).
// -------------------------------------------------------------------------------------------

int32_t get_note_velocity(const MenuContext& ctx);
void adjust_note_velocity(const MenuContext& ctx, int32_t offset);
int32_t get_note_probability(const MenuContext& ctx);
void adjust_note_probability(const MenuContext& ctx, int32_t offset);
int32_t get_note_iterance(const MenuContext& ctx);
void adjust_note_iterance(const MenuContext& ctx, int32_t offset);

/// @brief `get_note_iterance(ctx) == kCustomIterancePreset`, null-bridge-safe -- what
///        `chain_note_iterance` needs. A thin wrapper rather than inlining the check in `chain_note_iterance`
///        itself (see that function's own note on why it is declared in this header rather than the
///        `.cpp`): `note_bridge()` is only reachable from the `.cpp`, and `validate_tree`'s whole-tree
///        `select_chain` sweep (`tables_3_spec.cpp`) calls this hook with no bridge installed at all.
bool note_iterance_is_custom(const MenuContext& ctx);
int32_t get_note_fill(const MenuContext& ctx);
void adjust_note_fill(const MenuContext& ctx, int32_t offset);

int32_t get_note_iterance_divisor(const MenuContext& ctx);
void set_note_iterance_divisor(const MenuContext& ctx, int32_t value);

/// @brief One custom-iterance step toggle, both families. A macro for the reason
///        `nodes/sound_osc.h`'s `DELUGE_OSC_SOURCE` is one: the `get`/`set`/`relevant` hooks are
///        plain function pointers with no room for legacy's `index` constructor argument
///        (`note/iterance_step_toggle.h:30-32`), so each of the sixteen steps needs its own triple.
///        Bodies in the `.cpp` (`DELUGE_NOTE_STEP_HOOKS`/`DELUGE_NOTE_ROW_STEP_HOOKS`), each a thin
///        wrapper over one shared `index`-taking implementation.
/// @{
#define DELUGE_NOTE_STEP_DECL(i)                                                                                       \
	int32_t get_note_iterance_step##i(const MenuContext& ctx);                                                         \
	void set_note_iterance_step##i(const MenuContext& ctx, int32_t value);                                             \
	bool relevant_note_iterance_step##i(const MenuContext& ctx)

DELUGE_NOTE_STEP_DECL(1);
DELUGE_NOTE_STEP_DECL(2);
DELUGE_NOTE_STEP_DECL(3);
DELUGE_NOTE_STEP_DECL(4);
DELUGE_NOTE_STEP_DECL(5);
DELUGE_NOTE_STEP_DECL(6);
DELUGE_NOTE_STEP_DECL(7);
DELUGE_NOTE_STEP_DECL(8);
#undef DELUGE_NOTE_STEP_DECL
/// @}

inline constexpr Node note_iterance_divisor =
    with_permitted(int_item(sid(deluge::l10n::String::STRING_FOR_ITERANCE_DIVISOR), get_note_iterance_divisor,
                            set_note_iterance_divisor, IntRange{1, 8, 1}),
                   permitted_note);

#define DELUGE_NOTE_STEP_NODE(i)                                                                                       \
	inline constexpr Node note_iterance_step##i =                                                                      \
	    toggle_item(sid(deluge::l10n::String::STRING_FOR_ITERATION_STEP_##i), get_note_iterance_step##i,               \
	                set_note_iterance_step##i, relevant_note_iterance_step##i)

DELUGE_NOTE_STEP_NODE(1);
DELUGE_NOTE_STEP_NODE(2);
DELUGE_NOTE_STEP_NODE(3);
DELUGE_NOTE_STEP_NODE(4);
DELUGE_NOTE_STEP_NODE(5);
DELUGE_NOTE_STEP_NODE(6);
DELUGE_NOTE_STEP_NODE(7);
DELUGE_NOTE_STEP_NODE(8);
#undef DELUGE_NOTE_STEP_NODE

inline constexpr const Node* kNoteCustomIteranceChildren[] = {
    &note_iterance_divisor, &note_iterance_step1, &note_iterance_step2, &note_iterance_step3, &note_iterance_step4,
    &note_iterance_step5,   &note_iterance_step6, &note_iterance_step7, &note_iterance_step8,
};
inline constexpr Node note_custom_iterance_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_CUSTOM), kNoteCustomIteranceChildren, Layout::Vertical);

inline constexpr Node note_velocity =
    with_permitted(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_VELOCITY), get_note_velocity,
                                        nullptr, IntRange{1, 127, 1}, Hint::None),
                               adjust_note_velocity),
                   permitted_note);

inline constexpr Node note_editor_probability =
    with_permitted(with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_PROBABILITY),
                                                        get_note_probability, nullptr,
                                                        IntRange{1, kNumProbabilityValues | 127, 1}, Hint::None),
                                               adjust_note_probability),
                                   value_text_probability),
                   permitted_note);

inline constexpr Node note_fill = with_permitted(
    with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_FILL), get_note_fill, nullptr,
                                         IntRange{FillMode::OFF, FillMode::FILL, 1}, Hint::None),
                                adjust_note_fill),
                    value_text_fill),
    permitted_note);

/// `IterancePreset::selectButtonPress()` (`note/iterance_preset.h:59-67`): only `kCustomIterancePreset`
/// opens anything; every other value ascends. Defined here, not the `.cpp` -- see the file-level note.
inline constexpr const Node* kNoteIteranceChain[] = {&note_custom_iterance_root};
inline const Node* chain_note_iterance(const MenuContext& ctx, const MenuState&) {
	if (!note_iterance_is_custom(ctx)) {
		return nullptr;
	}
	return &note_custom_iterance_root;
}

inline constexpr Node note_iterance = with_select_chain(
    with_permitted(with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_ITERANCE),
                                                        get_note_iterance, nullptr,
                                                        IntRange{0, kNumIterancePresets + 1, 1}, Hint::None),
                                               adjust_note_iterance),
                                   value_text_iterance),
                   permitted_note),
    chain_note_iterance, kNoteIteranceChain);

inline constexpr const Node* kNoteEditorChildren[] = {&note_velocity, &note_editor_probability, &note_iterance,
                                                      &note_fill};
inline constexpr Node note_editor_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR), kNoteEditorChildren, Layout::Horizontal);

// -------------------------------------------------------------------------------------------
// The note-row editor. Same shape, through the row half of the bridge.
// -------------------------------------------------------------------------------------------

int32_t get_note_row_probability(const MenuContext& ctx);
void adjust_note_row_probability(const MenuContext& ctx, int32_t offset);
int32_t get_note_row_iterance(const MenuContext& ctx);

/// @brief `get_note_row_iterance(ctx) == kCustomIterancePreset`, null-bridge-safe. See
///        `note_iterance_is_custom`'s own doc.
bool note_row_iterance_is_custom(const MenuContext& ctx);
void adjust_note_row_iterance(const MenuContext& ctx, int32_t offset);
int32_t get_note_row_fill(const MenuContext& ctx);
void adjust_note_row_fill(const MenuContext& ctx, int32_t offset);

int32_t get_note_row_iterance_divisor(const MenuContext& ctx);
void set_note_row_iterance_divisor(const MenuContext& ctx, int32_t value);

#define DELUGE_NOTE_ROW_STEP_DECL(i)                                                                                   \
	int32_t get_note_row_iterance_step##i(const MenuContext& ctx);                                                     \
	void set_note_row_iterance_step##i(const MenuContext& ctx, int32_t value);                                         \
	bool relevant_note_row_iterance_step##i(const MenuContext& ctx)

DELUGE_NOTE_ROW_STEP_DECL(1);
DELUGE_NOTE_ROW_STEP_DECL(2);
DELUGE_NOTE_ROW_STEP_DECL(3);
DELUGE_NOTE_ROW_STEP_DECL(4);
DELUGE_NOTE_ROW_STEP_DECL(5);
DELUGE_NOTE_ROW_STEP_DECL(6);
DELUGE_NOTE_ROW_STEP_DECL(7);
DELUGE_NOTE_ROW_STEP_DECL(8);
#undef DELUGE_NOTE_ROW_STEP_DECL

inline constexpr Node note_row_iterance_divisor =
    with_permitted(int_item(sid(deluge::l10n::String::STRING_FOR_ITERANCE_DIVISOR), get_note_row_iterance_divisor,
                            set_note_row_iterance_divisor, IntRange{1, 8, 1}),
                   permitted_row);

#define DELUGE_NOTE_ROW_STEP_NODE(i)                                                                                   \
	inline constexpr Node note_row_iterance_step##i =                                                                  \
	    toggle_item(sid(deluge::l10n::String::STRING_FOR_ITERATION_STEP_##i), get_note_row_iterance_step##i,           \
	                set_note_row_iterance_step##i, relevant_note_row_iterance_step##i)

DELUGE_NOTE_ROW_STEP_NODE(1);
DELUGE_NOTE_ROW_STEP_NODE(2);
DELUGE_NOTE_ROW_STEP_NODE(3);
DELUGE_NOTE_ROW_STEP_NODE(4);
DELUGE_NOTE_ROW_STEP_NODE(5);
DELUGE_NOTE_ROW_STEP_NODE(6);
DELUGE_NOTE_ROW_STEP_NODE(7);
DELUGE_NOTE_ROW_STEP_NODE(8);
#undef DELUGE_NOTE_ROW_STEP_NODE

inline constexpr const Node* kNoteRowCustomIteranceChildren[] = {
    &note_row_iterance_divisor, &note_row_iterance_step1, &note_row_iterance_step2,
    &note_row_iterance_step3,   &note_row_iterance_step4, &note_row_iterance_step5,
    &note_row_iterance_step6,   &note_row_iterance_step7, &note_row_iterance_step8,
};
inline constexpr Node note_row_custom_iterance_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_CUSTOM), kNoteRowCustomIteranceChildren, Layout::Vertical);

inline constexpr Node note_row_probability = with_permitted(
    with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR_PROBABILITY),
                                         get_note_row_probability, nullptr, IntRange{1, kNumProbabilityValues | 127, 1},
                                         Hint::None),
                                adjust_note_row_probability),
                    value_text_probability),
    permitted_row);

inline constexpr Node note_row_fill = with_permitted(
    with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR_FILL), get_note_row_fill,
                                         nullptr, IntRange{FillMode::OFF, FillMode::FILL, 1}, Hint::None),
                                adjust_note_row_fill),
                    value_text_fill),
    permitted_row);

/// `IterancePreset::selectButtonPress()` (`note_row/iterance_preset.h:72-80`). Defined here, not the
/// `.cpp` -- see the file-level note.
inline constexpr const Node* kNoteRowIteranceChain[] = {&note_row_custom_iterance_root};
inline const Node* chain_note_row_iterance(const MenuContext& ctx, const MenuState&) {
	if (!note_row_iterance_is_custom(ctx)) {
		return nullptr;
	}
	return &note_row_custom_iterance_root;
}

inline constexpr Node note_row_iterance = with_select_chain(
    with_permitted(with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR_ITERANCE),
                                                        get_note_row_iterance, nullptr,
                                                        IntRange{0, kNumIterancePresets + 1, 1}, Hint::None),
                                               adjust_note_row_iterance),
                                   value_text_iterance),
                   permitted_row),
    chain_note_row_iterance, kNoteRowIteranceChain);

inline constexpr const Node* kNoteRowEditorChildren[] = {&sequence_direction, &note_row_probability, &note_row_iterance,
                                                         &note_row_fill};
inline constexpr Node note_row_editor_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR), kNoteRowEditorChildren, Layout::Horizontal);

} // namespace deluge::scenes::menu::nodes
