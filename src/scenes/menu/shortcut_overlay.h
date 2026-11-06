#pragma once

/// @file
/// @brief The SHIFT shortcut colour overlay under a sound menu: the pad grid coloured by what each shortcut pad opens,
///        and the patching-source column blue where a source may patch to the focused param -- `SoundEditor`'s
///        `renderMainPads` while `should_render_shortcut_overlay()`.

#include "gfx/grid/palette.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_state.h"
#include "scenes/menu/node.h"
#include "scenes/menu/shortcuts.h"
#include "ui/grid/frame.h"

#include <array>

namespace deluge::scenes::menu {

/// @brief `main`'s `shortcut_colours`, [x][y]: each shortcut pad's colour by what it edits.
extern const std::array<std::array<RGB, kShortcutRows>, kShortcutCols> kShortcutColours;
/// @brief `main`'s `dx_shortcut_colours`, [x][y]: the DX7 parameter page's own pad layout, coloured by what each pad
///        picks (`nodes::dx_pad_layout`).
extern const std::array<std::array<RGB, kShortcutRows>, kShortcutCols> kDxShortcutColours;

/// @brief `SoundEditor::should_render_shortcut_overlay`: the setting is on, @p shift is held (latched or not), and
///        the menu edits a sound-editor root -- not the settings, note or note-row editor.
bool overlay_shown(const MenuContext& ctx, bool shift);

/// @brief `renderMainShortcutsOnly` (`sound_editor.cpp:193-219`): every main-grid pad its colour from
///        `kShortcutColours` where its shortcut names an item relevant here, asked for source `x % 2`, else black.
void paint_shortcut_colours(deluge::ui::grid::GridFrame& f, const MenuContext& ctx);

/// @brief `renderMainPads`' source column (`sound_editor.cpp:290-342`): with @p focused a patched param or a cable
///        strength page, each patching-source pad blue where either layer's source may patch to the param -- or, when
///        the items from @p focused up answer "range" for a held source pad, may control a cable's range.
/// @param focused The node the menu edits (`focused_value`).
void paint_source_column(deluge::ui::grid::GridFrame& f, const MenuContext& ctx, const MenuState& st,
                         const Node* focused);

/// @brief Is (@p x, @p y) a patching-source pad: (14, 5..7) or column 15?
constexpr bool is_patching_source_pad(uint8_t x, uint8_t y) {
	return (x == kShortcutCols - 2 && y >= 5) || x == kShortcutCols - 1;
}

/// @brief `isOtherModSourcePadPressed` (`sound_editor.cpp:245-254`): a patching-source pad other than (@p x, @p y) is
///        in @p held.
bool other_source_pad_held(const deluge::ui::PadMask& held, uint8_t x, uint8_t y);

/// @brief `renderMainShortcutsOnly` under `isDxShortcutLayoutActive`: every main-grid pad its `kDxShortcutColours`.
void paint_dx_shortcut_colours(deluge::ui::grid::GridFrame& f);
/// @brief The whole overlay: the colour map (the DX7 layout's on its parameter page), then the source column over it.
void render_overlay(deluge::ui::grid::GridFrame& f, const MenuContext& ctx, const MenuState& st, const Node* focused);

} // namespace deluge::scenes::menu
