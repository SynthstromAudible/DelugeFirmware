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
/// @par What is NOT missing, so task 2 does not port it twice
///
/// `SampleBrowser::enterKeyPress`'s file arm is three statements, and only the last is deferred.
/// `AudioEngine::stopAnyPreviewing()` (`:342`) and the slicer fan-out (`:372-384`) are the other
/// two; both are recorded as findings on this task's report rather than performed here, because the
/// first runs on the folder arm too (which this dispatcher never sees) and the second reads
/// `Buttons::isShiftButtonPressed()` at a moment a tick later than the press that filed the choice.
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
