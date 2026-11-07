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

#include "gui/ui/ui.h"
#include "hid/button.h"

#include <cstdint>
#include <optional>

class MIDICable;

/// @file
/// @brief The three real `shell::Service`s the guest runs, and the one call that wires them.
///
/// @par Why this is its own translation unit and not three members of `ShellHostUI`
///
/// The guest owns the `Shell`, the `GuestSurfaceSet` and the three services, and three members
/// would be the obvious shape. It does not build: the host spec suite
/// compiles `gui/ui/shell_host_ui.cpp` **directly** and never builds the `deluge_scenes` closure
/// (`tests/spec/CMakeLists.txt`, which fakes `deluge::scenes::scene_registry()` for exactly
/// that reason), and `services::ModKnobs`, `services::GlobalControls` and `services::MidiLearn` are
/// concrete classes over `model/` that cannot be faked the way a free function can. Naming their
/// types in `shell_host_ui.cpp` would take `shell_host_ui_spec`'s 52 examples off the board.
///
/// So the guest keeps the `Shell` and this file keeps the services, joined by a single
/// no-arguments function the host suite stubs in `tests/spec/gui/support/fake_legacy_ui.cpp`. The
/// ownership is the same -- the services outlive nothing but the process, and they are installed
/// into `shellHostUI`'s own `Shell` -- and the seam is one symbol wide.
///
/// @par What this closes
///
/// Each of the three services is constructed from the `Shell`'s own `SceneStack&` (two also from its
/// `InputLowering&`), so a real service cannot exist before the shell it serves; `Shell::set_services`
/// installs it afterwards. This is the application making that call (see `scenes/registry.h`'s
/// boot-wiring note).

namespace deluge::gui::ui {

/// @brief Construct the three services over `shellHostUI`'s `Shell` (once), re-seed everything they
///        borrow from legacy, and install them.
///
/// Idempotent and cheap: the services themselves are built on the first call and kept; every call
/// re-reads `currentSong`, `view`'s active mod controllable and mod region, and the two `ViewState`
/// blocks, then re-installs the slots. `Shell::set_services` is defined to be safe over a live set
/// (it rebuilds the deduplicated tick list from scratch), so re-calling costs three pointer stores
/// and a list rebuild.
///
/// Call it at boot -- so the slots are never empty -- and again from `ShellHostUI::opened()`, which
/// is the moment the borrowed model state can have moved since. Both callers exist; see the
/// function's own comments for what each field is read from and which of them are stated as
/// uncovered.
void wireShellGuestServices();

/// @brief Re-read the facts about the root UI that the open menu and the knobs hold: the menu's greyout arm and
///        whether the root is the keyboard screen or the automation view (`MenuScene::set_root_facts`), and the two
///        `ViewState` blocks. Called whenever the root, or the automation view's editor, changes under the menu.
void refreshShellRootFacts();

/// @brief Whether the ported menu is the guest's frontmost scene, the guest is the current UI, and the menu's root is
///        not the settings: the guest's half of "is the sound menu open" (`util/sound_menu_open.h`).
bool shellSoundMenuOpen();

/// @brief Whether the ported menu is the guest's frontmost scene and the guest is the current UI, whatever the menu's
///        root: the guest's half of `getCurrentUI() == &soundEditor`.
bool shellMenuFrontmost();

/// @brief Whether the ported menu is frontmost on the note-range picker: the guest's half of `rangePickerOpen`.
bool shellRangePickerOpen();

/// @brief Whether the guest's frontmost scene is the sample browser or the audio recorder, or the ported menu on a
///        range-dependent item or the picker: the guest's half of `rangeDependentMenuOpen`.
bool shellRangeDependentOpen();

/// @brief Whether @p untransposed falls in the range the ported menu has selected, on a source of several: the guest's
///        half of `noteWithinCurrentRange`. Asks `Source::getRange`, which remembers the range as legacy's test does.
/// @param untransposed The note as the clip has it, before the sound's transpose.
bool shellNoteWithinCurrentRange(int32_t untransposed);

/// @brief A note played under the ported picker: `MenuScene::select_range_for_note`.
/// @param note The note, transposed as the sound plays it.
void shellChangeRangeForNote(int32_t note);

/// @brief After a guest tick, re-render the root's audition column (or the keyboard) when the ported menu's selected
///        range or the source's number of ranges changed (`SoundEditor::possibleChangeToCurrentRangeDisplay`, which
///        legacy's picker calls after every pick, insert and delete), or when the range highlight turned on or off, as
///        a move through legacy's menu re-renders the root.
void refreshShellRangeDisplay();

/// @brief The keyboard DX column's white row under the ported menu now (`MenuScene::dx_column_flash` on the guest's
///        clock), or nothing.
std::optional<int32_t> shellDxColumnFlashRow();

/// @brief After a guest tick, re-render the keyboard's sidebar when the DX column's white row changed: legacy's
///        `blinkSideColumn` re-renders it on every flip of the parameter page's flash, and a move off the page clears
///        it.
void refreshShellDxColumn();

/// @brief Re-read whether the guest's frontmost scene is the sample browser, one of the sample browser's long-press
///        menus or the slicer, for `shellSamplePreviewSounds`. Called on every guest tick, after the shell's, and as
///        the guest closes.
void refreshShellSamplePreviewSounds();

/// @brief Whether the guest is the current UI and, at its last tick, its frontmost scene was the sample browser, one of
///        the sample browser's long-press menus or the slicer: the guest's half of "does the sample preview sound"
///        (`util/sample_preview_open.h`), read on the audio tier.
bool shellSamplePreviewSounds();

/// @brief SAVE while the ported sound menu is frontmost, as `SoundEditor::buttonAction`'s SAVE arm takes it
///        (`gui/ui/sound_editor.cpp`): on a press with no UI mode up and an instrument clip's view at the root, Shift
///        deletes the note-range picker's range (`MultiRange::deletePress`, when the picker is focused) and a plain
///        press opens the save-preset screen.
/// @param b  The button.
/// @param on Its press (true) or release.
/// @return `DEALT_WITH` for SAVE under the menu, whatever it did; nullopt for any other button or with no menu up.
std::optional<ActionResult> forwardMenuSaveButton(deluge::hid::Button b, bool on);

/// @brief The ported menu re-reads its focused value from the model (`MenuScene::read_value_again`).
void shellMenuReadValueAgain();

/// @brief Whether the guest's knobs were last told the automation view's editor is on screen, for a spec.
bool guestKnobsSeeAutomationEditor();

/// @brief The guest's close hook: a knob popup still showing goes to legacy's display notification, with the
///        lifetime it had left, as legacy's own popup outlives the menu, and becomes the popup `view` last drew.
///        The guest's other passive overlays are dropped with its stack, and the kit context it gave `soundEditor` is
///        cleared. `wireShellGuestServices()` installs it.
/// @param now_ms The guest's clock at the close, which the popup's lifetime is read against.
void handOffGuestOverlays(uint32_t now_ms);

} // namespace deluge::gui::ui
