/*******************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only
 * intended for use with Renesas products. No other uses are authorized. This
 * software is owned by Renesas Electronics Corporation and is protected under
 * all applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
 * LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
 * TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
 * ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
 * ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
 * BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software
 * and to discontinue the availability of this software. By using this software,
 * you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Copyright (C) 2014 Renesas Electronics Corporation. All rights reserved.
 *******************************************************************************/
/*******************************************************************************
 * File Name     : rspi.c
 * Device(s)     : RZ/A1H (R7S721001)
 * Tool-Chain    : GNUARM-NONEv14.02-EABI
 * H/W Platform  : RSK+RZA1H CPU Board
 * Description   : This file implements device driver for RSPI module.
 *******************************************************************************/
/*******************************************************************************
 * History       : DD.MM.YYYY Version Description
 *               : 21.10.2014 1.00
 *******************************************************************************/

/******************************************************************************
Includes
******************************************************************************/
/* Default  type definition header */
#include "RZA1/system/r_typedefs.h"
#include "RZA1/system/rza_io_regrw.h"
/* I/O Register root header */
#include "RZA1/system/iodefine.h"
/* INTC Driver Header */
#include "RZA1/intc/devdrv_intc.h"
/* Device driver for RSPI header */
#include "RZA1/rspi/rspi.h"
/* Low level register read/write header */

#include "RZA1/system/iobitmasks/rspi_iobitmask.h"

#include "definitions.h"

#include <math.h>

/******************************************************************************
Global variables and functions
******************************************************************************/
/* RSPI1 transmit buffer address */
uint32_t* g_prspi1_tx_address;

/* RSPI1 transmit data number */
uint16_t g_rspi1_tx_count;

/* RSPI1 receive buffer address */
uint32_t* g_prspi1_rx_address;

/* RSPI1 receive data number */
uint16_t g_rspi1_rx_count;

/* RSPI1 receive data length */
uint16_t g_rspi1_rx_length;

/* SCI5 transmit buffer address */
uint8_t* g_pspi_tx_address;

/* SCI5 transmit data number */
uint16_t g_spi_tx_count;

/*******************************************************************************
 * Function Name: R_RSPI1_Create
 * Description  : This function initialises the RSPI1 module.
 * Arguments    : None
 * Return Value : None
 *******************************************************************************/

void R_RSPI_Create(uint8_t channel, uint32_t bitRate, uint8_t phase, uint8_t dataSize)
{
    uint16_t dummy_word = 0u;
    uint8_t dummy_byte  = 0u;

    UNUSED_VARIABLE(dummy_word);
    UNUSED_VARIABLE(dummy_byte);

    RSPI(channel).SPPCR = 0u;
    dummy_byte          = RSPI1.SPPCR;

    /* P1 clock = 66.67MHz, SPI bit rate = 11.11Mbits/s Check Table 16.3 */
    RSPI(channel).SPBR = ceil((float)66666666 / (bitRate * 2) - 1);
    dummy_byte         = RSPI(channel).SPBR;
    if (dataSize == 32)
        RSPI(channel).SPDCR = 0x60u; // 32-bit
    else if (dataSize == 16)
        RSPI(channel).SPDCR = 0x40u; // 16-bit
    else
        RSPI(channel).SPDCR = 0x20u; // 8-bit
    dummy_byte          = RSPI(channel).SPDCR;
    RSPI(channel).SPSCR = 0u;
    dummy_byte          = RSPI(channel).SPSCR;
    RSPI(channel).SPCKD = 0;
    dummy_byte          = RSPI(channel).SPCKD;
    RSPI(channel).SSLND = 0u;
    dummy_byte          = RSPI(channel).SSLND;
    RSPI(channel).SPND  = 0u;
    dummy_byte          = RSPI(channel).SPND;
    RSPI(channel).SSLP  = 0u;
    dummy_byte          = RSPI(channel).SSLP;
    RSPI(channel).SPSSR = 0u;
    dummy_byte          = RSPI(channel).SPSSR;
    if (dataSize == 32)
        RSPI(channel).SPBFCR.BYTE = 0b00100010;
    else if (dataSize == 16)
        RSPI(channel).SPBFCR.BYTE = 0b00100001;
    else
        RSPI(channel).SPBFCR.BYTE =
            0b00100000; // Receive buffer data triggering number is 1 byte. TX buffer declared "empty" as soon as it has
                        // 4 bytes of "space" in it (remember, it has 8 bytes total)
    dummy_byte = RSPI(channel).SPBFCR.BYTE;

    if (dataSize == 32)
        RSPI(channel).SPCMD0 = 0b0000001100000010 | phase; // 32-bit
    else if (dataSize == 16)
        RSPI(channel).SPCMD0 = 0b0000111100000010 | phase; // 16-bit
    else
        RSPI(channel).SPCMD0 = 0b0000011100000010 | phase; // 8-bit
    dummy_word = RSPI(channel).SPCMD0;

    /* Enable master mode */
    RSPI(channel).SPCR |= 0b00101000; // Just TX interrupt (for DMA). We'll manually enable the RX one when we need it.
    dummy_byte = RSPI(channel).SPCR;
}

/*******************************************************************************
 * Function Name: R_RSPI1_Start
 * Description  : This function starts the RSPI1 module operation.
 * Arguments    : None
 * Return Value : None
 *******************************************************************************/
void R_RSPI_Start(uint8_t channel)
{
    volatile uint8_t dummy = 0u;

    UNUSED_VARIABLE(dummy);

    /* Clear error sources */
    dummy                   = RSPI(channel).SPSR.BYTE;
    RSPI(channel).SPSR.BYTE = 0x00U;

    if (0 == RZA_IO_RegRead_8(&(RSPI(channel).SPSR.BYTE), RSPIn_SPSR_MODF_SHIFT, RSPIn_SPSR_MODF))
    {
        /* test */
        RSPI(channel).SPCR |= 0x40U;
        dummy = RSPI(channel).SPCR;
    }
    else
    {
        /* clear the MODF flag then set the SPE bit */
        dummy = RSPI(channel).SPSR.BYTE;

        RSPI(channel).SPCR |= 0x40U;
        dummy = RSPI(channel).SPCR;
    }
}

/*******************************************************************************
 * Function Name: R_RSPI1_Stop
 * Description  : This function stops the RSPI1 module operation.
 * Arguments    : None
 * Return Value : None
 *******************************************************************************/
void R_RSPI1_Stop(void)
{
    /* Disable RSPI interrupts */
    R_INTC_Disable(INTC_ID_SPTI4);

    /* Disable RSPI function */
    RZA_IO_RegWrite_8(&(RSPI1.SPCR), 0, RSPIn_SPCR_SPE_SHIFT, RSPIn_SPCR_SPE);
}

/*******************************************************************************
 * Function Name: R_RSPI1_SendReceive
 * Description  : This function sends and receives CSI#n1 data.
 * Arguments    : tx_buf -
 *                 transfer buffer pointer (not used when data is handled by DTC)
 *                tx_num -
 *                    buffer size
 *                rx_buf -
 *                 receive buffer pointer (not used when data is handled by DTC)
 * Return Value : status -
 *                    MD_OK or MD_ARGERROR
 *******************************************************************************/
uint8_t R_RSPI1_SendReceive(uint32_t* const tx_buf, uint16_t tx_num, uint32_t* const rx_buf)
{
    uint8_t status = 0u;

    if (tx_num < 1U)
    {
        status = 1u;
    }
    else
    {
        g_prspi1_tx_address = tx_buf;
        g_rspi1_tx_count    = tx_num;

        g_prspi1_rx_address = rx_buf;
        g_rspi1_rx_length   = tx_num;
        g_rspi1_rx_count    = 0U;

        /* Enable transmit interrupt */
        RZA_IO_RegWrite_8(&(RSPI1.SPCR), 1, RSPIn_SPCR_SPTIE_SHIFT, RSPIn_SPCR_SPTIE);

        /* Enable receive interrupt */
        RZA_IO_RegWrite_8(&(RSPI1.SPCR), 1, RSPIn_SPCR_SPRIE_SHIFT, RSPIn_SPCR_SPRIE);

        /* Enable RSPI function */
        RZA_IO_RegWrite_8(&(RSPI1.SPCR), 1, RSPIn_SPCR_SPE_SHIFT, RSPIn_SPCR_SPE);
    }

    return (status);
}

uint8_t R_RSPI1_SendReceiveBasic(uint8_t channel, uint8_t data)
{

    // Send data
    RSPI(channel).SPDR.BYTE.LL = data;

    // Wait til we receive the corresponding data
    while (0 == RZA_IO_RegRead_8(&(RSPI(channel).SPSR.BYTE), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
        ;

    // Receive data. Note that even if we didn't want the receive data, we still have to read it back, because SPI
    // transmission halts once the RX buffer is full
    return RSPI(channel).SPDR.BYTE.LL;
}

uint16_t R_RSPI1_SendReceiveBasic_16(uint8_t channel, uint16_t data)
{

    // Send data
    RSPI(channel).SPDR.WORD.L = data;

    // Wait til we receive the corresponding data
    while (0 == RZA_IO_RegRead_8(&(RSPI(channel).SPSR.BYTE), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
        ;

    // Receive data. Note that even if we didn't want the receive data, we still have to read it back, because SPI
    // transmission halts once the RX buffer is full
    return RSPI(channel).SPDR.WORD.L;
}

uint32_t R_RSPI1_SendReceiveBasic_32(uint8_t channel, uint32_t data)
{

    // Send data
    RSPI(channel).SPDR.LONG = data;

    // Wait til we receive the corresponding data
    while (0 == RZA_IO_RegRead_8(&(RSPI(channel).SPSR.BYTE), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
        ;

    // Receive data. Note that even if we didn't want the receive data, we still have to read it back, because SPI
    // transmission halts once the RX buffer is full
    return RSPI(channel).SPDR.LONG;
}

void R_RSPI_SendAndDontWait(uint8_t channel, uint8_t data)
{

    // Grab out any RX data that is there...
    while (RZA_IO_RegRead_8(&(RSPI(channel).SPSR.BYTE), RSPIn_SPSR_SPRF_SHIFT, RSPIn_SPSR_SPRF))
    {

        // Receive data. Note that even if we didn't want the receive data, we still have to read it back, because SPI
        // transmission halts once the RX buffer is full
        uint8_t dummy = RSPI(channel).SPDR.BYTE.LL;
    }

    // Send data
    RSPI(channel).SPDR.BYTE.LL = data;
}

/*******************************************************************************
 * Function Name: R_RSPI1_LoopBackReversed
 * Description  : This function .
 * Arguments    : None
 * Return Value : None
 *******************************************************************************/
void R_RSPI1_LoopBackReversed(void)
{
    RZA_IO_RegWrite_8(&(RSPI1.SPPCR), (uint8_t)1, RSPIn_SPPCR_SPLP_SHIFT, RSPIn_SPPCR_SPLP);
}

/*******************************************************************************
 * Function Name: R_RSPI1_LoopBackDisable
 * Description  : This function disable loop-back mode.
 * Arguments    : None
 * Return Value : None
 *******************************************************************************/
void R_RSPI1_LoopBackDisable(void)
{
    RZA_IO_RegWrite_8(&(RSPI1.SPPCR), 0, RSPIn_SPPCR_SPLP_SHIFT, RSPIn_SPPCR_SPLP);
}
