#pragma once

/// @file
/// @brief The menu's shortcut pads: the shortcut blinks on the grid, the SHIFT + pad jump with its second layer, the
///        landing the jump arms, and the grid greyout under the menu.

#include "scenes/menu/menu_core.h"
#include "scenes/menu/value_session.h"
#include "scenes/menu/walk.h"
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
	std::optional<deluge::ui::shell::Result> handle_pad(MenuCore core, MenuWalk& walk, ValueSession& session,
	                                                    const deluge::ui::shell::UiEvent& event,
	                                                    const deluge::ui::shell::UiContext& ui);
	/// @brief SHIFT + the patching-source pad (@p x, @p y): the source arm of `potentialShortcutPadAction`
	///        (`sound_editor.cpp:1532-1609`).
	///
	/// The pad's source -- its second layer when the pad is the active source of a patch-cable page that blinks it
	/// white -- is offered to the focused item, then each item above while they answer `SourcePress::GoUp`; past the
	/// root the menu closes. An item's `Open` opens its node one level below it, when that node's permission allows.
	/// @return The result when an item took the press or the pad names no source, else nullopt.
	std::optional<deluge::ui::shell::Result> source_press(MenuCore core, MenuWalk& walk, ValueSession& session,
	                                                      uint8_t x, uint8_t y, const deluge::ui::shell::UiContext& ui);
	/// @brief Arm the landing for a jump to @p target: its page's id when a chained page lists it, else its own.
	uint16_t arm_landing(const MenuContext& ctx, const Node& target);
	/// @brief Arm the next `on_enter` to focus @p column on @p page, which lists it.
	/// @return @p page's id (0 when it has none).
	uint16_t arm_landing_on(const Node& page, const Node& column);
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
