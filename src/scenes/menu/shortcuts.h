#pragma once

/// @brief The grid shortcut lookup: which `Node` the pad at (x, y) opens, and where a `Node`'s own
///        shortcut pad sits so `MenuScene::render_grid` can blink it.
///
/// The seven legacy tables -- `paramShortcutsForSounds`, `…SecondLayer`, `…ForAudioClips`,
/// `…ForSongView`, `…ForKitGlobalFX` (`gui/ui/menus.cpp:2172-2273`), `midiOrCVParamShortcuts` and
/// `gateDrumParamShortcuts` (`:1200-1220`) -- transcribed cell by cell as `Node` pointers, keyed by
/// `MenuContext::Root`. `sim/scenes_spec/shortcuts_spec.cpp` cross-checks every cell of every table
/// against the legacy array it came from, in both directions.
///
/// The board no longer carries any of this: `BoardProfile::shortcuts` (two
/// `std::span<const uint16_t>` layers) could not hold five 16x8 tables of *node pointers* plus two
/// one-dimensional ones selected by context, so the field is gone and the tables live here (ruling
/// Q2).

#include "scenes/menu/context.h"

#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

struct Node;

/// @brief The grid a shortcut table covers: legacy's `kDisplayWidth` x `kDisplayHeight`.
///
/// A fixed 16x8 rather than the board's own geometry on purpose -- these are transcriptions of
/// legacy arrays declared `[kDisplayWidth][kDisplayHeight]`, and a cell only means anything at the
/// coordinate legacy put it at. `shortcut_node` is bounds-checked, so a board with a smaller pad
/// grid simply never asks about the cells it has no pads for.
/// @{
inline constexpr uint8_t kShortcutCols = 16;
inline constexpr uint8_t kShortcutRows = 8;
/// @}

/// @brief Where a node's shortcut pad sits on the grid, in absolute pad coordinates.
struct ShortcutPos {
	uint8_t x = 0;
	uint8_t y = 0;
	/// False when the node has no shortcut pad in @p root's table at all.
	bool valid = false;
};

/// @brief One root's shortcut layer: `kShortcutCols * kShortcutRows` cells, addressed
///        `y * kShortcutCols + x`, null where the legacy cell was null or `comingSoonMenu`.
///
/// A legacy one-dimensional table (`midiOrCVParamShortcuts`, `gateDrumParamShortcuts`) is stored as
/// a full grid with only **column 11** filled -- the column `potentialShortcutPadAction`
/// (`sound_editor.cpp:1277`) and `updatePadLightsFor` (`:836`, `:848`) index it at. (The task brief
/// said column 0; legacy says 11.)
///
/// @param root         Which menu root is open.
/// @param second_layer Legacy's second shortcut layer -- see `shortcut_node`.
/// @return The cells. Every root resolves to a layer of exactly `kShortcutCols * kShortcutRows`
///         cells; `Root::Settings` resolves to an all-null one, because legacy never blinks or
///         jumps a shortcut while the settings menu is open (`updatePadLightsFor`'s
///         `!inSettingsMenu()` guard, `sound_editor.cpp:812`).
std::span<const Node* const> shortcut_layer(MenuContext::Root root, bool second_layer);

/// @brief The node the shortcut pad at (@p x, @p y) opens, or `nullptr` for a pad with no
///        shortcut.
///
/// Ports the table lookup inside `SoundEditor::potentialShortcutPadAction`
/// (`gui/ui/sound_editor.cpp:1140-1325`): which of the seven tables is consulted follows from
/// @p root, exactly as legacy's chain of `rootUIIsClipMinderScreen()` / `setupKitGlobalFXMenu` /
/// `ClipType::AUDIO` / output-type tests does.
///
/// @param root         Which menu root is open, i.e. which table applies.
/// @param second_layer Take the cell from the second layer where that layer has one. This is
///                     legacy's `secondLayerShortcutsToggled` (`sound_editor.cpp:1309-1324`) --
///                     pressing the focused item's *own* shortcut pad a second time -- **not**
///                     "shift is held". Shift is only the gate that lets a pad be a shortcut at
///                     all (`:1114`), which is the caller's business. As in legacy, a null second
///                     layer cell falls back to the first layer's, so this never *removes* a
///                     shortcut. Only `Root::Sound` and `Root::Drum` have a second layer
///                     (`paramShortcutsForSoundsSecondLayer`); for every other root both layers
///                     are the same cells.
/// @param x            Absolute pad column.
/// @param y            Absolute pad row.
/// @return The node, or `nullptr`.
const Node* shortcut_node(MenuContext::Root root, bool second_layer, uint8_t x, uint8_t y);

/// @brief Where @p node's own shortcut pad is, for the focused-param blink
///        (`SoundEditor::updatePadLightsFor` / `setupShortcutBlink`, `sound_editor.cpp:806-905`,
///        whose `currentParamShortcutX` / `currentParamShortcutY` this replaces).
///
/// The reverse of `shortcut_node` over the same tables, so `MenuScene` never has to know a table's
/// shape. The second layer is searched first and the first layer after it, which is the order
/// `updatePadLightsFor` scans in (`:867` before `:872`).
///
/// @note Legacy's two context-dependent suppressions are **not** ported: the reverb-sidechain one
///       (`:873`, "x == 10 && y < 6 && editingReverbSidechain()") and the odd-column/source-0 one
///       (`:876`). Both read `SoundEditor` state this signature does not carry, and both only
///       *withhold* a blink; a blink on the right pad is the failure mode either way.
///
/// @param root Which menu root is open.
/// @param node The node to locate.
/// @return Its pad, or a `ShortcutPos` with `valid == false`.
ShortcutPos shortcut_pos(MenuContext::Root root, const Node& node);

/// @brief The patching-source pads that blink alongside @p node, on the short phase of
///        `render_grid`'s cadence (`SoundEditor::blinkShortcut`'s even-counter arm,
///        `sound_editor.cpp:987-999`, which flashes `x + 14` for every armed
///        `sourceShortcutBlinkFrequencies[x][y]`).
///
/// @note **Still empty after Task 9, and deliberately so.** Which source pads blink is not table
///       data: legacy fills `sourceShortcutBlinkFrequencies` from
///       `MenuItem::shouldBlinkPatchingSourceShortcut` (`sound_editor.cpp:759-781`), which every
///       patch-cable and patched-param item answers out of the live patch matrix. In E1 every
///       patch-cable item is a `Kind::Custom` placeholder (`nodes::kPending`), so there is no
///       patching state in this layer to answer from; E2's patch-cable items bring both. The
///       cadence and its phase are already wired in `MenuScene::render_grid`, so filling this in
///       needs no change to the scene.
///
/// @param root Which menu root is open.
/// @param node The node whose sources are being shown.
/// @return The pads, viewing storage owned by this translation unit; empty in E1.
std::span<const ShortcutPos> source_shortcut_pads(MenuContext::Root root, const Node& node);

} // namespace deluge::scenes::menu
