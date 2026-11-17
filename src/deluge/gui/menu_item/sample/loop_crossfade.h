/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include "gui/menu_item/sample/utils.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::sample {
class LoopCrossfade final : public IntegerWithOff, public FormattedTitle {
public:
	LoopCrossfade(l10n::String name, l10n::String title_format_str, uint8_t source_id)
	    : IntegerWithOff(name), FormattedTitle(title_format_str, source_id + 1), source_id_{source_id} {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t) const override {
		return isSampleModeSample(modControllable, source_id_);
	}

	// The menu works in 10 ms steps, but the Source keeps milliseconds
	static constexpr int32_t kMsPerStep = 10;

	void readCurrentValue() override {
		setValue(soundEditor.currentSound->sources[source_id_].loopCrossfadeMs / kMsPerStep);
	}

	void writeCurrentValue() override {
		uint16_t ms = getValue() * kMsPerStep;

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			const Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					static_cast<SoundDrum*>(thisDrum)->sources[source_id_].loopCrossfadeMs = ms;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->sources[source_id_].loopCrossfadeMs = ms;
		}
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxLoopCrossfadeMs / kMsPerStep; }
	int32_t getDisplayValue() override { return getValue() * kMsPerStep; }
	const char* getUnit() override { return "MS"; }
	void getNotificationValue(StringBuf& value) override {
		if (getValue() == 0) {
			value.append(l10n::get(l10n::String::STRING_FOR_OFF));
		}
		else {
			value.appendInt(getDisplayValue());
			value.append(getUnit());
		}
	}

	// A time in ms, drawn as a ramp like the compressor and sidechain attack times
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return ATTACK; }
	void getColumnLabel(StringBuf& label) override {
		label.append(l10n::get(l10n::String::STRING_FOR_LOOP_CROSSFADE_SHORT));
	}

private:
	uint8_t source_id_;
};
} // namespace deluge::gui::menu_item::sample
