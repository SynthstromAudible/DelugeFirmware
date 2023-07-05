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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"

namespace menu_item::gate {
#if HAVE_OLED
// Why'd I put two NULLs? (Rohan)
// Gets updated in gate::Selection lol (Kate)
static char const* mode_options[] = {"V-trig", "S-trig", NULL, NULL};
#else
static char const* mode_options[] = {"VTRI", "STRI", NULL, NULL};
#endif

#if HAVE_OLED
static char mode_title[] = "Gate outX mode";
#else
static char* mode_title = nullptr;
#endif

class Mode final : public Selection {
public:
	Mode() : Selection(mode_title) { basicOptions = mode_options; }
	void readCurrentValue() { soundEditor.currentValue = cvEngine.gateChannels[soundEditor.currentSourceIndex].mode; }
	void writeCurrentValue() { cvEngine.setGateType(soundEditor.currentSourceIndex, soundEditor.currentValue); }
};

} // namespace menu_item::gate
