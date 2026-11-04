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
/// @brief The menu's walk through its tree. See `walk.h`.

#include "scenes/menu/walk.h"

#include "scenes/menu/chains.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"

#include "notify/notice_queue.h"

#include "gui/l10n/strings.h"
#include "scenes/menu/bindings.h"
#include "ui/oled/columns.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace deluge::scenes::menu {

using deluge::ui::shell::SceneState;
using namespace detail;

void MenuWalk::retarget(MenuCore core, ValueSession& session, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// A re-rooted menu is not the one a pending close was asked for.
	close_requested_ = false;
	st.path[0] = &node;
	for (uint8_t i = 1; i < kMenuDepth; i++) {
		st.path[i] = nullptr;
	}
	st.depth = 0;
	st.list[0] = deluge::ui::ListState{};
	st.columns[0] = deluge::ui::ColumnsState{};
	st.scroll[0] = deluge::ui::ScrollText{};
	// The tick's observations and everything the walk chose -- a pending range, the selectors' choices, the patch-cable
	// and MPE pages' selection, a custom item's scene or action request, the recorded opener -- belong to the path this
	// re-rooting abandons. Cleared before `enter_leaf` below, which may file a fresh request (landing straight on an
	// `EditNameItem` through `SceneId::arg`). `taken_action_` is not the walk's: a request already drained into the
	// outbox has been accepted and must still reach `take_action()`'s caller.
	st.tick = {};
	st.walk = {};
	ctx.selected_note_row = false;
	session.forget();
	if (is_leaf(node.kind)) {
		// `NoRowPress`: re-rooting is `open()`, a `SceneId` deep link or the shortcut-pad jump. The
		// jump IS a press -- on a grid pad, not on this node's row -- which is the distinction the
		// value's name carries.
		session.begin(core, &node); // `setup()`'s permission check
		session.enter_leaf(core, node, FocusCause::NoRowPress);
	}
	else {
		restore_row(core);
	}
}

void MenuWalk::remember_path(const MenuContext& ctx, const MenuState& st) {
	for (uint8_t d = 0; d <= st.depth && d < kMenuDepth; d++) {
		const Node* node = st.path[d];
		if (node == nullptr || node->kind != Kind::Submenu) {
			continue;
		}
		const Node* child = is_horizontal(ctx, *node) ? child_at(ctx, *node, st.columns[d].focused)
		                                              : child_at(ctx, *node, st.list[d].cursor);
		if (child == nullptr) {
			continue;
		}
		// The submenu's own entry, else an empty one, else the least recently recorded.
		RememberedRow* slot = &remembered_[0];
		for (RememberedRow& row : remembered_) {
			if (row.submenu == node) {
				slot = &row;
				break;
			}
			if (row.recorded < slot->recorded) {
				slot = &row;
			}
		}
		*slot = RememberedRow{.submenu = node, .child = child, .recorded = ++remembered_clock_};
	}
}

void MenuWalk::restore_row(MenuCore core) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	const Node* node = focused(st);
	if (node == nullptr || node->kind != Kind::Submenu) {
		return;
	}
	for (const RememberedRow& row : remembered_) {
		if (row.submenu != node) {
			continue;
		}
		// `Submenu::focusChild` (`submenu.cpp:21-36`): a child the page no longer shows leaves the first one.
		const Node* kids[kMaxMenuChildren];
		const size_t n = visible_children(ctx, *node, kids);
		for (size_t k = 0; k < n; k++) {
			if (kids[k] == row.child) {
				if (is_horizontal(ctx, *node)) {
					st.columns[st.depth].focused = static_cast<int8_t>(k);
				}
				else {
					st.list[st.depth].cursor = static_cast<int16_t>(k);
				}
				return;
			}
		}
		return;
	}
}

void MenuWalk::focus_column(MenuCore core, const Node& column) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	const Node* page = focused(st);
	if (page == nullptr) {
		return;
	}
	const Node* kids[kMaxMenuChildren];
	const size_t n = visible_children(ctx, *page, kids);
	for (size_t k = 0; k < n; k++) {
		if (kids[k] == &column) {
			st.columns[st.depth].focused = static_cast<int8_t>(k);
			return;
		}
	}
}

const Node* pad_subject(const MenuContext& ctx, const MenuState& st) {
	if (on_horizontal_page(ctx, st)) {
		return selected_child(ctx, st, *focused(st));
	}
	return focused(st);
}

bool on_horizontal_page(const MenuContext& ctx, const MenuState& st) {
	const Node* node = focused(st);
	return node != nullptr && is_horizontal(ctx, *node);
}

bool is_horizontal(const MenuContext& ctx, const Node& node) {
	return renders_horizontal(node, ctx);
}

size_t build_column_list(const MenuContext& ctx, const Node& node, std::span<const Node*> kids,
                         std::span<deluge::ui::oled::Column> cols, std::span<NodeText> label_scratch) {
	const size_t n = visible_children(ctx, node, kids);
	for (size_t i = 0; i < n; i++) {
		const Node& kid = *kids[i];
		cols[i] = deluge::ui::oled::Column{};
		cols[i].label = column_label(kid, ctx, label_scratch[i]);
		cols[i].slots = kid.slots;
		cols[i].show_label = kid.show_column_label;
		cols[i].relevant = bindings::relevant(kid, ctx);
		cols[i].is_submenu = kid.kind == Kind::Submenu;
	}
	if (node.is_group) {
		// Each member after the first opens its own page (`HorizontalMenuGroup::preparePaging`).
		size_t first = 0;
		for (const Node* member : node.children) {
			if (member == nullptr) {
				continue;
			}
			const Node* scratch[kMaxMenuChildren];
			const size_t count = bindings::listed_children(*member, ctx, scratch, bindings::ListFor::Columns);
			if (count != 0 && first < n) {
				cols[first].page_break = true;
			}
			first += count;
		}
	}
	return n;
}

deluge::ui::oled::Paging horizontal_paging(const MenuContext& ctx, const MenuState& st) {
	const Node* node = focused(st);
	if (node == nullptr || !on_horizontal_page(ctx, st)) {
		return {};
	}
	const Node* kids[kMaxMenuColumns];
	deluge::ui::oled::Column cols[kMaxMenuColumns]{};
	NodeText label_scratch[kMaxMenuColumns];
	const size_t n = build_column_list(ctx, *node, std::span<const Node*>{kids, kMaxMenuColumns},
	                                   std::span<deluge::ui::oled::Column>{cols, kMaxMenuColumns},
	                                   std::span<NodeText>{label_scratch, kMaxMenuColumns});
	return deluge::ui::oled::paging_of(std::span<const deluge::ui::oled::Column>{cols, n},
	                                   st.columns[st.depth].focused);
}

std::string_view page_title(const MenuContext& ctx, const MenuState& st, NodeText& scratch) {
	const Node* node = focused(st);
	if (node == nullptr) {
		return {};
	}
	const int32_t focus = std::max<int32_t>(0, st.columns[st.depth].focused);
	const Node* member = bindings::group_member_of(*node, ctx, static_cast<size_t>(focus));
	const Node& titled = member != nullptr ? *member : *node;
	if (titled.page_title != nullptr) {
		// The member's own page: legacy's `paging.visiblePageNumber` counts the member menu's pages alone.
		const Node* kids[kMaxMenuColumns];
		deluge::ui::oled::Column cols[kMaxMenuColumns]{};
		NodeText label_scratch[kMaxMenuColumns];
		const size_t n = build_column_list(ctx, *node, std::span<const Node*>{kids, kMaxMenuColumns},
		                                   std::span<deluge::ui::oled::Column>{cols, kMaxMenuColumns},
		                                   std::span<NodeText>{label_scratch, kMaxMenuColumns});
		const std::span<const deluge::ui::oled::Column> span{cols, n};
		int32_t first = std::min<int32_t>(focus, static_cast<int32_t>(n) - 1);
		while (first > 0 && bindings::group_member_of(*node, ctx, static_cast<size_t>(first - 1)) == member) {
			first--;
		}
		const auto page = static_cast<uint8_t>(deluge::ui::oled::paging_of(span, focus).page
		                                       - deluge::ui::oled::paging_of(span, first).page);
		const ValueText vt = titled.page_title(ctx, page, std::span<char>{scratch.chars, sizeof(scratch.chars)});
		if (!vt.fallback) {
			return {scratch.chars, std::min(vt.length, sizeof(scratch.chars))};
		}
	}
	return display_text(titled, ValueTextUse::Title, ctx, scratch);
}

bool MenuWalk::enter_range_picker_if_needed(MenuCore core, ValueSession& session, const Node& child,
                                            bool suppress_when_allowed) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	if (child.range_dependent && !ensure_first_range(ctx)) {
		deluge::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ERROR_INSUFFICIENT_RAM));
		return true;
	}
	// `handleInstrumentButtonPress`'s own suppression (`horizontal_menu.cpp:427-428`):
	// `MUST_SELECT_RANGE && !allowToBeginSessionFromHorizontalMenu()`. Only that ONE caller passes
	// `suppress_when_allowed = true` -- `enter_child`'s general descend and `handleItemAction`'s
	// already-focused-press arm (`enter_horizontal_item`, below) both honour `MUST_SELECT_RANGE`
	// unconditionally, exactly as their own legacy call sites do (`:424`, `:669-677`, neither reads
	// `allowToBeginSessionFromHorizontalMenu` at all).
	if (suppress_when_allowed && child.allow_session_from_horizontal) {
		return false;
	}
	if (permission_for(child, ctx) != Permission::MustSelectRange) {
		return false;
	}
	st.walk.range_picker_pending = true;
	st.walk.pending_after_range = &child;
	descend_to(core, session, nodes::multi_range);
	return true;
}

void MenuWalk::enter_horizontal_item(MenuCore core, ValueSession& session, const Node& child) {
	// `HorizontalMenu::handleItemAction`'s else-path: a submenu OR
	// an `allowToBeginSessionFromHorizontalMenu` column, already focused, pressed again. Permission
	// only matters for `MUST_SELECT_RANGE` (the picker substitutes for the item); every other
	// answer -- `Yes`, and `No`, which `handleItemAction` never even reads -- enters unconditionally
	// (`soundEditor.enterSubmenu(menuItem)`, no `shouldEnterSubmenu()`/`Permission::No` gate at all,
	// unlike `enter_child`'s general descend).
	if (!enter_range_picker_if_needed(core, session, child, /*suppress_when_allowed=*/false)) {
		descend_to(core, session, child);
	}
}

void MenuWalk::descend_to(MenuCore core, ValueSession& session, const Node& child) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	if (st.depth + 1 >= kMenuDepth) {
		return;
	}
	// `SoundEditor::beginScreen` runs `endScreen()` (-> `currentMenuItem->endSession()`) before
	// switching to any new item (`sound_editor.cpp:977-982`); `HorizontalMenu::endSession`
	// (`horizontal_menu.cpp:576-591`) is the one override that cancels a live notification popup
	// (`:587-589`) -- every other `MenuItem::endSession` (`menu_item.cpp:81`) is a no-op. This is
	// "entering a child" from a horizontal page.
	if (on_horizontal_page(ctx, st)) {
		deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelNotification);
	}
	remember_path(ctx, st);
	st.depth++;
	st.path[st.depth] = &child;
	st.list[st.depth] = deluge::ui::ListState{};
	st.columns[st.depth] = deluge::ui::ColumnsState{};
	st.scroll[st.depth] = deluge::ui::ScrollText{};
	if (is_leaf(child.kind)) {
		session.begin(core, &child);                           // `Submenu::selectButtonPress`'s permission check
		session.enter_leaf(core, child, FocusCause::RowPress); // the one path with a select press behind it
	}
	else {
		restore_row(core);
	}
}

void MenuWalk::enter_child(MenuCore core, ValueSession& session, const Node& child) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// `checkPermissionToBeginSessionForRangeSpecificParam`'s **first statement**
	// (`sound_editor.cpp:1993-1997`), ahead of every one of its arms, plus legacy's
	// `MUST_SELECT_RANGE` substitution (`:1857-1858`): both factored into
	// `enter_range_picker_if_needed`, which `handle()`'s button-focus code shares -- see its own
	// doc for why the two callers need the same piece.
	if (enter_range_picker_if_needed(core, session, child, /*suppress_when_allowed=*/false)) {
		return;
	}
	if (permission_for(child, ctx) == Permission::No) {
		return;
	}

	// The picker's job is done the moment the item it was heading to opens -- legacy drops
	// `menuItemHeadingTo` on the floor the same way, by simply navigating past it.
	if (&child == st.walk.pending_after_range) {
		st.walk.range_picker_pending = false;
		st.walk.pending_after_range = nullptr;
	}
	// `SoundEditor`'s select arm (`sound_editor.cpp:630-634`, the `maybeGetParentMenu` / `focusChild` pair before
	// `enterSubmenu`): an item a chained page lists is entered on that page, focused on it, unless that page is the one
	// already shown.
	if (const Node* page = shortcut_landing_page(ctx, child); page != nullptr && page != focused(st)) {
		descend_to(core, session, *page);
		focus_column(core, child);
		return;
	}
	descend_to(core, session, child);
}

void MenuWalk::ascend(MenuCore core, ValueSession& session) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// `SoundEditor::goUpOneLevel`: step up, skipping any level whose
	// permission has since become `No`. The cursor each level is restored to is the one
	// `st.list[depth]` has been holding all along, which is what legacy's `focusChild`
	// (`submenu.cpp:21`) reconstructs from the item it navigated backward from.
	//
	// Going up from depth 0 closes the editor (`:715-717`, `exitCompletely()`): a leaf a shortcut jump re-rooted
	// onto, or a scene opener whose scene returned (`MenuWalkState::go_up_on_resume`), leaves the menu rather than its
	// bare title screen. `after_render` turns the request into the scene's `Remove`.
	//
	// `endScreen()` (-> `HorizontalMenu::endSession`) runs before the depth test, so leaving a horizontal page
	// cancels its notification whether the step lands on a parent or closes the menu. Read before the loop pops
	// anything, while `focused()` still names the page being left.
	remember_path(ctx, st);
	if (on_horizontal_page(ctx, st)) {
		deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelNotification);
	}
	do {
		if (st.depth == 0) {
			close_requested_ = true;
			return;
		}
		st.path[st.depth] = nullptr;
		st.depth--;
	} while (st.path[st.depth] != nullptr && permission_for(*st.path[st.depth], ctx) == Permission::No);

	// `goUpOneLevel` ends with `beginScreen(oldItem)`, i.e. it re-runs
	// `beginSession` on the item it lands on -- so a value leaf reached by going *up* re-reads its
	// value from the model, exactly as one reached by going down does.
	//
	// Ascending to a leaf happens
	// only where a chain descended *from* one (`Node::select_chain`), and the chain target
	// is often another value leaf that writes the shared `MenuState::edit_int` on its way through:
	// `nodes::unison_count` -> `unison_spread` left the count's page redrawing the SPREAD's value and
	// editing from it, so the first gold detent after a Back jumped the count to a number the user
	// never chose. Nothing above a leaf needs this (a submenu's cursor is `st.list[depth]`, which
	// `ascend` deliberately keeps -- see the note above).
	//
	// **`Kind::Custom` gets its own arm, not `enter_leaf`.** `CustomItem::on_focus` is `beginSession`
	// with `navigatedBackwardFrom == nullptr` -- the *forward* arm only -- so re-running it here
	// would fire the forward arm on a backward navigation and send the CV/gate/device selectors back
	// to row 0 every time the user stepped out of the page they had just opened. (`enter_leaf` takes a
	// `FocusCause`, and every call site below passes `NoRowPress`.)
	//
	// Excluding `Kind::Custom` from `enter_leaf` alone would leave the exclusion standing in for a backward arm --
	// "leave `MenuState::list[depth]` alone" -- and that is sound
	// only while every chain-source item's backward behaviour happens to be exactly that. It is not:
	// `GateSelectionItem::on_focus_backward` (`nodes/dynamic.cpp`) is a case where it is not.
	// `CustomItem::on_focus_backward` (`node.h`) is the real hook; its do-nothing default gives every
	// other chain-source item (`CvSelectionItem`, `MidiDevicesItem`, `PatchCablesItem`,
	// `SourceSelectionItem`) the "keep the cursor" answer.
	if (const Node* const landed = st.path[st.depth]; landed != nullptr && is_leaf(landed->kind)) {
		if (landed->kind == Kind::Custom) {
			if (landed->custom != nullptr) {
				landed->custom->on_focus_backward(ctx, st);
			}
		}
		else {
			session.enter_leaf(core, *landed, FocusCause::NoRowPress);
		}
	}
}

void MenuWalk::save(const MenuContext& ctx, const MenuState& st, SceneState& state) const {
	// Value-initialised, not default-initialised: every member has its own initialiser, but the
	// `memcpy` at the end of this function copies `sizeof(MenuSavedState)` bytes into a
	// `SceneState`, and that includes the alignment padding between `depth` and `focus`. Default
	// initialisation leaves those bytes indeterminate, so two saves of the same menu position
	// could produce different `SceneState` bytes. `MenuSavedState`'s default constructor is
	// implicit, so `{}` zero-initialises the whole object -- padding included -- before the member
	// initialisers run. Unobservable today (nothing compares or hashes `SceneState`), and cheap
	// enough that it should not have to stay unobservable to be correct.
	MenuSavedState saved{};
	if (st.path[0] != nullptr) {
		saved.root_node_id = node_id(*st.path[0]);
	}
	saved.depth = st.depth;
	for (uint8_t level = 0; level < st.depth && level + 1 < kMenuDepth; level++) {
		const Node* parent = st.path[level];
		const Node* wanted = st.path[level + 1];
		if (parent == nullptr || wanted == nullptr) {
			break;
		}
		const Node* kids[kMaxMenuChildren];
		const size_t n = visible_children(ctx, *parent, kids);
		for (size_t i = 0; i < n; i++) {
			if (kids[i] == wanted) {
				saved.path_child[level] = static_cast<uint8_t>(i);
				break;
			}
		}
	}
	const Node* node = focused(st);
	saved.focus =
	    (node != nullptr && is_horizontal(ctx, *node)) ? st.columns[st.depth].focused : st.list[st.depth].cursor;
	std::memcpy(state.bytes, &saved, sizeof(saved));
}

void MenuWalk::restore(MenuCore core, ValueSession& session, const SceneState& state) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	MenuSavedState saved;
	std::memcpy(&saved, state.bytes, sizeof(saved));

	const Node* root = node_by_id(saved.root_node_id);
	if (root == nullptr) {
		return;
	}
	retarget(core, session, *root);

	for (uint8_t level = 0; level < saved.depth && level < kMenuDepth - 1; level++) {
		const Node* child = child_at(ctx, *st.path[level], saved.path_child[level]);
		if (child == nullptr) {
			break; // the table changed under the save; stop at the deepest level that still exists
		}
		// Seed the level we are leaving with the row we took out of it, so a `Back` straight after
		// a restore lands where the user was -- legacy rebuilds exactly this through
		// `Submenu::focusChild(navigatedBackwardFrom)` (`gui/menu_item/submenu.cpp:21-37`).
		if (is_horizontal(ctx, *st.path[level])) {
			st.columns[level].focused = static_cast<int8_t>(saved.path_child[level]);
		}
		else {
			st.list[level].cursor = saved.path_child[level];
		}
		st.depth = static_cast<uint8_t>(level + 1);
		st.path[st.depth] = child;
		if (is_leaf(child->kind)) {
			// This reaches `CustomItem::on_focus`, so restoring a menu that
			// was parked on an item which opens a scene (`nodes::EditNameItem`) re-opens that
			// scene on the next `after_render`. **That is deliberate, and it is what legacy does.**
			// `SoundEditor::focusRegained` ends in `beginScreen()`, which
			// calls `beginSession` on whatever the current item is (`:1015`), and
			// `EditName::beginSession` opens `RenameUI` unconditionally
			// (`gui/menu_item/edit_name.cpp:29-54`) -- there is no "only the first time" arm
			// anywhere in that path.
			//
			// It is also the only coherent answer: `restore_state` is `replace_root`'s way of
			// putting the user back where they were, and where they were *is* the editor. Skipping
			// the hook would restore a menu sitting on a name-editor item that draws only its own
			// title and does nothing until the user backs out and re-enters it. Specced by
			// `rename_scene_spec.cpp`'s "reopens the editor when a menu parked on the item is
			// restored".
			//
			// **It is deliberate only for an item whose legacy body is `beginSession`.** An
			// item whose `on_focus` stands in for `selectButtonPress` --
			// `nodes::MidiDeviceLinkedItem`, the four `nodes::ActionItem`s -- would have its
			// *press* replayed here, so restoring a menu parked on the MIDI device-definition
			// toggle cleared the linked file with no user action, and restoring one parked on
			// `start_stem_export` re-filed the export. `enter_leaf`'s `FocusCause` is the
			// discriminator and `CustomItem::on_focus_is_press` is the scope; see both.
			session.enter_leaf(core, *child, FocusCause::NoRowPress);
		}
	}

	const Node* node = focused(st);
	if (node != nullptr && is_horizontal(ctx, *node)) {
		st.columns[st.depth].focused = static_cast<int8_t>(saved.focus);
	}
	else {
		st.list[st.depth].cursor = saved.focus;
	}
}

bool MenuWalk::switch_chained_page(MenuCore core, ValueSession& session, int32_t direction) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// `HorizontalMenu::switchHorizontalMenu` (`horizontal_menu.cpp:364-394`).
	const std::span<const Node* const> chain = chain_for(ctx.root);
	if (chain.empty() || direction == 0) {
		return false;
	}
	const auto count = static_cast<int32_t>(chain.size());
	const auto at = std::ranges::find(chain, focused(st));
	const auto current = static_cast<int32_t>(std::distance(chain.begin(), at));
	for (int32_t step = 1; step <= count; step++) {
		const int32_t index = ((current + direction * step) % count + count) % count;
		const Node& target = *chain[index];
		const Node* kids[kMaxMenuChildren];
		if (visible_children(ctx, target, kids) == 0) {
			continue; // no relevant items: on to the next menu
		}
		retarget(core, session, target);
		st.columns[0] = deluge::ui::ColumnsState{};
		// Mod FX opens on its second page, the selected effect's controls (`:383`).
		if (&target == &nodes::mod_fx || &target == &nodes::global_mod_fx) {
			const Node* page_kids[kMaxMenuColumns];
			deluge::ui::oled::Column cols[kMaxMenuColumns]{};
			NodeText label_scratch[kMaxMenuColumns];
			const size_t n = build_column_list(ctx, target, std::span<const Node*>{page_kids, kMaxMenuColumns},
			                                   std::span<deluge::ui::oled::Column>{cols, kMaxMenuColumns},
			                                   std::span<NodeText>{label_scratch, kMaxMenuColumns});
			st.columns[0].focused = static_cast<int8_t>(
			    deluge::ui::oled::page_switch(std::span<const deluge::ui::oled::Column>{cols, n}, 0, 1));
		}
		deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelNotification);
		return true;
	}
	return false;
}

} // namespace deluge::scenes::menu
