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

#ifndef DRIVERS_RZA1_MTU_MTU_H_
#define DRIVERS_RZA1_MTU_MTU_H_

#include "RZA1/intc/devdrv_intc.h"
#include "RZA1/system/iobitmasks/mtu2_iobitmask.h"
#include "RZA1/system/iodefine.h"
#include "RZA1/system/r_typedefs.h"

static const uint8_t timerCST[] = {MTU2_TSTR_CST0, MTU2_TSTR_CST1, MTU2_TSTR_CST2, MTU2_TSTR_CST3, MTU2_TSTR_CST4};

static inline void enableTimer(int timerNo)
{
    MTU2.TSTR |= timerCST[timerNo];
}

static inline void disableTimer(int timerNo)
{
    MTU2.TSTR &= ~timerCST[timerNo];
}

static inline bool isTimerEnabled(int timerNo)
{
    return MTU2.TSTR & timerCST[timerNo];
}

static volatile uint8_t* const TIER[] = {&MTU2.TIER_0, &MTU2.TIER_1, &MTU2.TIER_2, &MTU2.TIER_3, &MTU2.TIER_4};

static volatile uint8_t* const TSR[] = {&MTU2.TSR_0, &MTU2.TSR_1, &MTU2.TSR_2, &MTU2.TSR_3, &MTU2.TSR_4};

extern void mtuEnableAccess();

static inline void timerClearCompareMatchTGRA(int timerNo)
{

    // Clear the TGFA flag.
    // Up to and including V3.1.1-RC the dummy_read was done before calling timerGoneOff() and then tested afterwards,
    // which is obviously not ideal. At the same time, there was a bizarre crash when sending arpeggiator MIDI to
    // Fraser's Gakken NSX-39. Making this change fixed that crash, but I am unsure if this has actually addressed the
    // cause of the crash, because making almost any other change to this function or even just adding one instruction
    // to the end, like switching an LED on or off, also made the crash vanish.
    // So, watch out for future peculiarities.
    // Another note - it doesn't seem to matter whether this clearing is done before or after calling timerGoneOff(),
    // but in the event of future problems, it would be worth trying both.
    while (true)
    {
        uint16_t dummy_read = *TSR[timerNo];
        if (!(dummy_read & 0x01))
            break;
        *TSR[timerNo] = dummy_read & 0xFE;
    }
}

static volatile uint8_t* const TCR[] = {&MTU2.TCR_0, &MTU2.TCR_1, &MTU2.TCR_2, &MTU2.TCR_3, &MTU2.TCR_4};

/// The R7S100 has 5 timers. This sets a timer to either reset  (clearedByTGRA true means it is reset when the timer
/// matches TGRA) and sets the prescaler value to divide P0 (33.33MHz) by. Valid values are 0, 4, 16 for all timers.
/// Timer 1, 4, 5 support 256. Timer 2, 4, 5 support 1024. Ref -
/// https://www.renesas.com/us/en/document/mah/rza1l-group-rza1lu-group-rza1lc-group-users-manual-hardware?r=1054491#G14.1027450
static inline void timerControlSetup(int timerNo, int clearedByTGRA, int prescaler)
{

    uint8_t TPSC = 0; // Defaults to prescaler of 1

    switch (prescaler)
    {
        case 4:
            TPSC = 1;
            break;
        case 16:
            TPSC = 2;
            break;
        case 64:
            TPSC = 3;
            break;
        case 256:
            if (timerNo == 1)
                TPSC = 0b110;
            else if (timerNo >= 3)
                TPSC = 0b100;
            break;
        case 1024:
            if (timerNo == 2)
                TPSC = 0b111;
            else if (timerNo >= 3)
                TPSC = 0b101;
            break;
    }

    uint8_t CCLR = clearedByTGRA ? (1 << 5) : 0;

    *TCR[timerNo] = TPSC | CCLR;
}

static volatile uint16_t* const TCNT[] = {&MTU2.TCNT_0, &MTU2.TCNT_1, &MTU2.TCNT_2, &MTU2.TCNT_3, &MTU2.TCNT_4};

static volatile uint16_t* const TGRA[] = {&MTU2.TGRA_0, &MTU2.TGRA_1, &MTU2.TGRA_2, &MTU2.TGRA_3, &MTU2.TGRA_4};

static uint16_t const INTC_ID_TGIA[] = {INTC_ID_TGI0A, INTC_ID_TGI1A, INTC_ID_TGI2A, INTC_ID_TGI3A, INTC_ID_TGI4A};

#endif /* DRIVERS_RZA1_MTU_MTU_H_ */
