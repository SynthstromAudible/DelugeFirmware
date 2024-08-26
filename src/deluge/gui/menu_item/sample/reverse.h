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
#include "gui/menu_item/sample/utils.h"
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::sample {
class Reverse final : public Toggle, public FormattedTitle {
public:
	Reverse(l10n::String name, l10n::String title_format_str, uint8_t source_id)
	    : Toggle(name), FormattedTitle(title_format_str, source_id + 1), source_id_{source_id} {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t) override {
		return isSampleModeSample(modControllable, source_id_);
	}

	bool usesAffectEntire() override { return true; }

	void readCurrentValue() override {
		const Source& source = soundEditor.currentSound->sources[source_id_];
		setValue(source.sampleControls.reversed);
	}

	void writeCurrentValue() override {
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			const Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[source_id_];

					soundDrum->unassignAllVoices();
					source->setReversed(getValue());
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->unassignAllVoices();
			Source& source = soundEditor.currentSound->sources[source_id_];
			source.setReversed(getValue());
		}
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		const bool reversed = getValue();
		const Icon& icon = OLED::directionIcon;
		OLED::main.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y + kHorizontalMenuSlotYOffset,
		                            reversed);
	}

	void getColumnLabel(StringBuf& label) override { label.append(l10n::get(l10n::String::STRING_FOR_PLAY)); }

	void getNotificationValue(StringBuf& valueBuf) override {
		valueBuf.append(l10n::get(getValue() ? l10n::String::STRING_FOR_ON : l10n::String::STRING_FOR_OFF));
	}

	void selectEncoderAction(int32_t offset) override {
		if (parent != nullptr && parent->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL) {
			// reverse direction
			offset *= -1;
		}
		Toggle::selectEncoderAction(offset);
	}

private:
	uint8_t source_id_;
};
} // namespace deluge::gui::menu_item::sample
