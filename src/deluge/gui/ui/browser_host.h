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
/// @brief The browser scene's host: the claims, imports, slices, deletions, MIDI definition loads, `.syx` reads and
///        previews `BrowserScene` asks for, performed with the legacy objects the scenes layer may not name.
///
/// The browser hands each request to its `BrowserHost` (`ui/scenes/browser/browser_scene.h`) when it decides, and
/// `wireShellBrowserHost` installs this file's host there. `SampleBrowser::enterKeyPress`'s file arm requests a claim
/// from `deluge::gui::sampleClaim` (`gui/ui/browser/sample_claim.h`), whose phases reach the sample browser,
/// `AudioEngine`, `slicer`, `PadLEDs` and `instrumentClipView`: `gui/` and `hid/` symbols the scenes layer may not name
/// (`scenes-includes-outside`, `scripts/layers.toml`).
///
/// **How answers reach the browser.** An answer produced before the request returns would reach the scene that asked
/// from inside its own hook, so it is held. `pollShellBrowserHost` delivers held answers after the shell's tick, and
/// `ShellHostUI::closeGuest` and `ShellHostUI::uisTakenDown` deliver what their stack's exits left. A new preview drops
/// an older preview's held answer, which would otherwise end the new one's wait. Every other answer is delivered when
/// it arrives.
///
/// @par Why this is its own translation unit, and why it is in `gui/ui/`
///
/// **In `gui/ui/`** because performing a request is exactly the half that gets to name the symbols above. **Its own
/// translation unit** because it names `deluge::ui::scenes::browser::browser_scene()`, and so belongs to the
/// `deluge_ui_scenes` closure the host spec suite never builds (`tests/trees/host/CMakeLists.txt`). The host suite
/// stubs the two functions the guest calls (`pollShellBrowserHost`, `deliverShellBrowserAnswers`) in
/// `tests/unit/deluge/gui/support/fake_legacy_ui.cpp`. This header names no `ui/scenes/` type.

#include "gui/ui/sound_target.h"

#include <cstdint>
#include <optional>
#include <string_view>

class ParamManagerForTimeline;

namespace deluge::gui {
struct ClaimRequest;
struct FolderImportRequest;
} // namespace deluge::gui

namespace deluge::gui::ui {

class SamplePreviewer;

/// @brief The sample browser's preview, recording each load's path as the last file loaded
///        (`deluge::model::lastFilePathLoaded`), which the browser opens beside.
/// @return The one previewer, made on first use.
SamplePreviewer& shellPreviewer();

/// @brief A chosen file split into a directory and a filename.
///
/// This is `SampleBrowser::getCurrentFilePath()` run backwards: `currentDir`, a separator when
/// `currentDir` is non-empty, then the filename with its extension. `BrowserScene::enter_key_press` builds the
/// chosen path the same way, so the two spellings agree. The claim takes the whole path; the split
/// gives the filename the claim's entered text is made from, and rejects a path that names no file.
///
/// Both members point **into the caller's path**; neither owns storage. The `BrowserChoice` the
/// host splits outlives every use of the target inside the request.
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
/// @param path A `BrowserChoice::path` (`ui/scenes/browser/browser_scene.h`).
/// @return The directory and filename, or `std::nullopt` for an empty path or one whose last
///         separator is the last character -- neither of which names a file to claim.
std::optional<SampleClaimTarget> sampleClaimTargetFor(std::string_view path);

/// @brief The arguments the browser host claims with.
///
/// `ClaimRequest`'s three detection choices (`gui/ui/browser/sample_claim.h`) plus the one that
/// differs from the browser's own Enter, named so a spec can read it rather than find it as a
/// literal inside the browser host.
struct SampleClaimArgs {
	/// 0 no, 1 auto, 2 definitely: the browser's own value.
	int32_t mayDoPitchDetection;
	/// As above.
	int32_t mayDoSingleCycle;
	/// As above.
	int32_t mayDoWaveTable;
};

/// @brief What the browser host claims with: the browser's three defaults.
///
/// The same values the Load button's auto-load claims with: load the file into the sound as if
/// confirmed. The browser scene waits on the claim's completion and closes itself.
inline constexpr SampleClaimArgs kShellSampleClaimArgs{
    .mayDoPitchDetection = 1, .mayDoSingleCycle = 1, .mayDoWaveTable = 1};

/// @brief The completion the browser host gives the claim it requests for a `Sample` choice.
///
/// On success it asks for the render and re-roots the menu over the claimed source,
/// `MenuScene::reroot_to_scene_opener_page()` against `MenuState::path`/`depth`.
///
/// It answers the waiting browser first (`BrowserScene::deferred_load_done`), held when the claim ends inside its own
/// request (`deliverShellBrowserAnswers`): a success closes it, a failure leaves it up for another file, as
/// `claimCurrentFile`'s error arm leaves `main`'s up.
///
/// @param success Whether the claim ended claimed. A cancelled claim calls nothing.
void shellSampleClaimDone(bool success);

/// @brief What the `Sample` arm hands its claim request to: `SampleClaim::request`'s shape.
using SampleClaimDispatch = void (*)(const deluge::gui::ClaimRequest& request);

/// @brief Install a claim dispatcher other than `deluge::gui::sampleClaim`'s `request`.
///
/// For specs: a spec that presses a real file in the browser scene reaches this dispatcher, and
/// with the shipped one the claim would load the sample for real. A recording dispatcher lets it
/// assert what was requested (the path, the entered text and `kShellSampleClaimArgs`) without a
/// load. Nothing outside a spec calls this.
///
/// @param dispatch The dispatcher, or null to put the shipped one back.
void setSampleClaimDispatch(SampleClaimDispatch dispatch);

/// @brief How the browser scene's file deletion ended, as `DeleteFile::applyOutcome` reads the card's answer.
enum class FileDeleteOutcome : uint8_t {
	Removed, ///< The card removed the file.
	Missing, ///< The file was not on the card.
	Failed,  ///< The card refused it, or the channel stalled.
};

/// @brief Finish the browser scene's file deletion: `main`'s popup, and the file leaves the browser's list once the
///        card has removed it. The shipped deleter calls this when the card answers; an answer inside the
///        deletion's own request is held (`deliverShellBrowserAnswers`).
/// @param path    The file asked for.
/// @param outcome How it ended.
void shellFileDeleteDone(std::string_view path, FileDeleteOutcome outcome);

/// @brief What removes a file from the card for the browser scene's delete menu.
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

/// @brief The slicer's open ended: the browser pushes `SlicerScene` on its next render when @p opened
///        (`BrowserScene::slice_ready`). `gui/ui/shell_slice_ops.cpp`'s open calls it.
/// @param opened Whether `slicing::slicer_scene()` holds the file now.
void shellSliceReady(bool opened);

/// @brief What a folder menu's import is handed to: `FolderImport::request`'s shape.
using FolderImportDispatch = void (*)(deluge::gui::FolderImportRequest request);

/// @brief Install a folder-import dispatcher other than `deluge::gui::folderImport`'s `request`: for specs, as
///        `setSampleClaimDispatch` is.
/// @param dispatch The dispatcher, or null to put the shipped one back.
void setFolderImportDispatch(FolderImportDispatch dispatch);

/// @brief Publish the owner and params of what the menu edits now, for `sound_menu_owner` and
///        `soundMenuParamManager` (`util/sound_menu_open.h`). The menu calls it through its context hook whenever its
///        context changes: every open and re-root, a kit-global adopt, a forget and a song swap.
void publishMenuTarget();

/// @brief The engine objects the menu's context names now, resolved in the current song: what a claim, an import
///        or a slice started from the menu acts on.
deluge::gui::SoundTarget menuTarget();

/// @brief Tell the browser scene whether its next open is the drum creator's: `InstrumentClipView::enterDrumCreator`
///        opens the browser with no `SoundEditor::beginScreen` after it, so only the browser's own BROWSE blink runs.
/// @param fromCreator True for the drum creator's open; false to forget it.
void setShellBrowserFromDrumCreator(bool fromCreator);

/// @brief Install the browser scene's host (`BrowserScene::set_host`): the requests it makes are performed here, with
///        the legacy objects this file names.
///
/// `wireShellGuestServices()` calls it at boot and on every open of the guest. A spec that drives the browser through a
/// bare `Shell` calls it itself.
void wireShellBrowserHost();

/// @brief Deliver the browser answers held since the last delivery, in the order they came.
///
/// An answer a request produces before it returns would reach the scene that asked from inside its own hook, so the
/// host holds it. `pollShellBrowserHost` delivers held answers first, before the next `Shell::tick`.
/// `ShellHostUI::closeGuest` and `ShellHostUI::uisTakenDown` deliver what their stack's exits left. Every other answer
/// is delivered when it arrives.
void deliverShellBrowserAnswers();

/// @return Whether a browser answer is held for `deliverShellBrowserAnswers`.
bool shellBrowserAnswersHeld();

/// @brief The browser host's share of every guest tick: deliver the answers held since the last poll, collect the
///        `.syx` read, tick the preview in hand, and run or hold the browser's scroll.
///
/// @par Call this after every `Shell::tick`, above the close check, and do not gate it
///
/// Held answers were produced inside the tick, so this is the first moment no scene is in a hook, and it is before the
/// next `Shell::tick` reads input. The `.syx` read and the preview's load advance only here. A scroll a delivered
/// answer asks for starts here, before the next tick renders the pads it hands over. The one call site,
/// `ShellHostUI::timerCallback`, polls above its close check, so a closing tick's answers are delivered too, and
/// unconditionally, because nothing announces any of the four.
void pollShellBrowserHost();

/// @brief Whether the visit open now pushed a scene that refused: the drum creator's browser or take, after which
///        legacy opens its editor on the file selector (`enterDrumCreator`'s `openUI(&soundEditor)`).
///
/// It is declared here because this file is a bridge that already names the menu scene: the UI layer rules count each
/// legacy file's includes of and names from the port, so the one read of `MenuScene::pushed_scene_refused` sits where
/// that count is already paid rather than adding to `shell_drum_creator.cpp`'s.
bool shellVisitFellBackToEditor();

} // namespace deluge::gui::ui
