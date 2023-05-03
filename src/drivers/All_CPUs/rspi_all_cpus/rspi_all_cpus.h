/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#ifndef DRIVERS_ALL_CPUS_RSPI_ALL_CPUS_RSPI_ALL_CPUS_H_
#define DRIVERS_ALL_CPUS_RSPI_ALL_CPUS_RSPI_ALL_CPUS_H_

#include "iodefine.h"
#include "r_typedefs.h"

void R_RSPI_SendBasic8(uint8_t channel, uint8_t data);
void R_RSPI_SendBasic32(uint8_t channel, uint32_t data);
void R_RSPI_WaitEnd(uint8_t channel);
int R_RSPI_HasEnded(uint8_t channel);

#endif /* DRIVERS_ALL_CPUS_RSPI_ALL_CPUS_RSPI_ALL_CPUS_H_ */
