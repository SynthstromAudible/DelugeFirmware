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

#include "gui/ui/shell_drum_creator.h"
#include "gui/ui/shell_choice_dispatch.h"

#include "gui/ui/ui.h" // uiNeedsRendering
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "storage/multi_range/multisample_range.h"
#include "util/shell_menu_entry.h"

#include <iterator>
#include <utility>

namespace deluge::gui::ui {

namespace {

/// The drum the creator made for the guest now open, until the guest closes.
SoundDrum* creatorDrum = nullptr;

/// Whether @p kit still holds @p drum: the guest's own loads can replace a kit's drums before it closes.
bool kitHolds(const Kit& kit, const SoundDrum* drum) {
	for (const Drum* d = kit.firstDrum; d != nullptr; d = d->next) {
		if (d == drum) {
			return true;
		}
	}
	return false;
}

/// `soundEditor.getCurrentAudioFileHolder()->filePath.isEmpty()` for a new drum: its first range's file.
bool fileChosen(SoundDrum& drum) {
	if (std::ssize(drum.sources[0].ranges) == 0) {
		return false;
	}
	const auto* range = static_cast<const MultisampleRange*>(drum.sources[0].ranges.getElement(0));
	return !range->sampleHolder.filePath.empty();
}

} // namespace

bool openShellDrumCreator(InstrumentClip* clip, SoundDrum* drum) {
	// `enterDrumCreator`'s `soundEditor.setup(clip, &menu_item::file0SelectorMenu, 0)`: a kit clip under the clip view,
	// on source 0's file selector, whose hook pushes the browser.
	if (!openShellMenu(clip, /*sourceIndex=*/0, /*performanceView=*/false, /*sessionOrArrangerRoot=*/false,
	                   /*sessionViewRoot=*/false, /*kitGlobalFx=*/false, ShellMenuNode::Osc0FileSelector)) {
		return false;
	}
	creatorDrum = drum;
	// The browser is pushed from the menu's next render, after this returns; `enterDrumCreator` opens it with no
	// `beginScreen`, so the file selector's own blink is never armed.
	setShellBrowserFromDrumCreator(true);
	return true;
}

void forgetShellDrumCreator() {
	creatorDrum = nullptr;
	setShellBrowserFromDrumCreator(false);
}

void discardShellDrumCreatorBeforeClose() {
	SoundDrum* drum = std::exchange(creatorDrum, nullptr);
	if (drum == nullptr) {
		return;
	}
	Kit* kit = getCurrentKit();
	if (kit == nullptr || !kitHolds(*kit, drum)) {
		return;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	// `SampleBrowser::exitAction` (`gui/ui/browser/sample_browser.cpp`): only with no file chosen, and only while the
	// kit holds a drum no row uses.
	if (kit->getFirstUnassignedDrum(clip) != nullptr && !fileChosen(*drum)) {
		instrumentClipView.deleteDrum(drum);
		uiNeedsRendering(&instrumentClipView);
	}
}

} // namespace deluge::gui::ui
