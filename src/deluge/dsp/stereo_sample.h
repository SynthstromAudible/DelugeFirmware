/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

// StereoSample now lives in dsp-next (deluge::dsp::StereoSample) so it's library-resident. This legacy header is a
// compat shim: it aliases the global ::StereoSample to the dsp-next type, so the app's existing (unqualified)
// usages keep working unchanged while sites migrate to deluge::dsp::StereoSample one at a time. (It also keeps
// re-exporting util/functions.h, which the old header pulled in and some includers relied on transitively.)
#include "dsp-next/core/stereo_sample.h"
#include "util/functions.h"

using StereoSample = deluge::dsp::StereoSample;
