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

namespace deluge::gui::menu_item::arpeggiator::midi_cv {
class ArpNonSoundInteger : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return kMinMenuValue; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.editingCVOrMIDIClip() || soundEditor.editingNonAudioDrumRow();
	}
	void configureRenderingOptions(const HorizontalMenuRenderingOptions &options) override {
		Integer::configureRenderingOptions(options);
		options.label = deluge::l10n::get(l10n::built_in::seven_segment, this->name);
	}
};
} // namespace deluge::gui::menu_item::arpeggiator::midi_cv
