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

// Most of the contents of this file have been copied from Renesas's SD card driver libraries.

#include "definitions.h"
#include "drivers/mtu/mtu.h"

#include "RZA1/sdhi/inc/sdif.h"
#include "deluge.h"
#include "scheduler_api.h"
#include "util/cfunctions.h"

uint16_t stopTime;

bool wrappedCheckTimer() {
	// this is a bit odd but it returns err when the timer goes off
	return (sddev_check_timer() == SD_ERR);
}

/******************************************************************************
 * Function Name: int32_t sddev_power_on(int32_t sd_port);
 * Description  : Power-on H/W to use SDHI
 * Arguments    : none
 * Return Value : success : SD_OK
 *              : fail    : SD_ERR
 ******************************************************************************/
int32_t sddev_power_on(int32_t sd_port) {
	/* ---Power On SD ---- */

	/* ---- Wait for  SD Wake up ---- */
	sddev_start_timer(100); /* wait 100ms */
#ifdef USE_TASK_MANAGER
	yieldingRoutineForSD(wrappedCheckTimer);
#else
	while (sddev_check_timer() == SD_OK) {
		/* wait */
		routineForSD(); // By Rohan
	}
#endif
	sddev_end_timer();

	return SD_OK;
}

bool sdIntFinished() {
	return (sd_check_int(SD_PORT) == SD_OK);
}
/******************************************************************************
 * Function Name: int32_t sddev_int_wait(int32_t sd_port, int32_t time);
 * Description  : Waitting for SDHI Interrupt
 * Arguments    : int32_t time : time out value to wait interrupt
 * Return Value : get interrupt : SD_OK
 *              : time out      : SD_ERR
 ******************************************************************************/
int32_t sddev_int_wait(int32_t sd_port, int32_t time) {

	logAudioAction("sddev_int_wait");
	int32_t loop;
#ifdef USE_TASK_MANAGER
	if (yieldingRoutineWithTimeoutForSD(sdIntFinished, time / 1000.)) {
		return SD_OK;
	}
	else
		return SD_ERR;
#else
	if (time > 500) {
		/* @1000ms */
		loop = (time / 500);
		if ((time % 500) != 0) {
			loop++;
		}
		time = 500;
	}
	else {
		loop = 1;
	}

	do {
		sddev_start_timer(time);

		while (1) {

			/* interrupt generated? */
			if (sd_check_int(sd_port) == SD_OK) {
				sddev_end_timer();
				return SD_OK;
			}
			/* detect timeout? */
			if (sddev_check_timer() == SD_ERR) {
				break;
			}
			// called during command execution
			routineForSD(); // By Rohan, obviously
		}

		loop--;
		if (loop <= 0) {
			break;
		}

	} while (1);

	sddev_end_timer();

	return SD_ERR;
#endif
}

/******************************************************************************
 * Function Name: static void sddev_start_timer(int32_t msec);
 * Description  : start timer
 * Arguments    :
 * Return Value : none
 ******************************************************************************/
void sddev_start_timer(int msec) {
	stopTime = *TCNT[TIMER_SYSTEM_SLOW] + msToSlowTimerCount(msec);
}

/******************************************************************************
 * Function Name: static void sddev_end_timer(void);
 * Description  : end timer
 * Arguments    :
 * Return Value : none
 ******************************************************************************/
void sddev_end_timer(void) {
}

/******************************************************************************
 * Function Name: static int32_t sddev_check_timer(void);
 * Description  : check
 * Arguments    :
 * Return Value : t
 ******************************************************************************/
int sddev_check_timer(void) {

	uint16_t howFarAbove = *TCNT[TIMER_SYSTEM_SLOW] - stopTime;

	if (howFarAbove < 16384) {
		return SD_ERR;
	}

	return SD_OK;
}
