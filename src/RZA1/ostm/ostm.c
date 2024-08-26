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

#include "RZA1/ostm/ostm.h"
#include "RZA1/intc/devdrv_intc.h"
#include "RZA1/system/iobitmasks/ostm_iobitmask.h"
#include "RZA1/system/iodefine.h"
#include "RZA1/system/iodefines/ostm_iodefine.h"
#include "RZA1/system/r_typedefs.h"

static struct st_ostm volatile* const OSTimers[] = {&OSTM0, &OSTM1};

void enableTimer(int timerNo)
{
    OSTimers[timerNo]->OSTMnTS = 1;
}

void disableTimer(int timerNo)
{
    OSTimers[timerNo]->OSTMnTT = 1;
}

bool isTimerEnabled(int timerNo)
{
    return OSTimers[timerNo]->OSTMnTE != 0;
}

void setOperatingMode(int timerNo, enum OSTimerOperatingMode mode, bool enable_interrupt)
{
    OSTimers[timerNo]->OSTMnCTL = mode << 1 | enable_interrupt;
}

void setTimerValue(int timerNo, uint32_t timerValue)
{
    OSTimers[timerNo]->OSTMnCMP = timerValue;
}

uint32_t getTimerValue(int timerNo)
{
    return OSTimers[timerNo]->OSTMnCNT;
}
