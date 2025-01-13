/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "edit_name.h"
#include "gui/ui/rename/rename_clip_ui.h"
#include "gui/ui/rename/rename_drum_ui.h"
#include "gui/ui/rename/rename_output_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/song/song.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

void EditName::beginSession(MenuItem* navigatedBackwardFrom) {
	RenameUI* ui;

	// Figure out which rename UI we need, and do any necessary setup.
	Clip* clip = getCurrentClip();
	Output* output = getCurrentOutput();
	if (output->type == OutputType::AUDIO) {
		// XXX: Before naming clips was implemented, we made name shortcut inside
		// audio clips name the output. This should probably open a context menu to
		// select the naming target, since being able to name audio clips as well
		// would be quite nice...
		renameOutputUI.output = output;
		ui = &renameOutputUI;
	}
	else if (output->type == OutputType::KIT && !getRootUI()->getAffectEntire()) {
		ui = &renameDrumUI;
	}
	else {
		renameClipUI.clip = clip;
		ui = &renameClipUI;
	}

	// Done, go for it.
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	if (clip->type == ClipType::INSTRUMENT) {
		instrumentClipView.cancelAllAuditioning();
	}
	openUI(ui);
}

} // namespace deluge::gui::menu_item
