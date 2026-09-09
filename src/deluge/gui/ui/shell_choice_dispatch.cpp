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

/// @file
/// @brief The choice seam's consumer. See `shell_choice_dispatch.h` for why it lives here, why its
///        caller may not gate it, and what the split between the choosing and acting halves is for.

#include "gui/ui/shell_choice_dispatch.h"

#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"
#include "storage/owner.h"

#include <optional>
#include <string_view>

namespace deluge::gui::ui {

using deluge::scenes::browser::browser_scene;
using deluge::scenes::browser::BrowserChoice;
using deluge::scenes::browser::BrowserId;

namespace {

/// @brief What the claim is dispatched onto. `setSampleClaimDispatch`'s subject; see its note for
///        why a spec must be able to move it.
///
/// The shipped value is the one `SampleBrowser::enterKeyPress`'s file arm uses
/// (`gui/ui/browser/sample_browser.cpp:392`), so on the device this indirection resolves to exactly
/// the call legacy makes.
SampleClaimDispatch claimDispatch = &deluge::storage::Owner::run_or_inline;

/// @brief Load the chosen sample, the way `SampleBrowser::enterKeyPress`'s file arm does.
///
/// @par Two statements, because legacy's one is only half the state it needs
///
/// Legacy's file arm is a single dispatch,
/// `deluge::storage::Owner::run_or_inline(&SampleBrowser::runClaimCurrentFileOp, nullptr)`
/// (`gui/ui/browser/sample_browser.cpp:392`), and that is the second statement here -- with a
/// different op, because legacy's closes a browser this one never opened (`runShellSampleClaimOp`).
/// The first exists because **`claimCurrentFile()`
/// takes no path**: it reads `Browser::currentDir` and `Browser::getCurrentFileItem()` for itself,
/// which is what `SampleBrowser::getCurrentFilePath()` (`:539-550`) composes. Legacy never has to
/// set them because the user browsed there; this dispatcher is handed a `BrowserChoice::path`
/// (`scenes/browser/browser_scene.h:248-253`) by a scene that has **already closed**, and
/// `BrowserScene::on_exit` empties the shared catalogue on the way out
/// (`scenes/browser/browser_scene.cpp:401`) exactly as `Browser::close()` does. So the state legacy
/// would have left behind is rebuilt from the path first, by
/// `SampleBrowser::selectFileForClaim` -- a legacy-side accessor, argued at its own declaration
/// (`gui/ui/browser/sample_browser.h:56-99`), because `fileIndexSelected` and
/// `setEnteredTextFromCurrentFilename()` are `Browser`'s protected state and "the selected index
/// indexes `fileItems`" is `Browser`'s invariant. Reaching around the class from here would have
/// meant this file owning that invariant instead.
///
/// @par Why the dispatch is read through `claimDispatch` rather than named directly
///
/// Because on this BSP `Owner::run_or_inline` runs the op on the caller's stack
/// (`storage/owner.h:28-30`, and again at `:40`), and every example that presses a real file gets
/// here: `BrowserScene::enter` always appends a non-empty `FileItem::filename`
/// (`scenes/browser/browser_scene.cpp:879-883`), so `sampleClaimTargetFor` always engages. Without a
/// settable seam, `sim/scenes_spec/browser_choice_spec.cpp`'s guest-tick example would run a real
/// `SampleBrowser::claimCurrentFile` (`sample_browser.cpp:814`) inside its assertion. See
/// `setSampleClaimDispatch`.
///
/// @warning **The file arm is a fan-out and only its `else` is deferred, so two of legacy's three
///          statements have no port at all. This is a live divergence under the toggle, not a
///          division of labour.** It is a row in `src/scenes/README.md`'s open-divergence table, and
///          the plan's own migration §10.2 item 4 needs qualifying when it is marked closed for the
///          `Sample` descriptor: **it is not closed for shift+select.**
///
///            * `AudioEngine::stopAnyPreviewing()` (`:342`) runs on the folder arm as well as the
///              file arm, and the port drops it on both. **Deliberately still dropped on both, and
///              two things say so rather than one.** *Structurally:* the folder arm files no choice,
///              so this dispatcher is not a site the folder half can ever be paid from -- paying only
///              the file half would make the gap non-uniform, which is worse than the gap.
///              *Materially:* it would buy nothing today either, because the only thing that ever
///              starts a preview is `SampleBrowser::currentFileChanged` and the ported scene's
///              equivalent hook is empty -- there is nothing to stop
///              (`sim/scenes_spec/browser_scene_spec.cpp:211-218`). Pay both halves when the scene
///              grows a preview, not before.
///            * **The slicer fan-out (`:371-384`) has no port and no route.** Legacy opens the
///              slicer on `soundEditor.editingKit() && Buttons::isShiftButtonPressed()` when
///              `canImportWholeKit()` (the `openUI(&slicer)` at `:378`), and announces
///              `STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT` when it does not (`:381-382`).
///              `Descriptor::accept` carries no shift bit (`scenes/browser/browser_scene.h:236`)
///              and `sample_accept` answers `Defer` unconditionally
///              (`scenes/browser/browser_scene.cpp:209-218`), so **shift+select on a file loads the
///              sample instead of opening the slicer**. `SlicerScene`
///              (`scenes/slicing/slicer_scene.h`) exists, so this is a missing *route*, not a
///              missing port. It cannot be recovered from here either: this runs a tick after the
///              press, so `Buttons::isShiftButtonPressed()` read from this function is a different
///              moment than the one that filed the choice. Closing it means the shift state
///              travelling on `BrowserChoice`.
///
/// @param target Where the claim loads from.
void claimSampleFile(const SampleClaimTarget& target) {
	if (!sampleBrowser.selectFileForClaim(target.dir, target.name)) {
		// Nothing is selected, so `claimCurrentFile()` would read a null `getCurrentFileItem()`
		// (`gui/ui/browser/browser.cpp:1138-1143`) straight through `getCurrentFilePath()`'s
		// unguarded `currentFileItem->filename` (`sample_browser.cpp:545-547`). Legacy never reaches
		// that because it only ever claims what it just listed.
		return;
	}
	// `SampleBrowser::enterKeyPress`'s file arm (`gui/ui/browser/sample_browser.cpp:392`):
	// fire-and-forget onto the storage owner, the dispatch's own result discarded exactly as legacy
	// discards it. The op is this file's rather than legacy's, for the one reason
	// `runShellSampleClaimOp` gives.
	(void)claimDispatch(&runShellSampleClaimOp, nullptr);
}

} // namespace

void runShellSampleClaimOp(void* unused) {
	(void)unused;
	// The three defaults plus `loadWithoutExiting = true`, read from `kShellSampleClaimArgs` rather
	// than written as literals so `sim/scenes_spec/browser_choice_spec.cpp` can pin the flag that
	// keeps `closeUI` out of this path. See the header for what `closeUI` would do here.
	if (sampleBrowser.claimCurrentFile(kShellSampleClaimArgs.mayDoPitchDetection,
	                                   kShellSampleClaimArgs.mayDoSingleCycle, kShellSampleClaimArgs.mayDoWaveTable,
	                                   kShellSampleClaimArgs.loadWithoutExiting)) {
		// The two statements `loadWithoutExiting` suppresses that the guest still wants
		// (`gui/ui/browser/sample_browser.cpp:1116-1118` and `:1125`), on legacy's own condition:
		// its tail runs only on success, every failure inside `claimCurrentFile` having returned
		// above `:1108`.
		//
		// **`loadWithoutExiting` is why the condition can only be honoured from in here.** The claim
		// is dispatched fire-and-forget (`claimSampleFile`, and `sample_browser.cpp:392` before it),
		// and on the Embassy BSP `Owner::run` queues it onto a fiber (`storage/owner.h:43`), so
		// `dispatchShellChoices` cannot know whether the load succeeded. Legacy's re-rooting is
		// gated on success, so it belongs where the success is known -- beside the render request
		// that was recovered for the same reason.
		uiNeedsRendering(&audioClipView);
		// `parentMenuHeadingTo->focusChild(menuItemHeadingTo); menuItemNavigationRecord[0] =
		// parentMenuHeadingTo; navigationDepth = 0` (`:1116-1118`), in the ported world's own terms:
		// the record is `MenuState::path`/`depth` and only `MenuScene` may write it, so the three
		// statements are one entry point reached through the `menu_scene()` singleton -- the route
		// `scenes/menu/nodes/context_menu_items.cpp:76` already takes to `context_menu_scene()`.
		//
		// It answers false, harmlessly, whenever there is nothing to move: the guest's menu is the
		// only thing that can have opened the ported browser, so the only way here with no recorded
		// opener is a caller that is not `dispatchShellChoices`.
		(void)deluge::scenes::menu_scene().reroot_to_scene_opener_page();
	}
}

void setSampleClaimDispatch(SampleClaimDispatch dispatch) {
	if (dispatch != nullptr) {
		claimDispatch = dispatch;
	}
}

std::optional<SampleClaimTarget> sampleClaimTargetFor(std::string_view path) {
	// `SampleBrowser::getCurrentFilePath` (`gui/ui/browser/sample_browser.cpp:539-550`) run
	// backwards, statement for statement: it appends the separator only when `currentDir` is
	// non-empty (`:541-543`), so a path with no separator is a file at the card root and the whole
	// of it is the filename.
	const size_t slash = path.rfind('/');
	if (slash == std::string_view::npos) {
		if (path.empty()) {
			return std::nullopt;
		}
		return SampleClaimTarget{.dir = {}, .name = path};
	}
	// A trailing separator names a directory, not a file. `BrowserScene::enter` cannot build one --
	// it appends `item->filename`, which a `FileItem` never has empty
	// (`scenes/browser/browser_scene.cpp:879-883`) -- so this is defence in depth against a caller
	// that is not the scene, in the same spirit as `openPerformanceView`'s `DISABLED` arm
	// (`gui/ui/shell_action_dispatch.cpp:86-88`) and unreachable for the same kind of reason.
	const std::string_view name = path.substr(slash + 1);
	if (name.empty()) {
		return std::nullopt;
	}
	return SampleClaimTarget{.dir = path.substr(0, slash), .name = name};
}

void dispatchShellChoices() {
	// One read, unconditionally, and the read is what empties the slot -- `take_choice()` moves the
	// choice out and resets the member in the same call (`scenes/browser/browser_scene.cpp:416-420`).
	// So an early return above this line would not be "nothing to do", it would be "the choice stays
	// in the outbox until something else asks", which is the stale delivery the header describes.
	const std::optional<BrowserChoice> choice = browser_scene().take_choice();
	if (!choice.has_value()) {
		return;
	}

	switch (choice->id) {
	case BrowserId::Sample:
		// The only descriptor that defers (`scenes/browser/browser_scene.cpp:209-218`). The split is
		// made before anything acts, so `sim/scenes_spec/browser_choice_spec.cpp` can pin the
		// decision over every path shape without the arm below it having to be reachable.
		if (const std::optional<SampleClaimTarget> target = sampleClaimTargetFor(choice->path)) {
			claimSampleFile(*target);
		}
		break;

	case BrowserId::MidiDeviceDefinition:
		// Never filed: this descriptor performs its own accept and answers `Close`, because
		// `StorageManager::loadMidiDeviceDefinitionFile` is in `storage/`, which the scenes layer may
		// include (`scenes/browser/browser_scene.h:207-209`). Spelled out rather than left to a
		// `default:` so that a future descriptor which starts deferring lands here loudly.
		break;

	case BrowserId::Count:
		// Not a browser -- it is the table's size (`scenes/browser/browser_scene.h:118`). Unreachable:
		// `BrowserScene::enter` files `id_` (`browser_scene.cpp:896`), and `BrowserScene::on_enter`
		// refuses the scene outright before assigning it (`:356-360`, the guard at `:357-359`) rather
		// than letting an out-of-range arg through. Spelled out because `-Werror=switch` counts it, and the
		// alternative is the `default:` the header rules out.
		break;
	}
}

} // namespace deluge::gui::ui
