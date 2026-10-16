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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/edits/arp_setting_for.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::arpeggiator {
class Octaves final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->numOctaves); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		const deluge::serialization::Value value = deluge::serialization::Value::makeNumber(this->getValue());

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			for (Drum* thisDrum = getCurrentKit()->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type != DrumType::GATE) {
					deluge::edits::laneArpSettingFor(*thisDrum, "arpeggiator/numOctaves", value);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			deluge::edits::arpSettingFor(*soundEditor.currentArpSettings, "arpeggiator/numOctaves", value);
		}
	}

	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 8; }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return !soundEditor.editingGateDrumRow() && !soundEditor.editingKitAffectEntire();
	}
	void getColumnLabel(etl::istring& label) override { label.append(deluge::l10n::get(this->name)); }

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }
};
} // namespace deluge::gui::menu_item::arpeggiator
