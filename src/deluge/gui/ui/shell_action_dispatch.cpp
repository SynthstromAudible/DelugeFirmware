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

/// @file
/// @brief The action seam's consumer. See `shell_action_dispatch.h` for why it lives here and why
///        its caller may not gate it.

#include "gui/ui/shell_action_dispatch.h"

#include "definitions_cxx.hpp" // PerformanceEditingMode, StemExportType
#include "gui/context_menu/configure_song_macros.h"
#include "gui/context_menu/midi_learn_mode.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h" // openUI, getRootUI
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_root.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/screensaver.h" // Screensaver::settingsChanged, for reapplyLiveSettingsAfterReset
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "model/clip/audio_clip.h"
#include "model/model_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h" // getCurrentOutputType
#include "processing/stem_export/stem_export.h"
#include "storage/flash_storage.h"
#include "ui/scenes/audio_clip/audio_clip_scene.h"
#include "ui/scenes/menu/action.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/registry.h"
#include "ui/shell/scene.h" // SceneKind

#include <optional>

namespace deluge::gui::ui {

using deluge::ui::scenes::menu_scene;
using deluge::ui::scenes::menu::MenuAction;
using deluge::ui::scenes::menu::MenuActionRequest;
using deluge::ui::shell::SceneKind;

namespace {

/// @brief `EditingMode::selectButtonPress`
///        (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`), performed on the real
///        `performanceView`.
///
/// @param mode What `MenuActionRequest::arg` carried: a `PerformanceEditingMode`
///        (`MenuActionRequest::arg`).
///
/// @par What `DISABLED` does here, and why it is not an assert
///
/// **It returns, doing nothing** -- and the reason is that legacy's `DISABLED` arm has no meaning on this side of the
/// seam. `EditingMode::selectButtonPress`
/// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`) answers it with `return nullptr`,
/// which is `SelectChainFn`'s "go up a level" (`ui/scenes/menu/node.h`): a *navigation* answer to the select press. The
/// dispatcher runs after `MenuScene::after_render` has finished with that press, so there is no navigation left for it
/// to ask for.
///
/// It has also already happened. The port reproduces the whole of legacy's `DISABLED` arm on the
/// **producing** side: `PerformEditorItem::render` leaves `MenuTick::render_resp.entered` set so
/// `after_render`'s leaf arm ascends, and files no request at all
/// (`PerformEditorItem::render`). That is what `MenuActionRequest::arg`'s "never `DISABLED`" promise is made of, and
/// `tests/integration/ui/scenes/menu_actions_spec.cpp`'s "goes up a level on DISABLED and files nothing" example is
/// what pins it. So this branch is unreachable through every real path, and is defence in depth rather than behaviour
/// -- and, being unreachable, it is the one line here no example can redden.
///
/// Ignoring rather than asserting, deliberately: the only assert on the device is
/// `FREEZE_WITH_ERROR`, a hard hang, and hanging the instrument over a menu press would be a worse
/// answer than a press that does nothing. What must not happen is falling through -- the branch
/// below is an `if`/`else` exactly as legacy's is, so a `DISABLED` reaching it would take the `else`
/// that means `PARAM` and open the view in a mode nobody chose.
void openPerformanceView(PerformanceEditingMode mode) {
	if (mode == PerformanceEditingMode::DISABLED) {
		return;
	}

	// `EditingMode::selectButtonPress`
	// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`), transcribed branch for branch
	// rather than collapsed into one assignment, so the two writes stay legible against their source. What is owed here
	// begins one line INSIDE this `else if`, because the first arm of that same function is the `DISABLED` arm handled
	// above.
	if (mode == PerformanceEditingMode::VALUE) {
		performanceView.defaultEditingMode = true;
		performanceView.editingParam = false;
	}
	else { // PerformanceEditingMode::PARAM
		performanceView.defaultEditingMode = true;
		performanceView.editingParam = true;
	}

	// The reset block of `EditingMode::selectButtonPress`
	// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`), guard and reason included:
	// reset the view when the mode changes, but not in param-editing mode, "cause that would reset param assignments to
	// FX columns".
	if (!performanceView.editingParam) {
		alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		performanceView.resetPerformanceView(modelStack);
	}

	// The `openUI` call. `origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`'s
	// `return NO_NAVIGATION` -- stay on the menu page while the view opens over it -- is the producer's half and is
	// already ported: `PerformEditorItem::render` clears `render_resp.entered`.
	openUI(&performanceView);
}

} // namespace

/// @brief `soundEditor.exitCompletely()` (`Start::selectButtonPress` in
/// `origin/main:src/deluge/gui/menu_item/stem_export/start.h`), ported.
///
/// @par What main's first statement is, and why it cannot be transcribed
///
/// `SoundEditor::exitCompletely` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) ends the menu item's session
/// (`endScreen()`) and closes the menu UI (`close()`, which is `closeUI(this)`, `gui/ui/ui.cpp`). Under the guest
/// **the thing that is open is `ShellHostUI`, not `soundEditor`** -- `openMenu` opens this UI in `SoundEditor`'s
/// place and nothing on that path opens `soundEditor` at all.
///
/// So calling `soundEditor.exitCompletely()` here would not close the menu. `closeUI`'s search loop stops at `u >= 1`
/// (`gui/ui/ui.cpp`); given a UI that is not on the stack it falls out with `u == 0`. `closeUI` (`gui/ui/ui.cpp`)
/// returns there, so the guest would stay open over a menu whose session `endScreen()` had just ended. Main's `closeUI`
/// (`origin/main:src/deluge/gui/ui/ui.cpp`, "If UI not found, chaos") has no such return: it reads
/// `uiNavigationHierarchy[u - 1]` -- index **-1** -- and sets `numUIsOpen = 0`, after which `getRootUI()` answers
/// `nullptr` for the whole application.
///
/// @par What this does instead
///
/// **Take the menu scene off the guest's stack, and let `ShellHostUI::timerCallback` perform the one close it already
/// owns.** The menu already has an `exitCompletely()`: a short Back at its root is main's
/// `SoundEditor::goUpOneLevel` depth-0 arm, which *is* `exitCompletely()`, and `MenuScene::handle_event`'s Back arm
/// answers it with `Action{.kind = Action::Kind::Remove, .pop_to = SceneKind::FirstApp}`. `SceneStack::remove` and
/// not `pop`, for the reason recorded there: `pop` would take a `Working` animation or a `Popup` sitting over the
/// menu down with it, and "close the editor" should not mean that. This is that same statement, reached by a
/// different press, so the menu's two exits agree by construction.
///
/// Taking the menu off is the whole of it, because `ShellHostUI::timerCallback` closes the guest itself once nothing
/// stacked takes input (`gui/ui/shell_host_ui.cpp`), the only close path the guest has besides `exitUI`'s. Both
/// alternatives are broken:
///
///   * **`closeGuest()` from here, leaving the menu stacked.** The menu still takes input, so the tick's close does
///     not fire and `rearmTimer` runs. `rearmTimer` refuses when this guest is not the current UI, but that guard
///     was written for a sibling of this mistake and does not rescue it: unguarded, it armed `TimerName::UI_SPECIFIC`
///     for a UI that is no longer current, and `UITimerManager::routine` dispatches that timer to `getCurrentUI()`
///     (`gui/ui_timer_manager.cpp`), i.e. to the root view underneath -- the hazard `ShellHostUI::closeGuest`'s own
///     comment names. What still rules this alternative out is simpler: it closes the UI while leaving the menu on
///     the shell stack, so the next open would resume a menu session this one was supposed to have ended. That is not
///     `exitCompletely()`.
///   * **Take the menu off *and* `closeGuest()` from here.** Then the tick's close fires too and closes a second
///     time; the second `closeUI(&shellHostUI)` finds nothing to close.
///
/// @par The `shellHostUI` global, deliberately
///
/// The guest is a singleton -- one object, one open site (`openMenu`) -- and this seam's sibling reaches for it
/// the same way for the same kind of question: `menuCurrent` (`gui/ui/shell_guest_services.cpp`) reads
/// `deluge::gui::ui::shellRuntime().shell().stack()` and answers the guest's `getCurrentUI() == &soundEditor`.
/// `SceneStack::remove` is a documented no-op when no scene of that kind is on the stack -- it returns false having
/// touched nothing and flagged nothing -- so if this is ever reached with no menu up, it does nothing rather than
/// corrupting a stack it does not own.
///
/// @par Why this sits between two anonymous-namespace blocks instead of inside one
///
/// The pad-forwarding seam (`gui/ui/shell_pad_forward.cpp`) calls it too -- main's `SoundEditor::padAction` tail
/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is the same `exitCompletely()` statement this ports, reached by a
/// pad press rather than a stem-export selection. Internal linkage inside `namespace {` would make that impossible from
/// another translation unit, so this function alone is outside the anonymous namespace and declared in
/// `shell_action_dispatch.h`, the same move `stemExportTypeForRootUI` below makes for the same reason.
/// `startStemExport`'s call to this function, in the anonymous-namespace block that follows, still resolves
/// unqualified: this file includes its own header before either anonymous-namespace block opens.
void closePortedMenu() {
	(void)deluge::gui::ui::shellRuntime().shell().stack().remove(SceneKind::FirstApp);
}

namespace {

/// @brief `stem_export::Start::selectButtonPress`'s second and third statements
///        (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`), performed on the real root UI.
///
/// Transcribed branch for branch rather than collapsed into a lookup, so the four arms stay legible against their
/// source -- the same treatment `openPerformanceView` above gives
/// `origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`.
///
/// @par What `getRootUI()` answers here, which is the question the close order raises
///
/// `getRootUI()` reads `uiNavigationHierarchy[0]` and answers `nullptr` only when `numUIsOpen == 0`
/// (`gui/ui/ui.cpp`). **Neither menu is at index 0 -- given a root view is open**, which is
/// the precondition this whole argument stands on and is worth saying out loud: `openUI` writes
/// `uiNavigationHierarchy[numUIsOpen]` and increments, so with `numUIsOpen == 0` the
/// guest would land at index 0 and `getRootUI()` would answer the guest itself. `openMenu`
/// cannot get there -- it refuses on `currentSong == nullptr`
/// and every path that opens it runs from a root view -- but an argument whose precondition
/// is unstated is one nobody can re-check. With a root view open, both main's `soundEditor` and
/// this guest sit *above* it, never at it.
///
/// And `closeUI` cannot move index 0: its search stops at `u >= 1` (`gui/ui/ui.cpp`) and it
/// assigns `numUIsOpen = u`, so closing a UI it finds above the root leaves `[0]` untouched
/// and `numUIsOpen >= 1`. Its one re-entrant hook is `newUI->focusRegained()`, and no
/// `focusRegained` override reaches `changeRootUI`: its callers are `buttonAction`/`padAction` handlers, explicit
/// `goTo…`/`transitionTo…`/`gridTransitionTo…` helpers, `SessionView::finishedTransitioningHere`, `PadLEDs`
/// transition-completion routines, `ActionLogger::revertAction`, and the harness.
///
/// **So the answer is the same before the close and after it, in main and under the guest**, and
/// main's close-then-read order is not what picks the arm. What the order decides is only whether
/// there is a `[0]` left to read at all, which is why `closePortedMenu` above does not call
/// `closeUI` on anything.
///
/// @par The one ordering divergence, named rather than absorbed
///
/// Main closes and *then* exports. Here the removal is filed first but the guest is popped by
/// `ShellHostUI::timerCallback`, after this function has returned, so the UI closes after the export is started
/// rather than before. That is harmless: `StemExport::startStemExportProcess`
/// (`processing/stem_export/stem_export.cpp`) returns at once, having stopped playback, entered
/// `UI_MODE_STEM_EXPORT` and requested the app loop, and the export then runs a step at a time from
/// `StemExport::exportRoutine`, after the guest has closed.
///
/// `exitCompletely`'s last statement, `currentUIMode = UI_MODE_NONE`, is the guest's close's
/// (`ShellHostUI::closeGuest`), which runs after this arm has started the export. The close keeps the export's own
/// `UI_MODE_STEM_EXPORT` and drops every other bit, which is the value main's order (reset, then
/// `enterUIMode(UI_MODE_STEM_EXPORT)`, which keeps only the non-exclusive bits the reset already cleared) leaves.
void startStemExport() {
	// The first statement of `Start::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`) --
	// first, exactly as main has it.
	closePortedMenu();

	// The `getRootUI()` chain of `Start::selectButtonPress`
	// (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`). `startStemExportProcess` is one call rather than
	// four because the fan-out has already chosen; the `std::nullopt` return is that chain's own fall-through, where it
	// matches nothing and main starts nothing.
	if (const std::optional<StemExportType> type = stemExportTypeForRootUI()) {
		stemExport.startStemExportProcess(*type);
	}
}

} // namespace

std::optional<StemExportType> stemExportTypeForRootUI() {
	// The `getRootUI()` chain of `Start::selectButtonPress`
	// (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`), statement for statement. `getRootUI()` may be null
	// (`gui/ui/ui.cpp`) and main never dereferences it either -- the three tests below are pointer comparisons, so a
	// null root falls through to the same `std::nullopt` every other unnamed root does.
	RootUI* rootUI = getRootUI();
	if (rootUI == &arrangerView) {
		// The arranger arm. Left as main's `if`/`else` rather than a conditional expression: the
		// two arms are different exports, not two values of one.
		if (stemExport.exportMixdown) {
			return StemExportType::MIXDOWN;
		}
		else {
			return StemExportType::TRACK;
		}
	}
	else if (rootUI == &sessionView) {
		// The session arm.
		return StemExportType::CLIP;
	}
	else if (rootUI == &instrumentClipView && getCurrentOutputType() == OutputType::KIT) {
		// The kit arm. The `&&` order is main's and is load-bearing rather than stylistic:
		// `getCurrentOutputType()` dereferences `currentSong->getCurrentClip()->output`
		// (`model/song/song.cpp`), so it is only ever asked once the root is known to be a
		// clip view.
		return StemExportType::DRUM;
	}
	// The chain matched nothing: main returns `NO_NAVIGATION` having started no
	// export. There is no `else` in main and there is none here.
	return std::nullopt;
}

namespace {
void performMenuAction(const MenuActionRequest& req);
} // namespace

void discardShellActionsBeforeClose() {
	(void)menu_scene().take_action();
}

void dispatchShellActions() {
	// One read, unconditionally, and the read is what empties the slot -- `take_action()` returns
	// the request and clears it in the same call (`MenuScene::take_action`). So an early
	// return above this line would not be "nothing to do", it would be "the request stays in the
	// outbox until something else asks", which is the stale delivery the mailbox's own warning
	// describes (`MenuScene::take_action`'s warning).
	const MenuActionRequest req = menu_scene().take_action();
	if (!req.engaged()) {
		return;
	}
	performMenuAction(req);
}

namespace {
void performMenuAction(const MenuActionRequest& req) {
	switch (req.action) {
	case MenuAction::None:
		// Unreachable: `MenuActionRequest::engaged()` IS `action != None`, so the guard
		// above has already returned. Spelled out anyway because the alternative is a `default:`,
		// and a `default:` here would silently absorb any enumerator added later -- see the
		// header's note on `-Werror=switch`.
		break;

	case MenuAction::OpenPerformanceView:
		// `arg` is a `PerformanceEditingMode` and is never `DISABLED` (`MenuActionRequest::arg`);
		// the helper says what it does with one anyway, and why.
		openPerformanceView(static_cast<PerformanceEditingMode>(req.arg));
		break;

	case MenuAction::StartStemExport:
		// All of `Start::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`), which
		// `MenuAction::StartStemExport` says this arm performs whole: the `soundEditor.exitCompletely()` that comes
		// first AND the `getRootUI()` fan-out that picks between the four `StemExportType`s. The helpers say what each
		// of those two means under the guest, where the open UI is `ShellHostUI` rather than `soundEditor` -- the first
		// is performed in equivalent form rather than verbatim, and that is this arm's one divergence.
		startStemExport();
		break;

	case MenuAction::SetClipLengthToSampleLength:
		// `audio_clip::SetClipLengthEqualToSampleLength::selectButtonPress`'s one statement
		// (`origin/main:src/deluge/gui/menu_item/audio_clip/set_clip_length_equal_to_sample_length.h`, the call to
		// `audioClipView.setClipLengthEqualToSampleLength()`). The item that files it hangs off the audio-clip root,
		// which `menu::context_for` picks only for an audio clip, so the current clip is one. The automation view over
		// an audio clip makes the edit with its own scroll, zoom and popups; the audio clip scene, its.
		if (getRootUI() == &automationView) {
			if (::Clip* clip = getCurrentClip(); clip != nullptr && clip->type == ClipType::AUDIO) {
				automationView.setAudioClipLengthToSampleLength(*static_cast<AudioClip*>(clip));
			}
		}
		else {
			deluge::ui::scenes::audio_clip::audio_clip_scene().set_clip_length_equal_to_sample_length();
			ShellRuntime::armTickSoon();
		}
		break;

	case MenuAction::OpenSongMidiLearn:
		// `song::MidiLearn::selectButtonPress`'s two statements
		// (`origin/main:src/deluge/gui/menu_item/song/midi_learn.h`), in legacy's order.
		// **The `bool` is discarded here because it is discarded there**, and this is the one of the two whose answer
		// can be false -- what that means, and what it costs, is the header's "@par The two context menus open even
		// when setupAndCheckAvailability() says no".
		(void)gui::context_menu::midiLearnMode.setupAndCheckAvailability();
		openUI(&gui::context_menu::midiLearnMode);
		break;

	case MenuAction::OpenSongMacros:
		// `song::ConfigureMacros::selectButtonPress`'s two statements
		// (`origin/main:src/deluge/gui/menu_item/song/configure_macros.h`), in legacy's order. The discarded `bool`
		// here can only ever be `true` (`ConfigureSongMacros::setupAndCheckAvailability` in
		// `gui/context_menu/configure_song_macros.cpp`, its `return true`), so the discard costs nothing at this site
		// -- see the header for the site where it does.
		(void)gui::context_menu::configureSongMacros.setupAndCheckAvailability();
		openUI(&gui::context_menu::configureSongMacros);
		break;
	}
}
} // namespace

bool hostInhibitsScreensaver() {
	return deluge::hid::display::OLED::isPermanentPopupPresent() || stemExport.processStarted;
}

void saveSettingsBeforeClose() {
	// main's `SoundEditor::inSettingsMenu` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) reads the navigation
	// record's root; the menu's is its context's, which outlives the stack on both close paths
	// (`exitNoteModeBeforeClose`'s note).
	if (deluge::ui::scenes::menu_scene().menu_context().root != deluge::ui::scenes::menu::MenuContext::Root::Settings) {
		return;
	}
	uiTimerManager.unsetTimer(TimerName::BACK_MENU_EXIT);
	display->displayLoadingAnimationText("Saving settings");
	FlashStorage::writeSettings();
	MIDIDeviceManager::writeDevicesToFile();
	runtimeFeatureSettings.writeSettingsToFile();
	midiFollow.writeDefaultsToFile();
	display->removeWorkingAnimation();
}

void pushShellHighlighting() {
	const MenuHighlighting stored = FlashStorage::accessibilityMenuHighlighting;
	deluge::hid::display::oled_canvas::Canvas::menuHighlightingInverts = stored != MenuHighlighting::NO_INVERSION;
	deluge::ui::widgets::core::Highlighting h = deluge::ui::widgets::core::Highlighting::PartialInversion;
	if (stored == MenuHighlighting::FULL_INVERSION) {
		h = deluge::ui::widgets::core::Highlighting::FullInversion;
	}
	else if (stored == MenuHighlighting::NO_INVERSION) {
		h = deluge::ui::widgets::core::Highlighting::NoInversion;
	}
	deluge::gui::ui::shellRuntime().shell().set_highlighting(h);
}

void pushShellRoundedCorners() {
	const bool rounded = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners);
	deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled = rounded;
	deluge::gui::ui::shellRuntime().shell().set_rounded(rounded);
}

void reapplyLiveSettingsAfterReset() {
	pushShellHighlighting();
	deluge::hid::display::Screensaver::settingsChanged();
	// The setting alone: a latch made before the reset stays, as `Buttons`' does in main.
	deluge::gui::ui::shellRuntime().shell().lowering().adopt_shift_is_sticky(
	    runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ShiftIsSticky));
	menu_scene().set_horizontal_menus(runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus));
}

} // namespace deluge::gui::ui
