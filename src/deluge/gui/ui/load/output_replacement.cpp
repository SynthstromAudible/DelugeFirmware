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

#include "gui/ui/load/output_replacement.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/reversion.h"
#include "gui/views/randomize_drums.h"

namespace deluge::gui {

bool anOutputMayBeReplaced() {
	return instrumentLoad.inFlight() || loadPatternUI.loadInFlight() || reversion.inFlight()
	       || loadSongUI.isLoadInFlight() || randomizeDrums.inFlight() || sampleClaim.inFlight()
	       || folderImport.inFlight();
}

} // namespace deluge::gui
