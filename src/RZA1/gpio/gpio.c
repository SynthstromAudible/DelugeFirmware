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

#include <drivers/RZA1/system/rza_io_regrw.h>
#include "iodefine.h"

void ioRegSet(volatile uint16_t* reg, uint8_t p, uint8_t q, uint8_t v)
{
    RZA_IO_RegWrite_16((volatile uint16_t*)((uint32_t)reg + (p - 1) * 4), v, q, (uint16_t)1 << q);
}

uint16_t ioRegGet(volatile uint16_t* reg, uint8_t p, uint8_t q)
{
    return RZA_IO_RegRead_16((volatile uint16_t*)((uint32_t)reg + (p - 1) * 4), q, (uint16_t)1 << q);
}

void setPinMux(uint8_t p, uint8_t q, uint8_t mux)
{
    // Manual page 2111 explains
    ioRegSet(&GPIO.PFCAE1, p, q, mux >= 5);
    ioRegSet(&GPIO.PFCE1, p, q, ((mux - 1) >> 1) & 1);
    ioRegSet(&GPIO.PFC1, p, q, (mux - 1) & 1);
    ioRegSet(&GPIO.PMC1, p, q, 1);
    ioRegSet(&GPIO.PIPC1, p, q, 1); // Haven't bothered understanding why - the examples just do it
}

void setPinAsOutput(uint8_t p, uint8_t q)
{
    ioRegSet(&GPIO.PMC1, p, q, 0);
    ioRegSet(&GPIO.PM1, p, q, 0);
    ioRegSet(&GPIO.PIPC1, p, q, 0);
}

void setPinAsInput(uint8_t p, uint8_t q)
{
    ioRegSet(&GPIO.PMC1, p, q, 0);
    ioRegSet(&GPIO.PM1, p, q, 1);
    ioRegSet(&GPIO.PIBC1, p, q, 1);
}

void setOutputState(uint8_t p, uint8_t q, uint16_t state)
{
    ioRegSet(&GPIO.P1, p, q, state);
}

uint16_t readInput(uint8_t p, uint8_t q)
{
    return ioRegGet(&GPIO.PPR1, p, q);
}
