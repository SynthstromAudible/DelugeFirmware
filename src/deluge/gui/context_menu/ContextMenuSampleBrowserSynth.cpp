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

#include <ContextMenuSampleBrowserSynth.h>
#include <samplebrowser.h>
#include "numericdriver.h"
#include "functions.h"
#include "soundeditor.h"
#include "source.h"
#include "sound.h"
#include "FileItem.h"

ContextMenuSampleBrowserSynth contextMenuFileBrowserSynth;

ContextMenuSampleBrowserSynth::ContextMenuSampleBrowserSynth() {
#if HAVE_OLED
	title = "Load file(s)";
#endif
}

char const** ContextMenuSampleBrowserSynth::getOptions() {
	static char const* options[] = {"Multisamples", "Basic", "Single-cycle", "Wavetable"};
	return options;
}

int ContextMenuSampleBrowserSynth::getNumOptions() {
	return 4;
}

bool ContextMenuSampleBrowserSynth::isCurrentOptionAvailable() {

	// Multisamples (load entire folder and auto-detect ranges). Will delete all previous Ranges.
	if (currentOption == 0) return (soundEditor.currentSound->getSynthMode() != SYNTH_MODE_RINGMOD);

	// Apart from that option, none of the other ones are valid if currently sitting on a folder-name.
	if (sampleBrowser.getCurrentFileItem()->isFolder) return false;

	switch (currentOption) {
	case 1:
		// "Basic" Sample - unavailable if ringmod.
		return (soundEditor.currentSound->getSynthMode() != SYNTH_MODE_RINGMOD);

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

bool ContextMenuSampleBrowserSynth::acceptCurrentOption() {

	switch (currentOption) {
	case 0: // Multisamples
		return sampleBrowser.importFolderAsMultisamples();
	case 1: // Basic
		return sampleBrowser.claimCurrentFile(0, 0, 0);
	case 2: // Single-cycle
		return sampleBrowser.claimCurrentFile(2, 2, 1);
	case 3:                                             // WaveTable
		return sampleBrowser.claimCurrentFile(1, 1, 2); // Could probably also be 0,0,2
	default:
		__builtin_unreachable();
		return false;
	}
}

int ContextMenuSampleBrowserSynth::padAction(int x, int y, int on) {
	return sampleBrowser.padAction(x, y, on);
}

bool ContextMenuSampleBrowserSynth::canSeeViewUnderneath() {
	return sampleBrowser.canSeeViewUnderneath();
}
