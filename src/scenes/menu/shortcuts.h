#pragma once

/// @brief The grid shortcut lookup: which `Node` the pad at (x, y) opens, and where a `Node`'s own
///        shortcut pad sits so `MenuScene::render_grid` can blink it.
///
/// The seven legacy tables -- `paramShortcutsForSounds`, `…SecondLayer`, `…ForAudioClips`,
/// `…ForSongView`, `…ForKitGlobalFX` (`gui/ui/menus.cpp:2174-2275`), `midiOrCVParamShortcuts` and
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
/// (`sound_editor.cpp:1315`) and `updatePadLightsFor` (`:874`, `:886`) index it at. (The task brief
/// said column 0; legacy says 11.)
///
/// @param root         Which menu root is open.
/// @param second_layer Legacy's second shortcut layer -- see `shortcut_node`.
/// @note This is the **transcription only**, and it is what `shortcut_pos` searches -- so it is the
///       blink's view, not the press's. Three roots resolve four further pads that are in no layer,
///       and `Root::KitGlobalFx` cannot press two cells that are; both asymmetries are legacy's and
///       are documented on `shortcut_node`.
///
/// @return The cells. Every root resolves to a layer of exactly `kShortcutCols * kShortcutRows`
///         cells; `Root::Settings` resolves to an all-null one, because legacy never blinks a
///         shortcut while the settings menu is open (`updatePadLightsFor`'s `!inSettingsMenu()`
///         guard, `sound_editor.cpp:850`).
std::span<const Node* const> shortcut_layer(MenuContext::Root root, bool second_layer);

/// @brief The node the shortcut pad at (@p x, @p y) opens, or `nullptr` for a pad with no
///        shortcut.
///
/// Ports the table lookup inside `SoundEditor::potentialShortcutPadAction`
/// (`gui/ui/sound_editor.cpp:1178-1363`): which of the seven tables is consulted follows from
/// @p root, exactly as legacy's chain of `rootUIIsClipMinderScreen()` / `setupKitGlobalFXMenu` /
/// `ClipType::AUDIO` / output-type tests does.
///
/// @param root         Which menu root is open, i.e. which table applies.
/// @param second_layer Take the cell from the second layer where that layer has one. This is
///                     legacy's `secondLayerShortcutsToggled` (`sound_editor.cpp:1347-1362`) --
///                     pressing the focused item's *own* shortcut pad a second time -- **not**
///                     "shift is held". Shift is only the gate that lets a pad be a shortcut at
///                     all (`:1114`), which is the caller's business. As in legacy, a null second
///                     layer cell falls back to the first layer's, so this never *removes* a
///                     shortcut. Only `Root::Sound` and `Root::Drum` have a second layer
///                     (`paramShortcutsForSoundsSecondLayer`); for every other root both layers
///                     are the same cells.
/// @note **Resolve-only cells.** `Root::MidiOrCv`, `Root::MidiDrum` and `Root::GateDrum` also
///       resolve four pads that are in no legacy table, because legacy spells them inline in the
///       very same arm (`sound_editor.cpp:1320-1340`): `(4, 7)` is `sequenceDirectionMenu` and
///       `(15, 1..3)` the MIDI/CV randomizer trio. They are reachable from here and **not** from
///       `shortcut_layer` or `shortcut_pos`, which is exactly legacy's asymmetry: the blink side
///       (`updatePadLightsFor`, `:833-853`) scans column 11 and nothing else, so pressing those
///       four pads navigates but never lights them. Two further arms of the same `if` stay out
///       because they are separate mechanisms rather than cells -- the DX7 pad grid (`:1170-1177`)
///       and the patching-source column, `x` in {14, 15} (`:1180-1268`).
///
/// @note **Press-only gate, the mirror of the above.** `Root::KitGlobalFx` resolves *fewer* pads
///       than its table holds: legacy's arm is gated `(x <= kDisplayWidth - 2) || (x == 15 && y == 1)`
///       (`sound_editor.cpp:1191-1195`), so `paramShortcutsForKitGlobalFX`'s own `(15, 2)` and
///       `(15, 3)` cells were never pressable. They are still in the layer, and still blink, because
///       `setupShortcutsBlinkFromTable` (`:825`) scans the whole table with no such guard. The other
///       four roots' guards match their tables exactly and add nothing here.
///
/// @param x            Absolute pad column.
/// @param y            Absolute pad row.
/// @return The node, or `nullptr`.
const Node* shortcut_node(MenuContext::Root root, bool second_layer, uint8_t x, uint8_t y);

/// @brief Where @p node's own shortcut pad is, for the focused-param blink
///        (`SoundEditor::updatePadLightsFor` / `setupShortcutBlink`, `sound_editor.cpp:844-943`,
///        whose `currentParamShortcutX` / `currentParamShortcutY` this replaces).
///
/// The reverse of `shortcut_node` over the same tables, so `MenuScene` never has to know a table's
/// shape. The second layer is searched first and the first layer after it, which is the order
/// `updatePadLightsFor` scans in (`:867` before `:872`).
///
/// @note This searches the **transcribed layers only**, so it is deliberately *not* a total inverse
///       of `shortcut_node`, in both directions: the four resolve-only cells are reachable by a
///       press and report no pad here (legacy's blink side never scans them), while the kit
///       global-FX cells its press gate excludes report a pad here and cannot be pressed (legacy's
///       blink side scans the whole table). Both asymmetries are legacy's; see `shortcut_node`.
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
///        `sound_editor.cpp:1025-1037`, which flashes `x + 14` for every armed
///        `sourceShortcutBlinkFrequencies[x][y]`).
///
/// @note **Still empty after Task 9, and deliberately so.** Which source pads blink is not table
///       data: legacy fills `sourceShortcutBlinkFrequencies` from
///       `MenuItem::shouldBlinkPatchingSourceShortcut` (`sound_editor.cpp:797-819`), which every
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

/// @brief Which of legacy's two shortcut layouts every lookup above resolves.
///
/// The enumerator values are legacy's own (`SHORTCUTS_VERSION_1` 0, `SHORTCUTS_VERSION_3` 1,
/// `gui/ui/sound_editor.h:26-27`) so the settings item's `int32_t` crosses unchanged; the names are
/// spelled here rather than reached for so this layer names no `gui/` header.
enum class ShortcutsVersion : uint8_t {
	V1 = 0, ///< The pre-3.0 layout.
	V3 = 1, ///< The current one, and the default.
};

/// @brief The layout in force. `V3` until something sets otherwise.
ShortcutsVersion shortcuts_version();

/// @brief Select @p version -- the port of `SoundEditor::setShortcutsVersion`
///        (`gui/ui/sound_editor.cpp:112-181`), and the reason `nodes::shortcuts_version` is a real
///        item rather than a setting that "selects nothing".
///
/// @note **This selects a layer; legacy *mutates* one.** `setShortcutsVersion`'s `V1` arm assigns
///       ten cells of `paramShortcutsForSounds` / `paramShortcutsForAudioClips` in place
///       (`:119-135`) and its `V3` arm is a comment reading "Uses defaults" (`:139-141`) -- so on
///       device, selecting V1 and then V3 again leaves the V1 cells in place until the next reboot.
///       Here both layouts are `constexpr` tables and this is a pointer swap, so the round trip
///       restores V3. That difference is deliberate: reproducing a one-way write is not fidelity.
///
/// @note **Two of legacy's ten assignments are the layers' only real difference.** Four of them
///       (`paramShortcutsForSounds[2][6]`, `[3][6]`, `[2][7]`, `[3][7]`, `:128-132`) write the value
///       the V3 table already holds, so they change nothing. Four more are the audio-clip marker
///       editor (`:119-124`), where legacy has a *start* and an *end* item and the port has one
///       `audio_clip_waveform` node standing in for both -- so the V1 audio-clip layer differs from
///       V3 only in that `(0..1, 6)` also resolves to it. The two that genuinely move are
///       `paramShortcutsForSounds[0][6]` / `[1][6]`, interpolation in V3 and the sample end point in
///       V1. `modSourceShortcuts` (`:134-135`) is not in this layer at all -- it is the patching
///       source column, which `source_shortcut_pads` above explains is not table data here.
void set_shortcuts_version(ShortcutsVersion version);

} // namespace deluge::scenes::menu
