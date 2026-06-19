/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/groove.h"
#include "deluge-project/reverb_settings.h"
#include "deluge-project/scale_settings.h"
#include "deluge-project/transport.h"

namespace deluge::project {

/// The project model: the persistent musical document — clips, instruments, tempo, scale,
/// sections, macros — i.e. exactly what is serialized to a song file. This is the boundary for
/// modern, idiomatic, object-oriented C++23, populated incrementally by the project-model
/// extraction (docs/dev/project_model_extraction.md).
///
/// During migration the legacy global `::Song` remains the controller (playback/UI/runtime) and
/// owns one of these as `::Song::model`; project data moves here group by group, with the song
/// file format held byte-identical at every step. Starts empty.
class Song {
public:
	Song();

	Groove groove;
	Transport transport;
	ScaleSettings scale;
	ReverbSettings reverb;
};

} // namespace deluge::project
