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
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::lfo {

class Type final : public Shape {
public:
	Type(deluge::l10n::String name, deluge::l10n::String type, uint8_t lfoId) : Shape(name, type), lfoId_(lfoId) {}
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->lfoConfig[lfoId_].waveType); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<LFOType>();
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->lfoConfig[lfoId_].waveType = current_value;
					// This fires unnecessarily for LFO2 assignments as well, but that's ok. It's not
					// entirely clear if we really need this for the LFO1, even: maybe the clock-driven resyncs
					// would be enough?
					soundDrum->resyncGlobalLFOs();
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->lfoConfig[lfoId_].waveType = current_value;
			// This fires unnecessarily for LFO2 assignments as well, but that's ok. It's not
			// entirely clear if we really need this for the LFO1, even: maybe the clock-driven resyncs
			// would be enough?
			soundEditor.currentSound->resyncGlobalLFOs();
		}
	}
	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override;
	[[nodiscard]] int32_t getColumnSpan() const override { return 2; }

private:
	const std::vector<uint8_t>& getLfoIconBitmap(LFOType type);
	const uint8_t getLfoIconBitmapXOffset(LFOType type);
	uint8_t lfoId_;
};

} // namespace deluge::gui::menu_item::lfo
