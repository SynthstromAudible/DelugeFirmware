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
///     their legacy items do. What each arm still owes is narrower than "nothing happens" and is
///     recorded per arm at `gui/ui/shell_action_dispatch.h`.
///  2. **A file chosen in the ported browser is parked, not claimed.** `BrowserScene`'s deferring
///     descriptor closes with the choice in `take_choice()`, which nothing polls either (§10.2
///     item 4). `SampleBrowser::claimCurrentFile` and legacy's `maybeGetParentMenu` re-rooting are
///     the missing half.
///  3. ~~**`perform_editor`'s four sync calls into `menu::performance_editing_mode()` are
///     absent**~~ -- **closed, F2 task 5** (§10.2 item 6). All four sites where `PerformanceView`
///     moves the two mirrored bools now call `syncEditingModeMirror(*this)`
///     (`gui/views/performance_view.cpp:844`, `:928`, `:942`, `:1411`), so the ported editing item
///     and the view cannot disagree. It was four sites and not the five `scenes/menu/action.h`
///     used to list; that header carries the corrected contract.
///  4. **Three ported settings items write flash and notify nobody.** The two screensaver setters
///     (`scenes/menu/nodes/settings.cpp`) and `nodes::set_shortcuts_version`: F1 task 8 found them,
///     and each needs its own enumerator and arm on (1)'s dispatcher. Changing the screensaver
///     mode or timeout **from the ported menu** leaves both screensavers armed at the old value
///     until the next reboot or the next change made from the legacy menu; choosing a shortcut
///     layout there holds for the session and is gone at the next power-on.
///  5. **Six of the fourteen entry points still open the legacy `SoundEditor`** -- see
///     `shell_menu_entry.cpp`'s table for which, and why each one's node has no port. So with the
///     toggle on the firmware has *both* menus, reachable by different gestures.
///
/// Items 2-4 are `src/scenes` gaps plan F1 does not close (item 1 was one, and plan F2 closed it);
/// item 5 is this task's own honest answer to "a wrong node is worse than a legacy menu".

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
/// (`gui/ui/sound_editor.cpp:1656-1889`), plus the `openUI` the fourteen sites do afterwards. The
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
/// @return Whether the guest is now open. False means nothing was opened and nothing was left
///         half-open: the caller should behave as it does when `soundEditor.setup()` returns false.
bool openShellMenu(Clip* clip, int32_t sourceIndex, bool performanceView, bool sessionOrArrangerRoot,
                   bool sessionViewRoot, bool kitGlobalFx, ShellMenuNode node = ShellMenuNode::ContextRoot);

} // namespace deluge::gui::ui
