/******************************************************************************
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
* File Name     : rspi.h
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : This file implements device driver for RSPI module.
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/* Multiple inclusion prevention macro */
#ifndef RSPI_H
#define RSPI_H

#include "rspi_all_cpus.h"

/*******************************************************************************
Macro definitions (Register bit)
*******************************************************************************/

/* Control Register (SPCR) */

/* RSPI Mode Select (SPMS) */
/* SPI operation (four-wire method) */
#define _00_RSPI_MODE_SPI                       (0x00U)
/* Clock synchronous operation (three-wire method) */
#define _01_RSPI_MODE_CLOCK_SYNCHRONOUS         (0x01U)
/* Communications Operating Mode Select (TXMD) */
/* Full-duplex synchronous serial communications */
#define _00_RSPI_FULL_DUPLEX_SYNCHRONOUS        (0x00U)
/* Serial communications with transmit only operations */
#define _02_RSPI_TRANSMIT_ONLY                  (0x02U)
/* Mode Fault Error Detection Enable (MODFEN) */
/* Disables the detection of mode fault error */
#define _00_RSPI_MODE_FAULT_DETECT_DISABLED     (0x00U)
/* Enables the detection of mode fault error */
#define _04_RSPI_MODE_FAULT_DETECT_ENABLED      (0x04U)
/* RSPI Master/Slave Mode Select (MSTR) */
/* Slave mode */
#define _00_RSPI_SLAVE_MODE                     (0x00U)
/* Master mode */
#define _08_RSPI_MASTER_MODE                    (0x08U)
/* RSPI Error Interrupt Enable (SPEIE) */
/* Disables the generation of RSPI error interrupt */
#define _00_RSPI_ERROR_INTERRUPT_DISABLED       (0x00U)
/* Enables the generation of RSPI error interrupt */
#define _10_RSPI_ERROR_INTERRUPT_ENABLED        (0x10U)
/* RSPI Transmit Interrupt Enable (SPTIE) */
/* Disables the generation of RSPI transmit interrupt */
#define _00_RSPI_TRANSMIT_INTERRUPT_DISABLED    (0x00U)
/* Enables the generation of RSPI transmit interrupt */
#define _20_RSPI_TRANSMIT_INTERRUPT_ENABLED     (0x20U)
/* RSPI Function Enable (SPE) */
/* Disables the RSPI function */
#define _00_RSPI_FUNCTION_DISABLED              (0x00U)
/* Enables the RSPI function */
#define _40_RSPI_FUNCTION_ENABLED               (0x40U)
/* RSPI Receive Interrupt Enable (SPRIE) */
/* Disables the generation of RSPI receive interrupt */
#define _00_RSPI_RECEIVE_INTERRUPT_DISABLED     (0x00U)
/* Enables the generation of RSPI receive interrupt */
#define _80_RSPI_RECEIVE_INTERRUPT_ENABLED      (0x80U)

/*
    RSPI Slave Select Polarity Register (SSLP)
*/
/* SSL0 Signal Polarity Setting (SSL0P) */
/* SSL0 signal is active low */
#define _00_RSPI_SSL0_POLARITY_LOW              (0x00U)
/* SSL0 signal is active high */
#define _01_RSPI_SSL0_POLARITY_HIGH             (0x01U)
/* SSL1 Signal Polarity Setting (SSL1P) */
/* SSL1 signal is active low */
#define _00_RSPI_SSL1_POLARITY_LOW              (0x00U)
/* SSL1 signal is active high */
#define _02_RSPI_SSL1_POLARITY_HIGH             (0x02U)
/* SSL2 Signal Polarity Setting (SSL2P) */
/* SSL2 signal is active low */
#define _00_RSPI_SSL2_POLARITY_LOW              (0x00U)
/* SSL2 signal is active high */
#define _04_RSPI_SSL2_POLARITY_HIGH             (0x04U)
/* SSL3 Signal Polarity Setting (SSL3P) */
/* SSL3 signal is active low */
#define _00_RSPI_SSL3_POLARITY_LOW              (0x00U)
/* SSL3 signal is active high */
#define _08_RSPI_SSL3_POLARITY_HIGH             (0x08U)

/*
    RSPI Pin Control Register (SPPCR)
*/
/* RSPI Loop-back (SPLP) */
/* Normal mode */
#define _00_RSPI_LOOPBACK_DISABLED              (0x00U)
/* Loop-back mode (reversed transmit data = receive data) */
#define _01_RSPI_LOOPBACK_ENABLED               (0x01U)
/* RSPI Loop-back 2 (SPLP2) */
/* Normal mode */
#define _00_RSPI_LOOPBACK2_DISABLED             (0x00U)
/* Loop-back mode (transmit data = receive data) */
#define _02_RSPI_LOOPBACK2_ENABLED              (0x02U)
/* MOSI Idle Fixed Value (MOIFV) */
/* Level output on MOSIA during idling corresponds to low */
#define _00_RSPI_MOSI_LEVEL_LOW                 (0x00U)
/* Level output on MOSIA during idling corresponds to high */
#define _10_RSPI_MOSI_LEVEL_HIGH                (0x10U)
/* MOSI Idle Value Fixing Enable (MOIFE) */
/* MOSI output value equals final data from previous transfer */
#define _00_RSPI_MOSI_FIXING_PREV_TRANSFER      (0x00U)
/* MOSI output value equals the value set in the MOIFV bit */
#define _20_RSPI_MOSI_FIXING_MOIFV_BIT          (0x20U)

/* RSPI Sequence Control Register (SPSCR) */

/* RSPI Sequence Length Specification (SPSLN[2:0]) */


/* 0 -> 0... */
#define _00_RSPI_SEQUENCE_LENGTH_1              (0x00U)
/* 0 -> 1 -> 0... */
#define _01_RSPI_SEQUENCE_LENGTH_2              (0x01U)
/* 0 -> 1 -> 2 -> 0... */
#define _02_RSPI_SEQUENCE_LENGTH_3              (0x02U)
/* 0 -> 1 -> 2 -> 3 -> 0... */
#define _03_RSPI_SEQUENCE_LENGTH_4              (0x03U)
/* 0 -> 1 -> 2 -> 3 -> 4 -> 0... */
#define _04_RSPI_SEQUENCE_LENGTH_5              (0x04U)
/* 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 0... */
#define _05_RSPI_SEQUENCE_LENGTH_6              (0x05U)
/* 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 0... */
#define _06_RSPI_SEQUENCE_LENGTH_7              (0x06U)
/* 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 0... */
#define _07_RSPI_SEQUENCE_LENGTH_8              (0x07U)

/* RSPI Data Control Register (SPDCR) */

/* Number of Frames Specification (SPFC[1:0]) */


/* 1 frame */
#define _00_RSPI_FRAMES_1                       (0x00U)
/* 2 frames */
#define _01_RSPI_FRAMES_2                       (0x01U)
/* 3 frames */
#define _02_RSPI_FRAMES_3                       (0x02U)
/* 4 frames */
#define _03_RSPI_FRAMES_4                       (0x03U)

/* RSPI Receive/Transmit Data Selection (SPRDTD) */
/* read SPDR values from receive buffer */
#define _00_RSPI_READ_SPDR_RX_BUFFER            (0x00U)
/* read SPDR values from transmit buffer (transmit buffer empty) */
#define _10_RSPI_READ_SPDR_TX_BUFFER            (0x10U)
/* RSPI Longword Access/Word Access Specification (SPLW) */ 
/* SPDR is accessed in words */
#define _00_RSPI_ACCESS_WORD                    (0x00U)
/* SPDR is accessed in longwords */
#define _20_RSPI_ACCESS_LONGWORD                (0x20U)

/* RSPI Clock Delay Register (SPCKD) */

/* RSPCK Delay Setting (SCKDL[2:0]) */
/* 1 RSPCK */
#define _00_RSPI_RSPCK_DELAY_1                  (0x00U)
/* 2 RSPCK */
#define _01_RSPI_RSPCK_DELAY_2                  (0x01U)
/* 3 RSPCK */
#define _02_RSPI_RSPCK_DELAY_3                  (0x02U)
/* 4 RSPCK */
#define _03_RSPI_RSPCK_DELAY_4                  (0x03U)
/* 5 RSPCK */
#define _04_RSPI_RSPCK_DELAY_5                  (0x04U)
/* 6 RSPCK */
#define _05_RSPI_RSPCK_DELAY_6                  (0x05U)
/* 7 RSPCK */
#define _06_RSPI_RSPCK_DELAY_7                  (0x06U)
/* 8 RSPCK */
#define _07_RSPI_RSPCK_DELAY_8                  (0x07U)

/* RSPI Slave Select Negation Delay Register (SSLND) */
/* SSL Negation Delay Setting (SLNDL[2:0]) */
/* 1 RSPCK */
#define _00_RSPI_SSL_NEGATION_DELAY_1           (0x00U)
/* 2 RSPCK */
#define _01_RSPI_SSL_NEGATION_DELAY_2           (0x01U)
#define _02_RSPI_SSL_NEGATION_DELAY_3           (0x02U)
/* 3 RSPCK */
/* 4 RSPCK */
#define _03_RSPI_SSL_NEGATION_DELAY_4           (0x03U)
/* 5 RSPCK */
#define _04_RSPI_SSL_NEGATION_DELAY_5           (0x04U)
/* 6 RSPCK */
#define _05_RSPI_SSL_NEGATION_DELAY_6           (0x05U)
/* 7 RSPCK */
#define _06_RSPI_SSL_NEGATION_DELAY_7           (0x06U)
/* 8 RSPCK */
#define _07_RSPI_SSL_NEGATION_DELAY_8           (0x07U)

/* RSPI Next-Access Delay Register (SPND) */
/* RSPI Next-Access Delay Setting (SPNDL[2:0]) */
/* 1 RSPCK + 2 PCLK */
#define _00_RSPI_NEXT_ACCESS_DELAY_1            (0x00U)
/* 2 RSPCK + 2 PCLK */
#define _01_RSPI_NEXT_ACCESS_DELAY_2            (0x01U)
/* 3 RSPCK + 2 PCLK */
#define _02_RSPI_NEXT_ACCESS_DELAY_3            (0x02U)
/* 4 RSPCK + 2 PCLK */
#define _03_RSPI_NEXT_ACCESS_DELAY_4            (0x03U)
/* 5 RSPCK + 2 PCLK */
#define _04_RSPI_NEXT_ACCESS_DELAY_5            (0x04U)
/* 6 RSPCK + 2 PCLK */
#define _05_RSPI_NEXT_ACCESS_DELAY_6            (0x05U)
/* 7 RSPCK + 2 PCLK */
#define _06_RSPI_NEXT_ACCESS_DELAY_7            (0x06U)
/* 8 RSPCK + 2 PCLK */
#define _07_RSPI_NEXT_ACCESS_DELAY_8            (0x07U)

/* RSPI Control Register 2 (SPCR2) */
/* Parity Enable (SPPE) */
/* Does not add parity bit to transmit data */
#define _00_RSPI_PARITY_DISABLE                 (0x00U)
/* Adds the parity bit to transmit data */
#define _01_RSPI_PARITY_ENABLE                  (0x01U)
/* Parity Mode (SPOE) */
/* Selects even parity for use in transmission and reception */
#define _00_RSPI_PARITY_EVEN                    (0x00U)
#define _02_RSPI_PARITY_ODD                     (0x02U)
/* Selects odd parity for use in transmission and reception */
/* RSPI Idle Interrupt Enable (SPIIE) */
/* Disables the generation of RSPI idle interrupt */
#define _00_RSPI_IDLE_INTERRUPT_DISABLED        (0x00U)
/* Enables the generation of RSPI idle interrupt */
#define _04_RSPI_IDLE_INTERRUPT_ENABLED         (0x04U)
/* Parity Self-Testing (PTE) */
/* Disables the self-diagnosis function of the parity circuit */
#define _00_RSPI_SELF_TEST_DISABLED             (0x00U)
/* Enables the self-diagnosis function of the parity circuit */
#define _08_RSPI_SELF_TEST_ENABLED              (0x08U)

/*
    RSPI Command Registers 0 to 7 (SPCMD0 to SPCMD7)
*/
/* RSPCK Phase Setting (CPHA) */
/* Data sampling on odd edge, data variation on even edge */
#define _0000_RSPI_RSPCK_SAMPLING_ODD           (0x0000U)
/* Data variation on odd edge, data sampling on even edge */
#define _0001_RSPI_RSPCK_SAMPLING_EVEN          (0x0001U)
/* RSPCK Polarity Setting (CPOL) */
/* RSPCK is low when idle */
#define _0000_RSPI_RSPCK_POLARITY_LOW           (0x0000U)
/* RSPCK is high when idle */
#define _0002_RSPI_RSPCK_POLARITY_HIGH          (0x0002U)
/* Bit Rate Division Setting (BRDV[1:0]) */
/* These bits select the base bit rate */
#define _0000_RSPI_BASE_BITRATE_1               (0x0000U)
/* These bits select the base bit rate divided by 2 */
#define _0004_RSPI_BASE_BITRATE_2               (0x0004U)
/* These bits select the base bit rate divided by 4 */
#define _0008_RSPI_BASE_BITRATE_4               (0x0008U)
/* These bits select the base bit rate divided by 8 */
#define _000C_RSPI_BASE_BITRATE_8               (0x000CU)
/* SSL Signal Assertion Setting (SSLA[2:0]) */
/* SSL0 */
#define _0000_RSPI_SIGNAL_ASSERT_SSL0           (0x0000U)
/* SSL1 */
#define _0010_RSPI_SIGNAL_ASSERT_SSL1           (0x0010U)
/* SSL2 */
#define _0020_RSPI_SIGNAL_ASSERT_SSL2           (0x0020U)
/* SSL3 */
#define _0030_RSPI_SIGNAL_ASSERT_SSL3           (0x0030U)
/* SSL Signal Level Keeping (SSLKP) */
/* Negates all SSL signals upon completion of transfer */
#define _0000_RSPI_SSL_KEEP_DISABLE             (0x0000U)
/* Keep SSL level from end of transfer till next access */
#define _0080_RSPI_SSL_KEEP_ENABLE              (0x0080U)
/* RSPI Data Length Setting (SPB[3:0]) */
/* 8 bits */
#define _0400_RSPI_DATA_LENGTH_BITS_8           (0x0400U)
/* 9 bits */
#define _0800_RSPI_DATA_LENGTH_BITS_9           (0x0800U)
/* 10 bits */
#define _0900_RSPI_DATA_LENGTH_BITS_10          (0x0900U)
/* 11 bits */
#define _0A00_RSPI_DATA_LENGTH_BITS_11          (0x0A00U)
/* 12 bits */
#define _0B00_RSPI_DATA_LENGTH_BITS_12          (0x0B00U)
/* 13 bits */
#define _0C00_RSPI_DATA_LENGTH_BITS_13          (0x0C00U)
/* 14 bits */
#define _0D00_RSPI_DATA_LENGTH_BITS_14          (0x0D00U)
/* 15 bits */
#define _0E00_RSPI_DATA_LENGTH_BITS_15          (0x0E00U)
/* 16 bits */
#define _0F00_RSPI_DATA_LENGTH_BITS_16          (0x0F00U)
/* 20 bits */
#define _0000_RSPI_DATA_LENGTH_BITS_20          (0x0000U)
/* 24 bits */
#define _0100_RSPI_DATA_LENGTH_BITS_24          (0x0100U)
/* 32 bits */
#define _0200_RSPI_DATA_LENGTH_BITS_32          (0x0200U)
/* RSPI LSB First (LSBF) */
/* MSB first */
#define _0000_RSPI_MSB_FIRST                    (0x0000U)
/* LSB first */
#define _1000_RSPI_LSB_FIRST                    (0x1000U)
/* RSPI Next-Access Delay Enable (SPNDEN) */
/* Next-access delay of 1 RSPCK + 2 PCLK */
#define _0000_RSPI_NEXT_ACCESS_DELAY_DISABLE    (0x0000U)
/* Next-access delay equal to setting of SPND register */
#define _2000_RSPI_NEXT_ACCESS_DELAY_ENABLE     (0x2000U)
/* SSL Negation Delay Setting Enable (SLNDEN) */
/* SSL negation delay of 1 RSPCK */
#define _0000_RSPI_NEGATION_DELAY_DISABLE       (0x0000U)
/* SSL negation delay equal to setting of SSLND register */
#define _4000_RSPI_NEGATION_DELAY_ENABLE        (0x4000U)
/* RSPCK Delay Setting Enable (SCKDEN) */
/* RSPCK delay of 1 RSPCK */
#define _0000_RSPI_RSPCK_DELAY_DISABLE          (0x0000U)
/* RSPCK delay equal to setting of the SPCKD register */
#define _8000_RSPI_RSPCK_DELAY_ENABLE           (0x8000U)

/*
    Interrupt Source Priority Register n (IPRn)
*/
/* Interrupt Priority Level Select (IPR[3:0]) */
/* Level 0 (interrupt disabled) */
#define _00_RSPI_PRIORITY_LEVEL0                (0x00U)
/* Level 1 */
#define _01_RSPI_PRIORITY_LEVEL1                (0x01U)
/* Level 2 */
#define _02_RSPI_PRIORITY_LEVEL2                (0x02U)
/* Level 3 */
#define _03_RSPI_PRIORITY_LEVEL3                (0x03U)
/* Level 4 */
#define _04_RSPI_PRIORITY_LEVEL4                (0x04U)
/* Level 5 */
#define _05_RSPI_PRIORITY_LEVEL5                (0x05U)
/* Level 6 */
#define _06_RSPI_PRIORITY_LEVEL6                (0x06U)
/* Level 7 */
#define _07_RSPI_PRIORITY_LEVEL7                (0x07U)
/* Level 8 */
#define _08_RSPI_PRIORITY_LEVEL8                (0x08U)
/* Level 9 */
#define _09_RSPI_PRIORITY_LEVEL9                (0x09U)
/* Level 10 */
#define _0A_RSPI_PRIORITY_LEVEL10               (0x0AU)
/* Level 11 */
#define _0B_RSPI_PRIORITY_LEVEL11               (0x0BU)
/* Level 12 */
#define _0C_RSPI_PRIORITY_LEVEL12               (0x0CU)
/* Level 13 */
#define _0D_RSPI_PRIORITY_LEVEL13               (0x0DU)
/* Level 14 */
#define _0E_RSPI_PRIORITY_LEVEL14               (0x0EU)
/* Level 15 (highest) */
#define _0F_RSPI_PRIORITY_LEVEL15               (0x0FU)


/*******************************************************************************
Macro definitions
*******************************************************************************/
#define _20_RSPI0_DIVISOR                       (0x20U)

/*******************************************************************************
Typedef definitions
*******************************************************************************/
/* SPI0 transmit interrupt priority */
#define ADC_PRI_SPTI1        (8uL)
/* SPI0 receive interrupt priority */
#define ADC_PRI_SPRI1        (6uL)
/* SPI0 error interrupt priority */
#define ADC_PRI_SPEI1        (7uL)
/* SPI0 idle interrupt priority */
#define ADC_PRI_SPII1        (5uL)

/*******************************************************************************
Global functions
*******************************************************************************/
void R_RSPI_Create (uint8_t channel, uint32_t bitRate, uint8_t phase, uint8_t dataSize);
void R_RSPI1_LoopBackReversed (void);
void R_RSPI1_LoopBackDisable (void);
void R_RSPI1_CallBackTransmitEnd (void);
void R_RSPI1_CallBackReceiveEnd (void);
void R_RSPI1_CallBackError (uint8_t err_type);

uint8_t R_RSPI1_SendReceive (uint32_t * const txbuf,
                                      uint16_t txnum, uint32_t * const rxbuf);
uint8_t R_RSPI1_SendReceiveBasic(uint8_t channel, uint8_t data);
uint16_t R_RSPI1_SendReceiveBasic_16(uint8_t channel, uint16_t data);
uint32_t R_RSPI1_SendReceiveBasic_32(uint8_t channel, uint32_t data);
void R_RSPI_SendAndDontWait(uint8_t channel, uint8_t data);

/* Start user code for function. Do not edit comment generated here */
void R_RSPI_Start (uint8_t channel);
void R_RSPI1_Stop (void);

void R_SPI_Init (uint8_t channel);

/* End user code. Do not edit comment generated here */

/* RSPI_H */
#endif
