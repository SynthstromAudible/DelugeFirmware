/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/sample_browser/synth.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "util/functions.h"
#include <cstdint>

namespace deluge::gui::context_menu::sample_browser {
Synth synth{};

namespace {
/// The dispatched op for the Multisamples option: the folder import, on the storage owner until the importers move.
/// On failure, closes just the ContextMenu (the import already closes everything, including this menu, on success).
void runMultisamplesOp(void*) {
	if (!sampleBrowser.importFolderAsMultisamples()) {
		synth.close();
	}
}

/// Closes just the ContextMenu when a claim it asked for fails. A claim that succeeds closes everything, including this
/// menu, itself.
void closeOnFailure(bool success) {
	if (!success) {
		synth.close();
	}
}
} // namespace

char const* Synth::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_LOAD_FILES);
}

std::span<char const*> Synth::getOptions() {
	using enum l10n::String;
	static char const* options[] = {
	    l10n::get(STRING_FOR_MULTISAMPLES), //<
	    l10n::get(STRING_FOR_BASIC),        //<
	    l10n::get(STRING_FOR_SINGLE_CYCLE), //<
	    l10n::get(STRING_FOR_WAVETABLE),    //<
	};
	return {options, 4};
}

bool Synth::isCurrentOptionAvailable() {

	// Multisamples (load entire folder and auto-detect ranges). Will delete all previous Ranges.
	if (currentOption == 0) {
		return (soundEditor.currentSound->getSynthMode() != SynthMode::RINGMOD);
	}

	// Apart from that option, none of the other ones are valid if currently sitting on a folder-name.
	if (sampleBrowser.getCurrentFileItem()->isFolder) {
		return false;
	}

	switch (currentOption) {
	case 1:
		// "Basic" Sample - unavailable if ringmod.
		return (soundEditor.currentSound->getSynthMode() != SynthMode::RINGMOD);

	case 3:
		// WaveTable
		// No break - return true.

	case 2:
		// Single-cycle - available even if we're locked (because of there being other Ranges) to this happening either
		// in Sample or WaveTable mode. Although, the user could get an error after selecting the option - e.g. if
		// we're locked to WaveTable mode, they've selected single-cycle, and it realises it can't do this
		// combination of things because it's a stereo file.
		return true;

	default:
		__builtin_unreachable();
		return false;
	}
}

bool Synth::acceptCurrentOption() {
	// Never report failure to ContextMenu::buttonAction() synchronously (it would close this menu at once): a claim,
	// and the import, report theirs once they know it.
	switch (currentOption) {
	case 0: // Multisamples
		deluge::storage::Owner::run_or_inline(&runMultisamplesOp, nullptr);
		break;
	case 1: // Basic
		sampleBrowser.requestClaim(0, 0, 0, &closeOnFailure);
		break;
	case 2: // Single-cycle
		sampleBrowser.requestClaim(2, 2, 1, &closeOnFailure);
		break;
	case 3:                                                   // WaveTable
		sampleBrowser.requestClaim(1, 1, 2, &closeOnFailure); // Could probably also be 0,0,2
		break;
	default:
		__builtin_unreachable();
	}
	return true;
}

ActionResult Synth::padAction(int32_t x, int32_t y, int32_t on) {
	return sampleBrowser.padAction(x, y, on);
}

bool Synth::canSeeViewUnderneath() {
	return sampleBrowser.canSeeViewUnderneath();
}
} // namespace deluge::gui::context_menu::sample_browser
