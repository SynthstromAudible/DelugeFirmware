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
******************************************************************************/
/******************************************************************************
* File Name     : sio_char.h
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Sample Program - Terminal I/O header file
******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/* Multiple inclusion prevention macro */
#ifndef SIO_CHAR_H
#define SIO_CHAR_H

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "devdrv_intc.h"
#include "uart_all_cpus.h"

#define SCIF_UART_MODE_W  (1)
#define SCIF_UART_MODE_R  (2)
#define SCIF_UART_MODE_RW (3)

typedef enum scif_cks_division {
    SCIF_CKS_DIVISION_1,
    SCIF_CKS_DIVISION_4,
    SCIF_CKS_DIVISION_16,
    SCIF_CKS_DIVISION_64
} scif_cks_division_t;

/******************************************************************************
Functions Prototypes
******************************************************************************/

void IoInitScif2(void);
char_t IoGetchar(uint8_t scifID);
void IoPutchar(uint8_t scifID, char_t buffer);

void io_init_scif(uint8_t scifID);

void uartInit(int uartItem, uint32_t baudRate);
void uartSetBaudRate(uint8_t scifID, uint32_t baudRate);

void Userdef_SCIF_UART_Init(uint8_t channel, uint8_t mode, uint16_t cks, uint32_t baudRate);

extern char_t picTxBuffer[];
extern char_t midiTxBuffer[];

// These are not thread safe! Do not call in ISRs.
#define bufferPICUart(charToSend)                                                                                      \
    {                                                                                                                  \
        picTxBuffer[uartItems[UART_ITEM_PIC].txBufferWritePos + UNCACHED_MIRROR_OFFSET] = charToSend;                  \
        uartItems[UART_ITEM_PIC].txBufferWritePos =                                                                    \
            (uartItems[UART_ITEM_PIC].txBufferWritePos + 1) & (PIC_TX_BUFFER_SIZE - 1);                                \
    }
#define bufferMIDIUart(charToSend)                                                                                     \
    {                                                                                                                  \
        midiTxBuffer[uartItems[UART_ITEM_MIDI].txBufferWritePos + UNCACHED_MIRROR_OFFSET] = charToSend;                \
        uartItems[UART_ITEM_MIDI].txBufferWritePos =                                                                   \
            (uartItems[UART_ITEM_MIDI].txBufferWritePos + 1) & (MIDI_TX_BUFFER_SIZE - 1);                              \
    }

// Aliases
#define bufferPICIndicatorsUart(charToSend) bufferPICUart(charToSend)
#define bufferPICPadsUart(charToSend)       bufferPICUart(charToSend)

/* SIO_CHAR_H */
#endif

/* End of File */
