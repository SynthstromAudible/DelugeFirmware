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

/*------------------------------------------------------------------------/
/  MMCv3/SDv1/SDv2 (in SPI mode) control module
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2014, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-------------------------------------------------------------------------*/

#include "RZA1/system/iodefine.h"
#include "RZA1/system/r_typedefs.h"
#include "definitions.h"

#include "RZA1/compiler/asm/inc/asm.h"
#include "RZA1/rspi/rspi.h"
#include "RZA1/sdhi/inc/sdif.h"
#include "RZA1/system/rza_io_regrw.h"
#include "deluge/deluge.h"
#include "deluge/drivers/uart/uart.h"
#include "diskio.h"
#include "ff.h"

uint8_t currentlyAccessingCard = 0;

#define _USE_WRITE 1 /* 1: Enable disk_write function */
#define _USE_IOCTL 1 /* 1: Enable disk_ioctl fucntion */

#define CTRL_POWER_OFF 7 /* Put the device off state */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC   0x01              /* MMC ver 3 */
#define CT_SD1   0x02              /* SD ver 1 */
#define CT_SD2   0x04              /* SD ver 2 */
#define CT_SDC   (CT_SD1 | CT_SD2) /* SD */
#define CT_BLOCK 0x08              /* Block addressing */

void ioRegSet2(volatile uint16_t* reg, uint8_t p, uint8_t q, uint8_t v)
{
    RZA_IO_RegWrite_16((volatile uint16_t*)((uint32_t)reg + (p - 1) * 4), v, q, (uint16_t)1 << q);
}

uint16_t ioRegGet2(volatile uint16_t* reg, uint8_t p, uint8_t q)
{
    return RZA_IO_RegRead_16((volatile uint16_t*)((uint32_t)reg + (p - 1) * 4), q, (uint16_t)1 << q);
}

DRESULT disk_read_without_streaming_first(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count);

extern int pendingGlobalMIDICommandNumClustersWritten;
extern int currentlySearchingForCluster;

DRESULT disk_read(BYTE pdrv, /* Physical drive nmuber (0) */
    BYTE* buff,              /* Pointer to the data buffer to store read data */
    LBA_t sector,            /* Start sector number (LBA) */
    UINT count               /* Sector count (1..128) */
)
{
    logAudioAction("disk_read");

    loadAnyEnqueuedClustersRoutine(); // Always ensure SD streaming is fulfilled before anything else

    DRESULT result = disk_read_without_streaming_first(pdrv, buff, sector, count);

    if (currentlySearchingForCluster)
        pendingGlobalMIDICommandNumClustersWritten++;

    return result;
}

BYTE diskStatus = STA_NOINIT;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
    return diskStatus;
}

int sdIntCallback(int sd_port, int cd)
{
    if (sd_port == SD_PORT)
    {
        if (cd)
        {
            uartPrintln("SD Card insert!\n");
            diskStatus &= ~STA_NODISK;
            sdCardInserted();
        }
        else
        {
            uartPrintln("SD Card extract!\n");
            diskStatus = STA_NOINIT | STA_NODISK;
            sdCardEjected();
        }
    }

    return 0;
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

#include "RZA1/sdhi/inc/sd_cfg.h"
#include "RZA1/sdhi/inc/sdif.h"

uint32_t initializationWorkArea[SD_SIZE_OF_INIT / sizeof(uint32_t)];

DSTATUS disk_initialize(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
    // If no card, return early
    if (diskStatus & STA_NODISK)
        return SD_ERR_NO_CARD;

    // Try initializing, configuring and mounting card; return on error
    currentlyAccessingCard = 1;
    int error              = sd_init(SD_PORT, SDCFG_IP1_BASE, &initializationWorkArea[0], SD_CD_SOCKET);
    currentlyAccessingCard = 0;
    if (error)
    {
processError:
        diskStatus = STA_NOINIT;
        if (error == SD_ERR_NO_CARD)
        {
            diskStatus |= STA_NODISK;
        }
        return diskStatus;
    }

#ifdef SDCFG_CD_INT
    error = sd_cd_int(SD_PORT, SD_CD_INT_ENABLE, sdIntCallback);
#else
    error = sd_cd_int(1, SD_CD_INT_DISABLE, 0);
#endif
    if (error)
        goto processError;

    currentlyAccessingCard = 1;
    error                  = sd_mount(SD_PORT, SDCFG_DRIVER_MODE, SD_VOLT_3_3);
    currentlyAccessingCard = 0;
    if (error)
        goto processError;

    // If we got here, card is ok
    diskStatus = 0;

    // but it might still be write-protected
    if (sd_iswp(SD_PORT))
    {
        diskStatus |= STA_PROTECT;
    }

    return diskStatus;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read_without_streaming_first(BYTE pdrv, /* Physical drive nmuber to identify the drive */
    BYTE* buff,                                      /* Data buffer to store read data */
    DWORD sector,                                    /* Sector address in LBA */
    UINT count                                       /* Number of sectors to read */
)
{

    logAudioAction("disk_read_without_streaming_first");

    BYTE err;

    if (currentlyAccessingCard)
    {
        if (ALPHA_OR_BETA_VERSION)
        {
            // Operatricks got! But I think I fixed.
            FREEZE_WITH_ERROR("E259");
        }
    }

    // uint16_t startTime = MTU2.TCNT_0;

    currentlyAccessingCard = 1;

    err = sd_read_sect(SD_PORT, buff, sector, count);

    currentlyAccessingCard = 0;

    /*
    uint16_t endTime = MTU2.TCNT_0;
    uint16_t duration = endTime - startTime;
    uartPrintln(intToStringUSB(duration));
    */

    // My good 16gb card gave about 150 per read. Bad card gave ~250, and occasionally up to 30,000!

    if (err == 0)
    {
        return RES_OK;
    }
    else
    {
        return RES_ERROR;
    }
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write(BYTE pdrv, /* Physical drive nmuber to identify the drive */
    const BYTE* buff,         /* Data to be written */
    LBA_t sector,             /* Sector address in LBA */
    UINT count                /* Number of sectors to write */
)
{

    loadAnyEnqueuedClustersRoutine(); // Always ensure SD streaming is fulfilled before anything else

    BYTE err;

    if (currentlyAccessingCard)
    {
        if (ALPHA_OR_BETA_VERSION)
        {
            FREEZE_WITH_ERROR("E258");
        }
    }

    currentlyAccessingCard = 1;

    err = sd_write_sect(SD_PORT, buff, sector, count, 0x0001u);

    currentlyAccessingCard = 0;

    if (err == 0)
    {
        return RES_OK;
    }
    else
    {
        return RES_ERROR;
    }
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl(BYTE pdrv, /* Physical drive nmuber (0..) */
    BYTE cmd,                 /* Control code */
    void* buff                /* Buffer to send/receive control data */
)
{
    DRESULT res;
    int result;

    if (pdrv)
        return RES_PARERR;
    if (diskStatus & STA_NOINIT)
        return RES_NOTRDY;

    switch (cmd)
    {
        case CTRL_SYNC:    /* Flush write-back cache, Wait for end of internal process */
            return RES_OK; // I think this just means "yes, you can do some writing now", which you always can. Rohan

        default:
            return RES_PARERR;
    }
}

/* ---- Recency clock -------------------------------------------------------
 * The Deluge has no real-time clock, so it used to stamp every saved file with
 * time 0. Instead we COUNT: each save gets a packed FAT datetime later than the
 * last, so sorting by date == sorting by save order. The counter is seeded at
 * card mount from the newest date already on the card (fatClockSeedFromPacked,
 * called from storage_manager) so it never goes backwards across power cycles.
 * The resulting "dates" are synthetic (they look odd on a computer) but give
 * true recency on the device. */
static uint32_t fatClockSecs2               = 0;  /* 2-second units since 1980-01-01 */
static const uint32_t fatClockStep          = 30; /* advance 60s per save (lots of headroom) */
static const unsigned char fatMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int fatIsLeap(uint32_t y)
{
    return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
}

/* 2-second-units-since-1980 -> packed FAT datetime */
static DWORD fatPack(uint32_t s2)
{
    uint32_t secs = s2 * 2u;
    uint32_t days = secs / 86400u;
    uint32_t rem  = secs % 86400u;
    uint32_t hour = rem / 3600u;
    rem %= 3600u;
    uint32_t minute = rem / 60u;
    uint32_t sec    = rem % 60u;
    uint32_t year   = 1980u;
    while (1)
    {
        uint32_t diy = 365u + (uint32_t)fatIsLeap(year);
        if (days >= diy)
        {
            days -= diy;
            year++;
        }
        else
            break;
    }
    uint32_t month = 1u;
    while (1)
    {
        uint32_t dim = fatMonthDays[month - 1] + ((month == 2u) ? (uint32_t)fatIsLeap(year) : 0u);
        if (days >= dim)
        {
            days -= dim;
            month++;
        }
        else
            break;
    }
    uint32_t day = days + 1u;
    return ((DWORD)(year - 1980u) << 25) | ((DWORD)month << 21) | ((DWORD)day << 16) | ((DWORD)hour << 11)
           | ((DWORD)minute << 5) | ((DWORD)(sec / 2u));
}

/* Bump the clock so it sits just after a packed FAT datetime seen on the card. */
void fatClockSeedFromPacked(DWORD packed)
{
    uint32_t year = 1980u + ((packed >> 25) & 0x7Fu);
    /* Don't seed off an implausible far-future date. A corrupt entry can decode to a year up near the FAT
     * ceiling, and seeding there pins every later save at ~2106, which a computer renders as a 1970 garbage
     * date. Skip anything from 2100 on. */
    if (year >= 2100u)
    {
        return;
    }
    uint32_t month  = (packed >> 21) & 0x0Fu;
    uint32_t day    = (packed >> 16) & 0x1Fu;
    uint32_t hour   = (packed >> 11) & 0x1Fu;
    uint32_t minute = (packed >> 5) & 0x3Fu;
    uint32_t sec    = (packed & 0x1Fu) * 2u;
    if (month < 1u)
        month = 1u;
    if (month > 12u)
        month = 12u;
    if (day < 1u)
        day = 1u;
    uint32_t days = 0u, y, m;
    for (y = 1980u; y < year; y++)
    {
        days += 365u + (uint32_t)fatIsLeap(y);
    }
    for (m = 1u; m < month; m++)
    {
        days += fatMonthDays[m - 1] + ((m == 2u) ? (uint32_t)fatIsLeap(year) : 0u);
    }
    days += (day - 1u);
    uint32_t s2 = (days * 86400u + hour * 3600u + minute * 60u + sec) / 2u;
    if (s2 + fatClockStep > fatClockSecs2)
    {
        fatClockSecs2 = s2 + fatClockStep;
    }
}

DWORD get_fattime(void)
{
    if (fatClockSecs2 == 0u)
    {
        /* Not seeded (empty card, or all-zero dates): start at 2024-01-01 so dates look sane. */
        fatClockSeedFromPacked(((DWORD)(2024u - 1980u) << 25) | ((DWORD)1u << 21) | ((DWORD)1u << 16));
    }
    DWORD t = fatPack(fatClockSecs2);
    fatClockSecs2 += fatClockStep;
    return t;
}

void disk_timerproc(UINT msPassed)
{
}
