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

#pragma once

#include "RZA1/system/r_typedefs.h"

extern const uint32_t ssiDmaTxLinkDescriptor[];
extern const uint32_t ssiDmaRxLinkDescriptor[];

void ssiInit(uint8_t ssiChannel, uint8_t dmaChannel);

void* getTxBufferCurrentPlace();
void* getRxBufferCurrentPlace();
int32_t* getTxBufferStart();
int32_t* getTxBufferEnd();
int32_t* getRxBufferStart();
int32_t* getRxBufferEnd();
