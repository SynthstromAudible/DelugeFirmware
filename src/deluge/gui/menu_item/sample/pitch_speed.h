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
#include "gui/menu_item/sample/utils.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

#include <gui/context_menu/sample_browser/kit.h>

namespace deluge::gui::menu_item::sample {
class PitchSpeed final : public Selection {
public:
	PitchSpeed(l10n::String newName, uint8_t sourceId) : Selection(newName), source_id_{sourceId} {}

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return getCurrentAudioClip() != nullptr || isSampleModeSample(modControllable, source_id_);
	}

	void readCurrentValue() override {
		const auto& sampleControls = getCurrentSampleControls(source_id_);
		setValue(sampleControls.pitchAndSpeedAreIndependent);
	}

	void writeCurrentValue() override {
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			const Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[source_id_];
					source->sampleControls.pitchAndSpeedAreIndependent = this->getValue();
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			auto& sampleControls = getCurrentSampleControls(source_id_);
			sampleControls.pitchAndSpeedAreIndependent = this->getValue();
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {l10n::getView(l10n::String::STRING_FOR_LINKED), l10n::getView(l10n::String::STRING_FOR_INDEPENDENT)};
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		const Icon& icon = getValue() ? OLED::pitchSpeedIndependentIcon : OLED::pitchSpeedLinkedIcon;
		OLED::main.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y - 1);
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Selection::configureRenderingOptions(options);
		options.label = getOptions(OptType::SHORT)[getValue()].data();
	}

private:
	uint8_t source_id_;
};
} // namespace deluge::gui::menu_item::sample
