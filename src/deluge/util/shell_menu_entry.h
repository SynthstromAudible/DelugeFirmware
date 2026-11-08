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
/// @brief `shellMenuEnabled`, and the one call that opens the ported menu in `shellHostUI`'s place.
///
/// The *first tenant* of the shell guest: every place legacy enters `soundEditor` becomes `openUI(&shellHostUI)` plus
/// a `Menu{node}` push, **while `RuntimeFeatureSettingType::LegacyMenu` is Off**. There are fifteen such places;
/// fourteen of them route here and one, site 10, stays on the legacy path for the reason its row gives. The table is in
/// `gui/ui/shell_menu_entry.cpp`.
///
/// @par Why this is its own translation unit
///
/// The same reason `gui/ui/shell_guest_services.h` gives, one layer out: the gated call sites are
/// ordinary legacy views, and making each of them name `deluge::scenes::menu::context_for`,
/// `MenuScene::roots()` and `SceneId` would put `src/scenes` includes into eight `gui/views/` and
/// `model/clip/` translation units for four lines of work apiece. `ShellMenuNode` below exists for
/// exactly that reason: it is how a caller names a *specific ported node* without naming the
/// `deluge::scenes::menu::nodes` namespace.
///
/// @par Why the header is in `util/` and the implementation in `gui/ui/`
///
/// **One of the gated call sites is `model/clip/instrument_clip_minder.cpp`**, and that file is
/// inside `check_layer_rules.py`'s `NON_UI` glob -- so a `gui/ui/` header there would raise the
/// `model-includes-ui` ratchet, which may only shrink. Keeping the count level by *displacing*
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
/// own terms -- it includes nothing but `<cstdint>` and forward-declares `class Clip`. Every
/// `src/scenes` and `gui/` dependency stays in `gui/ui/shell_menu_entry.cpp`, where the rule would
/// judge it if it moved.
///
/// @note `check_citations.py`'s `SCAN_ROOTS` includes `src/deluge/util`, so this file's citations
///       are gate-read.
///
/// @par What works with ShellMenu on, and what does not
///
/// This is what a user gets by default, and none of it is discoverable from a green test suite:
///
///  1. **Every `MenuAction` the ported items file has a body.** `gui::ui::dispatchShellActions`
///     drains the outbox once per guest tick, so `perform_editor`, `start_stem_export`,
///     `set_clip_length`, `song_midi_learn`, `configure_song_macros` and the two settings verbs of
///     item 4 do with ShellMenu on what their legacy items do. What each arm still owes is recorded
///     per arm at `gui/ui/shell_action_dispatch.h`.
///  2. **A file chosen in the ported browser is claimed.** `deluge::gui::ui::dispatchShellChoices`
///     (`gui/ui/shell_choice_dispatch.cpp`) drains `take_choice()` from `ShellHostUI::timerCallback`
///     after every `Shell::tick` and performs the sample claim (`SampleClaim`) against the parked
///     path, so the sample really loads with ShellMenu on; and `shellSampleClaimDone`'s success arm
///     then calls `MenuScene::reroot_to_scene_opener_page()` (`scenes/menu/menu_scene.h`), which is
///     legacy's `maybeGetParentMenu` re-rooting in `MenuState`'s own terms rather than the heading
///     pair's -- so a claimed file lands on the source page with the file selector focused, not one
///     BACK press further in. **One qualification**: the recorder's identical pair is unpaid
///     (`nodes::AudioRecorderItem::on_focus`). Shift+select on a file does not diverge --
///     `BrowserScene` decides it with the press, as legacy's `editingKit() && shift` branch does.
///  3. **`perform_editor`'s sync into `menu::performance_editing_mode()` is complete.** The four
///     sites where `PerformanceView` moves the two mirrored bools call
///     `syncEditingModeMirror(*this)`, and a fifth writer is covered too: legacy's own
///     `EditingMode::selectButtonPress` (`gui/menu_item/performance_session_view/editing_mode.h:84`),
///     which runs with ShellMenu off and would otherwise leave the mirror stale for the first
///     ported menu opened after a flip-on. With the writer set closed, the ported editing item and
///     the view cannot disagree with ShellMenu on or off.
///  4. **The ported settings items that write flash notify the legacy owner.** The shortcut-layout
///     setter reaches `SoundEditor::setShortcutsVersion` (and so flash byte 60):
///     `nodes::set_shortcuts_version` files `MenuAction::SetShortcutsVersion` beside its own
///     mirror write, and the dispatcher performs the legacy call. The two screensaver setters make
///     their `FlashStorage` assignment in the ported hook, matching the first statement of each
///     legacy `writeCurrentValue` (`gui/menu_item/defaults/screensaver.h:39`, `:61`), and file
///     `MenuAction::ScreensaverSettingsChanged` for the second, `Screensaver::settingsChanged()`;
///     one enumerator serves both because legacy's notify takes no argument.
///
///     **Not covered:** the play-cursor flash is unwired in both directions and reaches no menu
///     item, so it is not a thing a user can do wrong with ShellMenu on. It is noted in
///     `src/scenes/README.md`'s boot-wiring section.
///  5. **No entry point opens the legacy `SoundEditor`.** With ShellMenu on `SoundEditor::opened` refuses, so the
///     legacy editor, and the legacy browser, recorder, marker editor and slicer only it opens, belong to the
///     ShellMenu-off leg; site 10, the one row `shell_menu_entry.cpp`'s table does not gate, is that browser's own.

#include "definitions_cxx.hpp" // ActionResult

#include <cstdint>

class Clip;

namespace deluge::gui::ui {

/// @brief Which node a caller wants open, for the callers that want something other than the root
///        `context_for` resolves.
///
/// An enum rather than a `const deluge::scenes::menu::Node*` so that a legacy view can name a
/// ported node without including `src/scenes` -- see the file header. Every value other than
/// `ContextRoot` is resolved to a stable node id (`scenes/menu/node_id.h`) inside
/// `openShellMenu`, which is what `SceneId::arg` carries.
enum class ShellMenuNode : uint8_t {
	/// @brief Whatever root `MenuContext::root` resolved -- `SceneId::arg == 0`.
	ContextRoot,
	/// @brief `deluge::scenes::menu::nodes::file0_selector`, the port of `file0SelectorMenu`
	///        (`gui/menu_item/file_selector.h:26`; the correspondence is stated at
	///        `nodes::file0_selector`).
	Osc0FileSelector,
	/// @brief `deluge::scenes::menu::nodes::sample0_recorder`, the port of `sample0RecorderMenu`: the item whose hook
	///        pushes the recorder on source 0.
	Osc0Recorder,
	/// @brief `deluge::scenes::menu::nodes::dx_menu`, the port of `dxMenu` ("DX7 source", `gui/ui/menus.cpp`).
	Dx,
	/// @brief `deluge::scenes::menu::nodes::dx_param`, the port of `dxParam` (the DX7 parameter page), on the param
	///        `MenuScene`'s `DxState` holds.
	DxParam,
};

/// @brief Which of the two `currentUIMode`-driven roots a caller wants, in place of whatever `context_for` resolved --
/// the note editor and note-row
///        editor have no `ShellMenuNode` of their own because they are roots
///        (`MenuContext::Root::NoteEditor`/`NoteRowEditor`), not leaves the way
///        `Osc0FileSelector` is.
///
/// `openShellMenu` sets `ctx.root` from this after resolving the rest of the context, exactly as
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

/// @brief Is the ported menu the one the gated entry points should open?
///
/// True while `RuntimeFeatureSettingType::LegacyMenu` is Off. "ShellMenu on" and "ShellMenu off" in this tree's
/// comments and parity docs name this answer. **With it false every gated site runs the exact legacy statements it
/// ran before**, which is what the `else` branch at each site is shaped to make reviewable.
bool shellMenuEnabled();

/// @brief Resolve what the menu edits, point `MenuScene` at it, and open the guest.
///
/// The port of the half of `SoundEditor::setup` that decides *what* and *which root*,
/// plus the `openUI` the fifteen sites do afterwards. The
/// arguments are `menu::context_for`'s own, in its order, because they are the four UI facts
/// `src/scenes` may not read for itself (`scenes/menu/context.h`'s "No UI reads" note) -- each
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
///                               sets the latch (`shellKitGlobalFx`) for the shortcut presses that follow; false
///                               leaves it as it is. The open reads this argument, never the latch.
/// @param node                   Which node to land on; `ContextRoot` for the resolved root.
/// @param noteMode               Override the resolved root with the note editor or the note-row editor, which
///                               `src/scenes` cannot infer from `currentUIMode`. `NoteEditor`/`NoteRowEditor` are
///                               `enterNoteEditor`/`enterNoteRowEditor`'s explicit item: no undo-log wipe, and the
///                               kit `affectEntire` clear. `NoteEditorFromUiMode` is `setup(clip)`'s own inference:
///                               the wipe, no clear. `None` for every other caller.
/// @return Whether the guest is now open; with it already open the open menu re-roots in place, as legacy's setup()
///         re-sets the open editor. False means nothing was opened and nothing was left
///         half-open: the caller should behave as it does when `soundEditor.setup()` returns false.
bool openShellMenu(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot,
                   bool sessionViewRoot, bool kitGlobalFx, ShellMenuNode node = ShellMenuNode::ContextRoot,
                   NoteMode noteMode = NoteMode::None);

/// @brief Open the ported menu on the node a shortcut pad names, from a clip view.
///
/// The port of `SoundEditor::potentialShortcutPadAction`'s `setup(currentClip, item, x & 1)` and
/// the `openUI` after it: the context is resolved as `openShellMenu` resolves it, with
/// `sourceIndex = x & 1`; the node is `menu::shortcut_node(ctx.root, false, x, y)`; and the menu
/// lands where `setup()` lands a named item (`MenuScene::arm_shortcut_landing`). The undo log is
/// kept, as `setup()` keeps it when given an item.
///
/// @param clip                   The current clip, or null off a clip minder (the song is then edited).
/// @param x                      Absolute pad column.
/// @param y                      Absolute pad row.
/// @param performanceView        As `openShellMenu`'s.
/// @param sessionOrArrangerRoot  As `openShellMenu`'s.
/// @param sessionViewRoot        As `openShellMenu`'s.
/// @param kitGlobalFx            As `openShellMenu`'s: true sets the latch, and the open reads it.
/// @return Whether the menu is now open on the pad's node; with the guest already open the open menu re-roots in
///         place, as legacy's setup() re-sets the open editor. False, with nothing opened, when the context does not
///         resolve, the pad names no numbered node, or the node does not apply and has no DX7 fallback; the last says
///         "PARAMETER NOT APPLICABLE", as `setup()` does.
bool openShellMenuAtShortcut(Clip* clip, int32_t x, int32_t y, bool performanceView, bool sessionOrArrangerRoot,
                             bool sessionViewRoot, bool kitGlobalFx);

/// @brief `SoundEditor::setupKitGlobalFXMenu` for the ported menu: whether an open, and a shortcut press re-rooting
///        the open guest, edit a kit's own effects rather than its selected row.
///
/// Sticky, as legacy's member is. `shellShortcutPadAction` writes it with the guest closed, on every press it is
/// asked about; the clip views' SELECT sets it on a kit with affect-entire on (`openShellMenu`'s `kitGlobalFx`); only
/// the guest's close and the drum creator clear it (`clearShellKitGlobalFx`). It is read live, never copied: by the
/// shortcut presses with the guest closed (`shellShortcutPadAction`), by the open menu's own Shift + pad, SHIFT overlay
/// and horizontal-menu chain on a kit's row (`wireShellKitGlobalShortcuts`), and by the automation view
/// (`soundMenuKitGlobalFx`), as legacy's `get_basic_shortcut_action`, `setup()`, `renderMainShortcutsOnly` and
/// `getCurrentHorizontalMenusChain` read `setupKitGlobalFXMenu`. The other opens edit what the clip's affect-entire
/// says now (DIFFERENCES D130).
bool shellKitGlobalFx();

/// @brief Clear the latch `shellKitGlobalFx` reads, as legacy's `exitCompletely` and `enterDrumCreator` clear
///        `setupKitGlobalFXMenu`.
void clearShellKitGlobalFx();

/// @brief `SoundEditor::enterOrUpdateSoundEditor`'s write, for the menu re-entered from a UI over it (the browser's
///        claim, the marker editor's shortcut jump): a kit clip with affect-entire on, with SHIFT held and no audition,
///        sets the latch `shellKitGlobalFx` reads.
void latchShellKitGlobalFxOnReentry();

/// @brief Point the ported menu at the latch, its one source for every shortcut-layer decision on a kit's row (the
///        Shift + pad item, the SHIFT overlay, the horizontal-menu chain), and at the host's halves: the adopt when a
///        press moves the menu onto the kit's own effects, and the re-entry write after the marker editor's jump.
void wireShellKitGlobalShortcuts();

/// @brief Whether a shortcut pad a legacy view is asked about is the ported menu's to answer: ShellMenu on, or the
///        guest open, which re-roots itself whichever menu the setting now selects.
bool shellOwnsShortcutPads();

/// @brief `SoundEditor::potentialShortcutPadAction` for the ported menu, whole: what the clip views, the keyboard
///        screen and the performance view ask of a main-grid press.
///
/// The gate first (`shortcutPadGate`: SHIFT, or an audition held over the instrument clip view, inside the two
/// shortcut modes; the automation view's interpolation pads stay the view's). Then the item: a kit with nothing to edit
/// has none; a coming-soon cell says "Feature not (yet?) implemented"; the sequence-direction pad with one row
/// auditioning opens the note-row editor; any other pad goes to `openShellMenuAtShortcut`, which opens, re-roots or
/// says its refusal. Off a clip minder the context names no clip, so the song view's table and the song are read, as
/// legacy's `setup()` reads them. Nothing here opens the legacy editor.
/// @param x,y The pad.
/// @param on  Whether it is a press.
/// @return `NOT_DEALT_WITH` for a release, a sidebar pad or a press the gate passes on to the view; `DEALT_WITH`
///         otherwise, whether or not anything opened.
ActionResult shellShortcutPadAction(int32_t x, int32_t y, bool on);

/// @brief Shift + the keyboard's DX column pad for operator @p op: `DxParam::openForOpOrGlobal` under the ported menu.
///
/// With the guest closed it opens on the DX7 parameter page at the operator's param (`dx::column_param`), its pad
/// blinking, over the current clip, keeping the undo log as `setup()` given an item does. With the guest open the
/// press goes to `MenuScene::dx_column_press`, which toggles on the page or moves the menu there.
/// @param op The operator, 0-5, as the column numbers its rows; 6 and 7 for the two pads below them.
/// @return Whether the ported menu took the press: false where `openShellMenu` refused.
bool openShellDxColumn(int32_t op);

/// @brief `InstrumentClipView::enterNoteRowEditor`'s open with ShellMenu on: with the guest closed `openShellMenu` at
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
void landShellMenuOnFileSelectorIfRecorderRefuses();

} // namespace deluge::gui::ui
