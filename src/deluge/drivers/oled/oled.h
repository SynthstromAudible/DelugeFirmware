/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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
#include "stdbool.h"
#include "stdint.h"

#define SPI_TRANSFER_QUEUE_SIZE 32

void oledMainInit();
void oledDMAInit();
void enqueueSPITransfer(int32_t whichOled, uint8_t const* image);
void oledTransferComplete(uint32_t int_sense);

extern volatile bool spiTransferQueueCurrentlySending;
extern volatile uint8_t spiTransferQueueReadPos;
extern uint8_t spiTransferQueueWritePos;

struct SpiTransferQueueItem {
	uint8_t destinationId;
	uint8_t const* dataAddress;
};

extern struct SpiTransferQueueItem spiTransferQueue[SPI_TRANSFER_QUEUE_SIZE];
