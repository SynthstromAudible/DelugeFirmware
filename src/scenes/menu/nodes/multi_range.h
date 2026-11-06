#pragma once

/// @brief `MultiRangeItem` -- the note-range picker, and the one node it backs.
///
/// The port of `deluge::gui::menu_item::MultiRange` (`gui/menu_item/multi_range.{h,cpp}`, 435
/// lines): the page legacy substitutes for a range-dependent item when the source has more than one
/// `MultiRange` and none is selected yet (`SoundEditor::setup`:
/// `multiRangeMenu.menuItemHeadingTo = newItem; newItem = &multiRangeMenu;`).
///
/// ## How it is reached
///
/// Not from any submenu -- exactly as in legacy, where no menu literal names `multiRangeMenu`.
/// `MenuScene::enter_child` descends into `nodes::multi_range` whenever `permission_for` answers
/// `Permission::MustSelectRange`, having first recorded the item it was heading to in
/// `MenuWalkState::pending_after_range`. `multi_range_chain` -- this page's `Node::select_chain` -- then
/// returns that item, which is precisely `MultiRange::selectButtonPress`'s `return menuItemHeadingTo`
/// (`multi_range.cpp:360-362`).
///
/// `Node::chain_targets` is therefore `nodes::kRangeDependentNodes`: the only nodes that can ever
/// set `pending_after_range` are the ones `permission_for`'s range rule fires on, i.e. exactly the
/// nodes carrying `Node::range_dependent` (`nodes/nodes.h`, ten of them). That span is also what
/// makes this page *reachable* for `assign_node_ids` / `validate_tree` / the menu dump, which
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
/// ## Editing a range edge
///
/// A horizontal turn picks an edge (`MenuState::edit_edge`, legacy's `soundEditor.editingRangeEdge`), the select
/// encoder moves it a note at a time through one ranges write, and BACK cancels it before it would go up; the edited
/// half of the row is inverted and no row is selected. All of it is read in `render`, where the turns and BACK arrive.
/// Opening the picker turns the edge off (`MultiRange::beginSession` ends in `Range::beginSession`).
///
/// ## Inserting and deleting a range
///
/// Shift + a select turn splits the range at its midpoint, after it for a turn up and before it for a turn down
/// (`plan_range_insert`), and the cursor follows the new range; Shift+SAVE deletes it (`range_delete_press`, reached
/// through `MenuScene::delete_range_press` from the guest's SAVE arm), the range below taking the gap. Both go to the
/// sink as their own verbs, which kill the voices and keep the other ranges' samples, as legacy's bodies do. Every
/// select turn waits out a popup, as `selectEncoderAction` does.
///
/// ## What did NOT come across
///
///   * `noteOnToChangeRange` (`:364-381`): live MIDI note-in re-selecting the range. Nothing routes
///     MIDI into the scene stack yet (`MenuScene`'s learn entry points carry the same warning).
///   * `possibleChangeToCurrentRangeDisplay` / `uiNeedsRendering(keyboardScreen)` (`:250-258`):
///     `gui/ui/`.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/sound_osc.h" // the ten `range_dependent` nodes this page chains to

#include "instruments/deluge/sound_state.h"
#include "ui/oled/values.h" // RangeEdge

#include "shell/profile.h" // ui::shell::TextScratch

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace deluge::scenes::menu::nodes {

/// @brief The note-range picker. One row per `MultiRange` on the addressed `Source`, labelled with
///        the note span it covers (`MultiRange::getText`, `multi_range.cpp:313-358`).
class MultiRangeItem final : public CustomItem {
public:
	/// @brief True: the legacy item is a `Value` (`CustomItem::is_value`).
	bool is_value() const override { return true; }
	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	/// @brief The list's turn and select, the horizontal encoder (`MultiRange::horizontalEncoderAction`, every turn),
	///        and BACK while an edge is edited (`SoundEditor::buttonAction` cancels the edge before going up).
	deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;
};

/// @brief The single instance -- `multiRangeMenu` (`gui/menu_item/multi_range.cpp`).
inline const MultiRangeItem kMultiRangeItem{};

/// @brief One picker row's label -- `MultiRange::getText` (`multi_range.cpp`) with
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
/// @param ctx     What is being edited; the addressed source's recorded ranges supply the note numbers.
/// @param index   Which range's row.
/// @param buf     Caller-owned storage the label is written into.
/// @param cap     `buf`'s size.
/// @param scratch Digit storage for the "BOTTOM" lookup (`shell/profile.h`'s text path).
/// @return A view over @p buf.
std::string_view range_row_label(const MenuContext& ctx, int32_t index, char* buf, size_t cap,
                                 deluge::ui::shell::TextScratch& scratch);

/// @brief `MultiRange::mayEditRangeEdge` (`multi_range.cpp`): the left edge of any range but the first, the right
///        edge of any but the last.
/// @param ctx   What is being edited.
/// @param index The range whose edge.
/// @param edge  Which edge; `Off` is never editable.
bool may_edit_range_edge(const MenuContext& ctx, int32_t index, deluge::ui::oled::RangeEdge edge);

/// @brief `Range::horizontalEncoderAction` (`gui/menu_item/range.cpp`): the edge a horizontal turn leaves edited.
///
/// A left turn takes the left edge where it may, else turns the right one off; a right turn the mirror; a turn
/// towards the edge already edited turns it off.
/// @param ctx     What is being edited.
/// @param index   The picker's range.
/// @param current The edge edited now.
/// @param offset  The turn; only its sign counts.
/// @return The edge edited after the turn.
deluge::ui::oled::RangeEdge next_range_edge(const MenuContext& ctx, int32_t index, deluge::ui::oled::RangeEdge current,
                                            int32_t offset);

/// @brief `MultiRange::selectEncoderAction`'s edge arm (`multi_range.cpp`): move the edited edge one note.
///
/// The left edge is the range below's `topNote`, the right edge this range's; each moves by one and stops one note
/// short of its neighbours, at 0 and 127 for the left edge and 1 and 126 for the right.
/// @param ranges The source's ranges, rewritten in place.
/// @param index  The picker's range.
/// @param edge   The edge edited; `Off` changes nothing.
/// @param offset The turn; only its sign counts (raising for 0 too, as legacy's `offset >= 0`).
void step_range_edge(std::vector<deluge::instrument::RangeState>& ranges, int32_t index,
                     deluge::ui::oled::RangeEdge edge, int32_t offset);

/// @brief Where a range insert puts the new range: `MultiRange::selectEncoderAction`'s Shift arm.
struct RangeInsert {
	int32_t at;                          ///< the new range's index, where the cursor goes
	int16_t top_note;                    ///< the new range's top note
	std::optional<int16_t> previous_top; ///< the range below's new top note, when the new one goes after it
};

/// @brief `MultiRange::selectEncoderAction`'s Shift arm (`multi_range.cpp`): split @p index's range at the
///        midpoint of its bottom and top, after it for a turn up and before it for a turn down.
///
/// The bottom is the range below's top plus one (the first range's: its top minus one, at most 0); the top is the
/// range's own (the last range's: its bottom plus one, at least 127). A range whose bottom and top meet cannot split.
/// @param ranges The source's ranges.
/// @param index  The picker's range.
/// @param offset The turn; only its sign counts.
/// @return The insert, or nullopt for "RANGE CONTAINS ONE NOTE".
std::optional<RangeInsert> plan_range_insert(std::span<const deluge::instrument::RangeState> ranges, int32_t index,
                                             int32_t offset);

/// @brief What a range delete leaves: `MultiRange::deletePress`.
struct RangeDelete {
	int32_t cursor; ///< where the picker's cursor goes: the range below, or 0
	std::optional<int16_t>
	    lower_top; ///< the range below's new top note: 32767 under a deleted top range, else the mean
};

/// @brief `MultiRange::deletePress` (`multi_range.cpp`): what deleting @p index's range does to the one below.
/// @return The delete, or nullopt for "LAST RANGE CAN'T DELETE".
std::optional<RangeDelete> plan_range_delete(std::span<const deluge::instrument::RangeState> ranges, int32_t index);

/// @brief `MultiRange::deletePress` (`multi_range.cpp`) on the picker: nothing while an edge is edited or a popup is
///        up; "LAST RANGE CAN'T DELETE" for the last; else the range is deleted, the cursor goes to the one below, and
///        "RANGE DELETED" shows.
/// @param ctx   What is being edited.
/// @param state The menu's state, whose picker list and range choice follow.
void range_delete_press(const MenuContext& ctx, MenuState& state);

/// @brief `MultiRange::selectButtonPress` (`multi_range.cpp`): whatever the picker was
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
