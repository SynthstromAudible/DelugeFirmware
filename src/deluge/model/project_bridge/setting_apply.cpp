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

#include "model/project_bridge/setting_apply.h"

#include "engines/deluge/settings.h"
#include "processing/sound/sound.h"

namespace deluge::bridge {

Error applySetting(::Sound& /*sound*/, std::string_view path, const deluge::serialization::Value& value,
                   ::Song& /*song*/) {
	if (!deluge::instrument::check_setting(path, value)) {
		return Error::UNSPECIFIED;
	}
	return Error::UNSPECIFIED; // no path has a setter here yet
}

} // namespace deluge::bridge
