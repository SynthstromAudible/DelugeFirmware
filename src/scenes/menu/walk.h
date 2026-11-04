#pragma once

/// @file
/// @brief The menu's walk through its tree, and the read-only page queries the walk, the view and the buttons share.

#include "scenes/menu/menu_core.h"
#include "scenes/menu/present.h"
#include "scenes/menu/value_session.h"
#include "shell/scene.h"
#include "ui/oled/columns.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::scenes::menu {

/// @brief True when @p node renders and edits as a horizontal page: a `Layout::Horizontal` submenu with
///        `MenuContext::horizontal_menus` on (`HorizontalMenu::renderingStyle()`).
bool is_horizontal(const MenuContext& ctx, const Node& node);

/// @brief True when `focused()` is a `Layout::Horizontal` submenu -- the gate
///        `HorizontalMenu::buttonAction`'s six-button arm ports (`handle()`'s button code).
bool on_horizontal_page(const MenuContext& ctx, const MenuState& st);

/// @brief The node whose shortcut pad blinks and toggles the second layer: a horizontal page's
///        focused column (`Submenu::updatePadLights`), else `focused()`.
const Node* pad_subject(const MenuContext& ctx, const MenuState& st);

/// @brief Turn @p node's visible children into a `Column` span carrying label, slots,
///        relevance and `is_submenu` -- the part of a horizontal page's presentation
///        `handle()`'s button code needs as well as `render_horizontal`'s full render, factored
///        so the two read one list rather than two copies of the same rule
///        (`kid.relevant == nullptr || kid.relevant(ctx_)`, `kid.kind == Kind::Submenu`).
///        `render`/`ctx` are left null; only the renderer fills those in, and only when
///        actually drawing.
/// @param node          The horizontal submenu to list.
/// @param kids          Written with @p node's visible children, parallel to @p cols.
/// @param cols          Written with one `Column` per child.
/// @param label_scratch Backing storage for each column's label text; must outlive @p cols.
/// @return How many children were written (`visible_children`'s own count).
size_t build_column_list(const MenuContext& ctx, const Node& node, std::span<const Node*> kids,
                         std::span<deluge::ui::oled::Column> cols, std::span<NodeText> label_scratch);

/// @brief The focused horizontal page's paging (empty off one): the page counter and the SCALE/CROSS_SCREEN rule
///        read the same.
/// @return Page, total pages and position, as `ui::oled::paging_of` answers for the focused column.
deluge::ui::oled::Paging horizontal_paging(const MenuContext& ctx, const MenuState& st);

/// @brief The focused page's title: on a group (`Node::is_group`) the focused column's member's, as
///        `HorizontalMenuGroup::getTitle`.
/// @param ctx     The menu's context.
/// @param st      The menu's state; its path names the focused page.
/// @param scratch Storage the returned view may point into.
/// @return The title text.
std::string_view page_title(const MenuContext& ctx, const MenuState& st, NodeText& scratch);

/// @brief The menu's moves through its tree: re-rooting, descending, ascending, the range picker's substitution, the
///        chained pages, and the rows each submenu was last left on. A move that lands on a leaf begins its value
///        session through the `ValueSession` it is handed; the session never calls back.
class MenuWalk {
public:
	/// @brief Re-root the path at @p node (the shortcut jump and `on_enter`'s re-target).
	void retarget(MenuCore core, ValueSession& session, const Node& node);
	/// @brief Descend into @p child if its permission allows, opening the range picker for
	///        `MustSelectRange`.
	void enter_child(MenuCore core, ValueSession& session, const Node& child);
	/// @brief Push @p child onto the path with no permission question asked -- the half of
	///        `enter_child` the range-picker substitution reuses.
	void descend_to(MenuCore core, ValueSession& session, const Node& child);
	/// @brief Go up one level, skipping levels whose permission is now `No`
	///        (`sound_editor.cpp:710-721`).
	void ascend(MenuCore core, ValueSession& session);
	/// @brief Focus @p column on the page `focused()` shows, when the page lists it visibly;
	///        otherwise leave the focus where it is (`Submenu::focusChild`).
	void focus_column(MenuCore core, const Node& column);
	/// @brief `checkPermissionToBeginSessionForRangeSpecificParam`'s allocation
	///        (`sound_editor.cpp:1991-1995`) plus legacy's `MUST_SELECT_RANGE` substitution --
	///        `multiRangeMenu.menuItemHeadingTo = newItem; newItem = &multiRangeMenu;`
	///        (`:1857-1858`) -- the one piece `enter_child` (a descent) and `handle()`'s button
	///        focus (legacy's `initializeItem` call in `HorizontalMenu::handleItemAction`) both run,
	///        because both are ways into `MenuItem::checkPermissionToBeginSession`'s range branch.
	///
	///        **Not `Permission::No`.** `enter_child`'s own switch still owns that arm: a descent
	///        refuses outright on `No`, but `HorizontalMenu::handleInstrumentButtonPress` never
	///        consults `initializeItem`'s return for anything but `MUST_SELECT_RANGE` -- a button
	///        focus proceeds regardless of `Yes`/`No`, which is why this helper answers only the
	///        range question and leaves `No` to whichever caller cares.
	/// @param core                The menu's context and state.
	/// @param session             The value session the picker or the child would open.
	/// @param child               The node a session is about to begin on.
	/// @param suppress_when_allowed When true, and `child.allow_session_from_horizontal` is set,
	///         skip the `MUST_SELECT_RANGE` check entirely and answer false -- the port of
	///         `handleInstrumentButtonPress`'s own extra condition
	///         (`!(*current_item_)->allowToBeginSessionFromHorizontalMenu()`, `:427-428`), which
	///         no other caller of this helper has. See `Node::allow_session_from_horizontal`'s own
	///         doc for the two legacy call sites and why only one of them suppresses.
	/// @return True if @p child must not be entered/focused as itself this tick -- the range
	///         picker was opened instead, or the allocation failed and a notice was shown. False
	///         (permission `Yes` or `No`, or the range check was suppressed) means the caller
	///         proceeds with its own next step.
	bool enter_range_picker_if_needed(MenuCore core, ValueSession& session, const Node& child,
	                                  bool suppress_when_allowed);
	/// @brief `HorizontalMenu::handleItemAction`'s else-path: a
	///        submenu, or an `allow_session_from_horizontal` value column, pressed while already
	///        focused. Runs the range check unconditionally (unlike the button-focus arm) and then
	///        always enters -- `handleItemAction` never reads `Permission::No` at all, unlike
	///        `enter_child`'s general descend.
	/// @param core    The menu's context and state.
	/// @param session The value session the child's edit opens.
	/// @param child   The already-focused column being pressed again.
	void enter_horizontal_item(MenuCore core, ValueSession& session, const Node& child);
	/// @brief SHIFT + CROSS_SCREEN/SCALE at depth 0: replace the page with the next (or previous) menu of the root's
	///        chain that has columns, landing on its first page -- mod FX's second -- as `switchHorizontalMenu` does.
	/// @param direction +1 or -1.
	/// @return Whether the page changed.
	bool switch_chained_page(MenuCore core, ValueSession& session, int32_t direction);

	/// @brief Record the selected child of every submenu on the current path; only while `ctx_` is the one the path
	///        was walked with, since reading which children show runs their relevance tests.
	void remember_path(const MenuContext& ctx, const MenuState& st);
	/// @brief Put the submenu at the current depth on the child it was last left on, if it still shows it.
	void restore_row(MenuCore core);
	/// @brief Forget every remembered row, as a boot does.
	void forget_rows() {
		for (RememberedRow& row : remembered_) {
			row = RememberedRow{};
		}
		remembered_clock_ = 0;
	}

	/// @brief Write the path as `MenuSavedState` into @p state.
	void save(const MenuContext& ctx, const MenuState& st, deluge::ui::shell::SceneState& state) const;
	/// @brief Rebuild the path @p state saved, re-entering its leaf.
	void restore(MenuCore core, ValueSession& session, const deluge::ui::shell::SceneState& state);

	/// @brief Whether `ascend()` went up from depth 0 since the last call -- legacy's `exitCompletely()` -- clearing
	///        the request.
	bool take_close_request() { return std::exchange(close_requested_, false); }

private:
	/// @brief The child each submenu was last left on: legacy's per-`Submenu` `current_item_` (`submenu.cpp:9-18`),
	///        which a fresh visit lands on. Once all are in use the least recently recorded entry is overwritten; the
	///        submenus on the current path are recorded on every render, so a root is never the one evicted.
	struct RememberedRow {
		const Node* submenu = nullptr;
		const Node* child = nullptr;
		uint32_t recorded = 0; ///< `remembered_clock_` when last recorded; 0 for an empty entry
	};
	static constexpr size_t kRememberedRows = 128;
	RememberedRow remembered_[kRememberedRows]{};
	uint32_t remembered_clock_ = 0;
	/// @brief `ascend()` went up from depth 0: legacy's `exitCompletely()`, drained by `after_render`.
	bool close_requested_ = false;
};

} // namespace deluge::scenes::menu
