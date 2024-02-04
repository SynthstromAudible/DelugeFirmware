/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "dsp/fft/fft_config_manager.h"

#define FFT_CONFIG_MAX_MAGNITUDE 13

namespace FFTConfigManager {

ne10_fft_r2c_cfg_int32_t configs[FFT_CONFIG_MAX_MAGNITUDE + 1];

ne10_fft_r2c_cfg_int32_t getConfig(int32_t magnitude) {
	if (magnitude > FFT_CONFIG_MAX_MAGNITUDE)
		return NULL;

	if (!configs[magnitude]) {
		configs[magnitude] =
		    ne10_fft_alloc_r2c_int32(1 << magnitude); // Allocates and sets up. And we'll just never deallocate.
	}

	return configs[magnitude];
}

} // namespace FFTConfigManager
