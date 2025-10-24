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
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::arpeggiator {
class ArpUnpatchedParam : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	ArpUnpatchedParam(l10n::String newName, l10n::String title, int32_t newP, RenderingStyle style)
	    : UnpatchedParam(newName, title, newP), style_(style) {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingCVOrMIDIClip() && !soundEditor.editingNonAudioDrumRow();
	}

	void getColumnLabel(StringBuf& label) override {
		label.append(deluge::l10n::get(l10n::built_in::seven_segment, this->name));
	}

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return style_; }

private:
	RenderingStyle style_ = KNOB;
};

} // namespace deluge::gui::menu_item::arpeggiator
