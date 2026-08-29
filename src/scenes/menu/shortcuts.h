#pragma once

/// @brief The grid shortcut lookup: which `Node` the pad at (x, y) opens, and where a `Node`'s own
///        shortcut pad sits so `MenuScene::render_grid` can blink it.
///
/// @warning **This file is a stub in E1 (task-5 ruling 1).** The seven real tables -- legacy's
///          `paramShortcutsForSounds`, `…ForAudioClips`, `…ForSongView`, `…ForKitGlobalFX`,
///          `…ForMidiCv`, `…ForMidiDrum`, `…ForGateDrum` (`gui/ui/sound_editor.cpp:1111-1250`'s
///          `potentialShortcutPadAction` and the tables it indexes) -- need Tasks 6-8's nodes to
///          exist before they can be written down, so they arrive in **Task 9**. Until then
///          `shortcuts.cpp` resolves nothing except a table a spec installs through
///          `set_shortcut_table_for_test`, which exists purely so Task 5's own spec can exercise
///          the pad-jump path end to end. Task 9 replaces the body, deletes that hook, and
///          re-asserts the same behaviour against the real tables.
///
/// The interface itself is not provisional: `MenuScene` is written against these three functions
/// and Task 9 changes only what is behind them.

#include "scenes/menu/context.h"

#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

struct Node;

/// @brief Where a node's shortcut pad sits on the grid, in absolute pad coordinates.
struct ShortcutPos {
	uint8_t x = 0;
	uint8_t y = 0;
	/// False when the node has no shortcut pad in @p root's table at all.
	bool valid = false;
};

/// @brief The node the shortcut pad at (@p x, @p y) opens, or `nullptr` for a pad with no
///        shortcut.
///
/// Ports the table lookup inside `SoundEditor::potentialShortcutPadAction`
/// (`gui/ui/sound_editor.cpp:1111`): which of the seven tables is consulted follows from @p root,
/// exactly as legacy's chain of `rootUIIsClipMinderScreen()` / `setupKitGlobalFXMenu` /
/// `ClipType::AUDIO` / output-type tests does.
///
/// @param root  Which menu root is open, i.e. which table applies.
/// @param shift Whether shift is held: legacy's second shortcut layer
///              (`ShortcutTable::layer0`/`layer1`, `shell/profile.h:87`).
/// @param x     Absolute pad column.
/// @param y     Absolute pad row.
/// @return The node, or `nullptr`.
const Node* shortcut_node(MenuContext::Root root, bool shift, uint8_t x, uint8_t y);

/// @brief Where @p node's own shortcut pad is, for the focused-param blink
///        (`SoundEditor::setupShortcutBlink`, `sound_editor.cpp:959`, whose `currentParamShortcutX`
///        / `currentParamShortcutY` this replaces).
///
/// The reverse of `shortcut_node` over the same table, so `MenuScene` never has to know a table's
/// shape.
///
/// @param root Which menu root is open.
/// @param node The node to locate.
/// @return Its pad, or a `ShortcutPos` with `valid == false`.
ShortcutPos shortcut_pos(MenuContext::Root root, const Node& node);

/// @brief The patching-source pads that blink alongside @p node, at legacy's own 20 ms cadence
///        (`SoundEditor::blinkShortcut`'s even-counter arm, `sound_editor.cpp:987-999`, which
///        flashes `x + 14` for every armed `sourceShortcutBlinkFrequencies[x][y]`).
///
/// @note **Empty in E1.** `sourceShortcutBlinkFrequencies` is filled by
///       `MenuItem::updatePadLights` / the patch-source items, which are Task 6-8's nodes; until
///       they exist there is no source to point at. The 20 ms cadence and its own `Blink` are
///       still wired in `MenuScene`, so Task 9 fills this in and the blink starts working with no
///       change to the scene.
///
/// @param root Which menu root is open.
/// @param node The node whose sources are being shown.
/// @return The pads, viewing storage owned by this translation unit; empty in E1.
std::span<const ShortcutPos> source_shortcut_pads(MenuContext::Root root, const Node& node);

/// @brief **Test-only (deleted by Task 9).** Install @p table as the one table every `root`
///        resolves through, addressed `y * cols + x`.
///
/// Exists so `sim/scenes_spec/menu_scene_spec.cpp` can exercise the pad-jump and the focused-pad
/// blink before the real tables are written. Pass an empty span to uninstall.
///
/// @param table Cells in row-major order; entries may be `nullptr` for "no shortcut here".
/// @param cols  Table width.
/// @param rows  Table height.
void set_shortcut_table_for_test(std::span<const Node* const> table, uint8_t cols, uint8_t rows);

} // namespace deluge::scenes::menu
