#pragma once

/// @brief The grid shortcut lookup: which `Node` the pad at (x, y) opens, and where a `Node`'s own
///        shortcut pad sits so `plan_blinks` (`menu/shortcut_blink.h`) can blink it.
///
/// The seven legacy tables -- `paramShortcutsForSounds`, `…SecondLayer`, `…ForAudioClips`,
/// `…ForSongView`, `…ForKitGlobalFX` (`origin/main:src/deluge/gui/ui/menus.cpp`), `midiOrCVParamShortcuts` and
/// `gateDrumParamShortcuts` -- transcribed cell by cell as `Node` pointers, keyed by
/// `MenuContext::Root`. `tests/integration/ui/scenes/shortcuts_spec.cpp` cross-checks every cell of every table
/// against the legacy array it came from, in both directions.
///
/// The board profile does not carry these: `BoardProfile::shortcuts` (two
/// `std::span<const uint16_t>` layers) could not hold five 16x8 tables of *node pointers* plus two
/// one-dimensional ones selected by context, so the tables live here.

#include "project/device_settings.h"
#include "ui/scenes/menu/context.h"

#include <cstdint>
#include <span>

namespace deluge::ui::scenes::menu {

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
	/// True when the pad is the node's in the second layer, which legacy blinks yellow
	/// (`SoundEditor::updatePadLightsFor`).
	bool second_layer = false;
};

/// @brief One root's shortcut layer: `kShortcutCols * kShortcutRows` cells, addressed
///        `y * kShortcutCols + x`, null where the legacy cell was null or `comingSoonMenu`.
///
/// A legacy one-dimensional table (`midiOrCVParamShortcuts`, `gateDrumParamShortcuts`) is stored as
/// a full grid with only **column 11** filled -- the column `potentialShortcutPadAction`
/// and `updatePadLightsFor` index it at.
///
/// @param root         Which menu root is open.
/// @param version      The shortcut layout in force (`shortcuts_version`).
/// @param second_layer Legacy's second shortcut layer -- see `shortcut_node`.
/// @note This is the **transcription only**, and it is what `shortcut_pos` searches -- so it is the
///       blink's view, not the press's. Three roots resolve four further pads that are in no layer,
///       and `Root::KitGlobalFx` cannot press two cells that are; both asymmetries are legacy's and
///       are documented on `shortcut_node`.
///
/// @return The cells. Every root resolves to a layer of exactly `kShortcutCols * kShortcutRows`
///         cells; `Root::Settings` resolves to an all-null one, because legacy never blinks a
///         shortcut while the settings menu is open (`updatePadLightsFor`'s `!inSettingsMenu()`
///         guard).
std::span<const Node* const> shortcut_layer(MenuContext::Root root, project::ShortcutsVersion version,
                                            bool second_layer);

/// @brief The node the shortcut pad at (@p x, @p y) opens, or `nullptr` for a pad with no
///        shortcut.
///
/// Ports the table lookup inside `SoundEditor::potentialShortcutPadAction`
/// (in `origin/main:src/deluge/gui/ui/sound_editor.cpp`): which of the seven tables is consulted follows from
/// @p root, exactly as legacy's chain of `rootUIIsClipMinderScreen()` / `setupKitGlobalFXMenu` /
/// `ClipType::AUDIO` / output-type tests does.
///
/// @param root         Which menu root is open, i.e. which table applies.
/// @param version      The shortcut layout in force (`shortcuts_version`).
/// @param second_layer Take the cell from the second layer where that layer has one. This is
///                     legacy's `secondLayerShortcutsToggled` --
///                     pressing the focused item's *own* shortcut pad a second time -- **not**
///                     "shift is held". Shift is only the gate that lets a pad be a shortcut at
///                     all, which is the caller's business. As in legacy, a null second
///                     layer cell falls back to the first layer's, so this never *removes* a
///                     shortcut. Only `Root::Sound` and `Root::Drum` have a second layer
///                     (`paramShortcutsForSoundsSecondLayer`); for every other root both layers
///                     are the same cells.
/// @note **Resolve-only cells.** `Root::MidiOrCv`, `Root::MidiDrum` and `Root::GateDrum` also
///       resolve four pads that are in no legacy table, because legacy spells them inline in the
///       very same arm of `potentialShortcutPadAction`: `(4, 7)` is `sequenceDirectionMenu` and
///       `(15, 1..3)` the MIDI/CV randomizer trio. They are reachable from here and **not** from
///       `shortcut_layer` or `shortcut_pos`, which is exactly legacy's asymmetry: the blink side
///       (`updatePadLightsFor`) scans column 11 and nothing else, so pressing those
///       four pads navigates but never lights them. Two further arms of the same `if` stay out
///       because they are separate mechanisms rather than cells -- the DX7 pad grid
///       and the patching-source column, `x` in {14, 15}.
///
/// @note **Press-only gate, the mirror of the above.** `Root::KitGlobalFx` resolves *fewer* pads
///       than its table holds: legacy's arm is gated `(x <= kDisplayWidth - 2) || (x == 15 && y == 1)`
///       (`SoundEditor::get_basic_shortcut_action` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`), so
///       `paramShortcutsForKitGlobalFX`'s own `(15, 2)` and `(15, 3)` cells were never pressable. They
///       are still in the layer, and still blink, because `SoundEditor::setupShortcutsBlinkFromTable`
///       (the same file) scans the whole table with no such guard. The other four roots' guards match
///       their tables exactly and add nothing here.
///
/// @param x            Absolute pad column.
/// @param y            Absolute pad row.
/// @return The node, or `nullptr`.
const Node* shortcut_node(MenuContext::Root root, project::ShortcutsVersion version, bool second_layer, uint8_t x,
                          uint8_t y);

/// @brief Whether legacy's first-layer table holds `comingSoonMenu` at (@p x, @p y) for @p root: the cells
///        `shortcut_node` answers `nullptr` for and `SoundEditor::potentialShortcutPadAction` answers with "Feature not
///        (yet?) implemented". Sound and drum roots (`paramShortcutsForSounds`) and the audio clip
///        (`paramShortcutsForAudioClips`); neither shortcut layout moves them.
bool shortcut_coming_soon(MenuContext::Root root, uint8_t x, uint8_t y);

/// @brief Where @p node's own shortcut pad is, for the focused-param blink
///        (`SoundEditor::updatePadLightsFor` / `setupShortcutBlink`, `origin/main:src/deluge/gui/ui/sound_editor.cpp`,
///        whose `currentParamShortcutX` / `currentParamShortcutY` this replaces).
///
/// The reverse of `shortcut_node` over the same tables, so `MenuScene` never has to know a table's
/// shape. The second layer is searched first and the first layer after it, which is the order
/// `updatePadLightsFor` scans in.
///
/// @note This searches the **transcribed layers only**, so it is deliberately *not* a total inverse
///       of `shortcut_node`, in both directions: the four resolve-only cells are reachable by a
///       press and report no pad here (legacy's blink side never scans them), while the kit
///       global-FX cells its press gate excludes report a pad here and cannot be pressed (legacy's
///       blink side scans the whole table). Both asymmetries are legacy's; see `shortcut_node`.
///
/// @note Legacy's two context-dependent suppressions are **not** ported: the reverb-sidechain one
///       (`SoundEditor::updatePadLightsFor`, "x == 10 && y < 6 && editingReverbSidechain()") and the
///       odd-column/source-0 one (also in `SoundEditor::updatePadLightsFor`). Both read `SoundEditor` state this
///       signature does not carry, and both only *withhold* a blink; a blink on the right pad is the failure mode
///       either way.
///
/// @param root    Which menu root is open.
/// @param version The shortcut layout in force (`shortcuts_version`).
/// @param node    The node to locate.
/// @return Its pad, or a `ShortcutPos` with `valid == false`.
ShortcutPos shortcut_pos(MenuContext::Root root, project::ShortcutsVersion version, const Node& node);

/// @brief The layout in force, which the lookups above take: the device's `pads/shortcutsVersion` row, which the
///        settings item `nodes::shortcuts_version` writes.
///
/// @note **The row selects a layer; legacy *mutates* one.** `setShortcutsVersion`'s `V1` arm assigns
///       ten cells of `paramShortcutsForSounds` / `paramShortcutsForAudioClips` in place
///       and its `V3` arm is a comment reading "Uses defaults" -- so on
///       device, selecting V1 and then V3 again leaves the V1 cells in place until the next reboot.
///       Here both layouts are `constexpr` tables and the row picks one, so the round trip
///       restores V3. That difference is deliberate: reproducing a one-way write is not fidelity.
///
/// @note **Two of legacy's ten assignments are the layers' only real difference.** Four of them
///       (`paramShortcutsForSounds[2][6]`, `[3][6]`, `[2][7]`, `[3][7]`) write the value the V3 table already holds, so
///       they change nothing. Four more are the audio-clip marker editor, where legacy has a *start* and an *end* item:
///       `(0..1, 7)` is `audio_clip_waveform_start` and `(0..1, 6)` is `audio_clip_waveform`, so the V1 audio-clip
///       layer differs from V3 in both pairs. The two that genuinely move are `paramShortcutsForSounds[0][6]` /
///       `[1][6]`, interpolation in V3 and the sample end point in V1. `modSourceShortcuts`
///       (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is not in this layer at all -- it is the patching source
///       column, which is not table data here (`menu/shortcut_blink.h`).
/// @param ctx The menu's context, whose device port holds the row.
/// @return The row's layout; `V3`, the row's default, where the context binds no device port.
[[nodiscard]] project::ShortcutsVersion shortcuts_version(const MenuContext& ctx);

} // namespace deluge::ui::scenes::menu
