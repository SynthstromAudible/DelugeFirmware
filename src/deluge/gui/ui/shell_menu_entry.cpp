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

#include "util/shell_menu_entry.h"
#include "gui/ui/menu_view_of.h"

#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_choice_dispatch.h" // adoptShellMenuContext
#include "gui/ui/shell_drum_creator.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/dx_tables.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include <cstddef>
#include <span>

/// @file
/// @brief The fifteen `openUI(&soundEditor)` sites, and what each one wanted open.
///
/// The fifteen are not uniform, and a reader deciding whether to add a sixteenth needs to know
/// which shape it is.
///
/// **Gated (thirteen).** Each one's `else` is byte-for-byte what it ran before.
///
// clang-format off
/// | # | site | what node it wants, and how it says so today | mapped to |
/// |---|---|---|---|
/// | 1 | `gui/views/view.cpp:478-479` `View::buttonAction` | `soundEditor.setup()` with SHIFT held, so `setup()`'s three view tests (`sound_editor.cpp:1816`, `:1819`) are all false and the root is `settingsRootMenu` (`:1824`) | `MenuContext::Root::Settings`, i.e. all four UI arguments false. The same reading, for the same arm, that `services::GlobalControls::handle_settings_menu` already made (`services::GlobalControls::handle_settings_menu`) |
/// | 2 | `gui/views/session_view.cpp:569-570` `SessionView::buttonAction` | `soundEditor.setup()` with no shift from the session view -> `soundEditorRootMenuSongView` (`SoundEditor::setup`) | `Root::Song`; `sessionViewRoot` true, which is the *only* thing two items in that root read (`MenuContext::is_session_view`) |
/// | 3 | `gui/views/arranger_view.cpp:347-348` `ArrangerView::buttonAction` | the same call from the arranger -> the same `soundEditorRootMenuSongView` | `Root::Song`, `sessionViewRoot` **false** |
/// | 4 | `gui/views/performance_view.cpp:898-901` `PerformanceView::buttonAction` | `soundEditor.setup(nullptr, &soundEditorRootMenuPerformanceView)` -- an explicitly named root | `Root::Performance`, which `context_for` picks from `performance_view`. Named by fact rather than by pointer, and they agree because this arm is `SELECT_ENC && !shift` inside `PerformanceView` |
/// | 5 | `gui/views/audio_clip_view.cpp:423-426` `AudioClipView::buttonAction` | `soundEditor.setup(getCurrentClip())` on an `AudioClip` -> `soundEditorRootMenuAudioClip` (`gui/ui/sound_editor.cpp:1812`) | `Root::AudioClip` (`context_for`) |
/// | 6 | `model/clip/instrument_clip_minder.cpp:466-473` `InstrumentClipMinder::buttonAction` | `setupKitGlobalFXMenu` from `KIT && affectEntire`, then `soundEditor.setup(getCurrentClip())` -- the root is then one of six, chosen by output type, affect-entire and the selected drum's type (`gui/ui/sound_editor.cpp:1780-1808`) | whichever of `MidiOrCv` / `KitGlobalFx` / `MidiDrum` / `GateDrum` / `Drum` / `Sound` `context_for` picks -- the same six tests in the same order |
/// | 7 | `gui/views/automation_view.cpp:1641-1649` `AutomationView::handleSelectEncoderButtonAction` | the same, except the clip is `nullptr` on the arranger (so: the song root) **and** the arm also runs under `UI_MODE_NOTES_PRESSED` | the same six, or `Root::Song` when `onArrangerView`. **Only under `UI_MODE_NONE`**: see the note below |
/// | 8 | `gui/views/instrument_clip_view.cpp` `InstrumentClipView::buttonAction` | `soundEditor.setup(getCurrentInstrumentClip(), &menu_item::file0SelectorMenu, 0)` -- a named *leaf*, not a root | `Root::Sound` plus `SceneId::arg = node_id(nodes::file0_selector)`, the ported `file0SelectorMenu`. The one site that exercises a non-zero `arg` |
/// | 12 | `gui/views/instrument_clip_view.cpp:3609-3617` `InstrumentClipView::enterNoteEditor` | `soundEditor.setup(clip, &noteEditorRootMenu)` -- an explicitly named root, the one `setup()` would itself pick under `UI_MODE_NOTES_PRESSED` (`sound_editor.cpp:1774-1776`) | `Root::NoteEditor`. `context_for` never assigns it, because `src/scenes` may not read `currentUIMode`; the gate passes `NoteMode::NoteEditor` instead, and `openShellMenu` overrides the resolved root with it (`:136-139`) |
/// | 13 | `gui/views/instrument_clip_view.cpp:3813-3821` `InstrumentClipView::enterNoteRowEditor` | `soundEditor.setup(clip, &noteRowEditorRootMenu)` -- the same, under `UI_MODE_AUDITIONING` (`gui/ui/sound_editor.cpp:1777-1779`) | `Root::NoteRowEditor` through `NoteMode::NoteRowEditor`. **Only when `getCurrentUI() != &soundEditor`**: `sound_editor.cpp:1403-1409` also calls this from inside a legacy sound editor (audition + the sequence-direction shortcut, in an editor site 9's legacy fallback opened), and there the guest would stack on `soundEditor` and diverge after the first BACK, so that case keeps opening the legacy editor |
/// | 9 | `gui/ui/sound_editor.cpp` `SoundEditor::potentialShortcutPadAction`, from a clip view (the current UI is neither `soundEditor` nor `sampleMarkerEditor`) | `setup(currentClip, item, x & 1)` on the item legacy's shortcut tables name for the pad | `openShellMenuAtShortcut`, the node `menu::shortcut_node` names, landed where `setup()` lands it (`MenuScene::arm_shortcut_landing`: the horizontal page listing it, focused on it). The DX7 fallback, `modulator0Volume` refused on a DX7 source, opens `nodes::dx_param` alone on the param it showed last (`nodes::dx_shortcut_fallback`; legacy tests the source the previous menu left, DIFFERENCES D90). A press from the sample-marker editor stays legacy, as does any pad the port resolves no node for |
/// | 11 | `model/clip/instrument_clip_minder.cpp:304-305` `InstrumentClipMinder::createNewInstrument` | `soundEditor.setup(clip, &dxMenu, 0)` on a new FM synth -- a named submenu (`gui/ui/menus.cpp:1735`) | `ShellMenuNode::Dx`, `SceneId::arg = node_id(nodes::dx_menu)`; a named node, so no undo-log wipe |
/// | 15 | `gui/ui/keyboard/column_controls/dx.cpp:101-108` `DXColumn::handlePad`, Shift + a DX column pad on the keyboard screen | `dxParam.openForOpOrGlobal(op)` (`gui/menu_item/dx/param.cpp:508-536`): `setup(getCurrentClip(), &dxParam, 0)` when the editor is not already on the page, then the operator's param -- or, on the page, the toggle and carry | `openShellDxColumn`: with the guest closed `ShellMenuNode::DxParam` on `dx::column_param`'s param, its pad blinking, no undo-log wipe; with it open `MenuScene::dx_column_press`, which toggles on the page or re-targets the menu to it with a `Replace` |
///
/// **Not gated (two).** Each keeps opening `soundEditor` whatever the toggle says, for the reason its row
/// gives.
///
/// | # | site | what it wants | why it is not mapped |
/// |---|---|---|---|
/// | 10 | `gui/ui/browser/sample_browser.cpp:969-981` `SampleBrowser::claimCurrentFile` | `parentMenuHeadingTo` focused on `menuItemHeadingTo` -- two arbitrary `MenuItem*`s the browser was handed on the way in | this is the *legacy* browser: the ported one parks its choice in `BrowserScene::take_choice()`. **That outbox is drained and the re-rooting is done** -- `deluge::gui::ui::dispatchShellChoices` (`gui/ui/shell_choice_dispatch.cpp`) takes it every guest tick and performs the claim, and `shellSampleClaimDone`'s success arm then calls `MenuScene::reroot_to_scene_opener_page()`. **This pair is still never written under the toggle, and that is deliberate**: it is written in exactly one place, `SoundEditor::maybeGetParentMenu` (`SoundEditor::maybeGetParentMenu` in `gui/ui/sound_editor.cpp`, the two writes at its end), which the ported path never enters -- and `shellSampleClaimDone` claims with `loadWithoutExiting = true`, which skips the block that would have read it. Reviving that block would have let a stale pair from a pre-toggle legacy session re-root the *legacy* editor; the port re-roots `MenuState::path`/`depth` instead. **So "two arbitrary `MenuItem*`s with no port" is no longer the reason this row is not gated:** `MenuScene::reroot_to_scene_opener_page()` plus `MenuWalkState::scene_opener` ARE that pair's port. What is left is a decision rather than a lookup -- this is the *legacy* browser, reached with the toggle on only from the two unmapped sites and site 9's legacy fallback -- and it has not been made, which is why this row is in the not-gated table |
/// | 14 | `gui/views/instrument_clip_view.cpp:5687-5694` `InstrumentClipView::enterDrumCreator` | **the same node site 8 wants**: its own body ran `soundEditor.setup(getCurrentInstrumentClip(), &menu_item::file0SelectorMenu, 0)` a few lines earlier. | **not gated because it is a fallback, not an entry.** It runs only when `openUI(&audioRecorder)` or `openUI(&sampleBrowser)` has just *refused*, and what legacy then shows is the file-selector item standing in for the browser it could not open. Routing it to the guest would enter `nodes::file0_selector`, whose own hook asks the shell to push the **ported** browser -- answering a failed browser open by opening a browser. That is a behaviour decision, not a node lookup, so the node being known is not enough to map it. The creator's browser itself is gated, ahead of this fallback: under the toggle `openShellDrumCreator` (`gui/ui/shell_drum_creator.h`) opens the ported browser on the new drum, so the fallback runs only on the legacy path |
// clang-format on
///
/// @note **Site 7 is conditionally gated, and that is the honest half of it.**
///       `AutomationView::handleSelectEncoderButtonAction` runs under `UI_MODE_NONE` *or*
///       `UI_MODE_NOTES_PRESSED && padSelectionOn`, and in the second
///       case `setup(clip)` reaches `gui/ui/sound_editor.cpp:1774-1776` and opens
///       `noteEditorRootMenu` -- the root site 12 reaches through `NoteMode::NoteEditor`, which
///       this site does not pass. So the gate there also tests the UI mode, and the note-editor case falls
///       through to the legacy statements. Sites 6 and 5 need no such test: both are already
///       `currentUIMode == UI_MODE_NONE` arms.

namespace deluge::gui::ui {
namespace {

namespace menu = deluge::scenes::menu;

/// @brief Resolve @p node to the stable node id `SceneId::arg` carries, or 0 for the context root.
///
/// @return `false` when a named node has no id, which means `scenes::assigned_node_ids()` never
///         ran over a tree containing it. Refusing is deliberate: id 0 means "the context's root"
///         to `MenuScene::on_enter` (`scenes/menu/menu_scene.cpp`), so a silently-zero id
///         would open the wrong menu rather than none.
bool resolveNodeId(ShellMenuNode node, uint16_t& out) {
	switch (node) {
	case ShellMenuNode::ContextRoot:
		out = 0;
		return true;
	case ShellMenuNode::Osc0FileSelector:
		out = menu::node_id(menu::nodes::file0_selector);
		return out != 0;
	case ShellMenuNode::Dx:
		out = menu::node_id(menu::nodes::dx_menu);
		return out != 0;
	case ShellMenuNode::DxParam:
		out = menu::node_id(menu::nodes::dx_param);
		return out != 0;
	}
	return false;
}

/// @brief `menu::context_for` plus the two facts about the root UI that `src/scenes` may not read.
menu::MenuContext resolveContext(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot,
                                 bool sessionViewRoot, bool kitGlobalFx) {
	menu::MenuContext ctx =
	    menu::context_for(*currentSong, deluge::gui::menuViewOf(clip), static_cast<int8_t>(sourceIndex),
	                      performanceView, sessionOrArrangerRoot, sessionViewRoot, kitGlobalFx);
	ctx.root_greyout = deluge::gui::menuRootGreyout();
	ctx.root_is_keyboard = getRootUI() == &keyboardScreen;
	ctx.root_is_automation = getRootUI() == &automationView;
	return ctx;
}

/// @brief The node `MenuScene::roots()` holds for @p ctx's root, or `nullptr` when there is none.
///
/// A refusal, not decoration, though unreachable: `context_for` only ever assigns a real
/// enumerator, never `Root::Count`.
const menu::Node* rootNodeFor(const menu::MenuContext& ctx) {
	const std::span<const menu::Node* const> roots = menu::MenuScene::roots();
	const size_t rootIndex = static_cast<size_t>(ctx.root);
	return rootIndex < roots.size() ? roots[rootIndex] : nullptr;
}

/// @brief Install @p ctx on `MenuScene` and open the guest on @p arg, or on where @p shortcut lands
///        when it is given.
///
/// The root passed to `open` is not what decides where the menu lands -- `MenuScene::on_enter`
/// re-roots from `SceneId::arg`, or from `ctx_.root` for `arg == 0` (the `@warning` on
/// `MenuScene::open`). It is spelled with the resolved root because `open(root, ctx)` is the only
/// entry point that installs a `MenuContext`.
/// @return Whether the guest is now open.
bool openGuest(const menu::Node& root, const menu::MenuContext& ctx, uint16_t arg, const menu::Node* shortcut) {
	// A guest torn down without its close (`nullifyUIs`) leaves the creator's drum behind; a new guest is not its.
	forgetShellDrumCreator();
	deluge::scenes::menu_scene().open(root, ctx);
	// What the legacy views read as "the sound menu's" while it is open -- the automation view's model stacks and
	// affect-entire, the automation display -- is `soundEditor`'s context, which `setup()` writes and nothing here
	// runs.
	adoptShellMenuContext();
	if (shortcut != nullptr) {
		arg = deluge::scenes::menu_scene().arm_shortcut_landing(*shortcut);
	}
	shellHostUI.setInitialScene(deluge::ui::shell::SceneId{.kind = deluge::ui::shell::SceneKind::FirstApp, .arg = arg});
	return openUI(&shellHostUI);
}

} // namespace

bool shellMenuEnabled() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShellMenu) == RuntimeFeatureStateToggle::On;
}

bool openShellMenu(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot,
                   bool sessionViewRoot, bool kitGlobalFx, ShellMenuNode node, NoteMode noteMode) {
	if (currentSong == nullptr) {
		return false;
	}

	uint16_t arg = 0;
	if (!resolveNodeId(node, arg)) {
		return false;
	}

	menu::MenuContext ctx =
	    resolveContext(clip, sourceIndex, performanceView, sessionOrArrangerRoot, sessionViewRoot, kitGlobalFx);
	// `MenuContext::resolved` is where `setup()`'s two outright refusals live -- a kit whose selected
	// `SoundDrum` has no note row (`gui/ui/sound_editor.cpp:1719-1721`) and a kit with neither
	// affect-entire nor a selected drum (`:1733-1739`). Both are `return false` in legacy, and both
	// happen *before* the undo-log wipe below, which is why this test is above it and not beside the
	// open.
	if (!ctx.resolved) {
		return false;
	}

	// `enterNoteEditor`/`enterNoteRowEditor` pass an explicit item to `soundEditor.setup`
	// rather than letting it infer one from `currentUIMode` -- `NoteMode` is this call's equivalent,
	// applied only after `context_for` has resolved everything else (`ctx.affect_entire` above still
	// reads the clip as it stood before the clear below, exactly as legacy's own
	// `soundEditor.setup` call runs before its `if (clip->affectEntire)` clear).
	if (noteMode != NoteMode::None) {
		ctx.root = noteMode == NoteMode::NoteEditor ? menu::MenuContext::Root::NoteEditor
		                                            : menu::MenuContext::Root::NoteRowEditor;
	}

	const menu::Node* root = rootNodeFor(ctx);
	if (root == nullptr) {
		return false;
	}

	// **`SoundEditor::setup`'s one side effect that is neither UI nor context, picked up here.**
	// `sound_editor.cpp:1769-1771` wipes the undo history on every menu open, in exactly the `item ==
	// nullptr && clip != nullptr` arm this condition spells. `scenes/menu/context.cpp`'s header
	// lists it among the `setup()` behaviour that is neither UI nor context and that whoever opens a
	// menu must pick up -- this is that call site. Without it a user with the toggle on could undo
	// past a menu edit where legacy stops them.
	//
	// Remembering the previously selected range across sessions (`:1784-1788`) is not done here
	// either: the source keeps it (`Source::defaultRangeI`), which the picker reads.
	//
	// **Nor is the first range's allocation.**
	// `Source::getOrCreateFirstRange()`'s allocation inside the legacy permission check (`:1993`)
	// belongs to whoever *opens* a range-dependent item, and this function does not: it hands the
	// node id to `MenuScene::on_enter` through `SceneId::arg`, which is where the open happens and
	// where it belongs. Site 8 -- the only site here that names a leaf -- is the one this shows up
	// on: without the allocation the ported file selector's browser refuses to open on a synth whose
	// source has no ranges yet, while the toggle-OFF leg opens it.
	//
	// **`noteMode != NoteMode::None` must not wipe either.** `InstrumentClipView::enterNoteEditor`
	// and `InstrumentClipView::enterNoteRowEditor` each call
	// `soundEditor.setup(clip, &noteEditorRootMenu)` / `soundEditor.setup(clip, &noteRowEditorRootMenu)`
	// -- an explicit item, never `nullptr` -- so legacy's `item == nullptr` arm above never runs for
	// either, and the undo log survives a note-editor/note-row-editor open. `NoteMode` is spelled as
	// an *override of the resolved root*, not as a `ShellMenuNode` leaf, so `node` alone (still
	// `ContextRoot` for both note roots) cannot carry this distinction; `noteMode` has to gate the
	// wipe directly, the same way it already gates the root override and the `affectEntire` clear
	// below.
	if (node == ShellMenuNode::ContextRoot && clip != nullptr && noteMode == NoteMode::None) {
		actionLogger.deleteAllLogs();
	}

	// The kit `affectEntire` clear both note entries do once `soundEditor.setup` has succeeded and
	// before `openUI` -- `InstrumentClipView::enterNoteEditor` and `enterNoteRowEditor`, the same
	// three statements in both. `ctx.affect_entire` is `ctx.is_kit && affectEntire` (`context_for`),
	// so testing it alone is legacy's nested `if (KIT) { if (affectEntire) {` collapsed to one test.
	if (noteMode != NoteMode::None && ctx.affect_entire) {
		static_cast<InstrumentClip*>(clip)->affectEntire = false;
		// `ctx.affect_entire` is a snapshot taken by `context_for` above,
		// and nothing re-resolves it after this clear -- but `sequence::Direction::getIndividualNoteRow`
		// reads `clip->affectEntire` LIVE (`gui/menu_item/sequence/direction.h:44`), after legacy's own
		// identical clear. So the installed context must agree with the clip it just edited, or
		// the play-direction binding's `!ctx.affect_entire && ctx.is_kit` test
		// (`scenes/menu/bindings.cpp`) would still read `true` and the note-row editor would keep
		// editing the kit's own `sequenceDirectionMode` instead of the selected drum's row.
		ctx.affect_entire = false;
		view.setActiveModControllableTimelineCounter(clip);
	}

	return openGuest(*root, ctx, arg, nullptr);
}

bool openShellMenuAtShortcut(Clip* clip, int32_t x, int32_t y, bool performanceView, bool sessionOrArrangerRoot,
                             bool sessionViewRoot, bool kitGlobalFx) {
	if (currentSong == nullptr) {
		return false;
	}
	// `potentialShortcutPadAction`'s `thingIndex = x & 1`: an odd column addresses source 1.
	const menu::MenuContext ctx =
	    resolveContext(clip, x & 1, performanceView, sessionOrArrangerRoot, sessionViewRoot, kitGlobalFx);
	if (!ctx.resolved) {
		return false;
	}
	// A node that does not apply is refused here so that legacy's own `setup()` refuses it, with its
	// "PARAMETER NOT APPLICABLE" popup.
	const menu::Node* node = menu::shortcut_node(ctx.root, false, static_cast<uint8_t>(x), static_cast<uint8_t>(y));
	const menu::Node* root = rootNodeFor(ctx);
	if (node == nullptr || menu::node_id(*node) == 0 || root == nullptr) {
		return false;
	}
	if (!menu::shortcut_applies(*node, ctx)) {
		// The DX7 fallback opens the parameter page alone; any other refusal is legacy's `setup()`'s to show.
		const menu::Node* fallback = menu::nodes::dx_shortcut_fallback(*node, ctx, static_cast<uint8_t>(x));
		return fallback != nullptr && openGuest(*root, ctx, menu::node_id(*fallback), nullptr);
	}
	// No undo-log wipe: `setup()` wipes only when it is given no item.
	return openGuest(*root, ctx, 0, node);
}

bool openShellDxColumn(int32_t op) {
	if (isUIOpen(&shellHostUI)) {
		deluge::scenes::menu_scene().dx_column_press(op);
		return true;
	}
	// `openForOpOrGlobal`'s `setup(getCurrentClip(), this, 0)`, then the param, set after the screen began.
	menu::DxState dx = deluge::scenes::menu_scene().state().dx;
	dx.param = menu::dx::column_param(dx.param, op, false);
	dx.blink_on_focus = true;
	deluge::scenes::menu_scene().land_dx(dx);
	return openShellMenu(getCurrentClip(), 0, /*performanceView=*/false, /*sessionOrArrangerRoot=*/false,
	                     /*sessionViewRoot=*/false, /*kitGlobalFx=*/false, ShellMenuNode::DxParam);
}

} // namespace deluge::gui::ui
