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
#include "gui/menu_item/lfo/shape.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::lfo {

class Type final : public Shape {
public:
	Type(deluge::l10n::String name, deluge::l10n::String type, uint8_t lfoId) : Shape(name, type), lfoId_(lfoId) {}
	void readCurrentValue() override {
		this->setValue(soundEditor.currentSound->lfoConfig[lfoId_].waveType);
	}
	void writeCurrentValue() override {
		soundEditor.currentSound->lfoConfig[lfoId_].waveType = this->getValue<LFOType>();
		// This fires unnecessarily for LFO2 assignments as well, but that's ok. It's not
		// entirely clear if we really need this for the LFO1, even: maybe the clock-driven resyncs
		// would be enough?
		soundEditor.currentSound->resyncGlobalLFO();
	}
	bool wrapAround() override { return true; }
	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override;

private:
	uint8_t lfoId_;
};

} // namespace deluge::gui::menu_item::lfo
