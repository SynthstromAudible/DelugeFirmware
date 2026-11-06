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
/// @brief See `util/sample_preview_open.h`.

#include "util/sample_preview_open.h"

#include "gui/context_menu/sample_browser/kit.h"
#include "gui/context_menu/sample_browser/synth.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/shell_guest_services.h" // shellSamplePreviewSounds
#include "gui/ui/slicer.h"
#include "gui/ui/ui.h"

namespace deluge::gui::ui {

bool samplePreviewUIOpen() {
	return getCurrentUI() == &sampleBrowser || getCurrentUI() == &gui::context_menu::sample_browser::kit
	       || getCurrentUI() == &gui::context_menu::sample_browser::synth || getCurrentUI() == &slicer
	       || shellSamplePreviewSounds();
}

} // namespace deluge::gui::ui
