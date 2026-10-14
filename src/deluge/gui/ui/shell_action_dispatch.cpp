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
#include "gui/ui/sound_editor.h" // soundEditor.setShortcutsVersion -- the SetShortcutsVersion arm
#include "gui/ui/ui.h"           // openUI, getRootUI
#include "gui/views/arranger_view.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/display/screensaver.h" // Screensaver::settingsChanged -- the ScreensaverSettingsChanged arm
#include "model/model_stack.h"
#include "model/song/song.h" // getCurrentOutputType
#include "processing/stem_export/stem_export.h"
#include "scenes/menu/action.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"
#include "shell/scene.h" // SceneKind

#include <optional>

namespace deluge::gui::ui {

using deluge::scenes::menu_scene;
using deluge::scenes::menu::MenuAction;
using deluge::scenes::menu::MenuActionRequest;
using deluge::ui::shell::SceneKind;

namespace {

/// @brief `EditingMode::selectButtonPress`
///        (`gui/menu_item/performance_session_view/editing_mode.h:50-97`), performed on the real
///        `performanceView`.
///
/// @param mode What `MenuActionRequest::arg` carried: a `PerformanceEditingMode`
///        (`scenes/menu/action.h:113-119`).
///
/// @par What `DISABLED` does here, and why it is not an assert
///
/// **It returns, doing nothing** -- and the reason is that legacy's `DISABLED` arm has no meaning on
/// this side of the seam. `editing_mode.h:51-53` answers it with `return nullptr`, which is
/// `SelectChainFn`'s "go up a level" (`scenes/menu/node.h`): a *navigation* answer to the select
/// press. The dispatcher runs after `MenuScene::after_render` has finished with that press, so there
/// is no navigation left for it to ask for.
///
/// It has also already happened. The port reproduces the whole of legacy's `DISABLED` arm on the
/// **producing** side: `PerformEditorItem::render` leaves `MenuState::render_resp.entered` set so
/// `after_render`'s leaf arm ascends, and files no request at all
/// (`scenes/menu/nodes/actions.cpp:153-158`). That is what `scenes/menu/action.h:113-114`'s "never
/// `DISABLED`" promise is made of, and `sim/scenes_spec/menu_actions_spec.cpp`'s "goes up a level on
/// DISABLED and files nothing" example is what pins it. So this branch is unreachable through every
/// real path, and is defence in depth rather than behaviour -- and, being unreachable, it is the one
/// line here no example can redden.
///
/// Ignoring rather than asserting, deliberately: the only assert this tree has on the device is
/// `FREEZE_WITH_ERROR`, a hard hang, and hanging the instrument over a menu press would be a worse
/// answer than a press that does nothing. What must not happen is falling through -- the branch
/// below is an `if`/`else` exactly as legacy's is, so a `DISABLED` reaching it would take the `else`
/// that means `PARAM` and open the view in a mode nobody chose.
void openPerformanceView(PerformanceEditingMode mode) {
	if (mode == PerformanceEditingMode::DISABLED) {
		return;
	}

	// `editing_mode.h:55-62`, transcribed branch for branch rather than collapsed into one
	// assignment, so the two writes stay legible against their source. (`scenes/menu/action.h:116`
	// scopes what is owed here to `editing_mode.h:56-95`, which begins one line INSIDE this
	// `else if` because `editing_mode.h:51-53` is the `DISABLED` arm handled above.)
	if (mode == PerformanceEditingMode::VALUE) {
		performanceView.defaultEditingMode = true;
		performanceView.editingParam = false;
	}
	else { // PerformanceEditingMode::PARAM
		performanceView.defaultEditingMode = true;
		performanceView.editingParam = true;
	}

	// `editing_mode.h:86-93`, guard and reason included: reset the view when the mode changes, but
	// not in param-editing mode, "cause that would reset param assignments to FX columns" (`:88`).
	if (!performanceView.editingParam) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack =
		    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
		performanceView.resetPerformanceView(modelStack);
	}

	// `editing_mode.h:95`. `:96`'s `return NO_NAVIGATION` -- stay on the menu page while the view
	// opens over it -- is the producer's half and is already ported: `PerformEditorItem::render`
	// clears `render_resp.entered` (`scenes/menu/nodes/actions.cpp:167`).
	openUI(&performanceView);
}

} // namespace

/// @brief `soundEditor.exitCompletely()` (`gui/menu_item/stem_export/start.h:32`), ported.
///
/// @par What legacy's first statement is, and why it cannot be transcribed
///
/// `SoundEditor::exitCompletely` (`gui/ui/sound_editor.cpp:731-772`) ends the menu item's session
/// (`endScreen()`, `:759`) and closes the menu UI (`close()`, `:761`, which is `closeUI(this)`,
/// `gui/ui/ui.cpp:57-59`). Under the guest **the thing that is open is `ShellHostUI`, not
/// `soundEditor`** -- `openShellMenu` opens this UI in `SoundEditor`'s place
/// (`gui/ui/shell_menu_entry.cpp:208`) and nothing on that path opens `soundEditor` at all.
///
/// So calling `soundEditor.exitCompletely()` here would not be a harmless no-op. `closeUI`'s search
/// loop stops at `u >= 1` (`gui/ui/ui.cpp:183`); given a UI that is not on the stack it falls out
/// with `u == 0`, reads `uiNavigationHierarchy[u - 1]` -- index **-1** -- at `:194` and sets
/// `numUIsOpen = 0` at `:195`. After that `getRootUI()` answers `nullptr` for the whole application
/// (`:145-150`) and `getCurrentUI()` answers the `lastUIBeforeNullifying` residue (`:136-140`).
/// That is F1 task 11's wedge, one turn worse: its own header says "If UI not found, chaos"
/// (`:176`).
///
/// @par What this does instead, and why it is not a new idea
///
/// **Take the menu scene off the guest's stack, and let `ShellHostUI::timerCallback` perform the one
/// close it already owns.** The port already has an `exitCompletely()`: a short Back at the menu's
/// root is legacy's `SoundEditor::goUpOneLevel` depth-0 arm, which *is* `exitCompletely()`
/// (`gui/ui/sound_editor.cpp:710-717`, the call at `:716`), and `MenuScene::handle` ports it as
/// `Action{.kind = Action::Kind::Remove, .pop_to = SceneKind::FirstApp}`
/// (`scenes/menu/menu_scene.cpp:2057-2071`). `SceneStack::remove` and not `pop`, for the reason
/// recorded there (`:2044-2056`): `pop` would take a `Working` animation or a `Popup` sitting over
/// the menu down with it, and "close the editor" should not mean that. This is that same statement,
/// reached by a different press, so the two exits of the ported menu agree by construction.
///
/// Emptying the stack is the whole of it, because `ShellHostUI::timerCallback` closes the guest
/// itself the moment the stack is empty (`gui/ui/shell_host_ui.cpp:556-559`) -- F1 task 11's fix,
/// and the only close path the guest has besides `exitUI`'s (`:1127`). The two alternatives were
/// both tried on paper and both are broken:
///
///   * **`closeGuest()` from here, without emptying the stack.** The stack is still occupied, so
///     `:556` does not fire and `rearmTimer` at `:565` runs. **The timer half of this is now
///     guarded** -- `rearmTimer` refuses when this guest is not the current UI (`:630`) --
///     but read the rest, because the guard was added for a sibling of this exact mistake and the
///     bullet is why it exists. Unguarded it armed `TimerName::UI_SPECIFIC` for a UI that is no
///     longer current, and `UITimerManager::routine` dispatches that timer to `getCurrentUI()`
///     (`gui/ui_timer_manager.cpp:142-148`), i.e. to the root view underneath -- the hazard
///     `ShellHostUI::closeGuest`'s own comment names (`:1110-1114`). What survives the guard and
///     still rules this alternative out is simpler: it closes the UI while leaving the ported menu
///     on the shell stack, so the next open would resume a menu session this one was supposed to
///     have ended. That is not `exitCompletely()`.
///   * **Empty the stack *and* `closeGuest()` from here.** Then `:555` fires too and closes a second
///     time, and the second `closeUI(&shellHostUI)` is the index `-1` read above.
///
/// @par The `shellHostUI` global, deliberately
///
/// The guest is a singleton -- one object, one open site (`gui/ui/shell_menu_entry.cpp:208`) -- and
/// this seam's sibling reaches for it the same way for the same kind of question: `menuIsFrontmost`
/// (`gui/ui/shell_guest_services.cpp:190-192`) reads `shellHostUI.shell().stack()` and calls itself
/// "the port of `getCurrentUI() == &soundEditor`". `SceneStack::remove` is a documented no-op when
/// no scene of that kind is on the stack -- it returns false having touched nothing and flagged
/// nothing (`shell/scene_stack.cpp:104-106`) -- so if this is ever reached with no ported menu up,
/// it does nothing rather than corrupting a stack it does not own.
///
/// @par Why this sits between two anonymous-namespace blocks instead of inside one
///
/// It did, until the pad-forwarding seam (`gui/ui/shell_pad_forward.cpp`) needed to call it too --
/// `SoundEditor::padAction`'s tail (`gui/ui/sound_editor.cpp:1545`) is the same `exitCompletely()`
/// statement this ports, reached by a pad press rather than a stem-export selection. Internal
/// linkage inside `namespace {` made that impossible from another translation unit, so this
/// function alone was pulled out and declared in `shell_action_dispatch.h`, the same move
/// `stemExportTypeForRootUI` below already made for the same reason. `startStemExport`'s call to
/// this function, in the anonymous-namespace block that follows, still resolves unqualified: this
/// file includes its own header (`:22`) before either anonymous-namespace block opens.
void closePortedMenu() {
	(void)shellHostUI.shell().stack().remove(SceneKind::FirstApp);
}

namespace {

/// @brief `stem_export::Start::selectButtonPress`'s second and third statements
///        (`gui/menu_item/stem_export/start.h:33-47`), performed on the real root UI.
///
/// Transcribed branch for branch rather than collapsed into a lookup, so the four arms stay legible
/// against their source -- the same treatment `openPerformanceView` above gives `editing_mode.h`.
///
/// @par What `getRootUI()` answers here, which is the question the close order raises
///
/// `getRootUI()` reads `uiNavigationHierarchy[0]` and answers `nullptr` only when `numUIsOpen == 0`
/// (`gui/ui/ui.cpp:145-150`). **Neither menu is at index 0 -- given a root view is open**, which is
/// the precondition this whole argument stands on and is worth saying out loud: `openUI` writes
/// `uiNavigationHierarchy[numUIsOpen]` and increments (`:227-231`), so with `numUIsOpen == 0` the
/// guest would land at index 0 and `getRootUI()` would answer the guest itself. `openShellMenu`
/// cannot get there -- it refuses on `currentSong == nullptr` (`gui/ui/shell_menu_entry.cpp:111-113`)
/// and every path that opens it (`:208`) runs from a root view -- but an argument whose precondition
/// is unstated is one nobody can re-check. With a root view open, both legacy's `soundEditor` and
/// this guest sit *above* it, never at it.
///
/// And `closeUI` cannot move index 0: its search stops at `u >= 1` (`gui/ui/ui.cpp:183`) and it
/// assigns `numUIsOpen = u` (`:195`), so closing a UI it finds above the root leaves `[0]` untouched
/// and `numUIsOpen >= 1`. Its one re-entrant hook is `newUI->focusRegained()` (`:199`), and no
/// `focusRegained` override reaches `changeRootUI`: of the 56 call sites in `src/`, every one is a
/// `buttonAction`/`padAction`, an explicit `goTo…`/`transitionTo…`/`gridTransitionTo…` helper,
/// `SessionView::finishedTransitioningHere`, a `PadLEDs` transition-completion routine,
/// `ActionLogger::revertAction`, or the harness -- and none of the eight files holding one has a
/// brace-matched `::focusRegained` body containing any of them.
///
/// **So the answer is the same before the close and after it, in legacy and under the guest**, and
/// legacy's close-then-read order is not what picks the arm. What the order decides is only whether
/// there is a `[0]` left to read at all, which is why `closePortedMenu` above does not call
/// `closeUI` on anything.
///
/// @par The one ordering divergence, named rather than absorbed
///
/// Legacy closes and *then* exports. Here the removal is filed first but the guest is popped by
/// `timerCallback` (`gui/ui/shell_host_ui.cpp:556-559`), after this function has returned, so on a
/// BSP where `deluge::storage::Owner::run` is inline the export body runs *between* the two
/// (`OSLikeStuff/task_scheduler/task_scheduler_c_api.cpp:101-104`). On the shipping Embassy BSP
/// `startStemExportProcess` only queues (`processing/stem_export/stem_export.cpp:87-96`,
/// `storage/owner.h:28-31`), so on the device both orders are the same sequence: stack emptied,
/// export queued, guest closed, export runs. The divergence is real only on the cooperative BSPs,
/// where no stem export is driven from a ported menu.
///
/// One more of `exitCompletely`'s tail has no counterpart on the guest's close path and is left
/// unpaid here: `currentUIMode = UI_MODE_NONE` (`gui/ui/sound_editor.cpp:770`). **The export's own
/// mode engages either way, and that is the whole of what is safe to claim.**
/// `UI_MODE_STEM_EXPORT` is `64` (`gui/ui/ui.h:78`), which is not `> EXCLUSIVE_UI_MODES_MASK` (`255`,
/// `:81`), so `runStemExportProcess`'s `enterUIMode(UI_MODE_STEM_EXPORT)` (`stem_export.cpp:115`)
/// takes `enterUIMode`'s else branch, `currentUIMode = (currentUIMode & ~255) | 64`
/// (`gui/ui/ui.cpp:458-459`) -- it never refuses.
///
/// But that clears only the **exclusive** bits. The five non-exclusive modes (`gui/ui/ui.h:85-89`:
/// `QUANTIZE`, `STUTTERING`, `HORIZONTAL_SCROLL`, `AUDITIONING`,
/// `HOLDING_HORIZONTAL_ENCODER_BUTTON`, all bits 27-31) survive it, where legacy's
/// `UI_MODE_NONE` is `0` (`:34`) and clears every bit. So a menu entered with, say,
/// `UI_MODE_AUDITIONING` latched leaves that flag set through the whole export under the guest and
/// not under legacy. The residue is specifically those five flags; it belongs to the guest's close
/// path rather than to this arm, and is reported rather than fixed from here.
void startStemExport() {
	// `start.h:32` -- first, exactly as legacy has it.
	closePortedMenu();

	// `start.h:33-47`. `startStemExportProcess` is `start.h`'s `:36`/`:39`/`:43`/`:46`, one call
	// rather than four because the fan-out has already chosen; the `std::nullopt` return is
	// `start.h`'s own fall-through, where the chain matches nothing and legacy starts nothing.
	if (const std::optional<StemExportType> type = stemExportTypeForRootUI()) {
		stemExport.startStemExportProcess(*type);
	}
}

} // namespace

std::optional<StemExportType> stemExportTypeForRootUI() {
	// `start.h:33-47`, statement for statement. `getRootUI()` may be null (`gui/ui/ui.cpp:145-150`)
	// and legacy never dereferences it either -- the three tests below are pointer comparisons, so a
	// null root falls through to the same `std::nullopt` every other unnamed root does.
	RootUI* rootUI = getRootUI();
	if (rootUI == &arrangerView) {
		// `start.h:35-40`. Left as legacy's `if`/`else` rather than a conditional expression: the
		// two arms are different exports, not two values of one.
		if (stemExport.exportMixdown) {
			return StemExportType::MIXDOWN;
		}
		else {
			return StemExportType::TRACK;
		}
	}
	else if (rootUI == &sessionView) {
		// `start.h:42-44`.
		return StemExportType::CLIP;
	}
	else if (rootUI == &instrumentClipView && getCurrentOutputType() == OutputType::KIT) {
		// `start.h:45-47`. The `&&` order is legacy's and is load-bearing rather than stylistic:
		// `getCurrentOutputType()` dereferences `currentSong->getCurrentClip()->output`
		// (`model/song/song.cpp:135-137`), so it is only ever asked once the root is known to be a
		// clip view.
		return StemExportType::DRUM;
	}
	// `start.h:48` -- the chain matched nothing, legacy returns `NO_NAVIGATION` having started no
	// export. There is no `else` in legacy and there is none here.
	return std::nullopt;
}

void dispatchShellActions() {
	// One read, unconditionally, and the read is what empties the slot -- `take_action()` returns
	// the request and clears it in the same call (`scenes/menu/menu_scene.h:702-703`). So an early
	// return above this line would not be "nothing to do", it would be "the request stays in the
	// outbox until something else asks", which is the stale delivery the mailbox's own warning
	// describes (`:672-677`).
	const MenuActionRequest req = menu_scene().take_action();
	if (!req.engaged()) {
		return;
	}

	switch (req.action) {
	case MenuAction::None:
		// Unreachable: `engaged()` IS `action != None` (`scenes/menu/action.h:208`), so the guard
		// above has already returned. Spelled out anyway because the alternative is a `default:`,
		// and a `default:` here would silently absorb the enumerators later tasks add -- see the
		// header's note on `-Werror=switch`.
		break;

	case MenuAction::OpenPerformanceView:
		// `arg` is a `PerformanceEditingMode` and is never `DISABLED` (`scenes/menu/action.h:113-119`);
		// the helper says what it does with one anyway, and why.
		openPerformanceView(static_cast<PerformanceEditingMode>(req.arg));
		break;

	case MenuAction::StartStemExport:
		// All of `stem_export/start.h:31-49`, which `scenes/menu/action.h:122-131` says this arm
		// performs whole: the `soundEditor.exitCompletely()` that comes first AND the
		// `getRootUI()` fan-out that picks between the four `StemExportType`s. The helpers say what
		// each of those two means under the guest, where the open UI is `ShellHostUI` rather than
		// `soundEditor` -- the first is performed in equivalent form rather than verbatim, and that
		// is this arm's one divergence.
		startStemExport();
		break;

	case MenuAction::SetClipLengthToSampleLength:
		// `audio_clip::SetClipLengthEqualToSampleLength::selectButtonPress`'s one statement
		// (`gui/menu_item/audio_clip/set_clip_length_equal_to_sample_length.h:26-29`, the call at
		// `:27`), on the real view. No helper: there is no argument to choose and no branch to
		// transcribe.
		//
		// **What the callee assumes, and why no null test is added here.**
		// `AudioClipView::setClipLengthEqualToSampleLength` (`gui/views/audio_clip_view.h:50`,
		// defined `gui/views/audio_clip_view.cpp:758-769`) opens with
		// `AudioClip& audioClip = *getCurrentAudioClip();` (`:759`), and `getCurrentAudioClip()`
		// answers `nullptr` whenever `currentSong->getCurrentClip()` is not an `AudioClip`
		// (`model/song/song.cpp:98-104`); the next line reads `audioClip.guide` (`:760`) through it.
		// Neither of legacy's two callers tests it either. `AudioClipView::buttonAction`'s
		// X_ENC + Y_ENC chord (`audio_clip_view.cpp:384-393`, the call at `:391`) relies on
		// `audioClipView` being the open view; `SetClipLengthEqualToSampleLength::selectButtonPress`
		// relies on the item being reachable only under the audio-clip root, which
		// `SoundEditor::setup` picks from its `clip` argument's type (`gui/ui/sound_editor.cpp:1773`'s
		// `if`, the `else` that assigns `soundEditorRootMenuAudioClip` at `:1811-1813`).
		//
		// **The port inherits that reliance exactly, including its gap.** `set_clip_length` hangs off
		// `audio_clip_actions`, which hangs off `root_audio_clip` (`scenes/menu/nodes/roots.h:430-433`,
		// `:484-488`); `menu::context_for` picks `Root::AudioClip` on the same `clip->type` test
		// (`scenes/menu/context.cpp:330-363`, the assignment at `:361`, reached because `ClipType` has
		// only `INSTRUMENT` and `AUDIO`, `definitions_cxx.hpp:1095-1098`); and `openShellMenu` is the
		// one opener (`gui/ui/shell_menu_entry.cpp:109-209`, the context at `:120-121`). The gap both
		// sides share is that the argument and `currentSong->getCurrentClip()` are two different
		// reads: legacy resolves the root from the `clip` it was handed and the callee resolves the
		// clip from the song. Closing that here -- with a null test, or by reading the argument --
		// would be behaviour legacy does not have, which the no-new-UI/UX rule forbids. It is
		// reported instead.
		audioClipView.setClipLengthEqualToSampleLength();
		break;

	case MenuAction::OpenSongMidiLearn:
		// `song::MidiLearn::selectButtonPress`'s two statements (`gui/menu_item/song/midi_learn.h:27-31`,
		// the calls at `:28` and `:29`), in legacy's order. **The `bool` is discarded here because it
		// is discarded there**, and this is the one of the two whose answer can be false -- what that
		// means, and what it costs, is the header's "@par The two context menus open even when
		// setupAndCheckAvailability() says no".
		(void)gui::context_menu::midiLearnMode.setupAndCheckAvailability();
		openUI(&gui::context_menu::midiLearnMode);
		break;

	case MenuAction::OpenSongMacros:
		// `song::ConfigureMacros::selectButtonPress`'s two statements
		// (`gui/menu_item/song/configure_macros.h:27-31`, the calls at `:28` and `:29`), in legacy's
		// order. The discarded `bool` here can only ever be `true`
		// (`gui/context_menu/configure_song_macros.cpp:48-51`, the `return true` at `:50`), so the
		// discard costs nothing at this site -- see the header for the site where it does.
		(void)gui::context_menu::configureSongMacros.setupAndCheckAvailability();
		openUI(&gui::context_menu::configureSongMacros);
		break;

	case MenuAction::SetShortcutsVersion:
		// `shortcuts::Version::writeCurrentValue`'s one statement
		// (`gui/menu_item/shortcuts/version.h:27`), on the real `soundEditor`. No helper: there is
		// no branch to transcribe, and the value is legacy's own -- `writeCurrentValue` passes
		// `this->getValue()` through unnormalised and so does the port, so the two-way mapping
		// between the enum index and the setting is decided in exactly one place,
		// `SoundEditor::setShortcutsVersion`'s own `switch` (`gui/ui/sound_editor.cpp:153-180`,
		// whose `default` arm at `:177-179` is "Uses defaults").
		//
		// **This is the arm that reaches flash**, which no other arm in this switch does:
		// `SoundEditor::shortcutsVersion` (`gui/ui/sound_editor.cpp:114`) is what
		// `FlashStorage::writeSettings` stores at byte 60 (`storage/flash_storage.cpp:982`) and
		// `readSettings` hands back at boot (`:568`). Before F2 task 6 the ported settings item
		// wrote only `deluge::scenes::menu`'s mirror, so the layout a user chose from it survived
		// until power-off and no further.
		//
		// **It does not loop**, and the reason is worth having here as well as at the enumerator,
		// because this is the call that would close the loop if it ever did.
		// `setShortcutsVersion` mirrors back into the port -- `:149-151` calls
		// `deluge::scenes::menu::set_shortcuts_version`, F1 task 8's fix. That callee assigns one
		// file-scope variable in `scenes/menu/shortcuts.cpp:457-459`. It is **not**
		// `nodes::set_shortcuts_version`, the item hook that filed this request; it writes no
		// `MenuState`, files nothing, and calls nothing. One lap, then it stops.
		//
		// The mirror write it makes is also the same value the hook already wrote, by the same rule
		// spelled the same way on both sides (`V1` for `SHORTCUTS_VERSION_1`, `V3` otherwise), so
		// the lap is idempotent rather than merely finite.
		soundEditor.setShortcutsVersion(static_cast<int32_t>(req.arg));
		break;

	case MenuAction::ScreensaverSettingsChanged:
		// The second statement of `ScreensaverModeMenu::writeCurrentValue` and of
		// `ScreensaverTimeout::writeCurrentValue` (`gui/menu_item/defaults/screensaver.h:40`,
		// `:62`), which is the same statement in both. No helper and no payload: legacy's notify is
		// a zero-parameter static that re-reads `FlashStorage::screensaverMode` and
		// `screensaverTimeoutMinutes` for itself, and the ported hooks have already written both
		// (`scenes/menu/nodes/settings.cpp`'s two setters).
		//
		// **This arm is why the two screensavers agree.** `Screensaver::settingsChanged()`
		// (`hid/display/screensaver.cpp:200-241`) is a *pair* of reconfigurations: it recomputes the
		// idle period and pushes it into the guest's `shell::Screensaver` through
		// `shellHostUI.shell().set_screensaver_idle_ms(...)` (`:225-228`), then wakes the legacy
		// screensaver if it is showing (`:230-233`) and either cancels its timer for
		// `ScreensaverMode::OFF` (`:235-238`) or re-arms it from now (`:240`). One call, both
		// halves -- which is what let this task be one enumerator rather than two arms and a
		// duplicated formula.
		//
		// **The `shellHostUI` it names is the global guest**, not "whichever shell asked". That is
		// correct here for the same reason it is correct at every other caller: there is exactly one
		// guest in the firmware, and this dispatcher runs from that guest's own
		// `ShellHostUI::timerCallback`. It is only worth stating because it makes the write
		// unconditional on the guest being open -- see the header's note on why nothing here is
		// gated.
		deluge::hid::display::Screensaver::settingsChanged();
		break;
	}
}

} // namespace deluge::gui::ui
