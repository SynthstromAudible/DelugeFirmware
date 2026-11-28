#pragma once

/// @file
/// @brief The menu's shortcut blinks: which pads blink for the focused node, and legacy's counter
///        that decides when each one flashes.
///
/// Ports `SoundEditor::updatePadLightsFor` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), which sets the
/// blink up, and `SoundEditor::blinkShortcut`, which plays it. A blink is a pad
/// flash the control surface plays by itself (`ui::widgets::grid::GridFrame::flash`); it paints no pixel.

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/widgets/grid/shortcut_blink.h"

#include <array>
#include <cstdint>

namespace deluge::ui::scenes::menu {

struct MenuState;
struct Node;

using widgets::grid::blink_step;
using widgets::grid::BlinkPlan;
using widgets::grid::exclusive_blink;
using widgets::grid::fire_blink_step;
using widgets::grid::kBlinkCycleMs;
using widgets::grid::kNoBlink;
using widgets::grid::kShortcutBlinkMs;
using widgets::grid::kSourceBlinkMs;
using widgets::grid::ms_to_next_step;
using widgets::grid::ParamBlink;
using widgets::grid::SourceBlink;

static_assert(kShortcutRows == widgets::grid::kBlinkSourceRows);
static_assert(kShortcutCols - 2 == widgets::grid::kBlinkSourceCol);

/// @brief What blinks while @p focused is focused.
///
/// The focused node's own shortcut pad, every cycle, yellow when it is the second layer's
/// (`updatePadLightsFor`'s `setupShortcutBlink(x, y, 0, ...)`, `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
/// Nothing blinks for the name editors and the sample-marker editors, nor under the settings, note
/// and note-row roots (`SoundEditor::updatePadLightsFor`).
/// The table follows `updatePadLightsFor`'s choice: a kit with affect-entire on and the host's kit-global latch set
/// blinks from the kit's own table (`paramShortcutsForKitGlobalFX`), whatever row the menu edits; a kit-global menu
/// with affect-entire off falls through to the sound table, as legacy's chain does.
/// @param ctx What the menu edits.
/// @param state The menu's state.
/// @param focused The focused node.
/// @param kit_global_latched The host's latch (`MenuScene::kit_global_shortcut_context` answering); a kit-global
///        context implies it.
/// @return The plan; an empty one when nothing blinks.
BlinkPlan plan_blinks(const MenuContext& ctx, const MenuState& state, const Node& focused,
                      bool kit_global_latched = false);

} // namespace deluge::ui::scenes::menu
