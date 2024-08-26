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
#include "gui/menu_item/audio_interpolation.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/sample/utils.h"
#include "model/sample/sample_controls.h"
#include "processing/sound/sound.h"
#include "utils.h"

namespace deluge::gui::menu_item::sample {
class Interpolation final : public AudioInterpolation, public FormattedTitle {
public:
	Interpolation(l10n::String name, l10n::String title_format_str, uint8_t source_id)
	    : AudioInterpolation(name), FormattedTitle(title_format_str, source_id + 1), source_id_{source_id} {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void readCurrentValue() override {
		const auto& sampleControls = getCurrentSampleControls(source_id_);
		setValue(sampleControls.interpolationMode);
	}

	void writeCurrentValue() override {
		const auto current_value = getValue<InterpolationMode>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			const Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					// Note: we need to apply the same filtering as stated in the isRelevant() function
					Source* source = &soundDrum->sources[source_id_];
					source->sampleControls.interpolationMode = current_value;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			auto& sampleControls = getCurrentSampleControls(source_id_);
			sampleControls.interpolationMode = current_value;
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t) override {
		if (getCurrentAudioClip() != nullptr) {
			return true;
		}
		const auto sound = static_cast<Sound*>(modControllable);
		Source& source = sound->sources[source_id_];
		return sound->getSynthMode() == ::SynthMode::SUBTRACTIVE
		       && ((source.oscType == OscType::SAMPLE && source.hasAtLeastOneAudioFileLoaded())
		           || source.oscType == OscType::INPUT_L || source.oscType == OscType::INPUT_R
		           || source.oscType == OscType::INPUT_STEREO);
	}

	void getColumnLabel(StringBuf& label) override {
		label.append(l10n::get(l10n::String::STRING_FOR_INTERPOLATION_SHORT));
	}

private:
	uint8_t source_id_;
};
} // namespace deluge::gui::menu_item::sample
