#pragma once

/// @brief The note editor and note-row editor as data: the two horizontal roots, their four value
///        leaves apiece (minus the row family's velocity, which legacy never gave a row), and the
///        two custom-iterance pages a select press on either family's iterance leaf can chain to.
///
/// Each row cites its legacy object in `origin/main:src/deluge/gui/ui/menus.cpp`. All state
/// lives behind `instrumentClipView`, reached through `NoteBridge` (`ui/scenes/menu/note_bridge.h`)
/// rather than any model read here -- every hook below calls `note_bridge()` and nothing
/// else.
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `note_editor_root` | `noteEditorRootMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `note_velocity` | `noteVelocityMenu` | `note::Velocity` (`origin/main:src/deluge/gui/menu_item/note/velocity.h`) | Int |
/// | `note_editor_probability` | `noteProbabilityMenu` | `note::Probability` (`origin/main:src/deluge/gui/menu_item/note/probability.h`) | Int |
/// | `note_iterance` | `noteIteranceMenu` | `note::IterancePreset` (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`) | Int |
/// | `note_fill` | `noteFillMenu` | `note::Fill` (`origin/main:src/deluge/gui/menu_item/note/fill.h`) | Int |
/// | `note_custom_iterance_root` | `noteCustomIteranceRootMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`) | `Submenu` | Submenu (Vertical) |
/// | `note_iterance_divisor` | `noteCustomIteranceDivisor` | `note::IteranceDivisor` (`origin/main:src/deluge/gui/menu_item/note/iterance_divisor.h`) | Int |
/// | `note_iterance_step_1..8` | `noteCustomIteranceStep1..8` | `note::IteranceStepToggle` (`origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`) | Toggle |
/// | `note_row_editor_root` | `noteRowEditorRootMenu` | `HorizontalMenu` | Submenu (Horizontal) |
/// | `note_row_probability` | `noteRowProbabilityMenu` | `note_row::Probability` (`origin/main:src/deluge/gui/menu_item/note_row/probability.h`) | Int |
/// | `note_row_iterance` | `noteRowIteranceMenu` | `note_row::IterancePreset` (`origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`) | Int |
/// | `note_row_fill` | `noteRowFillMenu` | `note_row::Fill` (`origin/main:src/deluge/gui/menu_item/note_row/fill.h`) | Int |
/// | `note_row_custom_iterance_root` | `noteRowCustomIteranceRootMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`) | `Submenu` | Submenu (Vertical) |
/// | `note_row_iterance_divisor` | `noteRowCustomIteranceDivisor` | `note_row::IteranceDivisor` (`origin/main:src/deluge/gui/menu_item/note_row/iterance_divisor.h`) | Int |
/// | `note_row_iterance_step_1..8` | `noteRowCustomIteranceStep1..8` | `note_row::IteranceStepToggle` (`origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`) | Toggle |
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
///       (`origin/main:src/deluge/gui/menu_item/note/selected_note.h`) and `note_row::SelectedNoteRow`'s twin
///       (`origin/main:src/deluge/gui/menu_item/note_row/selected_note_row.h`): each tests the bridge's own selection
///       predicate and, on refusal, raises legacy's own popup text -- `"Select Note"` / `"Select Row"`, literally
///       legacy's (`origin/main:src/deluge/gui/menu_item/note/selected_note.h`, `SelectedNoteRow::shouldEnterSubmenu`
///       in `origin/main:src/deluge/gui/menu_item/note_row/selected_note_row.h`). Every leaf of its family carries the
///       guard **except** the step toggles: `IteranceStepToggle` derives from `Toggle`, not
///       `SelectedNote`/`SelectedNoteRow`, and overrides no permission hook
///       (`origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`,
///       `origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`).
///
/// @note **The iterance chain.** `IterancePreset::selectButtonPress()`
///       (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`,
///       `origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`) opens the matching custom-iterance root
///       only when the current value is `deluge::project::kCustomIterancePreset`; every other value ascends.
///       `chain_note_iterance`/`chain_note_row_iterance` port that, and are the one place in this file a hook is
///       defined in the header rather than the `.cpp` -- the same choice `nodes/sound_arp.h` makes for
///       `arp_preset_chain`, because the function needs the address of a `Node` declared alongside it.
///
/// @note **The "OFF becomes 1of1" conversion.** `IteranceDivisor::readCurrentValue`/ `writeCurrentValue` and
///       `IteranceStepToggle::readCurrentValue`/`writeCurrentValue` all convert a note or row's
///       `deluge::project::kIteranceOff`
///       ("OFF") to `deluge::project::kIteranceCustom` ("1of1") before reading or editing from it, so the
///       custom-iterance page always has a concrete divisor and step set to work with
///       (`origin/main:src/deluge/gui/menu_item/note/iterance_divisor.h`,
///       `origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`,
///       `origin/main:src/deluge/gui/menu_item/note_row/iterance_divisor.h`,
///       `origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`). `IteranceStepToggle::isRelevant` is
///       the one exception in either family: it reads the **unconverted** value, because it tests for
///       `deluge::project::kIteranceOff` directly, and returns false outright with no leftmost note (note family,
///       `origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`, `isRelevant`'s final `return false`) or no
///       note row (row family, `origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`, `isRelevant`'s
///       final `return false`).
///
/// @note **`note_editor_probability`, not `note_probability`.** `nodes::note_probability` already
///       names `sound_randomizer.h`'s `randomizerNoteProbabilityMenu` (the *randomizer's* per-note
///       trigger chance, an unrelated `RandomizerUnpatchedParam`); the two would collide as C++
///       identifiers in the same `nodes` namespace. Every other leaf here keeps the plain name
///       of its menu item in `origin/main` -- this is the one substitution a real naming clash forced.
///
/// @note **`note_editor_root` has no velocity counterpart in the row family.** Legacy's own
///       `noteRowEditorRootMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`) lists `sequenceDirectionMenu`,
///       `noteRowProbabilityMenu`, `noteRowIteranceMenu`, `noteRowFillMenu` -- four children, no
///       row-velocity item exists anywhere in `gui/menu_item/note_row/`. `sequence_direction` is
///       `nodes/sound_actions.h`'s existing node, reused here unchanged: its own note-row bridge
///       path is not this file's to edit.

#include "project/iterance.h"
#include "project/note_probability.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/sound_actions.h" // &sequence_direction, note_row_editor_root's first child

#include <cstdint>
#include <utility>

namespace deluge::ui::scenes::menu::nodes {

/// @name The two "Select Note"/"Select Row" guards. Bodies in the `.cpp`.
/// @{
bool permitted_note(const MenuContext& ctx);
bool permitted_row(const MenuContext& ctx);
/// @}

/// @name The three bespoke value texts, shared by the note and note-row families:
///       neither depends on which bridge half produced the value, only on the value and which
///       caller is asking. Bodies in the `.cpp`.
/// @{
ValueText value_text_probability(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText value_text_iterance(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
ValueText value_text_fill(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// The words a Fill page draws for its values, OFF, NOT FILL and FILL (`with_value_names`).
size_t fill_names(const MenuContext& ctx, std::span<std::string_view> out);
/// `Probability::drawPixelsForOled` (both editors): the percentage, then " (L)" when it latches.
ValueText leaf_text_probability(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `Fill::drawPixelsForOled` (both editors): `InstrumentClipView::getFillString`, the notification's text.
ValueText leaf_text_fill(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// `IterancePreset::drawPixelsForOled` (both editors): "N of M" with the long labels, the notification's text.
ValueText leaf_text_iterance(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// @}

// -------------------------------------------------------------------------------------------
// The note editor. Hook bodies in the `.cpp`, except the two select-chain functions (see the
// file-level note). `.cpp` shares one body per hook kind between the note and note-row families
// (a small `NoteBridge` member-pointer accessor); only the sixteen distinct exported symbols below
// -- one address per step, for `Node`'s function-pointer fields -- are per-family.
// -------------------------------------------------------------------------------------------

int32_t get_note_velocity(const MenuContext& ctx);
void adjust_note_velocity(const MenuContext& ctx, int32_t offset);
int32_t get_note_probability(const MenuContext& ctx);
void adjust_note_probability(const MenuContext& ctx, int32_t offset);
int32_t get_note_iterance(const MenuContext& ctx);
void adjust_note_iterance(const MenuContext& ctx, int32_t offset);

/// @brief `get_note_iterance(ctx) == deluge::project::kCustomIterancePreset`, null-bridge-safe -- what
///        `chain_note_iterance` needs. A thin wrapper rather than inlining the check in
///        `chain_note_iterance` itself (see that function's own note on why it is declared in this
///        header rather than the `.cpp`): `note_bridge()` is only reachable from the `.cpp`, and it
///        has to be null-safe there because `tables_3_spec.cpp`'s whole-tree "never lets a
///        select_chain hook name a node outside its own chain_targets" sweep calls every reachable
///        node's `select_chain` -- this one included -- with no bridge installed at all (that spec
///        validates the whole sound tree; it never touches the note family).
bool note_iterance_is_custom(const MenuContext& ctx);
int32_t get_note_fill(const MenuContext& ctx);
void adjust_note_fill(const MenuContext& ctx, int32_t offset);

int32_t get_note_iterance_divisor(const MenuContext& ctx);
void set_note_iterance_divisor(const MenuContext& ctx, int32_t value);

int32_t get_note_row_probability(const MenuContext& ctx);
void adjust_note_row_probability(const MenuContext& ctx, int32_t offset);
int32_t get_note_row_iterance(const MenuContext& ctx);

/// @brief `get_note_row_iterance(ctx) == deluge::project::kCustomIterancePreset`, null-bridge-safe. See
///        `note_iterance_is_custom`'s own doc.
bool note_row_iterance_is_custom(const MenuContext& ctx);
void adjust_note_row_iterance(const MenuContext& ctx, int32_t offset);
int32_t get_note_row_fill(const MenuContext& ctx);
void adjust_note_row_fill(const MenuContext& ctx, int32_t offset);

int32_t get_note_row_iterance_divisor(const MenuContext& ctx);
void set_note_row_iterance_divisor(const MenuContext& ctx, int32_t value);

/// @brief One custom-iterance step toggle, both families -- sixteen distinct symbols (one `get`/`set`/`relevant` triple
///        per step, per family) because `Node`'s function-pointer fields need a distinct address each, but every one of
///        the sixteen is a one-line call into the `.cpp`'s single family-parameterised implementation. A macro for the
///        reason `nodes/sound_osc.h`'s `DELUGE_OSC_SOURCE` is one: legacy's own `index` constructor argument
///        (`IteranceStepToggle::IteranceStepToggle` in
///        `origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`) has nowhere to live on a plain function
///        pointer.
/// @{
#define DELUGE_ITERANCE_STEP_DECL(prefix, i)                                                                           \
	int32_t get_##prefix##_iterance_step_##i(const MenuContext& ctx);                                                  \
	void set_##prefix##_iterance_step_##i(const MenuContext& ctx, int32_t value);                                      \
	bool relevant_##prefix##_iterance_step_##i(const MenuContext& ctx)

DELUGE_ITERANCE_STEP_DECL(note, 1);
DELUGE_ITERANCE_STEP_DECL(note, 2);
DELUGE_ITERANCE_STEP_DECL(note, 3);
DELUGE_ITERANCE_STEP_DECL(note, 4);
DELUGE_ITERANCE_STEP_DECL(note, 5);
DELUGE_ITERANCE_STEP_DECL(note, 6);
DELUGE_ITERANCE_STEP_DECL(note, 7);
DELUGE_ITERANCE_STEP_DECL(note, 8);
DELUGE_ITERANCE_STEP_DECL(note_row, 1);
DELUGE_ITERANCE_STEP_DECL(note_row, 2);
DELUGE_ITERANCE_STEP_DECL(note_row, 3);
DELUGE_ITERANCE_STEP_DECL(note_row, 4);
DELUGE_ITERANCE_STEP_DECL(note_row, 5);
DELUGE_ITERANCE_STEP_DECL(note_row, 6);
DELUGE_ITERANCE_STEP_DECL(note_row, 7);
DELUGE_ITERANCE_STEP_DECL(note_row, 8);
#undef DELUGE_ITERANCE_STEP_DECL
/// @}

inline constexpr Node note_iterance_divisor =
    with_permitted(int_item(sid(deluge::l10n::String::STRING_FOR_ITERANCE_DIVISOR), get_note_iterance_divisor,
                            set_note_iterance_divisor, IntRange{1, 8, 1}),
                   permitted_note);

/// @brief One custom-iterance step `Node`, both families -- see the prototypes' own doc.
#define DELUGE_ITERANCE_STEP_NODE(prefix, i)                                                                           \
	inline constexpr Node prefix##_iterance_step_##i =                                                                 \
	    toggle_item(sid(deluge::l10n::String::STRING_FOR_ITERATION_STEP_##i), get_##prefix##_iterance_step_##i,        \
	                set_##prefix##_iterance_step_##i, relevant_##prefix##_iterance_step_##i)

DELUGE_ITERANCE_STEP_NODE(note, 1);
DELUGE_ITERANCE_STEP_NODE(note, 2);
DELUGE_ITERANCE_STEP_NODE(note, 3);
DELUGE_ITERANCE_STEP_NODE(note, 4);
DELUGE_ITERANCE_STEP_NODE(note, 5);
DELUGE_ITERANCE_STEP_NODE(note, 6);
DELUGE_ITERANCE_STEP_NODE(note, 7);
DELUGE_ITERANCE_STEP_NODE(note, 8);

inline constexpr const Node* kNoteCustomIteranceChildren[] = {
    &note_iterance_divisor, &note_iterance_step_1, &note_iterance_step_2, &note_iterance_step_3, &note_iterance_step_4,
    &note_iterance_step_5,  &note_iterance_step_6, &note_iterance_step_7, &note_iterance_step_8,
};
inline constexpr Node note_custom_iterance_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_CUSTOM), kNoteCustomIteranceChildren, Layout::Vertical);

inline constexpr Node note_velocity =
    with_permitted(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_VELOCITY), get_note_velocity,
                                        nullptr, IntRange{1, 127, 1}, Hint::None),
                               adjust_note_velocity),
                   permitted_note);

inline constexpr Node note_editor_probability = with_leaf_text(
    with_permitted(
        with_value_text(
            with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_PROBABILITY), get_note_probability,
                                 nullptr, IntRange{1, deluge::project::kProbabilityValueCount | 127, 1}, Hint::None),
                        adjust_note_probability),
            value_text_probability),
        permitted_note),
    leaf_text_probability);

inline constexpr Node note_fill = with_value_names(
    with_leaf_text(
        with_permitted(
            with_value_text(
                with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_FILL), get_note_fill, nullptr,
                                     IntRange{std::to_underlying(deluge::project::FillMode::OFF),
                                              std::to_underlying(deluge::project::FillMode::FILL), 1},
                                     Hint::None),
                            adjust_note_fill),
                value_text_fill),
            permitted_note),
        leaf_text_fill),
    fill_names);

/// `IterancePreset::selectButtonPress()` (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`): only
/// `deluge::project::kCustomIterancePreset` opens anything; every other value ascends. Defined here, not the `.cpp` --
/// see the file-level note.
inline constexpr const Node* kNoteIteranceChain[] = {&note_custom_iterance_root};
inline const Node* chain_note_iterance(const MenuContext& ctx, const MenuState&) {
	if (!note_iterance_is_custom(ctx)) {
		return nullptr;
	}
	return &note_custom_iterance_root;
}

inline constexpr Node note_iterance = with_leaf_text(
    with_select_chain(
        with_permitted(
            with_value_text(
                with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR_ITERANCE), get_note_iterance,
                                     nullptr, IntRange{0, deluge::project::kIterancePresetCount + 1, 1}, Hint::None),
                            adjust_note_iterance),
                value_text_iterance),
            permitted_note),
        chain_note_iterance, kNoteIteranceChain),
    leaf_text_iterance);

inline constexpr const Node* kNoteEditorChildren[] = {&note_velocity, &note_editor_probability, &note_iterance,
                                                      &note_fill};
inline constexpr Node note_editor_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_NOTE_EDITOR), kNoteEditorChildren, Layout::Horizontal);

// -------------------------------------------------------------------------------------------
// The note-row editor. Same shape, through the row half of the bridge.
// -------------------------------------------------------------------------------------------

inline constexpr Node note_row_iterance_divisor =
    with_permitted(int_item(sid(deluge::l10n::String::STRING_FOR_ITERANCE_DIVISOR), get_note_row_iterance_divisor,
                            set_note_row_iterance_divisor, IntRange{1, 8, 1}),
                   permitted_row);

DELUGE_ITERANCE_STEP_NODE(note_row, 1);
DELUGE_ITERANCE_STEP_NODE(note_row, 2);
DELUGE_ITERANCE_STEP_NODE(note_row, 3);
DELUGE_ITERANCE_STEP_NODE(note_row, 4);
DELUGE_ITERANCE_STEP_NODE(note_row, 5);
DELUGE_ITERANCE_STEP_NODE(note_row, 6);
DELUGE_ITERANCE_STEP_NODE(note_row, 7);
DELUGE_ITERANCE_STEP_NODE(note_row, 8);
#undef DELUGE_ITERANCE_STEP_NODE

inline constexpr const Node* kNoteRowCustomIteranceChildren[] = {
    &note_row_iterance_divisor, &note_row_iterance_step_1, &note_row_iterance_step_2,
    &note_row_iterance_step_3,  &note_row_iterance_step_4, &note_row_iterance_step_5,
    &note_row_iterance_step_6,  &note_row_iterance_step_7, &note_row_iterance_step_8,
};
inline constexpr Node note_row_custom_iterance_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_CUSTOM), kNoteRowCustomIteranceChildren, Layout::Vertical);

inline constexpr Node note_row_probability = with_leaf_text(
    with_permitted(
        with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR_PROBABILITY),
                                             get_note_row_probability, nullptr,
                                             IntRange{1, deluge::project::kProbabilityValueCount | 127, 1}, Hint::None),
                                    adjust_note_row_probability),
                        value_text_probability),
        permitted_row),
    leaf_text_probability);

inline constexpr Node note_row_fill = with_value_names(
    with_leaf_text(
        with_permitted(
            with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR_FILL),
                                                 get_note_row_fill, nullptr,
                                                 IntRange{std::to_underlying(deluge::project::FillMode::OFF),
                                                          std::to_underlying(deluge::project::FillMode::FILL), 1},
                                                 Hint::None),
                                        adjust_note_row_fill),
                            value_text_fill),
            permitted_row),
        leaf_text_fill),
    fill_names);

/// `IterancePreset::selectButtonPress()` (`origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`). Defined
/// here, not the `.cpp` -- see the file-level note.
inline constexpr const Node* kNoteRowIteranceChain[] = {&note_row_custom_iterance_root};
inline const Node* chain_note_row_iterance(const MenuContext& ctx, const MenuState&) {
	if (!note_row_iterance_is_custom(ctx)) {
		return nullptr;
	}
	return &note_row_custom_iterance_root;
}

inline constexpr Node note_row_iterance = with_leaf_text(
    with_select_chain(
        with_permitted(
            with_value_text(with_adjust(int_item(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR_ITERANCE),
                                                 get_note_row_iterance, nullptr,
                                                 IntRange{0, deluge::project::kIterancePresetCount + 1, 1}, Hint::None),
                                        adjust_note_row_iterance),
                            value_text_iterance),
            permitted_row),
        chain_note_row_iterance, kNoteRowIteranceChain),
    leaf_text_iterance);

inline constexpr const Node* kNoteRowEditorChildren[] = {&sequence_direction, &note_row_probability, &note_row_iterance,
                                                         &note_row_fill};
inline constexpr Node note_row_editor_root =
    submenu(sid(deluge::l10n::String::STRING_FOR_NOTE_ROW_EDITOR), kNoteRowEditorChildren, Layout::Horizontal);

} // namespace deluge::ui::scenes::menu::nodes
