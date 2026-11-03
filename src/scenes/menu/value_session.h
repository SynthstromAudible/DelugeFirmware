#pragma once

/// @file
/// @brief The menu's value session: loading a focused leaf's value into the edit storage, committing it, and the
///        latch a value takes as its session begins.

#include "scenes/menu/menu_core.h"
#include "scenes/menu/node.h"

#include <cstdint>

namespace deluge::scenes::menu {

/// @brief Why a leaf is being focused -- the discriminator legacy spells as
///        `soundEditor.getCurrentMenuItem() == this` (`gui/menu_item/toggle.h:35-37`).
///
/// Legacy reaches a bespoke item's work by two routes and treats them differently:
/// `selectButtonPress()` (a press on the parent's row) *acts*, while `beginSession()` -- which
/// `focusRegained` -> `beginScreen` also runs on every re-entry, `sound_editor.cpp:330`,
/// `:1015` -- only (re-)initialises. `CustomItem::on_focus` is `beginSession`, so a port that
/// ran it on every focus transition ran *both* meanings, and an item whose `on_focus` stands in
/// for a press fired without one. This is that lost distinction, restored.
/// **The question is "a select press on THIS node's own row?", not "a press at all"** (E3 final
/// review, A6). The values used to be `Descended`/`Restored`, and `Restored` was a lie for one
/// of the routes it covers: a **shortcut-pad jump is a real press**, just not one on this node's
/// row. The behaviour was right -- legacy reaches a shortcut jump through `beginSession`, never
/// `selectButtonPress` -- so the names moved to say what the two values have always meant,
/// rather than a third cause being invented for a distinction nothing acts on.
/// `CustomItem::on_focus_is_press()`'s whole contract is then readable from this enum alone:
/// such an item is given `on_focus` for `RowPress` and withheld it for `NoRowPress`.
enum class FocusCause : uint8_t {
	/// A select press on this node's own row in its parent's list: `enter_child` ->
	/// `descend_to`, the one path with a press behind it. Legacy's `selectButtonPress` path.
	RowPress,
	/// Focus reached this node by any other route, so nothing pressed its row: `retarget` (a
	/// `SceneId` deep link, the shortcut-pad jump, `open()`), `restore_state`'s rebuild, and
	/// `ascend`'s re-seed. Legacy's `beginScreen`/`focusRegained` path, and exactly the case
	/// `Toggle::selectButtonPress`'s guard exists for. A shortcut jump belongs here **because
	/// its press was on a grid pad, not on this row** -- it is not a "restore", which is what
	/// the old name claimed.
	NoRowPress,
};

/// @brief The value being edited and the edit storage's round trip through `bindings`. Its state belongs to one visit
///        of the scene.
class ValueSession {
public:
	/// @brief Begin @p value's session: `Direction::checkPermissionToBeginSession`'s latch
	/// (`sequence/direction.h:117-119`)
	///        -- when @p value is Play Direction on a non-kit clip, `MenuContext::selected_note_row` takes whether a
	///        row is auditioning.
	///
	/// Taken where the item begins: a re-root, a row press, and a horizontal column gaining focus. Legacy's horizontal
	/// page also re-takes it on the way back from the item and after each column move (`HorizontalMenu::beginSession`
	/// and `selectEncoderAction` -> `focusChild`); Play Direction is a column only in the note-row editor, whose root
	/// answers for the latch (`bindings.cpp`'s `note_row_bridge_direction`), so those re-takes change nothing.
	void begin(MenuCore core, const Node* value);
	/// @brief The value the last render's session began on (`focused_value`); a change is a new session.
	const Node* value() const { return value_; }
	/// @brief Forget the session's value, so the next render begins a new one.
	void forget() { value_ = nullptr; }

	/// @brief Load @p node's value into the edit storage its kind uses.
	/// @param core  The scene's context and state.
	/// @param node  The leaf.
	/// @param cause Why this focus is happening -- see `FocusCause`. A `Kind::Custom` node whose item answers
	///              `CustomItem::on_focus_is_press()` is given `on_focus` only for `FocusCause::RowPress`.
	void enter_leaf(MenuCore core, const Node& node, FocusCause cause) const;
	/// @brief Commit the edit storage for @p node through `bindings::write`.
	void commit(MenuCore core, const Node& node) const;
	/// @brief Reload the focused leaf's value and redraw -- `Value::readValueAgain` (`value.h:77-80`).
	void read_again(MenuCore core) const;

private:
	const Node* value_ = nullptr;
};

} // namespace deluge::scenes::menu
