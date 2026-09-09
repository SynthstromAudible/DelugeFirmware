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
/// `AcceptOutcome::Defer` (`scenes/browser/browser_scene.cpp:209-218`, the `return` at `:217`),
/// `BrowserScene::enter` parks the chosen path and closes the scene in one statement
/// (`:895-897`), and `BrowserScene::take_choice()` (`scenes/browser/browser_scene.h:381`,
/// defined `browser_scene.cpp:416-420`) hands it out exactly once.
///
/// **Why it is deferred at all.** `SampleBrowser::enterKeyPress`'s file arm is one dispatch onto the
/// storage owner (`gui/ui/browser/sample_browser.cpp:333 enterKeyPress`, the dispatch at `:392`),
/// whose op is `SampleBrowser::runClaimCurrentFileOp` (`:397-399`) and whose body is
/// `SampleBrowser::claimCurrentFile` (`:814`). That body reaches `soundEditor`, `AudioEngine`,
/// `slicer`, `PadLEDs` and `instrumentClipView` -- five `gui/` and `hid/` symbols the scenes layer
/// may not name (`scenes-includes-outside`, `scripts/check_layer_rules.py`).
/// `Descriptor::accept`'s own note says the same thing from the other side
/// (`scenes/browser/browser_scene.h:198-239`, the `Defer` argument at `:200-209`).
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

namespace deluge::gui::ui {

/// @brief A chosen file split the way the legacy browser holds one: a directory and a filename.
///
/// This is `SampleBrowser::getCurrentFilePath()` (`gui/ui/browser/sample_browser.cpp:539-550`) run
/// backwards. That function is `currentDir`, a separator when `currentDir` is non-empty, then
/// `currentFileItem->filename` with its extension (`:540-547`), and `BrowserScene::enter` builds the
/// parked path with the same three statements (`scenes/browser/browser_scene.cpp:879-883`) precisely
/// so the two spellings agree. Splitting it back apart is what any performer of the claim has to do
/// first, because `claimCurrentFile` reads the browser's own `currentDir` and selected item rather
/// than taking a path.
///
/// Both members point **into the caller's path**; neither owns storage. The `BrowserChoice` the
/// dispatcher splits outlives every use of the target inside `dispatchShellChoices`.
struct SampleClaimTarget {
	/// `Browser::currentDir` (`gui/ui/browser/browser.h:106`), empty for a file at the card root.
	std::string_view dir;
	/// `FileItem::filename`, extension included.
	std::string_view name;
};

/// @brief How @p path splits, or `std::nullopt` where it names no file.
///
/// @par Why this is declared here rather than left inside the `.cpp`'s anonymous namespace
///
/// **Because the acting half cannot be run in a spec, and the choosing half must be** -- the
/// sentence `shell_action_dispatch.h` writes over `stemExportTypeForRootUI()`, and the same wall.
/// `SampleBrowser::enterKeyPress` (`gui/ui/browser/sample_browser.cpp:333 enterKeyPress`) hands the
/// claim to `deluge::storage::Owner::run_or_inline` at `:392`, which is queued onto a fiber only on
/// the Embassy BSP:
/// the cooperative default "runs inline on the caller's stack" (`storage/owner.h:28-30`, and again
/// at `:40`), and `deluge_scenes_spec` links that default. So an example that reached the real claim
/// would run `SampleBrowser::claimCurrentFile` (`sample_browser.cpp:814`) for real inside the
/// assertion: `display->displayLoadingAnimationText("Working")` (`:824`), a card read down through
/// `Sample::determinePitch`, an `AudioFileManager` allocation for the sample, and the
/// `audioFileIsNowSet()` / `openUI(&soundEditor)` tail (`:1097`, `:1119`).
///
/// So the split is named, `sim/scenes_spec/browser_choice_spec.cpp` drives it directly over every
/// path shape, and the dispatcher's arm calls it before it calls anything that acts.
///
/// @param path A `BrowserChoice::path` (`scenes/browser/browser_scene.h:251-256`).
/// @return The directory and filename, or `std::nullopt` for an empty path or one whose last
///         separator is the last character -- neither of which names a file to claim.
std::optional<SampleClaimTarget> sampleClaimTargetFor(std::string_view path);

/// @brief The arguments the drain claims with.
///
/// `SampleBrowser::claimCurrentFile`'s four parameters (`gui/ui/browser/sample_browser.h:53-54`),
/// named so the one that differs from legacy's file arm is a value a spec can read rather than a
/// literal buried in an op body.
struct SampleClaimArgs {
	/// 0 no, 1 auto, 2 definitely -- legacy's default.
	int32_t mayDoPitchDetection;
	/// As above.
	int32_t mayDoSingleCycle;
	/// As above.
	int32_t mayDoWaveTable;
	/// **The one that is not legacy's default, and it is a correctness requirement.** See
	/// `runShellSampleClaimOp`.
	bool loadWithoutExiting;
};

/// @brief What `dispatchShellChoices` claims with: `claimCurrentFile`'s three defaults, and
///        `loadWithoutExiting = true`.
///
/// The same four values legacy's own auto-load path passes -- `claimCurrentFile(1, 1, 1, true)` at
/// `gui/ui/browser/sample_browser.cpp:640`, the "load it into the instrument as if you had confirmed,
/// but keep the browser open" feature. That is exactly this dispatcher's situation, for a different
/// reason: there is no browser of legacy's to keep open or to close.
inline constexpr SampleClaimArgs kShellSampleClaimArgs{
    .mayDoPitchDetection = 1, .mayDoSingleCycle = 1, .mayDoWaveTable = 1, .loadWithoutExiting = true};

/// @brief The op `dispatchShellChoices` hands the storage owner for a `Sample` choice.
///
/// @par Why this is not `SampleBrowser::runClaimCurrentFileOp`
///
/// Because that one runs `claimCurrentFile()` with **default** arguments, and
/// `loadWithoutExiting = false` **closes the browser** on success:
/// `exitAndNeverDeleteDrum()` (`gui/ui/browser/sample_browser.cpp:1109`) is `close()`
/// (`:258-260`), which is `Browser::close()` (`gui/ui/browser/browser.cpp:189-193`) and then
/// `UI::close()` -> `closeUI(this)` (`gui/ui/ui.cpp:57-59`).
///
/// **Under the guest, `sampleBrowser` was never opened, so it is not in `uiNavigationHierarchy`.**
/// `closeUI`'s search runs `u` down to 1 and falls out at `u == 0`, so `uiNavigationHierarchy[u - 1]`
/// reads index `-1` (`gui/ui/ui.cpp:183-194`) -- and even reading that benignly, the next three
/// statements set `numUIsOpen = 0`, unset `TimerName::UI_SPECIFIC` (`:195-197`) from **inside**
/// `ShellHostUI::timerCallback`, and call `focusRegained()` on whatever that index produced. It is
/// the same index `-1` read `gui/ui/shell_action_dispatch.cpp:130-136` describes.
///
/// @par What the flag suppresses, and what this op pays back
///
/// `loadWithoutExiting` skips the whole of `sample_browser.cpp:1108-1126`, which is three statements
/// -- one correctly skipped and two paid back:
///
///   * `exitAndNeverDeleteDrum()` (`:1109`) -- the close above. **Correctly skipped:** there is no
///     legacy browser open to exit.
///   * the `menuItemHeadingTo`/`parentMenuHeadingTo` re-rooting (`:1111-1123`) -- **dead as written
///     under the guest, and paid back in the ported world's own terms instead (F3 task 3).** Dead
///     because nothing on the ported path writes either member: they are written in exactly one
///     place, `SoundEditor::maybeGetParentMenu` (`gui/ui/sound_editor.cpp:2073 maybeGetParentMenu`,
///     the two writes at `:2092-2093`), which the guest never enters. **Reviving this block would
///     have been worse than skipping it**, because a stale non-null pair left by a pre-toggle legacy
///     session would re-root the *legacy* editor. What the op does instead is
///     `MenuScene::reroot_to_scene_opener_page()` (`scenes/menu/menu_scene.h`), which is the same
///     three statements against `MenuState::path`/`depth` -- the port's `menuItemNavigationRecord`.
///   * `uiNeedsRendering(&audioClipView)` (`gui/ui/browser/sample_browser.cpp:1125`) -- **wanted, so
///     this op pays it back.** The legacy view under the guest is a real root UI, and
///     `uiNeedsRendering` is a walk down `uiNavigationHierarchy` that drops the request when
///     something above is opaque (`gui/ui/ui.cpp:279-303`), so it is safe under the guest as well as
///     faithful. The path is named in full rather than left as a bare `` `:NNNN` ``: the nearest
///     named citation above it is `browser_scene.h`, so a bare number here would inherit the wrong
///     file -- and neither citation gate can see that.
///
/// It is gated on the claim's own return, because legacy's tail is on the success path.
/// `claimCurrentFile` has exactly three returns, and the tail sits immediately above `return true`
/// (`gui/ui/browser/sample_browser.cpp:1108-1128`), so no path returns true without reaching it and
/// none reaches it without returning true -- `if (claim) { ... }` is legacy's own condition rather
/// than an approximation of it.
///
/// @param unused The storage-owner op signature's context pointer. Legacy's wrapper ignores it too
///               (`sample_browser.cpp:397-399`).
void runShellSampleClaimOp(void* unused);

/// @brief What the `Sample` arm hands the claim to -- `deluge::storage::Owner::run`'s own shape
///        (`storage/owner.h:43`), because the claim is what legacy's file arm dispatches
///        (`gui/ui/browser/sample_browser.cpp:392`).
using SampleClaimDispatch = bool (*)(void (*)(void*), void*);

/// @brief Install a dispatcher other than `deluge::storage::Owner::run_or_inline` on the claim.
///
/// @par For specs, and it is a requirement rather than a convenience
///
/// `BrowserScene::set_dispatch` (`scenes/browser/browser_scene.h:383-391`) is the same shape one
/// layer down -- and only the shape: that one swaps the dispatcher on the scene's **`Listing`**, the
/// directory read, and there is no claim seam anywhere in `BrowserScene`. The claim happens here, so
/// the settable dispatcher has to be here too.
///
/// Without it a spec cannot reach `dispatchShellChoices()` at all. `Owner::run_or_inline` runs the op
/// on the caller's stack on this BSP (`storage/owner.h:28-30`, and again at `:40`), and
/// `BrowserScene::enter` always appends a non-empty `FileItem::filename`
/// (`scenes/browser/browser_scene.cpp:879-883`), so every example that presses a real file reaches a
/// real `SampleBrowser::claimCurrentFile` (`sample_browser.cpp:814`): the "Working" animation
/// (`:824`), a card read down through `Sample::determinePitch`, an `AudioFileManager` allocation, and
/// the `openUI(&soundEditor)` tail (`:1112-1119`). With it, an example asserts something stronger
/// than it could otherwise -- that the claim was *requested*, against a browser pointed at the right
/// directory and filename, without it running.
///
/// It changes nothing on the device: nothing outside a spec calls this, so the shipped dispatcher is
/// `Owner::run_or_inline` from the first tick, exactly as `sample_browser.cpp:392` spells it.
///
/// @param dispatch The dispatcher. Must not be null; pass `&deluge::storage::Owner::run_or_inline`
///                 to put the shipped one back.
void setSampleClaimDispatch(SampleClaimDispatch dispatch);

/// @brief Take whatever the ported browser chose, if anything, and perform it.
///
/// @par Call this after every `Shell::tick`, and do not gate it
///
/// `BrowserScene::take_choice()` is a one-slot outbox that announces nothing -- `enter` files it and
/// returns an ordinary `Result` (`scenes/browser/browser_scene.cpp:895-897`), so no `Action` says a
/// choice is waiting and nothing enforces the polling. That is `MenuScene::take_action()`'s hazard
/// (`scenes/menu/menu_scene.h:612-617`, the poll at `:644`) in the same shape, and it lands as the same requirement on
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
/// `return close()` (`scenes/browser/browser_scene.cpp:896-897`), and `close()` is
/// `Action::Kind::Remove` naming the browser's own kind (`:917-926`). The router applies that inside
/// the same `Shell::tick` that routed the press, so by the time this drain runs the browser is off
/// the stack and the ported menu underneath is frontmost again. Nothing here may assume otherwise --
/// there is no state in which a choice is pending *and* the browser is still up.
///
/// @par Order against `dispatchShellActions`, and whether both can be pending at once
///
/// Both drains run after `Shell::tick`, actions first. **Yes, both can be pending on the same
/// tick**, and the path is real rather than theoretical: the browser's `Remove` is applied at
/// `Shell::tick`'s step 3 (`shell/shell.cpp:329`), so the menu underneath is frontmost again by
/// steps 5-6 and gets its own `after_render()` at step 7 (`:399-407`) -- on the very tick the choice
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
