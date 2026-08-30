#pragma once

/// @brief `EditNameItem` -- the `CustomItem` behind the three name-editor menu items, and the one
///        that opens `rename::RenameScene`.
///
/// The port of `deluge::gui::menu_item::EditName` (`gui/menu_item/edit_name.{h,cpp}`), which is a
/// `MenuItem` whose entire body is a `beginSession` override: it points the right `RenameUI` at the
/// right model object and calls `openUI()` on it. `CustomItem::on_focus` is `beginSession`
/// (`node.h`), so that is where this does the same thing -- except that a scene may not touch the
/// stack itself, so instead of opening a UI it records the push in `MenuState::pending_scene` and
/// `MenuScene::after_render` turns it into an `Action`.
///
/// Legacy's `soundEditor.shouldGoUpOneLevelOnBegin = true` (`edit_name.cpp:50`), which makes the
/// menu step back out of the item when the rename UI closes (`SoundEditor::focusRegained`,
/// `gui/ui/sound_editor.cpp:280-283`), is `MenuState::go_up_on_resume` -- set by the same
/// `after_render` arm, read by `MenuScene::on_resume`.
///
/// @note Legacy also calls `instrumentClipView.cancelAllAuditioning()` on the way
///       (`edit_name.cpp:51-53`) when the clip is an instrument clip. That is a `gui/` view's own
///       state, which this layer may not reach; it belongs to whichever scene owns the clip view.

#include "scenes/menu/node.h"

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @brief One menu item that opens the rename editor on a named target.
///
/// Not a placeholder (`is_placeholder()` keeps its `false` default): this is the finished item, and
/// `validate_tree` should stop reporting these three nodes.
class EditNameItem final : public CustomItem {
public:
	/// @param target The `rename::TargetId` this item opens, as the plain `uint16_t` a `SceneId`
	///               carries. Spelled as a number rather than the enum so this header does not have
	///               to pull `scenes/rename/` into every table that names an item.
	explicit EditNameItem(uint16_t target) : target_{target} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void on_focus(const MenuContext& ctx, MenuState& state) const override;

private:
	uint16_t target_;
};

/// @name The three instances, one per reachable `rename::TargetId`.
///
/// Not `constexpr` (they have a vtable), but their *addresses* are constant expressions, which is
/// all `custom()` needs -- exactly the spelling `nodes::kPending` uses.
/// @{
inline const EditNameItem kEditClipName{0};   ///< `rename::TargetId::Clip`
inline const EditNameItem kEditOutputName{1}; ///< `rename::TargetId::Output`
inline const EditNameItem kEditDrumName{2};   ///< `rename::TargetId::Drum`
/// @}

} // namespace deluge::scenes::menu::nodes
