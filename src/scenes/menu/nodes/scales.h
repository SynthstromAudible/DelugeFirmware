#pragma once

/// @file
/// @brief `ActiveScalesItem` -- the two preset-scale checklists.
///
/// The port of `deluge::gui::menu_item::ActiveScaleMenu` (`gui/menu_item/active_scales.{h,cpp}`),
/// which legacy instantiates twice (`gui/ui/menus.cpp:1670`, `:2071`):
///
// clang-format off
/// | node | legacy global | `ActiveScaleMenu::Kind` | the bitset it edits |
/// |---|---|---|---|
/// | `default_active_scale` | `defaultActiveScaleMenu` (`gui/ui/menus.cpp:1670`) | `DEFAULT` | `FlashStorage::defaultDisabledPresetScales` (`storage/flash_storage.h:74`) |
/// | `active_scales` | `activeScaleMenu` (`gui/ui/menus.cpp:2071`) | `SONG` | `currentSong->disabledPresetScales` (`model/song/song.h:223`) |
// clang-format on
///
/// One row per `PresetScale` from 0 to `LAST_PRESET_SCALE` (`model/scale/preset_scales.h:78`),
/// labelled from `scalelikeNames` (`:97`), each carrying a checked or unchecked box. The select
/// press toggles the row's bit and stays put; there is nothing to navigate into.
///
/// ## Why this needs no new widget
///
/// `ActiveScaleMenu::drawSubmenuItemsForOled` (`active_scales.cpp:62-99`) says so itself: its own
/// comment is "adapted from submenu.cpp & toggle.cpp". It is `Submenu::drawSubmenuItemsForOled` with
/// a checkbox in place of the arrow, and `ui::oled::list` already ports that renderer -- same label
/// origin (`style.inset_x` == `kTextSpacingX`), same icon x (`rect.right() - kSubmenuIconSpacingX -
/// 3`, `list.cpp:126` against `menu_item.h:280`), same label shortening (`4 + kSubmenuIconSpacingX +
/// 3`, `list.cpp:133-135` against `menu_item.h:278`), same `invertLeftEdgeForMenuHighlighting`
/// highlight, same side scroller on the focused row. So this item composes `list` and hands it
/// `icons::checkedBox` / `icons::uncheckedBox`, which is the same thing task 6's `ContextMenuScene`
/// could *not* do and had to grow `option_window` for.
///
/// The windowing agrees too, and that is worth stating because it looks like it might not.
/// `drawPixelsForOled` (`:28-59`) builds its three-row window by hand -- row 0 at the top of the
/// list, row 2 at the bottom, row 1 everywhere in between -- and `list_window_start` (`ui/oled/list.h`)
/// derives exactly that from "keep the cursor near the middle as the ends allow" for a three-row
/// window. On the 64-row panel `style.menu_rows` is five and the port shows five, where legacy's
/// hand-built vector is `kOLEDMenuNumOptionsVisible` (3, `static_assert`ed at
/// `gui/menu_item/source_selection.cpp:61`) whatever the panel; that is the same
/// let-the-widget-own-the-window simplification `MidiDevicesItem` and `MultiRangeItem` already make
/// (`nodes/dynamic.h`), and it only differs on a panel geometry no shipped device has.
///
/// ## The cursor
///
/// Legacy's `currentPos` is a mutable member of the menu item, so it survives leaving the page and
/// coming back, and `beginSession` deliberately does **not** reset it (`:17-19` is only a redraw).
/// A `CustomItem` here is `const`, so the cursor lives in `MenuState::list[depth]` -- which survives
/// the same round trip, because the scene keeps one per depth and both of these nodes are always at
/// the same depth (a direct child of their own root). The one case the two spellings could diverge
/// is a visit that arrives at a *different* depth, which no path in the tree produces.
///
/// ## The write
///
/// `selectButtonPress` (`:108-112`) flips the bit and returns `NO_NAVIGATION`. A `CustomItem::render`
/// may not touch the model (`menu_scene.h`), so the render records the press through
/// `MenuState::value_changed`/`value_node` and `commit()` performs the flip -- the same split the
/// three patch-cable pages use, and the one place in the scene where a write is allowed.

#include "scenes/menu/node.h"

#include <cstddef>
#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @brief Which bitset an `ActiveScalesItem` edits -- legacy's `ActiveScaleMenu::Kind`
///        (`gui/menu_item/active_scales.h:15`).
enum class ScaleSet : uint8_t {
	/// `currentSong->disabledPresetScales` (`active_scales.cpp:118-120`, `:130-132`).
	Song,
	/// `FlashStorage::defaultDisabledPresetScales` (`:115-117`, `:127-129`).
	Default,
};

/// @brief One preset-scale checklist.
class ActiveScalesItem final : public CustomItem {
public:
	explicit ActiveScalesItem(ScaleSet set) : set_{set} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;
	void commit(const MenuContext& ctx, MenuState& state) const override;

	/// @brief Which bitset this item edits. Exposed so a spec can assert the two-item mapping by
	///        identity rather than re-deriving it.
	ScaleSet set() const { return set_; }

	/// @brief Is scale @p index currently *disabled*? `ActiveScaleMenu::isDisabled`
	///        (`active_scales.cpp:114-124`), including its "no song, nothing is disabled" arm.
	bool is_disabled(const MenuContext& ctx, size_t index) const;
	/// @brief `ActiveScaleMenu::setDisabled` (`active_scales.cpp:126-133`). A null song is a no-op,
	///        exactly as legacy's `else if (currentSong)` makes it.
	void set_disabled(const MenuContext& ctx, size_t index, bool value) const;

private:
	ScaleSet set_;
};

/// @name The two instances. `inline const`, like every other real `CustomItem` here.
/// @{
inline const ActiveScalesItem kSongActiveScalesItem{ScaleSet::Song};
inline const ActiveScalesItem kDefaultActiveScalesItem{ScaleSet::Default};
/// @}

} // namespace deluge::scenes::menu::nodes
