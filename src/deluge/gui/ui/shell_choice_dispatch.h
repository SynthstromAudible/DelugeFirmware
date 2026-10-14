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
/// `AcceptOutcome::Defer` (`scenes/browser/browser_scene.cpp:210-219`, the `return` at `:218`),
/// `BrowserScene::enter` parks the chosen path and closes the scene in one statement
/// (`:896-898`), and `BrowserScene::take_choice()` (`scenes/browser/browser_scene.h:382`,
/// defined `browser_scene.cpp:417-421`) hands it out exactly once.
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
} // namespace deluge::gui

namespace deluge::gui::ui {

/// @brief A chosen file split into a directory and a filename.
///
/// This is `SampleBrowser::getCurrentFilePath()` run backwards: `currentDir`, a separator when
/// `currentDir` is non-empty, then the filename with its extension. `BrowserScene::enter` builds the
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

/// @brief Take whatever the ported browser chose, if anything, and perform it.
///
/// @par Call this after every `Shell::tick`, and do not gate it
///
/// `BrowserScene::take_choice()` is a one-slot outbox that announces nothing -- `enter` files it and
/// returns an ordinary `Result` (`scenes/browser/browser_scene.cpp:896-898`), so no `Action` says a
/// choice is waiting and nothing enforces the polling. That is `MenuScene::take_action()`'s hazard
/// (`scenes/menu/menu_scene.h:672-677`, the poll at `:704`) in the same shape, and it lands as the same requirement on
/// this function's *caller*: poll after every tick, or a choice is delivered whenever something next
/// asks, against whatever song and clip exist then.
///
/// So the one call site (`ShellHostUI::timerCallback`) makes the poll unconditional, and sits
/// **after** `Shell::tick` and **above** the close check that can return early -- both for the
/// reasons `dispatchShellActions`' call site gives, and one more that is this drain's own.
///
/// @par The scene is already gone by the time this runs, and that is the point
///
/// `Defer` **parks and closes in the same statement**: `choice_ = BrowserChoice{...}` then
/// `return close()` (`scenes/browser/browser_scene.cpp:897-898`), and `close()` is
/// `Action::Kind::Remove` naming the browser's own kind (`:918-927`). The router applies that inside
/// the same `Shell::tick` that routed the press, so by the time this drain runs the browser is off
/// the stack and the ported menu underneath is frontmost again. Nothing here may assume otherwise --
/// there is no state in which a choice is pending *and* the browser is still up.
///
/// @par Order against `dispatchShellActions`, and whether both can be pending at once
///
/// Both drains run after `Shell::tick`, actions first. **Yes, both can be pending on the same
/// tick**, and the path is real rather than theoretical: the browser's `Remove` is applied at
/// `Shell::tick`'s step 3 (`shell/shell.cpp:332`), so the menu underneath is frontmost again by
/// steps 5-6 and gets its own `after_render()` at step 7 (`:402-410`) -- on the very tick the choice
/// was filed. A `MenuActionRequest` filed by that resume sits in the menu's outbox beside the choice
/// in the browser's.
///
/// Neither drain reads the other's mailbox and neither is gated on the other, so the order changes
/// nothing observable. It is fixed as **actions, then choices** for the one interaction that is
/// now real rather than coming: `MenuAction::StartStemExport`'s arm removes the ported menu from
/// the shell stack (`closePortedMenu`, `gui/ui/shell_action_dispatch.cpp:189-191`), and the
/// re-rooting the claim performs writes that same menu's navigation state
/// (`scenes/browser/browser_scene.h:211-233`, applied through
/// `MenuScene::reroot_to_scene_opener_page()`). Draining actions first means a re-root can never be
/// applied to a menu the same tick's verb was about to take off the stack.
///
/// @par The `switch` has no `default:`, deliberately
///
/// The same rule `dispatchShellActions` runs under, for the same reason: `-Werror=switch` is on for
/// every target (`cmake/deluge_common_options.cmake:32`), so a `BrowserId` added without an arm here
/// fails the build, and a `default:` would absorb it silently.
void dispatchShellChoices();

} // namespace deluge::gui::ui
