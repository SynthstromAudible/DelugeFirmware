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
#include "gui/ui/sound_editor.h"
#include "modulation/patch/patch_cable_set.h"

namespace deluge::gui::menu_item::filter {

class FilterValue : public patched_param::Integer {
public:
	FilterValue(l10n::String newName, l10n::String title, int32_t newP, bool hpf_)
	    : Integer(newName, title, newP), hpf(hpf_) {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
		return (hpf ? modControllable->hpfMode : modControllable->lpfMode) != FilterMode::OFF;
	}

	// 7Seg ONLY
	void drawValue() override {
		int32_t offValue = hpf ? kMaxMenuValue : kMinMenuValue;
		auto param = hpf ? deluge::modulation::params::LOCAL_HPF_FREQ : deluge::modulation::params::LOCAL_LPF_FREQ;
		if (getP() == param && this->getValue() == offValue
		    && !soundEditor.currentParamManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(param)) {
			display->setText(l10n::get(l10n::String::STRING_FOR_DISABLED));
		}
		else {
			patched_param::Integer::drawValue();
		}
	}

protected:
	bool hpf;
};
} // namespace deluge::gui::menu_item::filter
