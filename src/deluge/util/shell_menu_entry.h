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
/// @brief The toggle, and the one call that opens the ported menu in `shellHostUI`'s place.
///
/// Spec F §2.1's *first tenant*: every place legacy enters `soundEditor` today becomes
/// `openUI(&shellHostUI)` plus a `Menu{node}` push, **behind
/// `RuntimeFeatureSettingType::ShellMenu`**. There are fourteen such places
/// (`grep -rn "openUI(&soundEditor)" src/deluge`); eight of them route here and six stay on the
/// legacy path because the node they want has no port. The table is in
/// `gui/ui/shell_menu_entry.cpp`.
///
/// @par Why this is its own translation unit
///
/// The same reason `gui/ui/shell_guest_services.h` gives, one layer out: the eight call sites are
/// ordinary legacy views, and making each of them name `deluge::scenes::menu::context_for`,
/// `MenuScene::roots()` and `SceneId` would put `src/scenes` includes into eight `gui/views/` and
/// `model/clip/` translation units for four lines of work apiece. `ShellMenuNode` below exists for
/// exactly that reason: it is how a caller names a *specific ported node* without naming the
/// `deluge::scenes::menu::nodes` namespace.
///
/// @par Why the header is in `util/` and the implementation in `gui/ui/`
///
/// **One of the eight call sites is `model/clip/instrument_clip_minder.cpp`**, and that file is
/// inside `check_layer_rules.py`'s `NON_UI` glob -- so a `gui/ui/` header there would raise the
/// `model-includes-ui` ratchet, which plan F1's global constraint 8 says may only shrink. Keeping
/// the count level by *displacing* another `gui/` include (the first version of this file did that,
/// with a dead one) is worse than it looks: it trades a dead coupling for a live one and leaves the
/// number saying nothing changed.
///
/// So the declaration lives outside `gui/` and the implementation stays inside it, which is
/// `util/notify.h`'s shape exactly -- a light header any layer may include, with the UI half in
/// `gui/ui/notify_drain.cpp`.
///
/// **The load-bearing fact, and it is not the precedent:** `src/deluge/util` is *itself* inside
/// `check_layer_rules.py`'s `NON_UI` glob (`src/deluge/{model,playback,processing,storage,io,
/// modulation,memory,sync,util,dsp}/**`). So this header is judged by the very rule the move is
/// about, and it passes on its own terms -- it includes nothing but `<cstdint>` and forward-declares
/// `class Clip`. That is what makes this a real move of the declaration out of the UI layer rather
/// than a rename that puts a `gui/` header somewhere the ratchet cannot see it. Every `src/scenes`
/// and `gui/` dependency stays in `gui/ui/shell_menu_entry.cpp`, where the rule would judge it if it
/// moved.
///
/// @note Moving a header to satisfy one gate can walk it out of another, and that happened here:
///       `check_citations.py`'s `SCAN_ROOTS` covered `src/deluge/gui/ui` and not `src/deluge/util`,
///       so this file's four citations left the citation gate at the moment they satisfied the layer
///       gate. `src/deluge/util` was added to `SCAN_ROOTS` in the same round (measured first: 0 new
///       flags), and the widening was probed rather than assumed -- a past-EOF citation planted here
///       is reported at `src/deluge/util/shell_menu_entry.h`. **This file is gate-read.**
///
/// @par What works under the toggle, and what does not -- read this before turning it on
///
/// The toggle is Off by default, so none of the following is a regression. Turned on, it is what a
/// user gets, and none of it is discoverable from a green test suite:
///
///  1. ~~**Four ported menu items do nothing.**~~ **CLOSED as of F2 task 4.** The outbox is polled
///     as of F2 task 1 -- `gui::ui::dispatchShellActions` drains it once per guest tick -- and all
///     five arms now have bodies (tasks 2, 3 and 4), so `perform_editor`, `start_stem_export`,
///     `set_clip_length`, `song_midi_learn` and `configure_song_macros` all do under the toggle what
///     their legacy items do. Tasks 6 and 7 added two more verbs (item 4 below), so the switch is
///     **seven** arms now, not five. What each arm still owes is narrower than "nothing happens"
///     and is recorded per arm at `gui/ui/shell_action_dispatch.h`.
///  2. ~~**A file chosen in the ported browser is parked, not claimed.**~~ -- **closed, F3 tasks 1-3**
///     (§10.2 item 4). `deluge::gui::ui::dispatchShellChoices`
///     (`gui/ui/shell_choice_dispatch.cpp`) drains `take_choice()` from `ShellHostUI::timerCallback`
///     after every `Shell::tick` and performs `SampleBrowser::claimCurrentFile` against the parked
///     path, so the sample really loads under the toggle; and `runShellSampleClaimOp`'s success arm
///     then calls `MenuScene::reroot_to_scene_opener_page()` (`scenes/menu/menu_scene.h`), which is
///     legacy's `maybeGetParentMenu` re-rooting in `MenuState`'s own terms rather than the heading
///     pair's -- so a claimed file lands on the source page with the file selector focused, not one
///     BACK press further in. **Two qualifications on "closed" for the `Sample` descriptor**, both
///     recorded at the code: shift+select still diverges (`claimSampleFile`'s `@warning`), and the
///     recorder's identical pair is still unpaid (`nodes::AudioRecorderItem::on_focus`).
///  3. ~~**`perform_editor`'s four sync calls into `menu::performance_editing_mode()` are
///     absent**~~ -- **closed, F2 task 5** (§10.2 item 6). All four sites where `PerformanceView`
///     moves the two mirrored bools call `syncEditingModeMirror(*this)`
///     (`gui/views/performance_view.cpp:844`, `:928`, `:942`, `:1411`), and review round 1 added a
///     fifth writer the first pass had excluded: legacy's own `EditingMode::selectButtonPress`
///     (`gui/menu_item/performance_session_view/editing_mode.h:84`), which runs with the toggle OFF
///     and would otherwise leave the mirror stale for the first ported menu opened after a flip-on.
///     With all five, and the writer set closed, the ported editing item and the view cannot
///     disagree in either toggle state. It was four sites in the view and not the five
///     `scenes/menu/action.h` used to list; that header carries the corrected contract.
///  4. ~~**Three ported settings items write flash and notify nobody.**~~ -- **closed, F2 tasks 6
///     and 7** (§10.2 items 5.2 and 5.1). Each got the enumerator and arm this item asked for, and
///     the two are **not** the same defect, which is why they took two tasks. The shortcut-layout
///     setter was losing the *value*: it held for the session and was gone at the next power-on,
///     because nothing reached `SoundEditor::setShortcutsVersion` and so nothing reached flash
///     byte 60. `nodes::set_shortcuts_version` (`scenes/menu/nodes/settings.cpp:891-892`) now files
///     `MenuAction::SetShortcutsVersion` beside its own mirror write, and the dispatcher performs
///     the legacy call. The two screensaver setters were losing only the *apply*: their
///     `FlashStorage` assignment was always made by the ported hook, matching the first statement
///     of each legacy `writeCurrentValue` (`gui/menu_item/defaults/screensaver.h:39`, `:61`), so
///     no value was ever lost -- what was missing was the second statement,
///     `Screensaver::settingsChanged()`, and both screensavers stayed armed at the old timing until
///     a reboot or a change made from the legacy menu. Both setters now file
///     `MenuAction::ScreensaverSettingsChanged` (`scenes/menu/nodes/settings.cpp:592`); one
///     enumerator serves both because legacy's notify takes no argument.
///
///     **What is NOT closed by these two and is not this door's symptom:** the play-cursor flash
///     (§10.2 item 5.3) is unwired in both directions and reaches no menu item, so it is not a
///     thing a user can do wrong from behind this toggle. It is recorded at §10.2 and in
///     `src/scenes/README.md`'s boot-wiring section, where it belongs.
///  5. **Six of the fourteen entry points still open the legacy `SoundEditor`** -- see
///     `shell_menu_entry.cpp`'s table for which, and why each one's node has no port. So with the
///     toggle on the firmware has *both* menus, reachable by different gestures.
///
/// Items 2-4 were `src/scenes` gaps plan F1 did not close. Plan F2 closed 1, 3 and 4 and left 2
/// (the `take_choice()` dispatcher) open by an explicit scope decision -- it is F3's. Item 5 is
/// this task's own honest answer to "a wrong node is worse than a legacy menu", and it is the one
/// entry here that is unchanged by F2.

#include <cstdint>

class Clip;

namespace deluge::gui::ui {

/// @brief Which node a caller wants open, for the callers that want something other than the root
///        `context_for` resolves.
///
/// An enum rather than a `const deluge::scenes::menu::Node*` so that a legacy view can name a
/// ported node without including `src/scenes` -- see the file header. Every value other than
/// `ContextRoot` is resolved to a stable node id (`scenes/menu/node_id.h`) inside
/// `openShellMenu`, which is what `SceneId::arg` carries (E1's ruling Q9).
enum class ShellMenuNode : uint8_t {
	/// @brief Whatever root `MenuContext::root` resolved -- `SceneId::arg == 0`.
	ContextRoot,
	/// @brief `deluge::scenes::menu::nodes::file0_selector`, the port of `file0SelectorMenu`
	///        (`gui/menu_item/file_selector.h:26`; the correspondence is stated at
	///        `scenes/menu/nodes/sound_osc.h:17`).
	Osc0FileSelector,
};

/// @brief Which of the two `currentUIMode`-driven roots (spec E-note-family task 6) a caller
///        wants, in place of whatever `context_for` resolved -- the note editor and note-row
///        editor have no `ShellMenuNode` of their own because they are roots
///        (`MenuContext::Root::NoteEditor`/`NoteRowEditor`), not leaves the way
///        `Osc0FileSelector` is.
///
/// `openShellMenu` sets `ctx.root` from this after resolving the rest of the context, exactly as
/// `InstrumentClipView::enterNoteEditor`/`enterNoteRowEditor` pass an explicit item to
/// `SoundEditor::setup` instead of letting it infer one from `currentUIMode`
/// (`gui/views/instrument_clip_view.cpp:3590`, `:3758`).
enum class NoteMode : uint8_t {
	/// @brief Use whatever root `context_for` resolved -- every caller except the note family.
	None,
	/// @brief `MenuContext::Root::NoteEditor`.
	NoteEditor,
	/// @brief `MenuContext::Root::NoteRowEditor`.
	NoteRowEditor,
};

/// @brief Is the ported menu the one the fourteen entry points should open?
///
/// `RuntimeFeatureSettingType::ShellMenu`, default Off
/// (`model/settings/runtime_feature_settings.cpp:175-178`). **With it off every gated site runs the
/// exact legacy statements it ran before**, which is plan F1's global constraint 2 and is what the
/// `else` branch at each site is shaped to make reviewable.
bool shellMenuEnabled();

/// @brief Resolve what the menu edits, point `MenuScene` at it, and open the guest.
///
/// The port of the half of `SoundEditor::setup` that decides *what* and *which root*
/// (`gui/ui/sound_editor.cpp:1673-1906`), plus the `openUI` the fourteen sites do afterwards. The
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
/// @param kitGlobalFx            `setup()`'s `setupKitGlobalFXMenu` member.
/// @param node                   Which node to land on; `ContextRoot` for the resolved root.
/// @param noteMode               Task 6: override the resolved root with `NoteEditor`/
///                               `NoteRowEditor`, the way `enterNoteEditor`/`enterNoteRowEditor`
///                               name an explicit item instead of letting `setup()` infer one from
///                               `currentUIMode`. `None` for every other caller.
/// @return Whether the guest is now open. False means nothing was opened and nothing was left
///         half-open: the caller should behave as it does when `soundEditor.setup()` returns false.
bool openShellMenu(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot,
                   bool sessionViewRoot, bool kitGlobalFx, ShellMenuNode node = ShellMenuNode::ContextRoot,
                   NoteMode noteMode = NoteMode::None);

} // namespace deluge::gui::ui
