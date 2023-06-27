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

#ifndef DRIVERS_RZA1_GPIO_GPIO_H_
#define DRIVERS_RZA1_GPIO_GPIO_H_

#include "r_typedefs.h"

void setPinMux(uint8_t p, uint8_t q, uint8_t mux);
void setPinAsOutput(uint8_t p, uint8_t q);
void setPinAsInput(uint8_t p, uint8_t q);
uint16_t getOutputState(uint8_t p, uint8_t q);
void setOutputState(uint8_t p, uint8_t q, uint16_t state);
uint16_t readInput(uint8_t p, uint8_t q);

#endif /* DRIVERS_RZA1_GPIO_GPIO_H_ */
