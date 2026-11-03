#pragma once

/// @file
/// @brief The menu's OLED rendering, and the input claims it makes -- stated here twice, once as the render reads
///        and once as `owned_input` predicts it, so the two stay in one file.

#include "scenes/menu/menu_core.h"
#include "scenes/menu/present.h"
#include "ui/core/encoder_acceleration.h"
#include "ui/core/frame_context.h"
#include "ui/oled/columns.h"

namespace deluge::scenes::menu {

/// @brief The inputs the render claims for @p st's focused node: the `render_*` function it dispatches to, answered
///        before it runs. `MenuScene::owns` reads it.
deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& st);

/// @brief Draws the focused node and consumes its input the toolkit way -- a widget claims what it owns while it is
///        drawn -- recording what it observed in `MenuState::tick` for `after_render`.
class MenuView {
public:
	/// @brief Draw @p node, which is `focused()`, and record this tick's observations.
	void render(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	/// @brief Forget the horizontal pages' select-turn acceleration, as a boot does.
	void forget_acceleration() { horizontal_acceleration_ = deluge::ui::EncoderAcceleration{}; }

private:
	void render_vertical(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	void render_horizontal(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	// One helper per kind: each owns the widget's own argument shapes rather than sharing an
	// `edit_value` the widgets do not agree on.
	void render_leaf_int(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_decimal(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_enum(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_toggle(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_sync_level(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_range(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node);

	/// What `render_horizontal`'s columns draw from, here rather than on the stack: a group page has up to
	/// `kMaxMenuColumns` of them, and the scene lives in SDRAM.
	ColumnSlot column_slots_[kMaxMenuColumns];
	/// `HorizontalMenu::acceleration()`: one select-turn acceleration for every horizontal page, as legacy's static is.
	deluge::ui::EncoderAcceleration horizontal_acceleration_{};
};

} // namespace deluge::scenes::menu
