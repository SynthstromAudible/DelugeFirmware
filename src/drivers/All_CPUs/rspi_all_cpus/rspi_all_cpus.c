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

#include "rspi_all_cpus.h"
#include "rspi_iodefine.h"
#include "rspi_iobitmask.h"

void R_RSPI_SendBasic8(uint8_t channel, uint8_t data)
{

    // If the TX buffer doesn't have 4 bytes of empty space in it, we'd better wait
    while (0 == RZA_IO_RegRead_8(&(RSPI(channel).SPSR), RSPI_SPSR_SPTEF_SHIFT, RSPI_SPSR_SPTEF))
        ;

    // Clear the RX buffer - we get problems if it gets full
    RSPI(channel).SPBFCR.BYTE |= 0b01000000;

    // Send data
    RSPI(channel).SPDR.BYTE.LL = data;
}

void R_RSPI_SendBasic32(uint8_t channel, uint32_t data)
{

    // If the TX buffer doesn't have 4 bytes of empty space in it, we'd better wait
    while (0 == RZA_IO_RegRead_8(&(RSPI(channel).SPSR.BYTE), RSPI_SPSR_SPTEF_SHIFT, RSPI_SPSR_SPTEF))
        ;

    // Clear the RX buffer - we get problems if it gets full
    RSPI(channel).SPBFCR.BYTE |= 0b01000000;

    // Send data
    RSPI(channel).SPDR.LONG = data;
}

void R_RSPI_WaitEnd(uint8_t channel)
{
    while (!RSPI(channel).SPSR.BIT.TEND)
    {
    }
}

int R_RSPI_HasEnded(uint8_t channel)
{
    return RSPI(channel).SPSR.BIT.TEND;
}
