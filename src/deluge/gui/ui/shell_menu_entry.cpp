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

#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_choice_dispatch.h" // adoptShellMenuContext
#include "gui/ui/shell_drum_creator.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_pad_forward.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/output.h"
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
#include <optional>
#include <span>

/// @file
/// @brief The fifteen `openUI(&soundEditor)` sites, and what each one wanted open.
///
/// The fifteen are not uniform, and a reader deciding whether to add a sixteenth needs to know
/// which shape it is.
///
/// **Gated (fourteen).** Each one's `else` is byte-for-byte what it ran before.
///
// clang-format off
/// | # | site | what node it wants, and how it says so today | mapped to |
/// |---|---|---|---|
/// | 1 | `gui/views/view.cpp` `View::buttonAction` | `soundEditor.setup()` with SHIFT held, so `setup()`'s three view tests (in `SoundEditor::setup`) are all false and the root is `settingsRootMenu` | `MenuContext::Root::Settings`, i.e. all four UI arguments false. The same reading, for the same arm, that `services::GlobalControls::handle_settings_menu` already made (`services::GlobalControls::handle_settings_menu`) |
/// | 2 | `gui/views/session_view.cpp:539-554` `SessionView::buttonAction` | `soundEditor.setup()` with no shift from the session view -> `soundEditorRootMenuSongView` (`SoundEditor::setup`) | `Root::Song`; `sessionViewRoot` true, which is the *only* thing two items in that root read (`MenuContext::is_session_view`) |
/// | 3 | `gui/views/arranger_view.cpp:337-350` `ArrangerView::buttonAction` | the same call from the arranger -> the same `soundEditorRootMenuSongView` | `Root::Song`, `sessionViewRoot` **false** |
/// | 4 | `gui/views/performance_view.cpp:896-912` `PerformanceView::buttonAction` | `soundEditor.setup(nullptr, &soundEditorRootMenuPerformanceView)` -- an explicitly named root | `Root::Performance`, which `context_for` picks from `performance_view`. Named by fact rather than by pointer, and they agree because this arm is `SELECT_ENC && !shift` inside `PerformanceView` |
/// | 5 | `gui/views/audio_clip_view.cpp` `AudioClipView::buttonAction` | `soundEditor.setup(getCurrentClip())` on an `AudioClip` -> `soundEditorRootMenuAudioClip` (`SoundEditor::setup` in `gui/ui/sound_editor.cpp`) | `Root::AudioClip` (`context_for`) |
/// | 6 | `model/clip/instrument_clip_minder.cpp` `InstrumentClipMinder::buttonAction` | `setupKitGlobalFXMenu` from `KIT && affectEntire`, then `soundEditor.setup(getCurrentClip())` -- the root is then one of six, chosen by output type, affect-entire and the selected drum's type (`SoundEditor::setup` in `gui/ui/sound_editor.cpp`) | whichever of `MidiOrCv` / `KitGlobalFx` / `MidiDrum` / `GateDrum` / `Drum` / `Sound` `context_for` picks -- the same six tests in the same order |
/// | 7 | `gui/views/automation_view.cpp` `AutomationView::handleSelectEncoderButtonAction` | `setup(clip)` as site 6, except the clip is `nullptr` on the arranger (so: the song root), and the arm also runs under `UI_MODE_NOTES_PRESSED` with pad selection on, where `setup()` infers `noteEditorRootMenu` for an instrument clip | the same six, `Root::Song` when `onArrangerView`, and `Root::NoteEditor` through `NoteMode::NoteEditorFromUiMode` under `UI_MODE_NOTES_PRESSED` on an instrument clip |
/// | 8 | `gui/views/instrument_clip_view.cpp` `InstrumentClipView::buttonAction` | `soundEditor.setup(getCurrentInstrumentClip(), &menu_item::file0SelectorMenu, 0)` -- a named *leaf*, not a root | `Root::Sound` plus `SceneId::arg = node_id(nodes::file0_selector)`, the ported `file0SelectorMenu`. The one site that exercises a non-zero `arg` |
/// | 12 | `gui/views/instrument_clip_view.cpp` `InstrumentClipView::enterNoteEditor` | `soundEditor.setup(clip, &noteEditorRootMenu)` -- an explicitly named root, the one `setup()` would itself pick under `UI_MODE_NOTES_PRESSED` (`SoundEditor::setup` in `sound_editor.cpp`) | `Root::NoteEditor`. `context_for` never assigns it, because `src/scenes` may not read `currentUIMode`; the gate passes `NoteMode::NoteEditor` instead, and `openShellMenu` overrides the resolved root with it |
/// | 13 | `gui/views/instrument_clip_view.cpp` `InstrumentClipView::enterNoteRowEditor` | `soundEditor.setup(clip, &noteRowEditorRootMenu)` -- the same, under `UI_MODE_AUDITIONING` (`gui/ui/sound_editor.cpp`) | `Root::NoteRowEditor` through `NoteMode::NoteRowEditor`. Not while the legacy `soundEditor` is current, which with ShellMenu on is only a session opened before the setting changed (`SoundEditor::opened` refuses every later open): that session's own shortcut keeps re-rooting it. Inside the ported menu the same shortcut re-roots the open menu (`openShellNoteRowEditor`); `main` stacks a second editor there and BACK then sticks, which is accepted as `main`'s bug (DIFFERENCES D92) |
/// | 9 | `gui/ui/sound_editor.cpp` `SoundEditor::potentialShortcutPadAction`, from a clip view, the keyboard screen or the performance view | `setup(currentClip, item, x & 1)` on the item legacy's shortcut tables name for the pad, behind the Shift/audition gate, the coming-soon popup and the sequence-direction arm | `shellShortcutPadAction`, the whole press: with the guest closed the kit-global-FX latch first (`shellKitGlobalFx`), as legacy's first statement writes `setupKitGlobalFXMenu`; then the gate (`shortcutPadGate`), the coming-soon popup (`menu::shortcut_coming_soon`), the sequence-direction arm, then `openShellMenuAtShortcut` on the node `menu::shortcut_node` names, landed where `setup()` lands it (`MenuScene::arm_shortcut_landing`); off a clip minder, the song view's table and the song, as legacy's call reads them. A node that does not apply says "PARAMETER NOT APPLICABLE"; the DX7 fallback, `modulator0Volume` refused on a DX7 source, opens `nodes::dx_param` alone on the param it showed last (`nodes::dx_shortcut_fallback`; legacy tests the source the previous menu left, DIFFERENCES D90). With the guest open, a pad the Settings root forwards re-roots it in place. Legacy's statements run only with ShellMenu off and no guest open, or when legacy's own editor or marker editor asks |
/// | 11 | `model/clip/instrument_clip_minder.cpp:304-305` `InstrumentClipMinder::createNewInstrument` | `soundEditor.setup(clip, &dxMenu, 0)` on a new FM synth -- a named submenu (`gui/ui/menus.cpp:1735`) | `ShellMenuNode::Dx`, `SceneId::arg = node_id(nodes::dx_menu)`; a named node, so no undo-log wipe |
/// | 15 | `gui/ui/keyboard/column_controls/dx.cpp:101-108` `DXColumn::handlePad`, Shift + a DX column pad on the keyboard screen | `dxParam.openForOpOrGlobal(op)` (`gui/menu_item/dx/param.cpp:514-542`): `setup(getCurrentClip(), &dxParam, 0)` when the editor is not already on the page, then the operator's param -- or, on the page, the toggle and carry | `openShellDxColumn`: with the guest closed `ShellMenuNode::DxParam` on `dx::column_param`'s param, its pad blinking, no undo-log wipe; with it open `MenuScene::dx_column_press`, which toggles on the page or re-targets the menu to it with a `Replace` |
/// | 14 | `gui/views/instrument_clip_view.cpp` `InstrumentClipView::enterDrumCreator` | `soundEditor.setup(clip, &file0SelectorMenu, 0)`, then `openUI(&audioRecorder)` for a take or `openUI(&sampleBrowser)`; if that refuses, `openUI(&soundEditor)`, whose `FileSelector::beginSession` opens the browser again | (a) RECORD (adding a drum row, or with an audition pad held): `openShellDrumRecorder` opens the guest on `nodes::sample0_recorder`, whose hook pushes the ported recorder; it leaves the menu at the take's end, as legacy's recorder leaves the clip view. A refused take lands on `nodes::file0_selector` instead (`landShellMenuOnFileSelectorIfRecorderRefuses`), whose hook pushes the ported browser, as legacy's fallback does. (b) Otherwise `openShellDrumCreator` opens the guest on `nodes::file0_selector`, whose hook pushes the ported browser on the new drum. With ShellMenu on a guest that refuses to open says setup()'s refusal (`Error::UNSPECIFIED`) and nothing legacy opens: the legacy recorder, browser and editor are the ShellMenu-off leg's alone |
///
/// **Not gated (one).** It is the legacy browser's own, which nothing opens with ShellMenu on, for the reason its row gives.
///
/// | # | site | what it wants | why it is not mapped |
/// |---|---|---|---|
/// | 10 | `gui/ui/browser/sample_claim.cpp` `SampleClaim::stepFinish` | `parentMenuHeadingTo` focused on `menuItemHeadingTo` -- two `MenuItem*`s the legacy browser was handed on the way in | the *legacy* browser's, and with ShellMenu on nothing opens that browser: it opens from the legacy editor's file selector, which cannot open then (`SoundEditor::opened`), and from site 14's ShellMenu-off leg. The ported browser parks its choice in `BrowserScene::take_choice()`; `deluge::gui::ui::dispatchShellChoices` claims it with `loadWithoutExiting = true`, which skips this block, and `shellSampleClaimDone`'s success arm re-roots the menu itself (`MenuScene::reroot_to_scene_opener_page()` and `MenuWalkState::scene_opener`, the port of this pair) |
// clang-format on
///

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
	case ShellMenuNode::Osc0Recorder:
		out = menu::node_id(menu::nodes::sample0_recorder);
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

/// @brief `shellKitGlobalFx`'s latch.
bool kitGlobalFxLatch = false;

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

/// @brief Both note entries' kit `affectEntire` clear, once the context resolved and before the menu opens: the clip
///        and the context both say off afterwards.
///
/// The kit `affectEntire` clear both note entries do once `soundEditor.setup` has succeeded and
/// before `openUI` -- `InstrumentClipView::enterNoteEditor` and `enterNoteRowEditor`, the same
/// three statements in both. `ctx.affect_entire` is `ctx.is_kit && affectEntire` (`context_for`),
/// so testing it alone is legacy's nested `if (KIT) { if (affectEntire) {` collapsed to one test.
void clearKitAffectEntireForNoteMode(menu::MenuContext& ctx, Clip* clip) {
	if (!ctx.affect_entire) {
		return;
	}
	static_cast<InstrumentClip*>(clip)->affectEntire = false;
	// `ctx.affect_entire` is a snapshot taken by `context_for` above,
	// and nothing re-resolves it after this clear -- but `sequence::Direction::getIndividualNoteRow`
	// reads `clip->affectEntire` LIVE (`gui/menu_item/sequence/direction.h`), after legacy's own
	// identical clear. So the installed context must agree with the clip it just edited, or
	// the play-direction binding's `!ctx.affect_entire && ctx.is_kit` test
	// (`scenes/menu/bindings.cpp`) would still read `true` and the note-row editor would keep
	// editing the kit's own `sequenceDirectionMode` instead of the selected drum's row.
	ctx.affect_entire = false;
	view.setActiveModControllableTimelineCounter(clip);
}

/// @brief `SoundEditor::shouldEditKitAffectEntire()`: a kit clip with affect-entire on.
bool kitAffectEntireNow(const Clip* clip) {
	return clip != nullptr && clip->type == ClipType::INSTRUMENT && clip->output->type == OutputType::KIT
	       && static_cast<const InstrumentClip*>(clip)->affectEntire;
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
///
/// With the guest already open the open menu re-roots in place instead, as legacy's `setup()` re-sets the open
/// editor. The guest is taken to be the frontmost UI there: the legacy context menus and the ported ones take every
/// pad, so no press arrives while another UI is pushed above it.
/// @return Whether the guest is now open.
bool openGuest(const menu::Node& root, const menu::MenuContext& ctx, uint16_t arg, const menu::Node* shortcut) {
	if (isUIOpen(&shellHostUI)) {
		// `setup()` on the open editor, then `enterOrUpdateSoundEditor`'s already-open arm, `beginScreen`: the open
		// menu re-roots in place. A Shift + pad the Settings root forwards to the keyboard screen or the performance
		// view arrives here; `openUI` would push the guest over itself.
		deluge::scenes::menu_scene().reroot_at(root, ctx, arg, shortcut);
		adoptShellMenuContext();
		shellHostUI.refreshRootFacts();
		display->cancelPopup();
		return true;
	}
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
	// `setup()` cancels whatever popup legacy's display holds once it has found its item: a drum name an audition
	// left up does not stay over the menu.
	display->cancelPopup();
	return openUI(&shellHostUI);
}

/// @brief `MenuScene::KitGlobalShortcutFn`: while the latch is set, the kit's own effects resolved now, as legacy's
///        `setup()` resolves them from `setupKitGlobalFXMenu`; `nullopt` while it is clear.
std::optional<menu::MenuContext> kitGlobalShortcutContext(const menu::MenuContext& open) {
	if (!kitGlobalFxLatch || currentSong == nullptr) {
		return std::nullopt;
	}
	const bool clipMinder = rootUIIsClipMinderScreen();
	const menu::MenuContext kitGlobal =
	    resolveContext(clipMinder ? getCurrentClip() : nullptr, open.source_index, getRootUI() == &performanceView,
	                   !clipMinder, getRootUI() == &sessionView, /*kitGlobalFx=*/true);
	if (!kitGlobal.resolved || !kitGlobal.is_kit_global_fx) {
		return std::nullopt;
	}
	return kitGlobal;
}

} // namespace

bool shellMenuEnabled() {
	return runtimeFeatureSettings.get(RuntimeFeatureSettingType::LegacyMenu) == RuntimeFeatureStateToggle::Off;
}

bool shellKitGlobalFx() {
	return kitGlobalFxLatch;
}

void clearShellKitGlobalFx() {
	kitGlobalFxLatch = false;
}

void latchShellKitGlobalFxOnReentry() {
	kitGlobalFxLatch = kitGlobalFxLatch
	                   || (currentSong != nullptr && kitAffectEntireNow(getCurrentClip())
	                       && Buttons::isShiftButtonPressed() && !isUIModeActive(UI_MODE_AUDITIONING));
}

void wireShellKitGlobalShortcuts() {
	deluge::scenes::menu_scene().set_kit_global_shortcut_source(&kitGlobalShortcutContext);
	deluge::scenes::menu_scene().set_context_adopted_hook(&adoptShellMenuContext);
	deluge::scenes::menu_scene().set_reentry_hook(&latchShellKitGlobalFxOnReentry);
}

bool openShellMenu(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot,
                   bool sessionViewRoot, bool kitGlobalFx, ShellMenuNode node, NoteMode noteMode) {
	// The site's own write comes before its `setup()`, and stays should `setup()` refuse. The open itself edits what
	// the site says, from the clip's affect-entire as it is now: the latch is for the shortcut presses that follow.
	kitGlobalFxLatch = kitGlobalFxLatch || kitGlobalFx;
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
	// `SoundDrum` has no note row (`SoundEditor::setup` in `gui/ui/sound_editor.cpp`) and a kit with neither
	// affect-entire nor a selected drum (the same function). Both are `return false` in legacy, and both
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
		ctx.root = noteMode == NoteMode::NoteRowEditor ? menu::MenuContext::Root::NoteRowEditor
		                                               : menu::MenuContext::Root::NoteEditor;
	}

	const menu::Node* root = rootNodeFor(ctx);
	if (root == nullptr) {
		return false;
	}

	// **`SoundEditor::setup`'s one side effect that is neither UI nor context, picked up here.**
	// `SoundEditor::setup` in `sound_editor.cpp` wipes the undo history on every menu open, in exactly the `item ==
	// nullptr && clip != nullptr` arm this condition spells. `scenes/menu/context.cpp`'s header
	// lists it among the `setup()` behaviour that is neither UI nor context and that whoever opens a
	// menu must pick up -- this is that call site. Without it a user with ShellMenu on could undo
	// past a menu edit where legacy stops them.
	//
	// Remembering the previously selected range across sessions (`SoundEditor::setup` in `sound_editor.cpp`) is not
	// done here either: the source keeps it (`Source::defaultRangeI`), which the picker reads.
	//
	// **Nor is the first range's allocation.**
	// `Source::getOrCreateFirstRange()`'s allocation inside the legacy permission check
	// (`SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`) belongs to whoever *opens* a range-dependent
	// item, and this function does not: it hands the node id to `MenuScene::on_enter` through `SceneId::arg`, which is
	// where the open happens and where it belongs. Site 8 -- the only site here that names a leaf -- is the one this
	// shows up on: without the allocation the ported file selector's browser refuses to open on a synth whose source
	// has no ranges yet, while the ShellMenu-off leg opens it.
	//
	// **A named note root must not wipe either.** `InstrumentClipView::enterNoteEditor` and
	// `InstrumentClipView::enterNoteRowEditor` call `soundEditor.setup(clip, &noteEditorRootMenu)` /
	// `soundEditor.setup(clip, &noteRowEditorRootMenu)` -- an explicit item -- so legacy's `item == nullptr` arm above
	// never runs for either, and the undo log survives. The automation view's notes-pressed SELECT is the other shape:
	// `setup(clip)` with no item, which wipes and then infers the note editor from `UI_MODE_NOTES_PRESSED`
	// (`NoteMode::NoteEditorFromUiMode`). Only the two named roots clear a kit's `affectEntire` after the open, as
	// their callers do.
	const bool namedNoteRoot = noteMode == NoteMode::NoteEditor || noteMode == NoteMode::NoteRowEditor;
	if (node == ShellMenuNode::ContextRoot && clip != nullptr && !namedNoteRoot) {
		actionLogger.deleteAllLogs();
	}

	if (namedNoteRoot) {
		clearKitAffectEntireForNoteMode(ctx, clip);
	}

	return openGuest(*root, ctx, arg, nullptr);
}

bool openShellMenuAtShortcut(Clip* clip, int32_t x, int32_t y, bool performanceView, bool sessionOrArrangerRoot,
                             bool sessionViewRoot, bool kitGlobalFx) {
	kitGlobalFxLatch = kitGlobalFxLatch || kitGlobalFx;
	if (currentSong == nullptr) {
		return false;
	}
	// `potentialShortcutPadAction`'s `thingIndex = x & 1`: an odd column addresses source 1.
	const menu::MenuContext ctx =
	    resolveContext(clip, x & 1, performanceView, sessionOrArrangerRoot, sessionViewRoot, kitGlobalFxLatch);
	if (!ctx.resolved) {
		return false;
	}
	// A pad with no node opens nothing and says nothing: legacy's item is null there.
	const menu::Node* node = menu::shortcut_node(ctx.root, false, static_cast<uint8_t>(x), static_cast<uint8_t>(y));
	const menu::Node* root = rootNodeFor(ctx);
	if (node == nullptr || menu::node_id(*node) == 0 || root == nullptr) {
		return false;
	}
	if (!menu::shortcut_applies(*node, ctx)) {
		// The DX7 fallback opens the parameter page alone; any other refusal is `setup()`'s, with its popup.
		const menu::Node* fallback = menu::nodes::dx_shortcut_fallback(*node, ctx, static_cast<uint8_t>(x));
		if (fallback == nullptr) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
			return false;
		}
		return openGuest(*root, ctx, menu::node_id(*fallback), nullptr);
	}
	// No undo-log wipe: `setup()` wipes only when it is given no item.
	return openGuest(*root, ctx, 0, node);
}

bool shellOwnsShortcutPads() {
	return shellMenuEnabled() || isUIOpen(&shellHostUI);
}

ActionResult shellShortcutPadAction(int32_t x, int32_t y, bool on) {
	// Legacy's first statement, ahead of every exit below: with the editor closed, any press it is asked about -- a
	// release, a sidebar pad, a note played on the keyboard screen, a clip view's edit pad -- rewrites the latch.
	if (!isUIOpen(&shellHostUI)) {
		kitGlobalFxLatch = currentSong != nullptr && kitAffectEntireNow(getCurrentClip());
	}
	// A release, a sidebar pad, and with SHIFT the automation view's interpolation and pad-selection pads: not
	// shortcuts, so the view keeps them (legacy's `ignoreAction` and its first return).
	const bool shift = Buttons::isShiftButtonPressed();
	if (!on || x >= kDisplayWidth || (shift && rootKeepsShortcutPad(x, y))) {
		return ActionResult::NOT_DEALT_WITH;
	}
	switch (shortcutPadGate(shift)) {
	case ShortcutPadGate::Forward:
		return ActionResult::NOT_DEALT_WITH;
	case ShortcutPadGate::Swallow:
		return ActionResult::DEALT_WITH; // outside the two shortcut modes: taken, and nothing happens
	case ShortcutPadGate::Shift:
	case ShortcutPadGate::Audition:
		break;
	}
	Clip* const clip = getCurrentClip();
	if (clip == nullptr) {
		return ActionResult::DEALT_WITH;
	}
	// Off a clip minder legacy reads the song view's table (`get_basic_shortcut_action`'s first arm) and `setup()`
	// edits the song, which is what a context naming no clip resolves, as the performance view's SELECT already opens
	// it.
	const bool clipMinder = rootUIIsClipMinderScreen();
	Clip* const editClip = clipMinder ? clip : nullptr;
	const bool performanceRoot = getRootUI() == &performanceView;
	const bool sessionRoot = getRootUI() == &sessionView;
	// With the guest open the press reads the latch as the last close and the presses since left it: a Settings menu
	// over a kit's keyboard screen edits the kit's own effects only when a pad was played since then.
	const menu::MenuContext ctx =
	    resolveContext(editClip, x & 1, performanceRoot, !clipMinder, sessionRoot, kitGlobalFxLatch);
	// `editingNothing()`: a kit with neither affect-entire nor a selected row has no table, so no item.
	if (!ctx.resolved && !ctx.is_drum) {
		return ActionResult::DEALT_WITH;
	}
	const auto px = static_cast<uint8_t>(x);
	const auto py = static_cast<uint8_t>(y);
	if (menu::shortcut_coming_soon(ctx.root, px, py)) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_UNIMPLEMENTED));
		return ActionResult::DEALT_WITH;
	}
	const menu::Node* const padNode = menu::shortcut_node(ctx.root, false, px, py);
	// With the menu current over the automation view, only a parameter's shortcut is taken; any other pad is swallowed
	// before the sequence-direction arm and before `setup()`.
	if (padNode != nullptr && getCurrentUI() == &shellHostUI && getRootUI() == &automationView
	    && !menu::is_automation_item(*padNode)) {
		return ActionResult::DEALT_WITH;
	}
	// `[audition pad] + [sequence direction pad]`: the note-row editor on PLAY DIRECTION (site 13's ported leg).
	if (clip->type == ClipType::INSTRUMENT && padNode == &menu::nodes::sequence_direction
	    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::HorizontalMenus) == RuntimeFeatureStateToggle::On
	    && instrumentClipView.getNumNoteRowsAuditioning() == 1) {
		(void)instrumentClipView.enterNoteRowEditor(/*onPlayDirection=*/true);
		return ActionResult::DEALT_WITH;
	}
	// Opens, re-roots the open guest, or says its refusal; a kit row with no note row is refused silently, as
	// `setup()` refuses it.
	(void)openShellMenuAtShortcut(editClip, x, y, performanceRoot, !clipMinder, sessionRoot, /*kitGlobalFx=*/false);
	return ActionResult::DEALT_WITH;
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

bool openShellNoteRowEditor(Clip* clip, bool onPlayDirection) {
	const bool guestOpen = isUIOpen(&shellHostUI);
	if (!guestOpen && !onPlayDirection) {
		return openShellMenu(clip, /*sourceIndex=*/0, /*performanceView=*/false, /*sessionOrArrangerRoot=*/false,
		                     /*sessionViewRoot=*/false, /*kitGlobalFx=*/false, ShellMenuNode::ContextRoot,
		                     NoteMode::NoteRowEditor);
	}
	if (currentSong == nullptr) {
		return false;
	}
	menu::MenuContext ctx = resolveContext(clip, 0, false, false, false, false);
	if (!ctx.resolved) {
		return false;
	}
	ctx.root = menu::MenuContext::Root::NoteRowEditor;
	const menu::Node* root = rootNodeFor(ctx);
	if (root == nullptr) {
		return false;
	}
	clearKitAffectEntireForNoteMode(ctx, clip);
	// `noteRowEditorRootMenu.focusChild(&sequenceDirectionMenu)`, then `setup()`: on the open editor, or opening it.
	if (!guestOpen) {
		return openGuest(*root, ctx, 0, &menu::nodes::sequence_direction);
	}
	deluge::scenes::menu_scene().reroot(*root, ctx, menu::nodes::sequence_direction);
	adoptShellMenuContext();
	shellHostUI.refreshRootFacts();
	return true;
}

void landShellMenuOnFileSelectorIfRecorderRefuses() {
	deluge::scenes::menu_scene().arm_refused_recorder_landing(menu::nodes::file0_selector);
}

} // namespace deluge::gui::ui
