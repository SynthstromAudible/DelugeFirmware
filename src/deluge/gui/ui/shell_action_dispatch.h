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
/// @brief The consumer half of the menu's action seam -- the poll of `MenuScene::take_action()` and
///        the legacy behaviour each request performs.
///
/// The producing half is `src/ui/scenes`': a menu item asks for a piece of legacy behaviour it
/// may not perform, `MenuScene::after_render` moves the request into a one-slot outbox, and
/// `MenuScene::take_action()` (`ui/scenes/menu/menu_scene.h`) hands it out.
///
/// The five verbs are root-UI behaviour, filed by writing `MenuWalkState::pending_action` and
/// tabulated in `ui/scenes/menu/action.h`. A settings write's side effect is not one of them: its
/// setter calls the host at the write, through a port (`ui/scenes/bind/ports/`).
///
/// @par Why this is its own translation unit, and why it is in `gui/ui/`
///
/// The same two reasons `gui/ui/shell_guest_services.h` gives for the same shape. **In `gui/ui/`**
/// because performing these actions is exactly the half that gets to name `performanceView`,
/// `audioClipView`, `stemExport` and the two `gui/context_menu/` singletons -- the symbols
/// `src/ui/scenes` may not include (`scenes-includes-outside`, `scripts/layers.toml`), which
/// is the whole reason the seam exists. **Its own translation unit** because it also names
/// `deluge::ui::scenes::menu_scene()`, and so belongs to the `deluge_ui_scenes` closure the host spec
/// suite deliberately never builds (`tests/trees/host/CMakeLists.txt`): naming it inside
/// `gui/ui/shell_host_ui.cpp` would take that file's specs off the board. The host suite stubs this
/// one symbol in `tests/unit/deluge/gui/support/fake_legacy_ui.cpp`, exactly as it stubs
/// `wireShellGuestServices`.
///
/// The header is in `gui/ui/`, beside its definitions in `gui/ui/shell_action_dispatch.cpp`.

#include "definitions_cxx.hpp" // StemExportType

#include <optional>

namespace deluge::gui::ui {

/// @brief Take whatever the menu asked for, if anything, and perform it.
///
/// @par Call this after every `Shell::tick`, and do not gate it
///
/// `MenuScene::take_action()` is **a one-slot mailbox with a silent overwrite, not an exactly-once
/// queue** (`MenuScene::take_action`, `ui/scenes/menu/menu_scene.h`), and the consequence stated there is a requirement
/// on this function's caller rather than on this function: "`after_render` returns no `Action`
/// announcing that one is waiting, so nothing enforces the polling: **poll after every tick, or
/// requests are dropped or stale**" (the same warning). Nothing in the shell will complain if a tick
/// skips the poll; the request simply arrives whenever someone next asks, against whatever state
/// exists then.
///
/// So the one call site (`ShellHostUI::timerCallback`) makes the poll unconditional, and sits
/// **after** `Shell::tick` and **above** the close check that can return early. After the tick,
/// because a request is filed by `MenuScene::after_render`, which the tick runs as its step 7
/// (`Shell::tick`) -- polling first would always be one tick late. Above the close
/// check, because that arm returns without re-arming the timer (`ShellHostUI::timerCallback`), so a
/// poll below it would be skipped on the very tick the guest closes, and the request stranded for
/// however long it is until something opens the guest again.
///
/// @par What it does
///
/// It drains, and it performs **every** verb: `OpenPerformanceView`, `StartStemExport`,
/// `SetClipLengthToSampleLength`, `OpenSongMidiLearn` and `OpenSongMacros`. The `switch` has an arm
/// for every `MenuAction` and no `default:` -- see below.
///
/// @par The two context menus open even when `setupAndCheckAvailability()` says no
///
/// `song::MidiLearn::selectButtonPress` and `song::ConfigureMacros::selectButtonPress`
/// (`origin/main:src/deluge/gui/menu_item/song/midi_learn.h`,
/// `origin/main:src/deluge/gui/menu_item/song/configure_macros.h`) are each two statements: a
/// `setupAndCheckAvailability()` **whose `bool` they throw away** and an `openUI(...)` that runs unconditionally. This
/// dispatcher does the same, and the `(void)` casts at those two arms are the only difference in spelling.
///
/// **Only one of the two can actually answer false, which is narrower than "it returns a bool
/// legacy discards" suggests.** `ConfigureSongMacros::setupAndCheckAvailability`
/// (`gui/context_menu/configure_song_macros.cpp`) enters the mode and then `return true;`
/// -- there is no false. `MidiLearnMode::setupAndCheckAvailability`
/// (`gui/context_menu/midi_learn_mode.cpp`) returns `currentUIMode == UI_MODE_MIDI_LEARN`
/// *after* calling `sessionView.enterMidiLearnMode()`, and that call reaches
/// `View::startMIDILearn`, which assigns the mode **only
/// under** `currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN`. So the
/// false answer means exactly one thing: some other UI mode was latched when the press landed, and
/// MIDI learn did not arm.
///
/// **Discarding it is not special to these two sites**, which is worth saying because the opposite
/// was assumed. Some callers discard the result and some bind it to a `bool available` and branch
/// (`gui/context_menu/save_song_or_instrument.cpp` branches;
/// `origin/main:src/deluge/gui/menu_item/reset_settings/reset.h` and `gui/views/session_view.cpp` discard).
/// It is a split convention, and these two sites are on the discarding side of it. Reproduced, not
/// improved.
///
/// @warning **What opening an unavailable MIDI-learn menu does, reported rather than fixed.** The
///          context menu appears and reads "MIDI learn", but `currentUIMode` still holds the other
///          mode, and `UI`'s base MIDI-learn answers act only when `currentUIMode ==
///          UI_MODE_MIDI_LEARN` (`UI::noteOnReceivedForMidiLearn` and its siblings) -- so the menu learns nothing.
///          `SessionView::enterMidiLearnMode` has meanwhile overwritten `previousGridModeActive` and forced
///          `gridModeActive` to `SessionGridModeLaunch`
///          (`gui/views/session_view.cpp`) regardless, and `SessionView::exitMidiLearnMode` restores those
///          while `View::endMIDILearn` leaves the latched mode alone, because it only
///          clears `currentUIMode` when it *is* `UI_MODE_MIDI_LEARN` (`gui/views/view.cpp`).
///          So it is a cosmetic-but-real defect -- a menu that cannot do its job -- and it is
///          main's, unchanged here.
///
/// @par The `switch` has no `default:`, deliberately
///
/// `MenuAction` is closed on purpose so that "a new enumerator breaks that switch loudly" (`MenuAction`'s doc in
/// `ui/scenes/menu/action.h`). `-Werror=switch` is on for every target (`cmake/deluge_common_options.cmake`), so an
/// enumerator added without an arm here fails the build; a `default:` would absorb it silently and the compiler would
/// stop being the check.
void dispatchShellActions();

/// @brief Drop whatever the menu asked for on the guest's last tick, for its close: a request filed by the input
///        the close feeds through opens a UI or moves a menu that is going, so nothing performs it.
void discardShellActionsBeforeClose();

/// @brief `soundEditor.exitCompletely()`, ported: take the menu off the guest's scene stack
///        and let `ShellHostUI::timerCallback` close the guest itself.
///
/// Full argument at its definition (`gui/ui/shell_action_dispatch.cpp`, the block between the two
/// anonymous-namespace blocks): why `soundEditor.exitCompletely()` cannot be called as-is under the
/// guest, why emptying the stack is the whole of the port, and why it must not also call
/// `closeGuest()`.
///
/// @par Why it is declared here rather than left inside the `.cpp`'s anonymous namespace
///
/// It has two callers ported from two different statements of main that both happen to be
/// `exitCompletely()`: `stem_export::Start::selectButtonPress`'s first statement, dispatched from
/// this same translation unit's `startStemExport`, and `SoundEditor::padAction`'s tail,
/// ported by the pad-forwarding seam
/// (`gui/ui/shell_pad_forward.cpp`) in a different translation unit. Internal linkage inside
/// `namespace {}` serves only the first caller; this declaration is what the second one needs.
void closePortedMenu();

/// @brief `SoundEditor::exitCompletely`'s settings arm (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): when the
///        menu's root is the settings menu, save the settings, the MIDI devices, the community features and the
///        MIDI-follow defaults, behind main's "Saving settings" animation. The guest's close path calls it.
void saveSettingsBeforeClose();

/// @brief What the host inhibits the screensaver for, beyond the shell's own working animation: a permanent-looking
///        popup is up, or a stem export is running (`Screensaver::timerEvent`'s inhibitors). The guest hands it to the
///        shell before every tick.
bool hostInhibitsScreensaver();

/// @brief Restyle the guest's panels from `FlashStorage::accessibilityMenuHighlighting`, which legacy's renderers read
///        live (`HorizontalMenu::renderMenuItems` in `origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`): at
///        boot once the settings are read, and after every write of it.
void pushShellHighlighting();

/// @brief Apply the RoundedCorners community feature to main's canvas (`Canvas::roundedCornersEnabled`, which its
///        rounded primitives read) and to the guest's panels: once the card's settings are read, and after every write
///        of it.
void pushShellRoundedCorners();

/// @brief After a flash, community-features or full reset, give the open guest every reset setting main reads live: the
///        menu highlighting, the screensaver's timing (which also re-arms legacy's screensaver, as
///        `Screensaver::settingsChanged` does), sticky shift and horizontal menus. Rounded corners and pad
///        brightness are left as they are, stale until the next boot, as main leaves its own copies.
void reapplyLiveSettingsAfterReset();

/// @brief Which stem export legacy's `Start::selectButtonPress` would start from whatever root UI is
///        open right now, or `std::nullopt` for the roots it leaves alone.
///
/// `stem_export::Start::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/stem_export/start.h`) is two
/// statements welded together: a fan-out over `getRootUI()` that picks one of four
/// `StemExportType`s, and a `stemExport.startStemExportProcess(...)` that acts on the
/// pick. `MenuAction::StartStemExport`'s doc says why the seam could not split them into four enumerators
/// -- "the choice is made from the root UI, which this layer cannot read" -- so the whole fan-out
/// lands on this side of the wall, and it is by some distance the most substantial thing any of the
/// seven verbs does.
///
/// @par Why it is declared here rather than left inside the `.cpp`'s anonymous namespace
///
/// **Because the acting half cannot be run in a spec, and the choosing half must be.**
/// `StemExport::startStemExportProcess` (`processing/stem_export/stem_export.cpp`) stops playback, presses
/// record, enters `UI_MODE_STEM_EXPORT` and requests the app loop to run the export a step at a time from
/// `StemExport::exportRoutine`. An example that let the real arm reach `startStemExportProcess` would therefore
/// change global transport and UI state inside the assertion, for an export that nothing in a spec is driving.
///
/// So the fan-out is named, and `tests/integration/ui/scenes/menu_actions_spec.cpp` drives it directly from each
/// of the root UIs legacy names. The dispatcher's own arm is still covered end to end, but only down
/// the branch where this returns `std::nullopt` -- which is legacy's own no-op and the one branch
/// that starts nothing.
///
/// @return The type legacy would export, or `std::nullopt` where legacy's `if`/`else if` chain falls
///         through: any root that is not one of the three named, `instrumentClipView` over a
///         non-`KIT` output, and a null root (`getRootUI` in `gui/ui/ui.cpp` answers `nullptr` while
///         `numUIsOpen` is 0, which is "during song load / swap").
std::optional<StemExportType> stemExportTypeForRootUI();

} // namespace deluge::gui::ui
