#pragma once

/// @brief `MultiRangeItem` -- the note-range picker, and the one node it backs.
///
/// The port of `deluge::gui::menu_item::MultiRange` (`gui/menu_item/multi_range.{h,cpp}`, 435
/// lines): the page legacy substitutes for a range-dependent item when the source has more than one
/// `MultiRange` and none is selected yet (`SoundEditor::setup`, `sound_editor.cpp:1857-1858`:
/// `multiRangeMenu.menuItemHeadingTo = newItem; newItem = &multiRangeMenu;`).
///
/// ## How it is reached
///
/// Not from any submenu -- exactly as in legacy, where no menu literal names `multiRangeMenu`.
/// `MenuScene::enter_child` descends into `nodes::multi_range` whenever `permission_for` answers
/// `Permission::MustSelectRange`, having first recorded the item it was heading to in
/// `MenuState::pending_after_range`. `multi_range_chain` -- this page's `Node::select_chain` -- then
/// returns that item, which is precisely `MultiRange::selectButtonPress`'s `return menuItemHeadingTo`
/// (`multi_range.cpp:360-362`).
///
/// `Node::chain_targets` is therefore `nodes::kRangeDependentNodes`: the only nodes that can ever
/// set `pending_after_range` are the ones `permission_for`'s range rule fires on, i.e. exactly the
/// nodes carrying `Node::range_dependent` (`nodes/nodes.h`, ten of them). That span is also what
/// makes this page *reachable* for `assign_node_ids` / `validate_tree` / `deluge_menu_dump`, which
/// walk `children` and `chain_targets` -- see `nodes.h`, where the page joins `kAllRoots` for the
/// same reason `root_cv` did.
///
/// ## `Node::range_dependent` is deliberately NOT set on this node
///
/// Legacy's `MultiRange::isRangeDependent()` returns true (`multi_range.h:32`), but legacy never
/// runs its own permission check against the picker -- the substitution at `:1802-1803` happens
/// *after* the check, on the item being opened. Setting the flag here would make
/// `permission_for(multi_range, ctx)` answer `MustSelectRange` and `enter_child` descend into the
/// picker from the picker, forever. `nodes::kRangeDependentArray` accordingly still holds ten nodes,
/// not eleven, and `nodes.h` says so.
///
/// ## What did NOT come across
///
///   * **Inserting a range** (shift + select encoder, `multi_range.cpp:158-228`) and **deleting one**
///     (`deletePress`, `:261-311`). Both are range *editing* rather than range *selection*, both
///     mutate `Source::ranges` under a `killAllVoices` + `AudioEngine::audioRoutineLocked` window,
///     and neither has an input route here: legacy reaches `deletePress` from `SoundEditor`'s own
///     BACK handling, which in this port is `MenuState::back_claimed` -> `MenuScene::ascend`.
///   * **Editing a range edge** (`soundEditor.editingRangeEdge`, `:74-154`, and the `invertArea`
///     highlight at `:415-433`): a `SoundEditor` mode this layer has no equivalent of, and the
///     highlight is a frame primitive.
///   * `noteOnToChangeRange` (`:364-381`): live MIDI note-in re-selecting the range. Nothing routes
///     MIDI into the scene stack yet (`MenuScene`'s learn entry points carry the same warning).
///   * `possibleChangeToCurrentRangeDisplay` / `uiNeedsRendering(keyboardScreen)` (`:250-258`):
///     `gui/ui/`.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/sound_osc.h" // the ten `range_dependent` nodes this page chains to

#include "shell/profile.h" // ui::shell::TextScratch

#include <cstddef>
#include <span>
#include <string_view>

namespace deluge::scenes::menu::nodes {

/// @brief The note-range picker. One row per `MultiRange` on the addressed `Source`, labelled with
///        the note span it covers (`MultiRange::getText`, `multi_range.cpp:313-358`).
class MultiRangeItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
};

/// @brief The single instance -- `multiRangeMenu` (`gui/menu_item/multi_range.cpp:37`).
inline const MultiRangeItem kMultiRangeItem{};

/// @brief One picker row's label -- `MultiRange::getText` (`multi_range.cpp:313-358`) with
///        `mayShowJustOne = false`, which is what `drawPixelsForOled` passes (`:401`).
///
/// The lower end is "BOTTOM" for range 0 and the previous range's `topNote + 1` otherwise, padded
/// out to column 7; the upper end is "- top" for the last range and "- " plus this range's
/// `topNote` otherwise. **Byte-for-byte** what legacy writes into its own buffer, padding included
/// -- which is what lets `tables_1_spec` compare the two directly instead of comparing a
/// normalisation of them.
///
/// Exported rather than kept file-local for exactly that reason: it is the one piece of this page
/// with a legacy counterpart a spec can call.
///
/// @param ctx     What is being edited; `ctx.source`'s ranges supply the note numbers.
/// @param index   Which range's row.
/// @param buf     Caller-owned storage the label is written into.
/// @param cap     `buf`'s size.
/// @param scratch Digit storage for the "BOTTOM" lookup (`shell/profile.h`'s text path).
/// @return A view over @p buf.
std::string_view range_row_label(const MenuContext& ctx, int32_t index, char* buf, size_t cap,
                                 deluge::ui::shell::TextScratch& scratch);

/// @brief `MultiRange::selectButtonPress` (`multi_range.cpp:360-362`): whatever the picker was
///        substituted for.
///
/// Null when nothing is pending, which `MenuScene` reads as "ascend" -- the same answer a leaf with
/// no chain gives, and the only thing it could mean with no item to head to.
const Node* multi_range_chain(const MenuContext& ctx, const MenuState& state);

/// @brief Every node `multi_range_chain` may return: the ten `Node::range_dependent` nodes, which
///        are the only ones `permission_for` can answer `MustSelectRange` for.
///
/// Spelled out here rather than reusing `nodes::kRangeDependentNodes` (`nodes/nodes.h`) because that
/// header includes this one; the two are asserted equal in `tables_1_spec`, so they cannot drift.
inline constexpr const Node* kMultiRangeChainArray[] = {
    &sample0_recorder,  &sample1_recorder, &file0_selector, &file1_selector, &source0_transpose,
    &source1_transpose, &sample0_start,    &sample0_end,    &sample1_start,  &sample1_end,
};
inline constexpr std::span<const Node* const> kMultiRangeChain{kMultiRangeChainArray};

/// @brief The picker's node -- title `STRING_FOR_NOTE_RANGE` (`multi_range.h:43`).
inline constexpr Node multi_range = with_select_chain(
    custom(sid(deluge::l10n::String::STRING_FOR_NOTE_RANGE), &kMultiRangeItem), &multi_range_chain, kMultiRangeChain);

/// @brief `multi_range` as a one-element root span, the shape `kAllRoots` and the specs want.
inline constexpr const Node* kMultiRangeRootArray[] = {&multi_range};
inline constexpr std::span<const Node* const> kMultiRangeRoots{kMultiRangeRootArray};

} // namespace deluge::scenes::menu::nodes
