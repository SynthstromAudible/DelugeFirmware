#pragma once

/// @file
/// @brief The menu's shortcut pads: the shortcut blinks on the grid, the SHIFT + pad jump with its second layer, the
///        landing the jump arms, and the grid greyout under the menu.

#include "scenes/menu/menu_core.h"
#include "shell/scene.h"
#include "ui/grid/effects.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace deluge::scenes::menu {

/// @brief The grid greyout under the menu -- `SoundEditor::getGreyoutColsAndRows` (`sound_editor.cpp:441-484`).
deluge::ui::grid::GreyoutMask greyout_mask(const MenuContext& ctx, const MenuState& st);

/// @brief When the pad blinks next need a frame: the shorter of the shortcut counter's two steps the plan uses, or
///        nullopt with nothing blinking.
std::optional<uint32_t> pad_next_timer(const MenuContext& ctx, const MenuState& st);

/// @brief The shortcut pads and the landing a jump arms.
class MenuPads {
public:
	/// @brief Flash the shortcut blinks for `pad_subject()` (`blinkShortcut`, `sound_editor.cpp:1205-1245`).
	void render_grid(MenuCore core, deluge::ui::grid::GridFrame& f);
	/// @brief SHIFT + a pad: the second-layer toggle and the jump (`potentialShortcutPadAction`).
	/// @return The result when the event was a SHIFT + pad press, else nullopt.
	std::optional<deluge::ui::shell::Result> handle_pad(MenuCore core, const deluge::ui::shell::UiEvent& event,
	                                                    const deluge::ui::shell::UiContext& ui);
	/// @brief Arm the landing for a jump to @p target: its page's id when a chained page lists it, else its own.
	uint16_t arm_landing(const MenuContext& ctx, const Node& target);
	/// @brief The armed landing column, disarming it.
	const Node* take_landing() { return std::exchange(landing_column_, nullptr); }
	/// @brief Disarm the landing.
	void forget_landing() { landing_column_ = nullptr; }

private:
	/// @brief The column the next `on_enter` focuses on the page it opens, set by
	///        `arm_shortcut_landing` (from `handle()`'s jump, alongside the `Replace` it returns, or
	///        from the application before it opens the guest). Consumed by `on_enter`, cleared by
	///        `open()` and `on_resume()`. Not part of `MenuState`: the shell restores that between the
	///        `Replace` and `on_enter`.
	const Node* landing_column_ = nullptr;
};

} // namespace deluge::scenes::menu
