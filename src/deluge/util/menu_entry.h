/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief The one call that opens the menu in `shellHostUI`'s place.
///
/// The *first tenant* of the shell guest: every place legacy enters `soundEditor` becomes `openUI(&shellHostUI)` plus
/// a `Menu{node}` push. There are fourteen such places, and each routes
/// here; the table is in `gui/ui/menu_entry.cpp`.
///
/// @par Why this is its own translation unit
///
/// The same reason `gui/ui/shell_guest_services.h` gives, one layer out: the gated call sites are
/// ordinary legacy views, and making each of them name `deluge::ui::scenes::menu::context_for`,
/// `MenuScene::roots()` and `SceneId` would put `src/ui/scenes` includes into eight `gui/views/` and
/// `model/clip/` translation units for four lines of work apiece. `MenuNode` below exists for
/// exactly that reason: it is how a caller names a *specific node* without naming the
/// `deluge::ui::scenes::menu::nodes` namespace.
///
/// @par Why the header is in `util/` and the implementation in `gui/ui/`
///
/// **One of the gated call sites is `model/clip/instrument_clip_minder.cpp`**, and that file is
/// inside the `model-includes-ui` rule's glob (`scripts/layers.toml`) -- so a `gui/ui/` header there would
/// raise that ratchet, which may only shrink. Keeping the count level by *displacing*
/// another `gui/` include trades a dead coupling for a live one and leaves the number saying
/// nothing changed.
///
/// So the declaration lives outside `gui/` and the implementation stays inside it, which is
/// `util/notify.h`'s shape exactly -- a light header any layer may include, with the UI half in
/// `gui/ui/notify_drain.cpp`.
///
/// **The load-bearing fact:** `src/deluge/util` is *itself* inside `check_layer_rules.py`'s
/// `NON_UI` glob (`src/deluge/{model,playback,processing,storage,io,modulation,memory,sync,util,
/// dsp}/**`). So this header is judged by the very rule the split is about, and it passes on its
/// own terms -- it includes only `definitions_cxx.hpp`, `ui/gfx/grid/rgb.h` and `<cstdint>`, and forward-declares
/// `class Clip`. Every `src/ui/scenes` and `gui/` dependency stays in `gui/ui/menu_entry.cpp`, where the rule would
/// judge it if it moved.
///
/// @par What works, and what does not
///
/// This is what a user gets by default, and none of it is discoverable from a green test suite:
///
///  1. **Every `MenuAction` the menu's items file has a body.** `gui::ui::dispatchShellActions` drains the outbox once
///     per guest tick, so `perform_editor`, `start_stem_export`, `set_clip_length`, `song_midi_learn` and
///     `configure_song_macros` do what their legacy items do. What each arm still owes is recorded per arm at
///     `gui/ui/shell_action_dispatch.h`.
///  2. **A file chosen in the browser scene is claimed.** The browser's host
///     (`gui/ui/browser_host.cpp`, installed by `wireShellGuestServices`) performs the sample
///     claim (`SampleClaim`) on the press, so the sample really loads; and `shellSampleClaimDone`'s success arm
///     then calls `MenuScene::reroot_to_scene_opener_page()` (`ui/scenes/menu/menu_scene.h`), which is
///     legacy's `maybeGetParentMenu` re-rooting in `MenuState`'s own terms rather than the heading
///     pair's -- so a claimed file lands on the source page with the file selector focused, not one
///     BACK press further in. **One qualification**: the recorder's identical pair is unpaid
///     (`nodes::AudioRecorderItem::on_focus`). Shift+select on a file does not diverge --
///     `BrowserScene` decides it with the press, as legacy's `editingKit() && shift` branch does.
///  3. **`perform_editor` shows the performance view's own mode.** It reads
///     `UiFacts::performance_editing_mode()`, which the host answers from `PerformanceView`'s two bools, so the
///     item and the view cannot disagree.
///  4. **The settings items that write flash notify the owner.** Each writes its DEVICE row through the device port,
///     whose store sets the flash setting and does the rest of main's `writeCurrentValue`: for the two screensaver
///     items (`origin/main:src/deluge/gui/menu_item/defaults/screensaver.h`), `Screensaver::settingsChanged()`.

#include "definitions_cxx.hpp" // ActionResult
#include "ui/gfx/grid/rgb.h"

#include <cstdint>

class Clip;

namespace deluge::gui::ui {

/// @brief Which node a caller wants open, for the callers that want something other than the root
///        `context_for` resolves.
///
/// An enum rather than a `const deluge::ui::scenes::menu::Node*` so that a view in `src/deluge/gui/views` can
/// name a menu node without including `src/ui/scenes` -- see the file header. Every value other than
/// `ContextRoot` is resolved to a stable node id (`ui/scenes/menu/node_id.h`) inside
/// `openMenu`, which is what `SceneId::arg` carries.
enum class MenuNode : uint8_t {
	/// @brief Whatever root `MenuContext::root` resolved -- `SceneId::arg == 0`.
	ContextRoot,
	/// @brief `deluge::ui::scenes::menu::nodes::file0_selector`, the port of `file0SelectorMenu`
	///        (`origin/main:src/deluge/gui/menu_item/file_selector.h`; the correspondence is stated at
	///        `nodes::file0_selector`).
	Osc0FileSelector,
	/// @brief `deluge::ui::scenes::menu::nodes::sample0_recorder`, the port of `sample0RecorderMenu`: the item whose
	/// hook
	///        pushes the recorder on source 0.
	Osc0Recorder,
	/// @brief `deluge::ui::scenes::menu::nodes::dx_menu`, the port of `dxMenu` ("DX7 source",
	/// `origin/main:src/deluge/gui/ui/menus.cpp`).
	Dx,
	/// @brief `deluge::ui::scenes::menu::nodes::dx_param`, the port of `dxParam` (the DX7 parameter page), on the param
	///        `MenuScene`'s `DxState` holds.
	DxParam,
};

/// @brief Which of the two `currentUIMode`-driven roots a caller wants, in place of whatever `context_for` resolved --
/// the note editor and note-row
///        editor have no `MenuNode` of their own because they are roots
///        (`MenuContext::Root::NoteEditor`/`NoteRowEditor`), not leaves the way
///        `Osc0FileSelector` is.
///
/// `openMenu` sets `ctx.root` from this after resolving the rest of the context, exactly as
/// `InstrumentClipView::enterNoteEditor`/`enterNoteRowEditor` pass an explicit item to
/// `SoundEditor::setup` instead of letting it infer one from `currentUIMode`.
enum class NoteMode : uint8_t {
	/// @brief Use whatever root `context_for` resolved -- every caller except the note family.
	None,
	/// @brief `MenuContext::Root::NoteEditor`, named as `enterNoteEditor` names `noteEditorRootMenu`: the undo log is
	///        kept and a kit's `affectEntire` is cleared.
	NoteEditor,
	/// @brief `MenuContext::Root::NoteRowEditor`, named as `enterNoteRowEditor` names its root: the same two rules.
	NoteRowEditor,
	/// @brief `MenuContext::Root::NoteEditor` as `setup(clip)` infers it under `UI_MODE_NOTES_PRESSED` with no item
	///        named (the automation view's SELECT): the undo log is wiped as for any inferred root, and a kit's
	///        `affectEntire` is left as it is.
	NoteEditorFromUiMode,
};

/// @brief Resolve what the menu edits, point `MenuScene` at it, and open the guest.
///
/// The port of the half of `SoundEditor::setup` that decides *what* and *which root*,
/// plus the `openUI` the fifteen sites do afterwards. The
/// arguments are `menu::context_for`'s own, in its order, because they are the four UI facts
/// `src/ui/scenes` may not read for itself (`ui/scenes/menu/context.h`'s "No UI reads" note) -- each
/// caller already knows all four statically from the arm it is in.
///
/// @param clip                   The clip being edited, or null for a song-level/settings menu --
///                               `setup()`'s own first parameter.
/// @param sourceIndex            `setup()`'s `sourceIndex`.
/// @param performanceView        `setup()`'s `currentUI == &performanceView && !shift`.
/// @param sessionOrArrangerRoot  `setup()`'s `!rootUIIsClipMinderScreen()`.
/// @param sessionViewRoot        `getRootUI() == &sessionView`, the narrower fact two items read.
/// @param kitGlobalFx            True to edit a kit's own effects: the clip views' SELECT on a kit with
///                               affect-entire on, which also sets `setupKitGlobalFXMenu` before its `setup()`. True
///                               sets the latch (`menuKitGlobalFx`); false leaves it as it is. The open edits what
///                               the latch then says, under the root the clip's affect-entire picks.
/// @param node                   Which node to land on; `ContextRoot` for the resolved root.
/// @param noteMode               Override the resolved root with the note editor or the note-row editor, which
///                               `src/ui/scenes` cannot infer from `currentUIMode`. `NoteEditor`/`NoteRowEditor` are
///                               `enterNoteEditor`/`enterNoteRowEditor`'s explicit item: no undo-log wipe, and the
///                               kit `affectEntire` clear. `NoteEditorFromUiMode` is `setup(clip)`'s own inference:
///                               the wipe, no clear. `None` for every other caller.
/// @return Whether the guest is now open; with it already open the open menu re-roots in place, as legacy's setup()
///         re-sets the open editor. False means nothing was opened and nothing was left
///         half-open: the caller should behave as it does when `soundEditor.setup()` returns false.
bool openMenu(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot, bool sessionViewRoot,
              bool kitGlobalFx, MenuNode node = MenuNode::ContextRoot, NoteMode noteMode = NoteMode::None);

/// @brief Open the menu on the node a shortcut pad names, from a clip view.
///
/// The port of `SoundEditor::potentialShortcutPadAction`'s `setup(currentClip, item, x & 1)` and
/// the `openUI` after it: the context is resolved as `openMenu` resolves it, with
/// `sourceIndex = x & 1`; the node is `menu::shortcut_node(ctx.root, false, x, y)`; and the menu
/// lands where `setup()` lands a named item (`MenuScene::arm_shortcut_landing`). The undo log is
/// kept, as `setup()` keeps it when given an item.
///
/// @param clip                   The current clip, or null off a clip minder (the song is then edited).
/// @param x                      Absolute pad column.
/// @param y                      Absolute pad row.
/// @param performanceView        As `openMenu`'s.
/// @param sessionOrArrangerRoot  As `openMenu`'s.
/// @param sessionViewRoot        As `openMenu`'s.
/// @param kitGlobalFx            As `openMenu`'s: true sets the latch, and the open reads it.
/// @return Whether the menu is now open on the pad's node; with the guest already open the open menu re-roots in
///         place, as legacy's setup() re-sets the open editor. False, with nothing opened, when the context does not
///         resolve, the pad names no numbered node, or the node does not apply and has no DX7 fallback; the last says
///         "PARAMETER NOT APPLICABLE", as `setup()` does.
bool openMenuAtShortcut(Clip* clip, int32_t x, int32_t y, bool performanceView, bool sessionOrArrangerRoot,
                        bool sessionViewRoot, bool kitGlobalFx);

/// @brief The one kit-global-FX latch, main's `SoundEditor::setupKitGlobalFXMenu`: whether an open, and a
///        shortcut press re-rooting the open guest, edit a kit's own effects rather than its selected row.
///
/// Sticky. `menuShortcutPadAction` writes it with the guest closed, on every press it is
/// asked about; the clip views' SELECT sets it on a kit with affect-entire on (`openMenu`'s `kitGlobalFx`); only
/// the guest's close and the drum creator clear it (`clearMenuKitGlobalFx`). It is read live, never copied: by the
/// shortcut presses with the guest closed (`menuShortcutPadAction`), by every open (`openMenu`), by the open
/// menu's own Shift + pad, SHIFT overlay and horizontal-menu chain on a kit's row (`wireShellKitGlobalShortcuts`), and
/// by the automation view (`AutomationView::getAffectEntire`), as legacy's `get_basic_shortcut_action`, `setup()`,
/// `renderMainShortcutsOnly` and `getCurrentHorizontalMenusChain` read `setupKitGlobalFXMenu`.
bool menuKitGlobalFx();

/// @brief Clear the latch, as the editor's `exitCompletely` and `enterDrumCreator` do.
void clearMenuKitGlobalFx();

/// @brief Set the latch, for a spec that sets it as a shortcut press would, or puts it back after one.
/// @param latched The latch's new value.
void setMenuKitGlobalFx(bool latched);

/// @brief `SoundEditor::enterOrUpdateSoundEditor`'s write, for the menu re-entered from a UI over it (the browser's
///        claim, the marker editor's shortcut jump): a kit clip with affect-entire on, with SHIFT held and no audition,
///        sets the latch `menuKitGlobalFx` reads.
void latchMenuKitGlobalFxOnReentry();

/// @brief Point the menu at the latch, its one source for every shortcut-layer decision on a kit's row (the
///        Shift + pad item, the SHIFT overlay, the horizontal-menu chain), and at the host's halves: the adopt when a
///        press moves the menu onto the kit's own effects, and the re-entry write after the marker editor's jump.
void wireShellKitGlobalShortcuts();

/// @brief Whether a menu opened now would edit the current kit's own effects: the current clip is a kit's, with
///        affect-entire on.
bool editKitAffectEntire();

/// @brief The clip view's SHIFT shortcut overview: every main-grid pad its shortcut colour for the current clip, as the
///        menu's overlay paints it. A kit with neither affect-entire nor a selected drum names none, and only a
///        sound (a synth, a kit's own effects, a kit row with a sound drum) is asked whether each item is relevant.
///        The clip's affect-entire picks the table; whether an item is asked as on the kit's own effects is the
///        kit-global latch (`menuKitGlobalFx`), clear right after a menu close and set once a pad press latches.
void renderClipShortcutsOverview(RGB image[][kDisplayWidth + kSideBarWidth],
                                 uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

/// @brief The overview's colours alone, `[y][x]`, with no greyout or send: what the audio clip scene paints, through
///        `RootViews::clip_shortcut_overview`.
void clipShortcutsOverviewColours(RGB colours[kDisplayHeight][kDisplayWidth]);

/// @brief Shift + the keyboard's DX column pad for operator @p op: `DxParam::openForOpOrGlobal` under the menu.
///
/// With the guest closed it opens on the DX7 parameter page at the operator's param (`dx::column_param`), its pad
/// blinking, over the current clip, keeping the undo log as `setup()` given an item does. With the guest open the
/// press goes to `MenuScene::dx_column_press`, which toggles on the page or moves the menu there.
/// @param op The operator, 0-5, as the column numbers its rows; 6 and 7 for the two pads below them.
/// @return Whether the menu took the press: false where `openMenu` refused.
bool openShellDxColumn(int32_t op);

/// @brief `InstrumentClipView::enterNoteRowEditor`'s open: with the guest closed `openMenu` at
///        `NoteMode::NoteRowEditor`; with it open (audition + the sequence-direction shortcut inside the menu) the open
///        menu re-roots at the note-row editor on PLAY DIRECTION, as legacy's `setup()` re-roots the open editor.
/// @param onPlayDirection The sequence-direction shortcut asked: the closed leg opens on PLAY DIRECTION too, as
///        `noteRowEditorRootMenu.focusChild(&sequenceDirectionMenu)` before the open does; otherwise it opens on the
///        root's first row.
/// @return Whether the note-row editor is now the menu's root.
bool openShellNoteRowEditor(Clip* clip, bool onPlayDirection = false);

/// @brief `InstrumentClipView::enterDrumCreator`'s fallback for the ported take: should the recorder the open guest is
///        about to push refuse, the menu lands on `nodes::file0_selector` instead of going up, as legacy opens the
///        editor on `file0SelectorMenu` after `openUI(&audioRecorder)` refused; that item's hook then pushes the
///        browser. Call it right after the open that will push the recorder.
void landMenuOnFileSelectorIfRecorderRefuses();

} // namespace deluge::gui::ui
