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

#pragma once

#include "dsp-next/fixed/filter/filter_set.h"

namespace deluge::voice {

/// @brief The scratch every filter set on the audio tier shares (the voices' and the host chains'): room for a stereo
/// block of SSI_TX_BUFFER_NUM_SAMPLES frames each for the fades and for the parallel route's copy. Sets render one at
/// a time on that tier, so one backing serves them all.
[[nodiscard]] dsp::fixed::FilterScratch filter_scratch();

} // namespace deluge::voice
