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

#include "r_typedefs.h"
#include "definitions.h"
#include "iodefine.h"

#include "Deluge.h"
#include "asm.h"
#include "diskio.h"
#include "ff.h"
#include "rspi.h"
#include "rza_io_regrw.h"
#include "uart_all_cpus.h"

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

    if (currentlySearchingForCluster) pendingGlobalMIDICommandNumClustersWritten++;

    return result;
}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
#include "devdrv_intc.h"
#include "dmac_iobitmask.h"

/* Socket controls  (Platform dependent) */
#define CS_LOW()  ioRegSet2(&GPIO.P1, 6, 1, 0) /* MMC CS = L */
#define CS_HIGH() ioRegSet2(&GPIO.P1, 6, 1, 1) /* MMC CS = H */
#define CD        !ioRegGet2(&GPIO.PPR1, 6, 7) /* Card detected   (yes:true, no:false, default:true) */
#define WP        false                        /* Write protected (yes:true, no:false, default:false) */

/* SPI bit rate controls */
#define FCLK_SLOW()                                                                                                    \
    RSPI0.SPBR = ceil((float)66666666 / (400000 * 2) - 1); /* Set slow clock, 100k-400k (Nothing to do) */
//#define	FCLK_FAST()	RSPI0.SPBR = ceil((float)66666666 / (25000000 * 2) - 1);		/* Set fast clock, depends on the CSD (Nothing to do) */
#define FCLK_FAST() RSPI0.SPBR = 0; /* Set fast clock, depends on the CSD (Nothing to do) */

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* Definitions for MMC/SDC command */
#define CMD0   (0)         /* GO_IDLE_STATE */
#define CMD1   (1)         /* SEND_OP_COND */
#define ACMD41 (41 | 0x80) /* SEND_OP_COND (SDC) */
#define CMD8   (8)         /* SEND_IF_COND */
#define CMD9   (9)         /* SEND_CSD */
#define CMD10  (10)        /* SEND_CID */
#define CMD12  (12)        /* STOP_TRANSMISSION */
#define ACMD13 (13 | 0x80) /* SD_STATUS (SDC) */
#define CMD16  (16)        /* SET_BLOCKLEN */
#define CMD17  (17)        /* READ_SINGLE_BLOCK */
#define CMD18  (18)        /* READ_MULTIPLE_BLOCK */
#define CMD23  (23)        /* SET_BLOCK_COUNT */
#define ACMD23 (23 | 0x80) /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24  (24)        /* WRITE_BLOCK */
#define CMD25  (25)        /* WRITE_MULTIPLE_BLOCK */
#define CMD41  (41)        /* SEND_OP_COND (ACMD) */
#define CMD55  (55)        /* APP_CMD */
#define CMD58  (58)        /* READ_OCR */

static volatile DSTATUS Stat = STA_NOINIT; /* Disk status */

static volatile UINT Timer1, Timer2; /* 1000Hz decrement timer */

static UINT CardType;

/*-----------------------------------------------------------------------*/
/* Power Control  (Platform dependent)                                   */
/*-----------------------------------------------------------------------*/
/* When the target system does not support socket power control, there   */
/* is nothing to do in these functions.                                  */

static void power_on(void)
{
}

static void power_off(void)
{
}

/*-----------------------------------------------------------------------*/
/* Transmit/Receive data to/from MMC via SPI  (Platform dependent)       */
/*-----------------------------------------------------------------------*/

#define RSPIx (*(struct st_rspi*)((uint32_t)&RSPI0 + 0 * 0x800uL))

const uint32_t dummyOutput __attribute__((aligned(4))) = 0xFFFFFFFF;

BYTE dummyInput[4] __attribute__((aligned(4)));

BYTE dmaTokenBuffer[32] __attribute__((aligned(32)));

#define send_dma_channel    8
#define receive_dma_channel 9

BYTE currentDataLength = 8;

static void setDataLength(BYTE newLength)
{
    if (newLength == currentDataLength) return;

    currentDataLength = newLength;
    if (currentDataLength == 32)
    {

        RSPIx.SPDCR  = 0x60u;                  // 32-bit
        RSPIx.SPCMD0 = 0b0000001100000010 | 1; // 32-bit
        RSPIx.SPBFCR =
            0b00110010; // Transmit trigger number set to 0 bytes (only trigger when it's completely empty). You'd think setting it to 4 bytes would be faster, but somehow that doesn't work with DMA.
                        //RSPIx.SPBFCR = 0b00100010; // 4 bytes

        //*g_dma_reg_tbl[send_dma_channel].chcfg =    0b0001100100010001001100000 | (send_dma_channel % 8u);
        //*g_dma_reg_tbl[receive_dma_channel].chcfg = 0b0000100100010001001100000 | (receive_dma_channel % 8u);
    }
    else if (currentDataLength == 16)
    {
        RSPIx.SPDCR  = 0x40u;                  // 16-bit
        RSPIx.SPCMD0 = 0b0000111100000010 | 1; // 16-bit
        RSPIx.SPBFCR = 0b00100001;
    }
    else if (currentDataLength == 8)
    {
        RSPIx.SPDCR  = 0x20u;             // 8-bit
        RSPIx.SPCMD0 = 0b11110000010 | 1; // 8-bit
        RSPIx.SPBFCR = 0b00100000;

        //*g_dma_reg_tbl[send_dma_channel].chcfg =	0b0001100000000001001100000 | (send_dma_channel % 8u);
        //*g_dma_reg_tbl[receive_dma_channel].chcfg =	0b0000100000000001001100000 | (receive_dma_channel % 8u);
    }
}

/* Single byte SPI transfer */

static BYTE xchg_spi(BYTE dat)
{
    setDataLength(8);
    return R_RSPI1_SendReceiveBasic(0, dat);
}

#include "rspi_iobitmask.h"

/* Block SPI transfers */

static void xmit_spi_multi(const BYTE* buff, /* Data to be sent */
    UINT cnt                                 /* Number of bytes to send */
)
{
    setDataLength(32);
    BYTE doneAny = false;
    int countUp  = 0;

    do
    {

        uint32_t data;
        BYTE* dataReader = (BYTE*)&data + 3;
        int i;
        for (i = 0; i < 4; i++)
        {
            if (cnt)
            {
                *dataReader-- = *buff++;
                cnt--;
            }
        }

        RSPIx.SPDR.UINT32 = data;
        //RSPIx.SPDR.UINT32 = ((uint32_t)(*buff++)) | ((uint32_t)(*buff++) << 8) | ((uint32_t)(*buff++) << 16) | ((uint32_t)(*buff++) << 24);

        if (doneAny)
        {

            // With 33MHz clock, we'll get here 23.6x per audio sample (at 44.1KHz).
            //if (((countUp++) & 15 == 0)) routineForSD();

            // Wait til we receive the corresponding data
            while (0 == RZA_IO_RegRead_8(&(RSPIx.SPSR), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
                ;

            // Receive data. Note that even if we didn't want the receive data, we still have to read it back, because SPI transmission halts once the RX buffer is full
            RSPIx.SPDR.UINT32;
        }
        else doneAny = true;
    } while (cnt);

    // Wait til we receive the corresponding data
    while (0 == RZA_IO_RegRead_8(&(RSPIx.SPSR), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
        ;

    // Receive data. Note that even if we didn't want the receive data, we still have to read it back, because SPI transmission halts once the RX buffer is full
    RSPIx.SPDR.UINT32;
}

UINT transferSectorsLeft;
BYTE* volatile transferBuffer;
volatile BYTE transferStatus;

UINT transferOriginalCount;

#define DMA_STATUS_WAIT_DATA_RESPONSE 0
#define DMA_STATUS_WAIT_READY         1
#define DMA_STATUS_WAIT_DATA_TOKEN    2
#define DMA_STATUS_WAIT_SECTOR        3
#define DMA_STATUS_COMPLETE           4
#define DMA_STATUS_ERROR              5

#define SD_DIRECTION_READING 0
#define SD_DIRECTION_WRITING 1

BYTE sdDirection;

static void spiDMARx(BYTE* buff, UINT cnt)
{

    setDataLength(8);

    // Send DMA -----------------------------------------------------
    DMACn(send_dma_channel).N0TB_n  = cnt;
    DMACn(send_dma_channel).N0SA_n  = (uint32_t)&dummyOutput;
    DMACn(send_dma_channel).CHCFG_n = 0b0001100000000001001100000 | (send_dma_channel % 8u);
    DMACn(send_dma_channel).CHCTRL_n |= DMAC0_CHCTRL_n_SETEN; // ---- Enable DMA Transfer ----

    // Invalidate - before doing the read. For some weird reason it doesn't work unless we do this. We invalidate after, also - I suspect that that helps too
    v7_dma_inv_range(buff, buff + cnt);

    // Receive DMA ----------------------------------------------------
    DMACn(receive_dma_channel).N0DA_n = (uint32_t)buff;
    DMACn(receive_dma_channel).N0TB_n = cnt;

    DMACn(receive_dma_channel).CHCFG_n = 0b0000100000000001001100000 | (receive_dma_channel % 8u);

    DMACn(receive_dma_channel).CHCTRL_n |= DMAC0_CHCTRL_n_SWRST; // ---- Software Reset ----
    DMACn(receive_dma_channel).CHCTRL_n |= DMAC0_CHCTRL_n_SETEN; // ---- Enable DMA Transfer ----
}

static void spiDMATx(const BYTE* buff, UINT cnt)
{

    setDataLength(8);

    v7_dma_flush_range(buff, buff + cnt);

    // Send DMA -----------------------------------------------------
    DMACn(send_dma_channel).CHITVL_n = 0; // No delays!

    DMACn(send_dma_channel).N0TB_n  = cnt;
    DMACn(send_dma_channel).N0SA_n  = (uint32_t)buff;
    DMACn(send_dma_channel).CHCFG_n = 0b1001000000000001001100000 | (send_dma_channel % 8u);

    DMACn(send_dma_channel).CHCTRL_n |= DMAC0_CHCTRL_n_SETEN; // ---- Enable DMA Transfer ----

    // Receive DMA ----------------------------------------------------
    DMACn(receive_dma_channel).N0DA_n = (uint32_t)dummyInput; // Write from dummy input
    DMACn(receive_dma_channel).N0TB_n = cnt;

    DMACn(receive_dma_channel).CHCFG_n = 0b0001100000000001001100000 | (receive_dma_channel % 8u);

    DMACn(receive_dma_channel).CHCTRL_n |= DMAC0_CHCTRL_n_SWRST; // ---- Software Reset ----
    DMACn(receive_dma_channel).CHCTRL_n |= DMAC0_CHCTRL_n_SETEN; // ---- Enable DMA Transfer ----
}

static void mmcDMAInterrupt(uint32_t int_sense)
{

    // Clear Transfer End Bit Status
    DMACn(receive_dma_channel).CHCTRL_n |= (DMAC0_CHCTRL_n_CLREND | DMAC0_CHCTRL_n_CLRTC);

    // Reading ----------------------------------------------------
    if (sdDirection == SD_DIRECTION_READING)
    {

        // If we were waiting for data token...
        if (transferStatus == DMA_STATUS_WAIT_DATA_TOKEN)
        {

            v7_dma_inv_range(dmaTokenBuffer + 2, dmaTokenBuffer + 3);

            // If we got 0xFF...
            if (dmaTokenBuffer[2] == 0xFF)
            {
                // If timer ran out, error...
                if (!Timer1)
                {
returnError:
                    transferStatus = DMA_STATUS_ERROR;
                }

                // Otherwise, try again...
                else
                {
                    // Add a delay before each SPI transaction - we're expecting to have to keep quizzing for a while, and we want to avoid doing this very often.
                    if (transferSectorsLeft != transferOriginalCount) DMACn(send_dma_channel).CHITVL_n = 512;
                    spiDMARx(dmaTokenBuffer + 2, 1);
                }
            }

            // Or if we got the token...
            else if (dmaTokenBuffer[2] == 0xFE)
            {
                transferStatus                   = DMA_STATUS_WAIT_SECTOR;
                DMACn(send_dma_channel).CHITVL_n = 0; // No delays!
                spiDMARx(transferBuffer, 512);        /* Receive the data block into buffer */
            }

            // Otherwise, error
            else
            {
                goto returnError;
            }
        }

        // Or if we were waiting for a sector to transfer...
        else if (transferStatus == DMA_STATUS_WAIT_SECTOR)
        {

            // Invalidate cache. For some reason I can't work out, this has to be done here, once per sector.
            // Trying to do it for the whole transfer at the end leads to CORR errors when flicking through presets fast.
            // Update - currently doing it before read, too. That in conjunction with after whole read would probably be enough.
            v7_dma_inv_range(transferBuffer, transferBuffer + 512);

            transferSectorsLeft--;

            if (transferSectorsLeft == 0)
            {
                transferStatus = DMA_STATUS_COMPLETE;
            }
            else
            {
                transferBuffer += 512;
                Timer1         = 101;
                transferStatus = DMA_STATUS_WAIT_DATA_TOKEN;
                spiDMARx(dmaTokenBuffer, 3);
            }
        }
    }

    // Writing ----------------------------------------------------
    else
    {

        if (transferStatus == DMA_STATUS_WAIT_SECTOR)
        {
            transferStatus = DMA_STATUS_WAIT_DATA_RESPONSE;
            spiDMARx(dmaTokenBuffer, 3);
        }
        else
        {

            v7_dma_inv_range(dmaTokenBuffer, dmaTokenBuffer + 3);

            if (transferStatus == DMA_STATUS_WAIT_READY)
            {

                // If we're ready now...
                if (dmaTokenBuffer[0] == 0xFF)
                {

                    // If finished all sectors, we're done!
                    if (transferSectorsLeft == 0)
                    {
                        transferStatus = DMA_STATUS_COMPLETE;
                    }

                    // Otherwise, send a sector
                    else
                    {

                        DMACn(send_dma_channel).CHITVL_n = 0; // No delays!

                        xchg_spi(0xFC); /* Xmit a token */
                        transferStatus = DMA_STATUS_WAIT_SECTOR;

                        spiDMATx(transferBuffer, 512); /* Xmit the data block to the MMC */
                    }
                }

                // Or if we're still not...
                else
                {

                    // If we actually timed out...
                    if (!Timer2)
                    {
                        transferStatus = DMA_STATUS_ERROR;
                    }

                    // Or otherwise, try again
                    else
                    {
                        DMACn(send_dma_channel).CHITVL_n = 512;
                        spiDMARx(dmaTokenBuffer, 1);
                    }
                }
            }

            else if (transferStatus == DMA_STATUS_WAIT_DATA_RESPONSE)
            {
                if ((dmaTokenBuffer[2] & 0x1F) != 0x05)
                {
                    transferStatus = DMA_STATUS_ERROR;
                }
                else
                {
                    transferSectorsLeft--;

                    // Do a wait-ready-for-next-command. Even if we've now finished all sectors, we'll need to wait to be ready to send the STOP command
                    transferStatus                   = DMA_STATUS_WAIT_READY;
                    DMACn(send_dma_channel).CHITVL_n = 8192; // Delay a lot before the first one
                    Timer2                           = 500;
                    transferBuffer += 512;

                    spiDMARx(dmaTokenBuffer, 1);
                }
            }
        }
    }
}

void setupMMCDMA(void)
{

    uint8_t mask;

    //R_INTC_GetMaskLevel(&mask);
    //(void) R_INTC_SetMaskLevel(0);

    // Send DMA

    // ---- DMA Control Register Setting ----
    DCTRLn(send_dma_channel) = 0;

    // ---- DMA Next0 Address Setting ----
    DMACn(send_dma_channel).N0SA_n = (uint32_t)&dummyOutput;
    DMACn(send_dma_channel).N0DA_n = ((uint32_t)&RSPIx.SPDR.UINT8[0]);

    // ----- Transmission Side Setting ----
    DMACn(send_dma_channel).CHCFG_n = 0b1001100000000001001100000 | (send_dma_channel % 8u);

    // ---- DMA Expansion Resource Selector Setting ----
    setDMARS(send_dma_channel, 0b100100001); // RSPI send, channel 0

    // ---- DMA Channel Interval Register Setting ----
    DMACn(send_dma_channel).CHITVL_n = 0;

    // ---- DMA Channel Extension Register Setting ----
    DMACn(send_dma_channel).CHEXT_n = 0;

    // ---- Software Reset ----
    DMACn(send_dma_channel).CHCTRL_n |= DMAC0_CHCTRL_n_SWRST;

    // ---- Release DMA Interrupt Mask ----
    DMACn(send_dma_channel).CHCFG_n &= ~DMAC0_CHCFG_n_DEM;

    // Receive DMA

    R_INTC_RegistIntFunc(INTC_ID_DMAINT0 + receive_dma_channel, &mmcDMAInterrupt);
    R_INTC_SetPriority(INTC_ID_DMAINT0 + receive_dma_channel, 5);

    // ---- DMA Control Register Setting ----
    DCTRLn(receive_dma_channel) = 0;

    // ---- DMA Next0 Address Setting ----
    DMACn(receive_dma_channel).N0SA_n = ((uint32_t)&RSPIx.SPDR.UINT8[0]);

    // ----- Transmission Side Setting ----
    DMACn(receive_dma_channel).CHCFG_n = 0b1000100000000001001100000 | (receive_dma_channel % 8u);

    // ---- DMA Expansion Resource Selector Setting ----
    setDMARS(receive_dma_channel, 0b100100010); // RSPI receive, channel 0

    // ---- DMA Channel Interval Register Setting ----
    DMACn(receive_dma_channel).CHITVL_n = 0;

    // ---- DMA Channel Extension Register Setting ----
    DMACn(receive_dma_channel).CHEXT_n = 0;

    // ---- Release DMA Interrupt Mask ----
    DMACn(receive_dma_channel).CHCFG_n &= ~DMAC0_CHCFG_n_DEM;

    //(void) R_INTC_SetMaskLevel(mask);

    R_INTC_Enable(INTC_ID_DMAINT0 + receive_dma_channel);
}

static void rcvr_spi_multi(BYTE* buff, /* Buffer to store received data */
    UINT cnt                           /* Number of bytes to receive */
)
{

    // Non-DMA code - weirdly runs a bit faster. Loading 512 bytes on the max clock setting only takes 0.123 mS anyway.
    // Biggest problem is that calling the audio routine is likely to take longer than this (at least at heavy load when it matters), so we're slowed way down.
    setDataLength(32);

    RSPIx.SPDR.UINT32 = 0xFFFFFFFF;

    int countUp = 0;

    do
    {

        // Send data
        if (cnt > 4) RSPIx.SPDR.UINT32 = 0xFFFFFFFF;

        // With 33MHz clock, we'll get here 23.6x per audio sample (at 44.1KHz).
        //if (((countUp++) & 15 == 0)) routineForSD();

        // Wait til we receive the corresponding data
        while (0 == RZA_IO_RegRead_8(&(RSPIx.SPSR), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
            ;

        uint32_t data = RSPIx.SPDR.UINT32;

        BYTE* dataReader = (BYTE*)&data + 3;
        int i;
        for (i = 0; i < 4; i++)
        {
            if (cnt)
            {
                *buff++ = *dataReader--;
                cnt--;
            }
        }

    } while (cnt);
}

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static int wait_ready(void)
{
    BYTE d;

    Timer2 = 500; /* Wait for ready in timeout of 500ms */
    do
    {
        routineForSD();
        d = xchg_spi(0xFF);
    } while ((d != 0xFF) && Timer2);

    return (d == 0xFF) ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Deselect the card and release SPI bus                                 */
/*-----------------------------------------------------------------------*/

static void deselect(void)
{
    CS_HIGH();      /* Set CS# high */
    xchg_spi(0xFF); /* Dummy clock (force MMC DO hi-z for multiple follower SPI) */
}

/*-----------------------------------------------------------------------*/
/* Select the card and wait ready                                        */
/*-----------------------------------------------------------------------*/

static int select(void) /* 1:Successful, 0:Timeout */
{
    CS_LOW();       /* Set CS# low */
    xchg_spi(0xFF); /* Dummy clock (force MMC DO enabled) */

    if (wait_ready()) return 1; /* Wait for card ready */

    deselect();
    return 0; /* Timeout */
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from MMC                                        */
/*-----------------------------------------------------------------------*/

static int rcvr_datablock(/* 1:OK, 0:Failed */
    BYTE* buff,           /* Data buffer to store received data */
    UINT btr              /* Byte count (must be multiple of 4) */
)
{
    BYTE token;

    Timer1 = 100;
    while (true)
    { /* Wait for data packet in timeout of 100ms */
        token = xchg_spi(0xFF);
        if (!((token == 0xFF) && Timer1)) break;
        routineForSD();
    }

    if (token != 0xFE) return 0; /* If not valid data token, return with error */

    rcvr_spi_multi(buff, btr); /* Receive the data block into buffer */
    xchg_spi(0xFF);            /* Discard CRC */
    xchg_spi(0xFF);

    return 1; /* Return with success */
}

/*-----------------------------------------------------------------------*/
/* Send a data packet to MMC                                             */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
static int xmit_datablock(/* 1:OK, 0:Failed */
    const BYTE* buff,     /* 512 byte data block to be transmitted */
    BYTE token            /* Data token */
)
{
    BYTE resp;

    if (!wait_ready()) return 0;

    xchg_spi(token); /* Xmit a token */
    if (token != 0xFD)
    {                              /* Not StopTran token */
        xmit_spi_multi(buff, 512); /* Xmit the data block to the MMC */
        xchg_spi(0xFF);            /* CRC (Dummy) */
        xchg_spi(0xFF);
        resp = xchg_spi(0xFF);     /* Receive a data response */
        if ((resp & 0x1F) != 0x05) /* If not accepted, return with error */
            return 0;
    }

    return 1;
}
#endif

void spiDMAWriteSectors(BYTE* buff, UINT* count)
{

    sdDirection           = SD_DIRECTION_WRITING;
    transferBuffer        = buff;
    transferSectorsLeft   = *count;
    transferOriginalCount = *count;

    transferStatus                   = DMA_STATUS_WAIT_READY;
    DMACn(send_dma_channel).CHITVL_n = 8192; // Delay a lot before the first one
    Timer2                           = 500;

    spiDMARx(dmaTokenBuffer, 1);

    while (transferStatus < DMA_STATUS_COMPLETE)
    {
        routineForSD();
    }

    if (transferStatus == DMA_STATUS_ERROR)
    {
        *count = 1;
        return;
    }

    *count = transferSectorsLeft;

    xchg_spi(0xFD); /* STOP_TRAN token */
}

/*-----------------------------------------------------------------------*/
/* Send a command packet to MMC                                          */
/*-----------------------------------------------------------------------*/

static BYTE send_cmd(BYTE cmd, /* Command byte */
    DWORD arg                  /* Argument */
)
{
    BYTE n, res;

    if (cmd & 0x80)
    { /* ACMD<n> is the command sequense of CMD55-CMD<n> */
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }

    /* Select the card and wait for ready except to stop multiple block read */
    if (cmd != CMD12)
    {
        deselect();
        if (!select()) return 0xFF; // This will call the audio routine
    }

    /* Send command packet */
    setDataLength(8);

    RSPIx.SPDR.UINT8[0] = 0x40 | cmd;        /* Start + Command index */
    RSPIx.SPDR.UINT8[0] = (BYTE)(arg >> 24); /* Argument[31..24] */
    RSPIx.SPDR.UINT8[0] = (BYTE)(arg >> 16); /* Argument[23..16] */
    RSPIx.SPDR.UINT8[0] = (BYTE)(arg >> 8);  /* Argument[15..8] */
    RSPIx.SPDR.UINT8[0] = (BYTE)arg;         /* Argument[7..0] */
    n                   = 0x01;              /* Dummy CRC + Stop */
    if (cmd == CMD0) n = 0x95;               /* Valid CRC for CMD0(0) + Stop */
    if (cmd == CMD8) n = 0x87;               /* Valid CRC for CMD8(0x1AA) + Stop */
    RSPIx.SPDR.UINT8[0] = n;

    int bytesSent = 6;

    /* Receive command response */
    if (cmd == CMD12)
    {
        RSPIx.SPDR.UINT8[0] = 0xFF; /* Skip a stuff byte on stop to read */
        bytesSent++;
    }

    RSPIx.SPDR.UINT8[0] =
        0xFF; // Extra one we're not going to add to the count, because we want to read it back after separately after reading back the counted ones

    //routineForSD();

    // Wait for all that data to send
    do
    {
        while (0 == RZA_IO_RegRead_8(&(RSPIx.SPSR), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
            ;
        RSPIx.SPDR.UINT8[0];
    } while (--bytesSent);

    n = 10; /* Wait for a valid response in timeout of 10 attempts */

    // Read back that extra one we sent above
    while (0 == RZA_IO_RegRead_8(&(RSPIx.SPSR), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
        ;
    res = RSPIx.SPDR.UINT8[0];

    // And send and receive the remaining 9, til we get the right response
    while ((res & 0x80) && --n)
    {
        res = xchg_spi(0xFF);
    }

    return res; /* Return with the response value */
}

void spiDMAReadSectors(BYTE* buff, UINT* count)
{

    sdDirection           = SD_DIRECTION_READING;
    transferBuffer        = buff;
    transferSectorsLeft   = *count;
    transferOriginalCount = *count;
    Timer1                = 1000;
    transferStatus        = DMA_STATUS_WAIT_DATA_TOKEN;
    DMACn(send_dma_channel).CHITVL_n =
        8192; // It'll take ages to respond to this first one, so don't bother quizzing it often
    spiDMARx(dmaTokenBuffer + 2, 1);

    do
    {
        routineForSD();

        // Nadia was getting timeouts somehow - the interrupt wasn't getting called. Will catching them here work, in the absence of a proper solution?
        if (!Timer1)
        {
            transferStatus = DMA_STATUS_ERROR;
        }
    } while (transferStatus < DMA_STATUS_COMPLETE);

    // If no error and we just finished the whole transfer, we'll have a CRC to discard at the end of the final block
    if (transferStatus == DMA_STATUS_COMPLETE)
    {
        xchg_spi(0xFF); // Discard CRC
        xchg_spi(0xFF);
        *count = 0;

        // Ideally we'd expect to just invalidate the cache here. But for some reason I can't work out, that doesn't work.
        // Instead, it's done per sector in the interrupt function - see there.
        // Update - now it's done before, too.
        //v7_dma_inv_range(buff, buff + (transferOriginalCount * 512));
    }
}

DRESULT disk_read_without_streaming_first(BYTE pdrv, /* Physical drive nmuber (0) */
    BYTE* buff,                                      /* Pointer to the data buffer to store read data */
    LBA_t sector,                                    /* Start sector number (LBA) */
    UINT count                                       /* Sector count (1..128) */
)
{
    if (!count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    currentlyAccessingCard = 1;

    if (!(CardType & CT_BLOCK)) sector *= 512; /* Convert to byte address if needed */

    UINT oldCount = count;

    BYTE commandResult = send_cmd((count == 1) ? CMD17 : CMD18, sector);

    if (commandResult == 0)
    {

        if (count == 1)
        {
            if (rcvr_datablock(buff, 512)) count = 0;

            // Theoretically, I should be able to replace the above with this function, which does DMA. It doesn't work for some reason. It's ok - since this would only be for 1 sector,
            // I have this idea that this might actually be faster.
            //spiDMAReadSectors(buff, &count);
        }

        else
        {
            // Have deactivated DMA here, because Nadia's card was occasionally locking these up.

            //spiDMAReadSectors(buff, &count);

            do
            {
                if (!rcvr_datablock(buff, 512)) break;
                buff += 512;
            } while (--count);

            send_cmd(CMD12, 0); // STOP_TRANSMISSION
        }
    }

    deselect();

    currentlyAccessingCard = 0;

    return count ? RES_ERROR : RES_OK;
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE pdrv /* Physical drive nmuber (0) */
)
{
    if (pdrv != 0) return STA_NOINIT; /* Supports only single drive */

    return Stat;
}

/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(BYTE pdrv /* Physical drive nmuber (0) */
)
{
    BYTE n, cmd, ty, ocr[4];

    if (pdrv != 0) return STA_NOINIT;   /* Supports only single drive */
    if (Stat & STA_NODISK) return Stat; /* No card in the socket */

    currentlyAccessingCard = 1;

    power_on(); /* Initialize memory card interface */
    FCLK_SLOW();
    for (n = 10; n; n--)
    {
        xchg_spi(0xFF); /* 80 dummy clocks */
        routineForSD();
    }

    ty = 0;
    if (send_cmd(CMD0, 0) == 1)
    {                  /* Enter Idle state */
        Timer1 = 1000; /* Initialization timeout of 1000 msec */
        if (send_cmd(CMD8, 0x1AA) == 1)
        { /* SDv2? */
            for (n = 0; n < 4; n++)
                ocr[n] = xchg_spi(0xFF); /* Get trailing return value of R7 resp */
            routineForSD();
            if (ocr[2] == 0x01 && ocr[3] == 0xAA)
            { /* The card can work at vdd range of 2.7-3.6V */
                while (Timer1 && send_cmd(ACMD41, 0x40000000))
                { /* Wait for leaving idle state (ACMD41 with HCS bit) */
                    routineForSD();
                }
                if (Timer1 && send_cmd(CMD58, 0) == 0)
                { /* Check CCS bit in the OCR */
                    for (n = 0; n < 4; n++)
                        ocr[n] = xchg_spi(0xFF);
                    routineForSD();
                    ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2; /* SDv2 */
                }
            }
        }
        else
        { /* SDv1 or MMCv3 */
            if (send_cmd(ACMD41, 0) <= 1)
            {
                ty  = CT_SD1;
                cmd = ACMD41; /* SDv1 */
            }
            else
            {
                ty  = CT_MMC;
                cmd = CMD1; /* MMCv3 */
            }
            while (Timer1 && send_cmd(cmd, 0))
            { /* Wait for leaving idle state */
                routineForSD();
            }
            if (!Timer1 || send_cmd(CMD16, 512) != 0) /* Set read/write block length to 512 */
                ty = 0;
        }
    }
    CardType = ty;
    deselect();

    if (ty)
    {                        /* Function succeded */
        Stat &= ~STA_NOINIT; /* Clear STA_NOINIT */
        FCLK_FAST();
    }
    else
    {                /* Function failed */
        power_off(); /* Deinitialize interface */
    }

    currentlyAccessingCard = 0;

    return Stat;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
DRESULT disk_write(BYTE pdrv, /* Physical drive nmuber (0) */
    const BYTE* buff,         /* Pointer to the data to be written */
    LBA_t sector,             /* Start sector number (LBA) */
    UINT count                /* Sector count (1..128) */
)
{

    loadAnyEnqueuedClustersRoutine(); // Always ensure SD streaming is fulfilled before anything else

    if (pdrv || !count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (Stat & STA_PROTECT) return RES_WRPRT;

    currentlyAccessingCard = 1;

    if (!(CardType & CT_BLOCK)) sector *= 512; /* Convert to byte address if needed */

    if (count == 1)
    {                                      /* Single block write */
        if ((send_cmd(CMD24, sector) == 0) /* WRITE_BLOCK */
            && xmit_datablock(buff, 0xFE))
            count = 0;
    }
    else
    { /* Multiple block write */
        if (CardType & CT_SDC) send_cmd(ACMD23, count);
        if (send_cmd(CMD25, sector) == 0)
        { /* WRITE_MULTIPLE_BLOCK */
            spiDMAWriteSectors(buff, &count);
        }
    }
    deselect();

    uartPrintln("disk_write complete");

    currentlyAccessingCard = 0;

    return count ? RES_ERROR : RES_OK;
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL
DRESULT disk_ioctl(BYTE pdrv, /* Physical drive nmuber (0) */
    BYTE cmd,                 /* Control code */
    void* buff                /* Buffer to send/receive data block */
)
{
    DRESULT res;
    BYTE n, csd[16], *ptr = (BYTE*)buff;
    DWORD csz;

    if (pdrv) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    res = RES_ERROR;
    switch (cmd)
    {
        case CTRL_SYNC: /* Flush write-back cache, Wait for end of internal process */
            if (select()) res = RES_OK;
            break;

        case GET_SECTOR_COUNT: /* Get number of sectors on the disk (WORD) */
            if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
            {
                if ((csd[0] >> 6) == 1)
                { /* SDv2? */
                    csz           = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                    *(DWORD*)buff = csz << 10;
                }
                else
                { /* SDv1 or MMCv3 */
                    n             = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                    csz           = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
                    *(DWORD*)buff = csz << (n - 9);
                }
                res = RES_OK;
            }
            break;

        case GET_BLOCK_SIZE: /* Get erase block size in unit of sectors (DWORD) */
            if (CardType & CT_SD2)
            { /* SDv2? */
                if (send_cmd(ACMD13, 0) == 0)
                { /* Read SD status */
                    xchg_spi(0xFF);
                    if (rcvr_datablock(csd, 16))
                    { /* Read partial block */
                        for (n = 64 - 16; n; n--)
                            xchg_spi(0xFF); /* Purge trailing data */
                        *(DWORD*)buff = 16UL << (csd[10] >> 4);
                        res           = RES_OK;
                    }
                }
            }
            else
            { /* SDv1 or MMCv3 */
                if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
                { /* Read CSD */
                    if (CardType & CT_SD1)
                    { /* SDv1 */
                        *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1)
                                        << ((csd[13] >> 6) - 1);
                    }
                    else
                    { /* MMCv3 */
                        *(DWORD*)buff =
                            ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
                    }
                    res = RES_OK;
                }
            }
            break;

        case MMC_GET_TYPE: /* Get card type flags (1 byte) */
            *ptr = CardType;
            res  = RES_OK;
            break;

        case MMC_GET_CSD:                /* Receive CSD as a data block (16 bytes) */
            if ((send_cmd(CMD9, 0) == 0) /* READ_CSD */
                && rcvr_datablock((BYTE*)buff, 16))
                res = RES_OK;
            break;

        case MMC_GET_CID:                 /* Receive CID as a data block (16 bytes) */
            if ((send_cmd(CMD10, 0) == 0) /* READ_CID */
                && rcvr_datablock((BYTE*)buff, 16))
                res = RES_OK;
            break;

        case MMC_GET_OCR: /* Receive OCR as an R3 resp (4 bytes) */
            if (send_cmd(CMD58, 0) == 0)
            { /* READ_OCR */
                for (n = 0; n < 4; n++)
                    *((BYTE*)buff + n) = xchg_spi(0xFF);
                res = RES_OK;
            }
            break;

        case MMC_GET_SDSTAT: /* Receive SD statsu as a data block (64 bytes) */
            if ((CardType & CT_SD2) && send_cmd(ACMD13, 0) == 0)
            { /* SD_STATUS */
                xchg_spi(0xFF);
                if (rcvr_datablock((BYTE*)buff, 64)) res = RES_OK;
            }
            break;

        case CTRL_POWER_OFF: /* Power off */
            power_off();
            Stat |= STA_NOINIT;
            res = RES_OK;
            break;

        default:
            res = RES_PARERR;
    }

    deselect();

    return res;
}
#endif

/*-----------------------------------------------------------------------*/
/* Device Timer Driven Procedure                                         */
/*-----------------------------------------------------------------------*/

void disk_timerproc(UINT msPassed)
{
    BYTE s;
    UINT n;

    n = Timer1; /* 1000Hz decrement timer with zero stopped */
    if (n)
    {
        if (n <= msPassed) Timer1 = 0;
        else Timer1 = n - msPassed;
    }
    n = Timer2;
    if (n)
    {
        if (n <= msPassed) Timer2 = 0;
        else Timer2 = n - msPassed;
    }

    /* Update socket status */

    s = Stat;

    if (WP) s |= STA_PROTECT;
    else s &= ~STA_PROTECT;

    if (CD) s &= ~STA_NODISK;
    else s |= (STA_NODISK | STA_NOINIT);

    Stat = s;
}

DWORD get_fattime(void)
{
    return 0;
}

#else

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

#include "sdif.h"
#include "sd_cfg.h"

//#define SD_RW_BUFF_SIZE    (1 * 1024)
//static uint32_t test_sd_rw_buff[ SD_RW_BUFF_SIZE / sizeof(uint32_t) ]; // Actually this buffer never gets used, so I've removed it! - Rohan
uint32_t initializationWorkArea[SD_SIZE_OF_INIT / sizeof(uint32_t)];

DSTATUS disk_initialize(BYTE pdrv /* Physical drive nmuber to identify the drive */
)
{
    //uartPrintln("disk_initialize");

    // If no card present, nothing more we can do
    if (diskStatus & STA_NODISK) return SD_ERR_NO_CARD;

    int error;

    if (false)
    {
processError:
        diskStatus = STA_NOINIT;

        if (error == SD_ERR_NO_CARD) diskStatus |= STA_NODISK;

        return diskStatus;
    }

    diskStatus = STA_NOINIT; // But then we'll try and initialize it now

    currentlyAccessingCard = 1;
    error                  = sd_init(SD_PORT, SDCFG_IP1_BASE, &initializationWorkArea[0], SD_CD_SOCKET);
    currentlyAccessingCard = 0;

    if (error) goto processError;

#ifdef SDCFG_CD_INT
    //uartPrintln("set card detect by interrupt\n");
    error = sd_cd_int(SD_PORT, SD_CD_INT_ENABLE, sdIntCallback);
#else
    //uartPrintln("set card detect by polling\n");
    error = sd_cd_int(1, SD_CD_INT_DISABLE, 0);
#endif
    if (error) goto processError;

    //sd_set_buffer(SD_PORT, &test_sd_rw_buff[0], SD_RW_BUFF_SIZE); // Actually this buffer never gets used, so I've removed it! - Rohan

    //error = cmd_sd_ioatt(0, 0);
    currentlyAccessingCard = 1;
    error                  = sd_mount(SD_PORT, SDCFG_DRIVER_MODE, SD_VOLT_3_3);
    currentlyAccessingCard = 0;

    if (error) goto processError;

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

    if (currentlyAccessingCard) freezeWithError("E259"); // Operatricks got! But I think I fixed.

    //uint16_t startTime = MTU2.TCNT_0;

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

    if (currentlyAccessingCard) freezeWithError("E258");

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

    if (pdrv) return RES_PARERR;
    if (diskStatus & STA_NOINIT) return RES_NOTRDY;

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
#endif
