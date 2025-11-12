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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::sample {
class TimeStretch final : public Integer, public FormattedTitle {
public:
	TimeStretch(l10n::String name, l10n::String title_format_str, uint8_t source_id)
	    : Integer(name), FormattedTitle(title_format_str, source_id + 1), source_id_{source_id} {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t) override {
		return isSampleModeSample(modControllable, source_id_);
	}

	void readCurrentValue() override {
		const Source& source = soundEditor.currentSound->sources[source_id_];
		setValue(source.timeStretchAmount);
	}

	void writeCurrentValue() override {
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			const Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[source_id_];
					source->timeStretchAmount = getValue();
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			Source& source = soundEditor.currentSound->sources[source_id_];
			source.timeStretchAmount = getValue();
		}
	}
	[[nodiscard]] int32_t getMinValue() const override { return -48; }
	[[nodiscard]] int32_t getMaxValue() const override { return 48; }

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Integer::configureRenderingOptions(options);
		options.label = l10n::get(l10n::String::STRING_FOR_SPEED_SHORT);
	}
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

private:
	uint8_t source_id_;
};
} // namespace deluge::gui::menu_item::sample
