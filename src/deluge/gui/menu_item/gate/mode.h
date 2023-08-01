/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"
#include "util/misc.h"

namespace menu_item::gate {
// Why'd I put two NULLs? (Rohan)
// Gets updated in gate::Selection lol (Kate)
static char const* mode_options[] = {HAVE_OLED ? "V-trig" : "VTRI", // V-trigger
                                     HAVE_OLED ? "S-trig" : "STRI", // S-trigger
                                     NULL, NULL};

static char mode_title[] = "Gate outX mode";

class Mode final : public Selection {
public:
	Mode() : Selection(HAVE_OLED ? mode_title : "") { basicOptions = mode_options; }
	void readCurrentValue() {
		soundEditor.currentValue = util::to_underlying(cvEngine.gateChannels[soundEditor.currentSourceIndex].mode);
	}
	void writeCurrentValue() {
		cvEngine.setGateType(soundEditor.currentSourceIndex, static_cast<GateType>(soundEditor.currentValue));
	}
};

} // namespace menu_item::gate
