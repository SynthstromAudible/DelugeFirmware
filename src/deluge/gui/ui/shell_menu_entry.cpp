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

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include <cstddef>
#include <span>

/// @file
/// @brief The fourteen `openUI(&soundEditor)` sites, and what each one wanted open.
///
/// Step 1 of F1 task 9, kept here rather than only in a report: the fourteen are not uniform, and a
/// reader deciding whether to add a fifteenth needs to know which shape it is. The line numbers are
/// the ones this commit leaves behind.
///
/// **Gated (eight).** Each one's `else` branch is byte-for-byte the statements it ran before.
///
// clang-format off
/// | # | site | what node it wants, and how it says so today | mapped to |
/// |---|---|---|---|
/// | 1 | `gui/views/view.cpp:477-478` `View::buttonAction` | `soundEditor.setup()` with SHIFT held, so `setup()`'s three view tests (`sound_editor.cpp:1816`, `:1819`) are all false and the root is `settingsRootMenu` (`:1824`) | `MenuContext::Root::Settings`, i.e. all four UI arguments false. The same reading, for the same arm, that `services::GlobalControls::handle_settings_menu` already made (`scenes/services/global_controls.cpp:319-331`) |
/// | 2 | `gui/views/session_view.cpp:569-570` `SessionView::buttonAction` | `soundEditor.setup()` with no shift from the session view -> `soundEditorRootMenuSongView` (`gui/ui/sound_editor.cpp:1821`) | `Root::Song`; `sessionViewRoot` true, which is the *only* thing two items in that root read (`MenuContext::is_session_view`) |
/// | 3 | `gui/views/arranger_view.cpp:346-347` `ArrangerView::buttonAction` | the same call from the arranger -> the same `soundEditorRootMenuSongView` | `Root::Song`, `sessionViewRoot` **false** |
/// | 4 | `gui/views/performance_view.cpp:898-901` `PerformanceView::buttonAction` | `soundEditor.setup(nullptr, &soundEditorRootMenuPerformanceView)` -- an explicitly named root | `Root::Performance`, which `context_for` picks from `performance_view` (`context.cpp:361-363`). Named by fact rather than by pointer, and they agree because this arm is `SELECT_ENC && !shift` inside `PerformanceView` |
/// | 5 | `gui/views/audio_clip_view.cpp:423-426` `AudioClipView::buttonAction` | `soundEditor.setup(getCurrentClip())` on an `AudioClip` -> `soundEditorRootMenuAudioClip` (`gui/ui/sound_editor.cpp:1812`) | `Root::AudioClip` (`context.cpp:356-358`) |
/// | 6 | `model/clip/instrument_clip_minder.cpp:416-423` `InstrumentClipMinder::buttonAction` | `setupKitGlobalFXMenu` from `KIT && affectEntire`, then `soundEditor.setup(getCurrentClip())` -- the root is then one of six, chosen by output type, affect-entire and the selected drum's type (`gui/ui/sound_editor.cpp:1780-1808`) | whichever of `MidiOrCv` / `KitGlobalFx` / `MidiDrum` / `GateDrum` / `Drum` / `Sound` `context_for` picks -- the same six tests in the same order (`context.cpp:330-358`) |
/// | 7 | `gui/views/automation_view.cpp:1622-1630` `AutomationView::handleSelectEncoderButtonAction` | the same, except the clip is `nullptr` on the arranger (so: the song root) **and** the arm also runs under `UI_MODE_NOTES_PRESSED` | the same six, or `Root::Song` when `onArrangerView`. **Only under `UI_MODE_NONE`**: see the note below |
/// | 8 | `gui/views/instrument_clip_view.cpp:515-519` `InstrumentClipView::buttonAction` | `soundEditor.setup(getCurrentInstrumentClip(), &menu_item::file0SelectorMenu, 0)` -- a named *leaf*, not a root | `Root::Sound` plus `SceneId::arg = node_id(nodes::file0_selector)`, the ported `file0SelectorMenu` (`scenes/menu/nodes/sound_osc.h:17`). The one site that exercises a non-zero `arg` |
///
/// **Not gated (six), because the node has no port.** Step 4 of the task: a wrong node is worse
/// than a legacy menu, so these keep opening `soundEditor` whatever the toggle says.
///
/// | # | site | what it wants | why it is not mapped |
/// |---|---|---|---|
/// | 9 | `gui/ui/sound_editor.cpp:1442` `SoundEditor::enterOrUpdateSoundEditor` | whatever `potentialShortcutPadAction` (`:1425`) or `dx::param` (`gui/menu_item/dx/param.cpp:544`) just `setup()` an item for -- a legacy `MenuItem*` computed from legacy's own seven shortcut tables | there is no legacy-`MenuItem*`-to-`Node` map in this repository, and re-deriving the pad through `menu::shortcut_node` would be a second implementation of the lookup rather than a translation of the caller's intent. It is also the one site with three outcomes rather than one (`changeUIAtLevel` onto `sampleMarkerEditor`, `openUI`, or `beginScreen` on an already-open editor), only one of which is an open. **Under the toggle, shift+pad from a clip view still opens the legacy menu.** |
/// | 10 | `gui/ui/browser/sample_browser.cpp:1075-1087` `SampleBrowser::claimCurrentFile` | `parentMenuHeadingTo` focused on `menuItemHeadingTo` -- two arbitrary `MenuItem*`s the browser was handed on the way in | same reason, and this is the *legacy* browser: the ported one parks its choice in `MenuScene::take_choice()`, which nothing polls (migration §10.2 item 4). Mapping this site would need that dispatcher first |
/// | 11 | `model/clip/instrument_clip_minder.cpp:251-252` `InstrumentClipMinder::createNewInstrument` | `&dxMenu` (`gui/ui/menus.cpp:1722`) | **`dxMenu` has no port.** `src/scenes/README.md:1450-1451` records it as owed: three bespoke DX items that are E2's browser work |
/// | 12 | `gui/views/instrument_clip_view.cpp:3590-3598` `InstrumentClipView::enterNoteEditor` | `&noteEditorRootMenu` | **no ported root.** `MenuContext::Root` deliberately has no enumerator for it: legacy selects it from `currentUIMode` (`sound_editor.cpp:1774-1776`), a fact `src/scenes` may not read, and `scenes/menu/context.h`'s own note leaves it "with the caller (spec E's `NoteClipScene`)" -- a scene this plan does not build |
/// | 13 | `gui/views/instrument_clip_view.cpp:3758-3766` `InstrumentClipView::enterNoteRowEditor` | `&noteRowEditorRootMenu` | the same, from `UI_MODE_AUDITIONING` (`gui/ui/sound_editor.cpp:1777-1779`) |
/// | 14 | `gui/views/instrument_clip_view.cpp:5598-5605` `InstrumentClipView::enterDrumCreator` | **the same node site 8 wants**: its own body ran `soundEditor.setup(getCurrentInstrumentClip(), &menu_item::file0SelectorMenu, 0)` a few lines earlier (`gui/views/instrument_clip_view.cpp:5581`). | **not gated because it is a fallback, not an entry.** It runs only when `openUI(&audioRecorder)` or `openUI(&sampleBrowser)` has just *refused*, and what legacy then shows is the file-selector item standing in for the browser it could not open. Routing it to the guest would enter `nodes::file0_selector`, whose own hook asks the shell to push the **ported** browser -- answering a failed browser open by opening a browser. That is a behaviour decision for task 11's A/B run, not a node lookup, so the node being known is not enough to map it. |
// clang-format on
///
/// @note **Site 7 is conditionally gated, and that is the honest half of it.**
///       `AutomationView::handleSelectEncoderButtonAction` runs under `UI_MODE_NONE` *or*
///       `UI_MODE_NOTES_PRESSED && padSelectionOn` (`automation_view.cpp:1593`), and in the second
///       case `setup(clip)` reaches `gui/ui/sound_editor.cpp:1774-1776` and opens
///       `noteEditorRootMenu` -- site 12's
///       unmapped root. So the gate there also tests the UI mode, and the note-editor case falls
///       through to the legacy statements. Sites 6 and 5 need no such test: both are already
///       `currentUIMode == UI_MODE_NONE` arms.

namespace deluge::gui::ui {
namespace {

namespace menu = deluge::scenes::menu;

/// @brief Resolve @p node to the stable node id `SceneId::arg` carries, or 0 for the context root.
///
/// @return `false` when a named node has no id, which means `scenes::assigned_node_ids()` never
///         ran over a tree containing it. Refusing is deliberate: id 0 means "the context's root"
///         to `MenuScene::on_enter` (`scenes/menu/menu_scene.cpp:334-343`), so a silently-zero id
///         would open the wrong menu rather than none.
bool resolveNodeId(ShellMenuNode node, uint16_t& out) {
	switch (node) {
	case ShellMenuNode::ContextRoot:
		out = 0;
		return true;
	case ShellMenuNode::Osc0FileSelector:
		out = menu::node_id(menu::nodes::file0_selector);
		return out != 0;
	}
	return false;
}

} // namespace

bool shellMenuEnabled() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShellMenu) == RuntimeFeatureStateToggle::On;
}

bool openShellMenu(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot,
                   bool sessionViewRoot, bool kitGlobalFx, ShellMenuNode node) {
	if (currentSong == nullptr) {
		return false;
	}

	uint16_t arg = 0;
	if (!resolveNodeId(node, arg)) {
		return false;
	}

	const menu::MenuContext ctx =
	    menu::context_for(*currentSong, clip, static_cast<int8_t>(sourceIndex), performanceView, sessionOrArrangerRoot,
	                      sessionViewRoot, kitGlobalFx);
	// `MenuContext::resolved` is where `setup()`'s two outright refusals live -- a kit whose selected
	// `SoundDrum` has no note row (`gui/ui/sound_editor.cpp:1719-1721`) and a kit with neither
	// affect-entire nor a selected drum (`:1716-1722`). Both are `return false` in legacy, and both
	// happen *before* the undo-log wipe below, which is why this test is above it and not beside the
	// open.
	if (!ctx.resolved) {
		return false;
	}

	const std::span<const menu::Node* const> roots = menu::MenuScene::roots();
	const size_t rootIndex = static_cast<size_t>(ctx.root);
	if (rootIndex >= roots.size() || roots[rootIndex] == nullptr) {
		return false;
	}

	// **`SoundEditor::setup`'s one side effect that is neither UI nor context, picked up here.**
	// `sound_editor.cpp:1771` wipes the undo history on every menu open, in exactly the `item ==
	// nullptr && clip != nullptr` arm this condition spells. `scenes/menu/context.cpp:44` records it
	// as "owed to E2 ... has no owner yet, and will silently change if nothing picks it up when
	// `MenuScene` lands" -- this is the call site that lands, so it picks it up. Without it a user
	// with the toggle on could undo past a menu edit where legacy stops them.
	//
	// Two of the three items on that list are still owed and are NOT done here, because neither has
	// a home on this side of the seam: remembering the previously selected `MultiRange` across
	// sessions (`:1784-1788`, `MenuContext::range`) and `Source::getOrCreateFirstRange()`'s
	// allocation inside the legacy permission check (`:1938`).
	if (node == ShellMenuNode::ContextRoot && clip != nullptr) {
		actionLogger.deleteAllLogs();
	}

	// The root passed here is **not** what decides where the menu lands -- `MenuScene::on_enter`
	// re-roots from `SceneId::arg`, or from `ctx_.root` for `arg == 0`, one call later. That is a
	// `MenuScene` API property rather than anything this call site can fix, and it is recorded as a
	// `@warning` on `MenuScene::open` together with the mutation that measured it. The call is
	// spelled with the resolved root anyway, because `open(root, ctx)` is the only entry point that
	// installs a `MenuContext` and `roots[0]` here would be actively misleading to a reader.
	//
	// The `rootIndex` bounds test above IS a refusal and not decoration, but it is unreachable
	// today: `context_for` only ever assigns a real enumerator, never `Root::Count`.
	deluge::scenes::menu_scene().open(*roots[rootIndex], ctx);
	shellHostUI.setInitialScene(deluge::ui::shell::SceneId{.kind = deluge::ui::shell::SceneKind::FirstApp, .arg = arg});
	return openUI(&shellHostUI);
}

} // namespace deluge::gui::ui
