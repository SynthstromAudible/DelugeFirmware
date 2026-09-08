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
/// @brief The consumer half of the menu's action seam -- the poll `MenuScene::take_action()` has
///        been documented as owing since it was written, and nothing performed.
///
/// The producing half is `src/scenes`': five ported menu items ask for a piece of legacy root-UI
/// behaviour by writing `MenuState::pending_action`, `MenuScene::after_render` moves that into a
/// one-slot outbox, and `MenuScene::take_action()` (`scenes/menu/menu_scene.h:611`) hands it out.
/// The five verbs and the legacy statement each one stands for are tabulated at
/// `scenes/menu/action.h:13-19`.
///
/// @par Why this is its own translation unit, and why it is in `gui/ui/`
///
/// The same two reasons `gui/ui/shell_guest_services.h` gives for the same shape. **In `gui/ui/`**
/// because performing these actions is exactly the half that gets to name `performanceView`,
/// `audioClipView`, `stemExport` and the two `gui/context_menu/` singletons -- the symbols
/// `src/scenes` may not include (`scenes-includes-outside`, `scripts/check_layer_rules.py`), which
/// is the whole reason the seam exists. **Its own translation unit** because it also names
/// `deluge::scenes::menu_scene()`, and so belongs to the `deluge_scenes` closure the host spec
/// suite deliberately never builds (`tests/spec/CMakeLists.txt:28-35`): naming it inside
/// `gui/ui/shell_host_ui.cpp` would take that file's specs off the board. The host suite stubs this
/// one symbol in `tests/spec/gui/support/fake_legacy_ui.cpp`, exactly as it stubs
/// `wireShellGuestServices`.
///
/// The header stays in `gui/ui/` rather than moving to `src/deluge/util/` the way
/// `util/shell_menu_entry.h` had to: that move was forced by a *caller* inside
/// `check_layer_rules.py`'s `NON_UI` glob (`model/clip/instrument_clip_minder.cpp`), and this one
/// has exactly one caller, `gui/ui/shell_host_ui.cpp`, which is inside `gui/` already.

namespace deluge::gui::ui {

/// @brief Take whatever the ported menu asked for, if anything, and perform it.
///
/// @par Call this after every `Shell::tick`, and do not gate it
///
/// `MenuScene::take_action()` is **a one-slot mailbox with a silent overwrite, not an exactly-once
/// queue** (`scenes/menu/menu_scene.h:593-607`), and the consequence stated there is a requirement
/// on this function's caller rather than on this function: "`after_render` returns no `Action`
/// announcing that one is waiting, so nothing enforces the polling: **poll after every tick, or
/// requests are dropped or stale**" (`:596-598`). Nothing in the shell will complain if a tick
/// skips the poll; the request simply arrives whenever someone next asks, against whatever state
/// exists then.
///
/// So the one call site (`ShellHostUI::timerCallback`) makes the poll unconditional, and sits
/// **after** `Shell::tick` and **above** the close check that can return early. After the tick,
/// because a request is filed by `MenuScene::after_render`, which the tick runs as its step 7
/// (`shell/shell.cpp:399-407`) -- polling first would always be one tick late. Above the close
/// check, because that arm returns without re-arming the timer (`ShellHostUI::timerCallback`), so a
/// poll below it would be skipped on the very tick the guest closes, and the request stranded for
/// however long it is until something opens the guest again.
///
/// @par What it does today
///
/// It drains, and it performs one of the five verbs. The `switch` has an arm for every `MenuAction`;
/// `OpenPerformanceView` has a body as of F2 task 2 and the other four are still empty, each
/// arriving with the task that ports it. So a select press on `start_stem_export` still exports
/// nothing -- but the request is consumed on the tick it was filed rather than left in the outbox.
///
/// @par The `switch` has no `default:`, deliberately
///
/// `MenuAction` is closed on purpose so that "a new enumerator breaks that switch loudly"
/// (`scenes/menu/action.h:66-68`). `-Werror=switch` is on for every target
/// (`cmake/deluge_common_options.cmake:32`), so an enumerator added without an arm here fails the
/// build; a `default:` would absorb it silently and the compiler would stop being the check.
void dispatchShellActions();

} // namespace deluge::gui::ui
