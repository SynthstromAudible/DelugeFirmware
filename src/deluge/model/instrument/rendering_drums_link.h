/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include <cstddef>
#include <etl/intrusive_links.h>

inline constexpr size_t kRenderingDrumsLinkId = 0;

/// @brief A SoundDrum's link in its kit's list of drums rendering now (model/instrument/rendering_drums.h). Two
///        pointers, both null while the drum is in no list.
using RenderingDrumsLink = etl::bidirectional_link<kRenderingDrumsLinkId>;
