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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "gui/menu_item/toggle.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::delay {

class PingPong final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->value_ = soundEditor.currentModControllable->delay.pingPong; }
	void writeCurrentValue() override { soundEditor.currentModControllable->delay.pingPong = this->value_; }
};

} // namespace deluge::gui::menu_item::delay
