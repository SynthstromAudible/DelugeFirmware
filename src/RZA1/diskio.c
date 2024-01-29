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

// #define SD_RW_BUFF_SIZE    (1 * 1024)
// static uint32_t test_sd_rw_buff[ SD_RW_BUFF_SIZE / sizeof(uint32_t) ]; // Actually this buffer never gets used, so
// I've removed it! - Rohan
uint32_t initializationWorkArea[SD_SIZE_OF_INIT / sizeof(uint32_t)];

DSTATUS disk_initialize(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
    // uartPrintln("disk_initialize");

    // If no card present, nothing more we can do
    if (diskStatus & STA_NODISK)
        return SD_ERR_NO_CARD;

    int error;

    if (false)
    {
processError:
        diskStatus = STA_NOINIT;

        if (error == SD_ERR_NO_CARD)
            diskStatus |= STA_NODISK;

        return diskStatus;
    }

    diskStatus = STA_NOINIT; // But then we'll try and initialize it now

    currentlyAccessingCard = 1;
    error                  = sd_init(SD_PORT, SDCFG_IP1_BASE, &initializationWorkArea[0], SD_CD_SOCKET);
    currentlyAccessingCard = 0;

    if (error)
        goto processError;

#ifdef SDCFG_CD_INT
    // uartPrintln("set card detect by interrupt\n");
    error = sd_cd_int(SD_PORT, SD_CD_INT_ENABLE, sdIntCallback);
#else
    // uartPrintln("set card detect by polling\n");
    error = sd_cd_int(1, SD_CD_INT_DISABLE, 0);
#endif
    if (error)
        goto processError;

    // sd_set_buffer(SD_PORT, &test_sd_rw_buff[0], SD_RW_BUFF_SIZE); // Actually this buffer never gets used, so I've
    // removed it! - Rohan

    // error = cmd_sd_ioatt(0, 0);
    currentlyAccessingCard = 1;
    error                  = sd_mount(SD_PORT, SDCFG_DRIVER_MODE, SD_VOLT_3_3);
    currentlyAccessingCard = 0;

    if (error)
        goto processError;

    diskStatus = 0; // Disk is ok!

    return 0; // Success
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

DWORD get_fattime(void)
{
    return 0;
}

void disk_timerproc(UINT msPassed)
{
}
