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

#include "engines/effects/reverb.h"
#include "gui/menu_item/integer.h"
#include "model/edits/setting_edit.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace deluge::gui::menu_item::reverb {
class RoomSize final : public Integer {
public:
	using Integer::Integer;
	// The song's reverb record is the truth; SetSetting publishes it to the render.
	void readCurrentValue() override {
		this->setValue(std::round(currentSong->project.reverb.room_size * kMaxMenuValue));
	}
	void writeCurrentValue() override { deluge::edits::songSettingFromMenu("reverb/roomSize", this->getValue()); }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		switch (AudioEngine::reverb.model()) {
		case deluge::effects::ReverbModel::DIGITAL:
		case deluge::effects::ReverbModel::MUTABLE:
			return l10n::getView(STRING_FOR_TIME);
		default:
			return l10n::getView(this->name);
		}
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }
};
} // namespace deluge::gui::menu_item::reverb
