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

#include "ui/controls/page.h"

#include <cstddef>

namespace deluge::ui::controls {

Page page_of(const plugin::RemotePage& page) {
	Page out{
	    .section = page.section, .page_id = page.page_id, .group = page.group, .keeps_stutter = page.keeps_stutter};
	for (size_t i = 0; i < page.slots.size(); i++) {
		const plugin::RemoteSlot& from = page.slots[i];
		out.slots[i] = Slot{.target = from.param ? std::optional{project::ParamTarget{
		                                               .space = project::ParamSpace::ENGINE, .id = *from.param}}
		                                         : std::nullopt,
		                    .press = from.press,
		                    .press_param = from.press_param};
	}
	return out;
}

} // namespace deluge::ui::controls
