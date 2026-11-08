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
/// @brief The consumer half of the **browser's** choice seam -- the noun half of the wall
///        `gui/ui/shell_action_dispatch.h` is the verb half of.
///
/// The producing half is `src/scenes`': `BrowserScene`'s `Sample` descriptor answers
/// `AcceptOutcome::DeferAndWait`, `BrowserScene::enter_key_press` parks the chosen path and keeps the browser up, and
/// `BrowserScene::take_choice()` hands it out exactly once; the claim's completion answers `deferred_load_done`.
///
/// **Why it is deferred at all.** `SampleBrowser::enterKeyPress`'s file arm requests a claim from
/// `deluge::gui::sampleClaim` (`gui/ui/browser/sample_claim.h`), whose phases reach `soundEditor`,
/// `AudioEngine`, `slicer`, `PadLEDs` and `instrumentClipView` -- `gui/` and `hid/` symbols the
/// scenes layer may not name (`scenes-includes-outside`, `scripts/check_layer_rules.py`).
/// `Descriptor::accept`'s own note says the same thing from the other side
/// (`scenes/browser/browser_scene.h`).
///
/// @par Why this is its own translation unit, and why it is in `gui/ui/`
///
/// Both of `shell_action_dispatch.h`'s reasons, unchanged. **In `gui/ui/`** because performing a
/// choice is exactly the half that gets to name the five symbols above. **Its own translation unit**
/// because it also names `deluge::scenes::browser::browser_scene()`, and so belongs to the
/// `deluge_scenes` closure the host spec suite deliberately never builds
/// (`tests/spec/CMakeLists.txt`): naming it inside `gui/ui/shell_host_ui.cpp` would take that file's
/// specs off the board. The host suite stubs this one symbol in
/// `tests/spec/gui/support/fake_legacy_ui.cpp`, exactly as it stubs `dispatchShellActions` and
/// `wireShellGuestServices`.

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::gui {
struct ClaimRequest;
struct FolderImportRequest;
} // namespace deluge::gui

namespace deluge::gui::ui {

/// @brief A chosen file split into a directory and a filename.
///
/// This is `SampleBrowser::getCurrentFilePath()` run backwards: `currentDir`, a separator when
/// `currentDir` is non-empty, then the filename with its extension. `BrowserScene::enter_key_press` builds the
/// parked path the same way, so the two spellings agree. The claim takes the whole path; the split
/// gives the filename the claim's entered text is made from, and rejects a path that names no file.
///
/// Both members point **into the caller's path**; neither owns storage. The `BrowserChoice` the
/// dispatcher splits outlives every use of the target inside `dispatchShellChoices`.
struct SampleClaimTarget {
	/// The directory, as `Browser::currentDir` spells it: empty for a file at the card root.
	std::string_view dir;
	/// The filename, extension included.
	std::string_view name;
};

/// @brief How @p path splits, or `std::nullopt` where it names no file.
///
/// Declared here, not in the `.cpp`'s anonymous namespace, so a spec can drive the decision over
/// every path shape without reaching the claim.
///
/// @param path A `BrowserChoice::path` (`scenes/browser/browser_scene.h`).
/// @return The directory and filename, or `std::nullopt` for an empty path or one whose last
///         separator is the last character -- neither of which names a file to claim.
std::optional<SampleClaimTarget> sampleClaimTargetFor(std::string_view path);

/// @brief The arguments the drain claims with.
///
/// `ClaimRequest`'s three detection choices (`gui/ui/browser/sample_claim.h`) plus the one that
/// differs from the browser's own Enter, named so a spec can read it rather than find it as a
/// literal inside the dispatcher.
struct SampleClaimArgs {
	/// 0 no, 1 auto, 2 definitely: the browser's own value.
	int32_t mayDoPitchDetection;
	/// As above.
	int32_t mayDoSingleCycle;
	/// As above.
	int32_t mayDoWaveTable;
	/// Keep the browser open once the file is claimed. **A correctness requirement**: the legacy
	/// browser was never opened under the guest, so it is not in `uiNavigationHierarchy`, and
	/// `SampleClaim::stepFinish`'s exit would `closeUI` a browser that is not there.
	bool loadWithoutExiting;
};

/// @brief What `dispatchShellChoices` claims with: the browser's three defaults, and
///        `loadWithoutExiting = true`.
///
/// The same four values the Load button's auto-load claims with: load the file into the sound as if
/// confirmed, without exiting a browser.
inline constexpr SampleClaimArgs kShellSampleClaimArgs{
    .mayDoPitchDetection = 1, .mayDoSingleCycle = 1, .mayDoWaveTable = 1, .loadWithoutExiting = true};

/// @brief The completion `dispatchShellChoices` gives the claim it requests for a `Sample` choice.
///
/// `loadWithoutExiting` skips `SampleClaim::stepFinish`'s browser exit, the legacy menu re-rooting
/// and its `uiNeedsRendering(&audioClipView)`. On success this pays back the two the guest still
/// wants: the render request, and the re-rooting in the ported world's own terms,
/// `MenuScene::reroot_to_scene_opener_page()` against `MenuState::path`/`depth`. The legacy
/// `menuItemHeadingTo`/`parentMenuHeadingTo` pair is left alone: nothing on the ported path writes
/// it, and a stale pair from a legacy session would re-root the legacy editor.
///
/// It answers the waiting browser first (`BrowserScene::deferred_load_done`): a success closes it, a failure leaves it
/// up for another file, as `claimCurrentFile`'s error arm leaves `main`'s up.
///
/// @param success Whether the claim ended claimed. A cancelled claim calls nothing.
void shellSampleClaimDone(bool success);

/// @brief What the `Sample` arm hands its claim request to: `SampleClaim::request`'s shape.
using SampleClaimDispatch = void (*)(const deluge::gui::ClaimRequest& request);

/// @brief Install a claim dispatcher other than `deluge::gui::sampleClaim`'s `request`.
///
/// For specs: a spec that presses a real file in the ported browser reaches this dispatcher, and
/// with the shipped one the claim would load the sample for real. A recording dispatcher lets it
/// assert what was requested (the path, the entered text and `kShellSampleClaimArgs`) without a
/// load. Nothing outside a spec calls this.
///
/// @param dispatch The dispatcher, or null to put the shipped one back.
void setSampleClaimDispatch(SampleClaimDispatch dispatch);

/// @brief How the ported browser's file deletion ended, as `DeleteFile::applyOutcome` reads the card's answer.
enum class FileDeleteOutcome : uint8_t {
	Removed, ///< The card removed the file.
	Missing, ///< The file was not on the card.
	Failed,  ///< The card refused it, or the channel stalled.
};

/// @brief Finish the ported browser's file deletion: `main`'s popup, and the file leaves the browser's list once the
///        card has removed it. The shipped deleter calls this when the card answers.
/// @param path    The file asked for.
/// @param outcome How it ended.
void shellFileDeleteDone(std::string_view path, FileDeleteOutcome outcome);

/// @brief What removes a file from the card for the ported browser's delete menu.
using FileDeleteDispatch = void (*)(std::string_view path);

/// @brief Install a deleter other than the card's, for specs, which cannot complete an unlink. Nothing outside a spec
///        calls this.
/// @param dispatch The deleter, or null to put the shipped one back.
void setFileDeleteDispatch(FileDeleteDispatch dispatch);

/// @brief What cancels an auto-load claim: `SampleClaim::cancelAutoLoad`'s shape.
using SampleClaimCancel = void (*)();

/// @brief Install an auto-load cancel other than `deluge::gui::sampleClaim`'s `cancelAutoLoad`, for specs, which
///        cannot run a claim to cancel. Nothing outside a spec calls this.
/// @param cancel The cancel, or null to put the shipped one back.
void setSampleClaimCancel(SampleClaimCancel cancel);

/// @brief The completion of a claim a sample browser folder menu asked for (BASIC, SINGLE-CYCLE, WAVETABLE).
/// A failure closes the menu and leaves the browser (`Synth`'s `closeOnFailure`); a success closes both, as the claim's
/// own exit does in `main`, and pays back what `shellSampleClaimDone` does.
/// @param success Whether the claim ended claimed. A cancelled claim calls nothing.
void shellLongPressClaimDone(bool success);

/// @brief The completion of a folder import a sample browser folder menu asked for (LOAD ALL, MULTISAMPLES): a failure
///        closes the menu, a success the menu and the browser.
/// @param success Whether the import succeeded.
void shellLongPressImportDone(bool success);

/// @brief What a folder menu's import is handed to: `FolderImport::request`'s shape.
using FolderImportDispatch = void (*)(deluge::gui::FolderImportRequest request);

/// @brief Install a folder-import dispatcher other than `deluge::gui::folderImport`'s `request`: for specs, as
///        `setSampleClaimDispatch` is.
/// @param dispatch The dispatcher, or null to put the shipped one back.
void setFolderImportDispatch(FolderImportDispatch dispatch);

/// @brief Point `soundEditor`'s current sound, source and range at what the ported menu edits, before an operation
///        that reads them on the menu's behalf.
void adoptShellMenuContext();

/// @brief Tell the ported browser whether its next open is the drum creator's: `InstrumentClipView::enterDrumCreator`
///        opens the browser with no `SoundEditor::beginScreen` after it, so only the browser's own BROWSE blink runs.
/// @param fromCreator True for the drum creator's open; false to forget it.
void setShellBrowserFromDrumCreator(bool fromCreator);

/// @brief Take whatever the ported browser chose, if anything, and perform it.
///
/// @par Call this after every `Shell::tick`, and do not gate it
///
/// `BrowserScene::take_choice()` is a one-slot outbox that announces nothing -- `enter` files it and
/// returns an ordinary `Result`, so no `Action` says a
/// choice is waiting and nothing enforces the polling. That is `MenuScene::take_action()`'s hazard
/// (`scenes/menu/menu_scene.h`, the warning on `take_action`) in the same shape, and it lands as the same requirement
/// on this function's *caller*: poll after every tick, or a choice is delivered whenever something next asks, against
/// whatever song and clip exist then.
///
/// So the one call site (`ShellHostUI::timerCallback`) makes the poll unconditional, and sits
/// **after** `Shell::tick` and **above** the close check that can return early -- both for the
/// reasons `dispatchShellActions`' call site gives, and one more that is this drain's own.
///
/// @par The browser is still up while the claim runs
///
/// `DeferAndWait` parks and stays: `choice_ = BrowserChoice{...}` and `awaiting_deferred_` are set together, and the
/// browser refuses input while the claim is in flight (`RuntimeFacts::sample_load_in_flight`). The claim's completion,
/// `shellSampleClaimDone`, answers `deferred_load_done` and re-roots the covered menu; the browser closes on its next
/// render, as `claimCurrentFile` closes `main`'s once the file is in.
///
/// @par Order against `dispatchShellActions`, and whether both can be pending at once
///
/// Both drains run after `Shell::tick`, actions first. A choice is parked while the browser is the input top, so the
/// menu beneath files nothing on that tick; the order matters only for the one interaction below.
///
/// Neither drain reads the other's mailbox and neither is gated on the other, so the order changes
/// nothing observable. It is fixed as **actions, then choices** for the one interaction that is
/// real: `MenuAction::StartStemExport`'s arm removes the ported menu from
/// the shell stack (`closePortedMenu`, `gui/ui/shell_action_dispatch.cpp`), and the
/// re-rooting the claim performs writes that same menu's navigation state
/// (`BrowserScene`'s note on the sample dispatcher, applied through
/// `MenuScene::reroot_to_scene_opener_page()`). Draining actions first means a re-root can never be
/// applied to a menu the same tick's verb was about to take off the stack.
///
/// @par The preview, after the choice
///
/// Then `BrowserScene::take_preview()`, the sample browser's other outbox, one deep in the same way: a file is
/// previewed (the shared `SamplePreviewer`, loading into `AudioEngine::sampleForPreview`), an empty path stops
/// previewing; and on every tick the preview in hand is ticked, its landing answered through
/// `BrowserScene::preview_settled`. The browser's `on_exit` files a stop, so a preview never outlives the scene.
///
/// @par The `switch` has no `default:`, deliberately
///
/// The same rule `dispatchShellActions` runs under, for the same reason: `-Werror=switch` is on for
/// every target (`cmake/deluge_common_options.cmake:32`), so a `BrowserId` added without an arm here
/// fails the build, and a `default:` would absorb it silently.
///
/// @par The menu's context, last
///
/// A shortcut press that moved the menu onto a kit's own effects (`MenuScene::take_context_replaced`) is adopted
/// into `soundEditor`'s context (`adoptShellMenuContext`), as legacy's `setup()` re-points its editor.
void dispatchShellChoices();

/// @brief Whether the visit open now pushed a scene that refused: the drum creator's browser or take, after which
///        legacy opens its editor on the file selector (`enterDrumCreator`'s `openUI(&soundEditor)`).
///
/// It is declared here because this file is a bridge that already names the menu scene: the UI layer rules count each
/// legacy file's includes of and names from the port, so the one read of `MenuScene::pushed_scene_refused` sits where
/// that count is already paid rather than adding to `shell_drum_creator.cpp`'s.
bool shellVisitFellBackToEditor();

} // namespace deluge::gui::ui
