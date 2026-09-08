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
/// one-slot outbox, and `MenuScene::take_action()` (`scenes/menu/menu_scene.h:625`) hands it out.
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

#include "definitions_cxx.hpp" // StemExportType

#include <optional>

namespace deluge::gui::ui {

/// @brief Take whatever the ported menu asked for, if anything, and perform it.
///
/// @par Call this after every `Shell::tick`, and do not gate it
///
/// `MenuScene::take_action()` is **a one-slot mailbox with a silent overwrite, not an exactly-once
/// queue** (`scenes/menu/menu_scene.h:593-621`), and the consequence stated there is a requirement
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
/// It drains, and it performs **all seven** verbs: `OpenPerformanceView` as of F2 task 2,
/// `StartStemExport` as of F2 task 3, `SetClipLengthToSampleLength`, `OpenSongMidiLearn` and
/// `OpenSongMacros` as of F2 task 4, `SetShortcutsVersion` as of F2 task 6, and
/// `ScreensaverSettingsChanged` as of F2 task 7. The last two are a different shape from the five --
/// a **write** rather than a select press, filed from a `Binding::set` hook through
/// `MenuScene::request_action` -- which `scenes/menu/action.h` argues at each enumerator. The
/// `switch` has an arm for every `MenuAction` and no `default:` -- see below.
///
/// @par The two context menus open even when `setupAndCheckAvailability()` says no
///
/// `song::MidiLearn::selectButtonPress` and `song::ConfigureMacros::selectButtonPress`
/// (`gui/menu_item/song/midi_learn.h:27-31`, `gui/menu_item/song/configure_macros.h:27-31`) are each
/// two statements: a `setupAndCheckAvailability()` **whose `bool` they throw away** (`:28` in both)
/// and an `openUI(...)` (`:29` in both) that runs unconditionally. This dispatcher does the same, and
/// the `(void)` casts at those two arms are the only difference in spelling.
///
/// **Only one of the two can actually answer false, which is narrower than "it returns a bool
/// legacy discards" suggests.** `ConfigureSongMacros::setupAndCheckAvailability`
/// (`gui/context_menu/configure_song_macros.cpp:48-51`) enters the mode and then `return true;`
/// (`:50`) -- there is no false. `MidiLearnMode::setupAndCheckAvailability`
/// (`gui/context_menu/midi_learn_mode.cpp:50-53`) returns `currentUIMode == UI_MODE_MIDI_LEARN`
/// (`:52`) *after* calling `sessionView.enterMidiLearnMode()` (`:51`), and that call reaches
/// `View::startMIDILearn` (`gui/views/view.cpp:489-497`), which assigns the mode (`:493`) **only
/// under** `currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN` (`:490`). So the
/// false answer means exactly one thing: some other UI mode was latched when the press landed, and
/// MIDI learn did not arm.
///
/// **Discarding it is not special to these two sites**, which is worth saying because the opposite
/// was assumed. `grep -rn "setupAndCheckAvailability" src/deluge` returns 41 lines; 14 are
/// declarations or definitions and one is a doc comment in `shell_host_ui.h`, leaving **26 call
/// sites, of which 11 discard the result and 15 bind it to a `bool available` and branch**
/// (`gui/context_menu/save_song_or_instrument.cpp:62` is one of the 15;
/// `gui/menu_item/reset_settings/reset.h:31` and `gui/views/session_view.cpp:522` are two of the 11).
/// It is a split convention, and these two sites are on the discarding side of it. Reproduced, not
/// improved.
///
/// @warning **What opening an unavailable MIDI-learn menu does, reported rather than fixed.** The
///          context menu appears and reads "MIDI learn", but `currentUIMode` still holds the other
///          mode, and every legacy MIDI-learn dispatch arm is gated on `currentUIMode ==
///          UI_MODE_MIDI_LEARN` (`playback/playback_handler.cpp:2897`, `:2963`, `:3168`) -- so the
///          menu learns nothing. `SessionView::enterMidiLearnMode` has meanwhile overwritten
///          `previousGridModeActive` and forced `gridModeActive` to `SessionGridModeLaunch`
///          (`gui/views/session_view.cpp:3072-3077`) regardless, and the exit restores those
///          (`:3079-3083`) while `View::endMIDILearn` leaves the latched mode alone, because it only
///          clears `currentUIMode` when it *is* `UI_MODE_MIDI_LEARN` (`gui/views/view.cpp:511-513`).
///          So it is a cosmetic-but-real defect -- a menu that cannot do its job -- and it is
///          legacy's, unchanged here.
///
/// @par The `switch` has no `default:`, deliberately
///
/// `MenuAction` is closed on purpose so that "a new enumerator breaks that switch loudly"
/// (`scenes/menu/action.h:106-108`). `-Werror=switch` is on for every target
/// (`cmake/deluge_common_options.cmake:32`), so an enumerator added without an arm here fails the
/// build; a `default:` would absorb it silently and the compiler would stop being the check.
void dispatchShellActions();

/// @brief Which stem export legacy's `Start::selectButtonPress` would start from whatever root UI is
///        open right now, or `std::nullopt` for the roots it leaves alone.
///
/// `stem_export::Start::selectButtonPress` (`gui/menu_item/stem_export/start.h:31-49`) is two
/// statements welded together: a fan-out over `getRootUI()` that picks one of four
/// `StemExportType`s (`:33-47`), and a `stemExport.startStemExportProcess(...)` that acts on the
/// pick. `scenes/menu/action.h:123-127` says why the seam could not split them into four enumerators
/// -- "the choice is made from the root UI, which this layer cannot read" -- so the whole fan-out
/// lands on this side of the wall, and it is by some distance the most substantial thing any of the
/// five verbs does.
///
/// @par Why it is declared here rather than left inside the `.cpp`'s anonymous namespace
///
/// **Because the acting half cannot be run in a spec, and the choosing half must be.**
/// `StemExport::startStemExportProcess` hands the export to `deluge::storage::Owner::run`
/// (`processing/stem_export/stem_export.cpp:87-96`), which is queued onto a fiber only on the
/// Embassy BSP: the cooperative default runs the op **inline on the caller's stack**
/// (`storage/owner.h:28-31`; `OSLikeStuff/task_scheduler/task_scheduler_c_api.cpp:101-104`), and
/// `deluge_scenes_spec` links that default. An example that let the real arm reach
/// `startStemExportProcess` would therefore run `StemExport::runStemExportProcess` for real inside
/// the assertion -- arming the recorder, writing stems to `DELUGE_SD_ROOT`, and blocking in
/// `renderWait` (`stem_export.cpp:587-596`) for a transport that nothing in a spec is driving.
///
/// So the fan-out is named, and `sim/scenes_spec/menu_actions_spec.cpp` drives it directly from each
/// of the root UIs legacy names. The dispatcher's own arm is still covered end to end, but only down
/// the branch where this returns `std::nullopt` -- which is legacy's own no-op and the one branch
/// that starts nothing.
///
/// @return The type legacy would export, or `std::nullopt` where legacy's `if`/`else if` chain falls
///         through: any root that is not one of the three named, `instrumentClipView` over a
///         non-`KIT` output, and a null root (`getRootUI()` answers `nullptr` while `numUIsOpen ==
///         0`, `gui/ui/ui.cpp:145-150`, which is "during song load / swap", `:64`).
std::optional<StemExportType> stemExportTypeForRootUI();

} // namespace deluge::gui::ui
