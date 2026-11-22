#pragma once

/// @file
/// @brief The SHIFT shortcut colour overlay under a sound menu: the pad grid coloured by what each shortcut pad opens,
///        and the patching-source column blue where a source may patch to the focused param -- `SoundEditor`'s
///        `renderMainPads` while `should_render_shortcut_overlay()`.

#include "project/device_settings.h"
#include "ui/gfx/grid/palette.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_state.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/widgets/grid/frame.h"

#include <array>

namespace deluge::ui::scenes::menu {

/// @brief `main`'s `shortcut_colours`, [x][y]: each shortcut pad's colour by what it edits.
extern const std::array<std::array<RGB, kShortcutRows>, kShortcutCols> kShortcutColours;
/// @brief `main`'s `dx_shortcut_colours`, [x][y]: the DX7 parameter page's own pad layout, coloured by what each pad
///        picks (`nodes::dx_pad_layout`).
extern const std::array<std::array<RGB, kShortcutRows>, kShortcutCols> kDxShortcutColours;

/// @brief `SoundEditor::should_render_shortcut_overlay` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the setting
///        is on, @p shift is held (latched or not), and the menu edits a sound-editor root -- not the settings, note or
///        note-row editor.
bool overlay_shown(const MenuContext& ctx, bool shift);

/// @brief One main-grid pad's colour in the shortcut colour map: `kShortcutColours[x][y]` where the pad's shortcut
///        names a node and (when @p ask_relevance) that node is relevant for source `x % 2`, else black.
/// @param ctx           The menu's context.
/// @param version       The shortcut layout in force (`shortcuts_version(ctx)`), read once by a caller painting every
///                      pad.
/// @param x             The pad's column.
/// @param y             The pad's row.
/// @param ask_relevance Black out a pad whose node is not relevant.
/// @return The pad's colour.
RGB shortcut_colour(const MenuContext& ctx, project::ShortcutsVersion version, uint8_t x, uint8_t y,
                    bool ask_relevance = true);

/// @brief `renderMainShortcutsOnly` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): every main-grid pad its colour
///        from `kShortcutColours` where its shortcut names an item relevant here, asked for source `x % 2`, else black.
void paint_shortcut_colours(deluge::ui::widgets::grid::GridFrame& f, const MenuContext& ctx);

/// @brief `renderMainPads`' source column (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): with @p focused a patched
///        param or a cable strength page, each patching-source pad blue where either layer's source may patch to the
///        param -- or, when the items from @p focused up answer "range" for a held source pad, may control a cable's
///        range.
/// @param focused The node the menu edits (`focused_value`).
void paint_source_column(deluge::ui::widgets::grid::GridFrame& f, const MenuContext& ctx, const MenuState& st,
                         const Node* focused);

/// @brief Is (@p x, @p y) a patching-source pad: (14, 5..7) or column 15?
constexpr bool is_patching_source_pad(uint8_t x, uint8_t y) {
	return (x == kShortcutCols - 2 && y >= 5) || x == kShortcutCols - 1;
}

/// @brief `isOtherModSourcePadPressed` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): a patching-source pad other
///        than (@p x, @p y) is in @p held.
bool other_source_pad_held(const deluge::ui::widgets::core::PadMask& held, uint8_t x, uint8_t y);

/// @brief `renderMainShortcutsOnly` under `isDxShortcutLayoutActive`: every main-grid pad its `kDxShortcutColours`.
void paint_dx_shortcut_colours(deluge::ui::widgets::grid::GridFrame& f);
/// @brief The whole overlay: the colour map (the DX7 layout's on its parameter page), then the source column over it.
void render_overlay(deluge::ui::widgets::grid::GridFrame& f, const MenuContext& ctx, const MenuState& st,
                    const Node* focused);

} // namespace deluge::ui::scenes::menu
