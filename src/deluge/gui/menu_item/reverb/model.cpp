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
#include "gui/menu_item/reverb/model.h"
#include "model/edits/setting_edit.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item::reverb {

// project.reverb.model is authoritative; SetSetting rebuilds the reverb at it. Out-of-line
// because reading currentSong needs song.h, which includes this header.
void Model::readCurrentValue() {
	this->setValue(util::to_underlying(currentSong->project.reverb.model));
}

void Model::writeCurrentValue() {
	deluge::edits::songSettingFor("reverb/model", deluge::serialization::Value::makeNumber(this->getValue()));
}

} // namespace deluge::gui::menu_item::reverb
