#pragma once

/// @brief The bend-range subtree as data: the two-item `bend` submenu, and the single
///        `drum_bend_range` item that stands in for it on a kit row.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`'s `bendMenu` literal (`:795-801`) and the globals it names (`:790-791`,
/// `:1732`):
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `bend_range_main` | `mainBendRangeMenu` (`:792`) | `bend_range::Main` (`bend_range/main.h:25`) | Int |
/// | `bend_range_per_finger` | `perFingerBendRangeMenu` (`:793`) | `bend_range::PerFinger` (`bend_range/per_finger.h:25`) | Int |
/// | `bend` | `bendMenu` (`:795`) | `submenu::Bend` (`submenu/bend.h:23`) | Submenu (Vertical) |
/// | `drum_bend_range` | `drumBendRangeMenu` (`:1732`) | `bend_range::PerFinger` | Int |
// clang-format on
///
/// @note **`bendMenu` and `drumBendRangeMenu` are siblings under the sound root sharing one name**
///       (`STRING_FOR_BEND_RANGE`). That is legacy's own doing and is not a `DuplicateName` report:
///       they are never relevant at once -- the submenu is for SYNTH/CV/MIDI outputs
///       (`submenu/bend.h:26-30`), the single item for a kit row (`bend_range/per_finger.h:40-42`)
///       -- and `validate.cpp`'s `co_visible` is what encodes that.
///
/// @note **The two `bend_range::PerFinger` objects need different relevance**, because the class's
///       own override depends on how deep the menu is (`per_finger.h:41`) and the two are used at
///       different depths. See each node's note below; review round 1, I1.
///
/// @note **All three items read and write the clip's `ExpressionParamSet`**, creating it on demand
///       (`getOrCreateExpressionParamSet(editingKit())`, `bend_range/main.h:28-30`), and fall back
///       to `FlashStorage::defaultBendRange` when there is none. That is a `ParamManager` call, not
///       a param *read*, so these are `get`/`set` items rather than `ParamRef` ones.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The bend-range hooks. Bodies (and their citations) in the `.cpp`.
/// @{
int32_t get_bend_range_main(const MenuContext& ctx);
void set_bend_range_main(const MenuContext& ctx, int32_t value);
int32_t get_bend_range_per_finger(const MenuContext& ctx);
void set_bend_range_per_finger(const MenuContext& ctx, int32_t value);
/// @}

/// @brief `BendRange::getMaxValue` (`gui/menu_item/bend_range.h:24`) over `Number`'s default
///        minimum of 0.
inline constexpr IntRange kBendRange{0, 96, 1};

inline constexpr Node bend_range_main = int_item(sid(deluge::l10n::String::STRING_FOR_NORMAL), get_bend_range_main,
                                                 set_bend_range_main, kBendRange, Hint::Knob);
/// @brief The per-finger range **inside the bend submenu**, where legacy always shows it.
///
/// `bend_range::PerFinger::isRelevant` is `soundEditor.navigationDepth == 1 || editingKit()`
/// (`bend_range/per_finger.h:41`). The depth is 0 while the root list is on screen
/// (`sound_editor.cpp:1885`) and 1 inside a first-level submenu (`:366`), and this object is only
/// ever reached one level down, inside `bendMenu` -- so the first arm short-circuits to true and
/// legacy shows the item unconditionally there. A null `relevant` is that fact: always relevant,
/// wherever this node is reachable. (`drum_bend_range` below is the *same class* used as a root
/// child at depth 0, where only the `editingKit()` arm can fire -- hence the predicate there and
/// not here.)
inline constexpr Node bend_range_per_finger =
    int_item(sid(deluge::l10n::String::STRING_FOR_POLY_FINGER_MPE), get_bend_range_per_finger,
             set_bend_range_per_finger, kBendRange, Hint::Knob);

inline constexpr const Node* kBendChildren[] = {&bend_range_main, &bend_range_per_finger};
inline constexpr Node bend =
    submenu(sid(deluge::l10n::String::STRING_FOR_BEND_RANGE), kBendChildren, Layout::Vertical, relevance::bend_submenu);

/// The same class and hooks as `bend_range_per_finger`, under its own name -- legacy's
/// "the single option available for Drums" (`menus.cpp:1732`) -- but a **root child**, i.e. shown
/// at `navigationDepth == 0`, where `per_finger.h:41`'s first arm is false and only `editingKit()`
/// decides. `with_relevant` is what attaches the predicate to the shared shape.
inline constexpr Node drum_bend_range =
    with_relevant(int_item(sid(deluge::l10n::String::STRING_FOR_BEND_RANGE), get_bend_range_per_finger,
                           set_bend_range_per_finger, kBendRange, Hint::Knob),
                  relevance::per_finger_bend);

/// @brief This file's two contributions to the sound-editor root, in the order the root lists them.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundBendChildren;

} // namespace deluge::scenes::menu::nodes
