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

#include "scenes/browser/browser_scene.h"

#include <optional>
#include <string_view>

namespace deluge::gui::ui {

using deluge::scenes::browser::browser_scene;
using deluge::scenes::browser::BrowserChoice;
using deluge::scenes::browser::BrowserId;

namespace {

/// @brief Load the chosen sample, the way `SampleBrowser::enterKeyPress`'s file arm does.
///
/// @warning **Unpaid. This is F3 task 2's, and the body is deliberately empty until then.** The
///          drain above it is complete and covered; what is missing is the one statement legacy's
///          file arm ends in, `deluge::storage::Owner::run_or_inline(&SampleBrowser::runClaimCurrentFileOp,
///          nullptr)` (`gui/ui/browser/sample_browser.cpp:392`), plus whatever it takes to point the
///          legacy `sampleBrowser` at @p target first -- `claimCurrentFile` reads `currentDir` and
///          `getCurrentFileItem()` for itself rather than taking a path
///          (`SampleBrowser::getCurrentFilePath`, `:503-514`), which is the open question task 2
///          owns.
///
/// @warning **What a spec would run if this body existed today**, and the reason the choosing half
///          above it is declared separately. On the cooperative/host BSP `Owner::run_or_inline` is
///          inline on the caller's stack (`storage/owner.h:28-30`, `:40`), so a `deluge_scenes_spec`
///          example that reached here would run `SampleBrowser::claimCurrentFile`
///          (`sample_browser.cpp:778`) for real: the "Working" loading animation (`:788`), a card
///          read down through `Sample::determinePitch`, an `AudioFileManager` allocation for the
///          sample, `audioFileIsNowSet()` (`:1061`) and the `closeUI`/`openUI(&soundEditor)` tail
///          (`:1076-1083`) -- inside the assertion. That is this seam's version of the export F2
///          task 3 could not let an example start.
///
/// @warning **And the choosing half above does NOT keep an example out of it. That makes this task
///          2's first move rather than an afterthought.** F2 task 3's `stemExportTypeForRootUI()`
///          was safe in a spec because its `std::nullopt` branch is an ordinary root-UI state; that
///          property does not exist here. `BrowserScene::enter` always appends a non-empty
///          `FileItem::filename` (`scenes/browser/browser_scene.cpp:872-876`), so
///          `sampleClaimTargetFor` **always** engages on a real press, and
///          `sim/scenes_spec/browser_choice_spec.cpp`'s guest-tick example presses a real file.
///
///          The mechanism that does close it is one this repository already uses for this exact
///          hazard, one layer down: `BrowserScene::set_dispatch`
///          (`scenes/browser/browser_scene.h:373-381`) swaps `storage::Owner::run` for a dispatcher
///          that stores the op instead of running it. The claim reaches the owner the same way the
///          listing does (`gui/ui/browser/sample_browser.cpp:392` is `Owner::run_or_inline`), so the
///          same swap applies here -- and it buys more than safety: with it the drain example can
///          assert the claim was **requested**, against the right target, without it ever running.
///          Nothing today can observe that wire at all.
///
/// @warning **The file arm is a fan-out and only its `else` is deferred, so two of legacy's three
///          statements have no port at all. This is a live divergence under the toggle, not a
///          division of labour.** It is a row in `src/scenes/README.md`'s open-divergence table, and
///          the plan's own migration §10.2 item 4 needs qualifying when it is marked closed for the
///          `Sample` descriptor: **it is not closed for shift+select.**
///
///            * `AudioEngine::stopAnyPreviewing()` (`:342`) runs on the folder arm as well as the
///              file arm, and the port drops it on both. No choice dispatcher can pay it, because
///              the folder arm files no choice.
///            * **The slicer fan-out (`:371-384`) has no port and no route.** Legacy opens the
///              slicer on `soundEditor.editingKit() && Buttons::isShiftButtonPressed()` when
///              `canImportWholeKit()` (the `openUI(&slicer)` at `:378`), and announces
///              `STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT` when it does not (`:381-382`).
///              `Descriptor::accept` carries no shift bit (`scenes/browser/browser_scene.h:236`)
///              and `sample_accept` answers `Defer` unconditionally
///              (`scenes/browser/browser_scene.cpp:202-211`), so **shift+select on a file loads the
///              sample instead of opening the slicer**. `SlicerScene`
///              (`scenes/slicing/slicer_scene.h`) exists, so this is a missing *route*, not a
///              missing port. It cannot be recovered from here either: this runs a tick after the
///              press, so `Buttons::isShiftButtonPressed()` read from this function is a different
///              moment than the one that filed the choice. Closing it means the shift state
///              travelling on `BrowserChoice`.
///
/// @param target Where the claim would load from.
void claimSampleFile(const SampleClaimTarget& target) {
	(void)target;
}

} // namespace

std::optional<SampleClaimTarget> sampleClaimTargetFor(std::string_view path) {
	// `SampleBrowser::getCurrentFilePath` (`gui/ui/browser/sample_browser.cpp:503-514`) run
	// backwards, statement for statement: it appends the separator only when `currentDir` is
	// non-empty (`:505-507`), so a path with no separator is a file at the card root and the whole
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
	// (`scenes/browser/browser_scene.cpp:872-876`) -- so this is defence in depth against a caller
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
	// choice out and resets the member in the same call (`scenes/browser/browser_scene.cpp:409-413`).
	// So an early return above this line would not be "nothing to do", it would be "the choice stays
	// in the outbox until something else asks", which is the stale delivery the header describes.
	const std::optional<BrowserChoice> choice = browser_scene().take_choice();
	if (!choice.has_value()) {
		return;
	}

	switch (choice->id) {
	case BrowserId::Sample:
		// The only descriptor that defers (`scenes/browser/browser_scene.cpp:202-211`). The split is
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
		// `BrowserScene::enter` files `id_` (`browser_scene.cpp:889`), and `BrowserScene::on_enter`
		// refuses the scene outright before assigning it (`:349-353`, the guard at `:350-352`) rather
		// than letting an out-of-range arg through. Spelled out because `-Werror=switch` counts it, and the
		// alternative is the `default:` the header rules out.
		break;
	}
}

} // namespace deluge::gui::ui
