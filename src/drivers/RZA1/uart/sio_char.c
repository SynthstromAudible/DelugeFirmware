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
* File Name     : sio_char.c
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Serial I/O character R/W (SCIF 2-ch process)
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include <drivers/RZA1/system/rza_io_regrw.h>
#include "sio_char.h"
#include "iodefine.h"
#include "scif_iobitmask.h"
#include "gpio_iobitmask.h"
#include "iodefine.h"
#include "definitions.h"
#include <stdlib.h>
#include "cpu_specific.h"
#include <math.h>

#include "uart_all_cpus.h"

char_t picTxBuffer[PIC_TX_BUFFER_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));
char_t midiTxBuffer[MIDI_TX_BUFFER_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));

char_t picRxBuffer[PIC_RX_BUFFER_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));
char_t midiRxBuffer[MIDI_RX_BUFFER_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));
uint32_t midiRxTimingBuffer[MIDI_RX_TIMING_BUFFER_SIZE] __attribute__((aligned(
    CACHE_LINE_SIZE))); // I'd like to store just 16 bits per entry for this, but the DMA wouldn't do it - whether or not I also set its source data size to 16 bits

char_t* const txBuffers[]      = {picTxBuffer, midiTxBuffer};
const uint16_t txBufferSizes[] = {PIC_TX_BUFFER_SIZE, MIDI_TX_BUFFER_SIZE};

char_t* const rxBuffers[]      = {picRxBuffer, midiRxBuffer};
const uint16_t rxBufferSizes[] = {PIC_RX_BUFFER_SIZE, MIDI_RX_BUFFER_SIZE};

const uint8_t uartChannels[]  = {UART_CHANNEL_PIC, UART_CHANNEL_MIDI};
const uint8_t txDmaChannels[] = {PIC_TX_DMA_CHANNEL, MIDI_TX_DMA_CHANNEL};
const uint8_t rxDmaChannels[] = {PIC_RX_DMA_CHANNEL, MIDI_RX_DMA_CHANNEL};

char_t* rxBufferReadAddr[] = {picRxBuffer, midiRxBuffer};

char_t const timingCaptureItems[]         = {UART_ITEM_MIDI};
uint16_t const timingCaptureBufferSizes[] = {MIDI_RX_TIMING_BUFFER_SIZE};
uint32_t* const timingCaptureBuffers[]    = {midiRxTimingBuffer};
uint8_t const timingCaptureDMAChannels[]  = {MIDI_RX_TIMING_DMA_CHANNEL};

void uartInit(int item, uint32_t baudRate)
{

    int scifID = uartChannels[item];

    Userdef_SCIF_UART_Init(scifID, SCIF_UART_MODE_RW, SCIF_CKS_DIVISION_1, baudRate);
}

void Userdef_SCIF_UART_Init(uint8_t channel, uint8_t mode, uint16_t cks, uint32_t baudRate)
{
    volatile uint8_t dummy_buf = 0u;

    /* Used to suppress the variable declared but unused warning */
    UNUSED_VARIABLE(dummy_buf);

    /* Module standby clear */
    /* Supply clock to the SCIF(channel 2) */
    // I've deactivated this because I think all clocks are switched on in stb.c, and we don't want just channel 2 anymore
    /*
    rza_io_reg_write_8((uint8_t *)&CPG.STBCR4,
    		          0,
    		          CPG_STBCR4_MSTP45_SHIFT,
    		          CPG_STBCR4_MSTP45);
*/
    /* Dummy read */
    dummy_buf = CPG.STBCR4;

    /* SCIF initial setting */
    /* Serial control register (SCSCR2) setting */
    /* SCIF transmitting and receiving operations stop */
    SCIFA(channel).SCSCR = 0x0000u;

    /* FIFO control register (SCFCR2) setting */
    if (SCIF_UART_MODE_W == (mode & SCIF_UART_MODE_W))
    {
        /* Transmit FIFO reset */
        RZA_IO_RegWrite_16((uint16_t*)&SCIFA(channel).SCFCR, 1, SCIF2_SCFCR_TFRST_SHIFT, SCIF2_SCFCR_TFRST);
    }

    if (SCIF_UART_MODE_R == (mode & SCIF_UART_MODE_R))
    {
        /* Receive FIFO data register reset */
        RZA_IO_RegWrite_16((uint16_t*)&SCIFA(channel).SCFCR, 1, SCIF2_SCFCR_RFRST_SHIFT, SCIF2_SCFCR_RFRST);
    }

    /* Serial status register (SCFSR2) setting */
    /* ER,BRK,DR bit clear */
    SCIFA(channel).SCFSR &= 0xFF6Eu;

    /* Line status register (SCLSR2) setting */
    /* ORER bit clear */
    RZA_IO_RegWrite_16((uint16_t*)&SCIFA(channel).SCLSR, 0, SCIF2_SCLSR_ORER_SHIFT, SCIF2_SCLSR_ORER);

    /* Serial control register (SCSCR2) setting */
    /* B'00 : Internal CLK */
    RZA_IO_RegWrite_16((uint16_t*)&SCIFA(channel).SCSCR, 0, SCIF2_SCSCR_CKE_SHIFT, SCIF2_SCSCR_CKE);

    /* Serial mode register (SCSMR2) setting
    b7    C/A  - Communication mode : Asynchronous mode
    b6    CHR  - Character length   : 8-bit data
    b5    PE   - Parity enable      : Add and check are disabled
    b3    STOP - Stop bit length    : 1 stop bit
    b1:b0 CKS  - Clock select       : cks(argument) */
    SCIFA(channel).SCSMR = (cks & 0b0011);

    /* Serial extension mode register (SCEMR2) setting
    b7 BGDM - Baud rate generator double-speed mode  : Double speed mode (increased by Rohan)
    b0 ABCS - Base clock select in asynchronous mode : Base clock is 16
    times the bit rate */
    SCIFA(channel).SCEMR = 0b10000000;

    uartSetBaudRate(channel, baudRate);

    /*
	b10:b8 RSTRG - RTS output active trigger         : Initial value
	b7:b6  RTRG  - Receive FIFO data trigger         : 1-data
	b5:b4  TTRG  - Transmit FIFO data trigger        : 0-data
	b3     MCE   - Modem control enable              : Disabled
	b2     TFRST - Transmit FIFO data register reset : Disabled
	b1     RFRST - Receive FIFO data register reset  : Disabled
	b0     LOOP  - Loop-back test                    : Disabled */
    SCIFA(channel).SCFCR = 0x0030u; // TX trigger 0; RX trigger 1
    SCIFA(channel).SCFCR = 0;       // TX trigger 8; RX trigger 1

    /* Serial port register (SCSPTR2) setting
    b1 SPB2IO - Serial port break output : Enabled
    b0 SPB2DT - Serial port break data   : High-level */
    SCIFA(channel).SCSPTR |= 0x0003u;
}

void uartSetBaudRate(uint8_t channel, uint32_t baudRate)
{

    uint8_t scbrr = round((float)(XTAL_SPEED_MHZ * 5) / (16 * baudRate) - 1);

    /* Bit rate register (SCBRR2) setting */
    SCIFA(channel).SCBRR = scbrr;
}

static void PIC_TX_INT_TrnEnd(uint32_t int_sense)
{
    tx_interrupt(UART_ITEM_PIC);
}

static void MIDI_TX_INT_TrnEnd(uint32_t int_sense)
{
    tx_interrupt(UART_ITEM_MIDI);
}

void (*const txInterruptFunctions[])(uint32_t) = {PIC_TX_INT_TrnEnd, MIDI_TX_INT_TrnEnd};

const uint8_t txInterruptPriorities[] = {
    5,
    13 // This has to be higher number / lower priority than the MIDI-send timer interrupt
};

const uint32_t picUartDmaRxLinkDescriptor[] __attribute__((aligned(CACHE_LINE_SIZE))) = {
    0b1101,                                                                          // Header
    (uint32_t)&SCIFA(UART_CHANNEL_PIC).SCFRDR,                                       // Source address
    (uint32_t)picRxBuffer,                                                           // Destination address
    PIC_RX_BUFFER_SIZE,                                                              // Transaction size
    0b10000001000100000000000001100000 | DMA_AM_FOR_SCIF | (PIC_RX_DMA_CHANNEL & 7), // Config
    0,                                                                               // Interval
    0,                                                                               // Extension
    (uint32_t)picUartDmaRxLinkDescriptor // Next link address (this one again)
};

const uint32_t midiUartDmaRxLinkDescriptor[] __attribute__((aligned(CACHE_LINE_SIZE))) = {
    0b1101,                                                                           // Header
    (uint32_t)&SCIFA(UART_CHANNEL_MIDI).SCFRDR,                                       // Source address
    (uint32_t)midiRxBuffer,                                                           // Destination address
    MIDI_RX_BUFFER_SIZE,                                                              // Transaction size
    0b10000001000100000000000001100000 | DMA_AM_FOR_SCIF | (MIDI_RX_DMA_CHANNEL & 7), // Config
    0,                                                                                // Interval
    0,                                                                                // Extension
    (uint32_t)midiUartDmaRxLinkDescriptor // Next link address (this one again)
};

const uint32_t midiUartDmaRxTimingLinkDescriptor[] __attribute__((aligned(CACHE_LINE_SIZE))) = {
    0b1101,                                                 // Header
    (uint32_t)&DMACnNonVolatile(SSI_TX_DMA_CHANNEL).CRSA_n, // Source address
    (uint32_t)midiRxTimingBuffer,                           // Destination address
    sizeof(midiRxTimingBuffer),                             // Transaction size
    0b10000001000100100010000000100000 | DMA_AM_FOR_SCIF
        | (MIDI_RX_TIMING_DMA_CHANNEL
            & 7), // Config. LVL has to be set to 0 for some reason, otherwise we get duplicates of each value
    0,            // Interval
    0,            // Extension
    (uint32_t)midiUartDmaRxTimingLinkDescriptor // Next link address (this one again)
};

const uint32_t* const uartRxLinkDescriptors[] = {picUartDmaRxLinkDescriptor, midiUartDmaRxLinkDescriptor};

const uint32_t* const timingCaptureLinkDescriptors[] = {midiUartDmaRxTimingLinkDescriptor};

/* End of File */
