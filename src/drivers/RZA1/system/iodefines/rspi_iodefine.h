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
* http://www.renesas.com/disclaimer*
* Copyright (C) 2013-2014 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
* File Name : rspi_iodefine.h
* $Rev: 2444 $
* $Date:: 2014-10-14 21:15:39 +0100#$
* Description : Definition of I/O Register (V1.00a)
******************************************************************************/
#ifndef RSPI_IODEFINE_H
#define RSPI_IODEFINE_H
/* ->SEC M1.10.1 : Not magic number */

struct st_rspi
{                                                          /* RSPI             */
    volatile uint8_t   SPCR;                                   /*  SPCR            */
    volatile uint8_t   SSLP;                                   /*  SSLP            */
    volatile uint8_t   SPPCR;                                  /*  SPPCR           */
    volatile union
    {
        unsigned char BYTE;
        struct
        {
            unsigned char OVRF:1;
            unsigned char :1;
            unsigned char MODF:1;
            unsigned char :2;
            unsigned char SPTEF:1;
            unsigned char TEND:1;
            unsigned char SPRF:1;
        } BIT;
    } SPSR;
    union
    {
        unsigned long LONG;
        struct
        {
            unsigned short L;
            unsigned short H;
        } WORD;
        struct
        {
            unsigned char LL;
            unsigned char LH;
            unsigned char HL;
            unsigned char HH;
        } BYTE;
        struct
        {
            unsigned char SPD0:1;
            unsigned char SPD1:1;
            unsigned char SPD2:1;
            unsigned char SPD3:1;
            unsigned char SPD4:1;
            unsigned char SPD5:1;
            unsigned char SPD6:1;
            unsigned char SPD7:1;
            unsigned char SPD8:1;
            unsigned char SPD9:1;
            unsigned char SPD10:1;
            unsigned char SPD11:1;
            unsigned char SPD12:1;
            unsigned char SPD13:1;
            unsigned char SPD14:1;
            unsigned char SPD15:1;
            unsigned char SPD16:1;
            unsigned char SPD17:1;
            unsigned char SPD18:1;
            unsigned char SPD19:1;
            unsigned char SPD20:1;
            unsigned char SPD21:1;
            unsigned char SPD22:1;
            unsigned char SPD23:1;
            unsigned char SPD24:1;
            unsigned char SPD25:1;
            unsigned char SPD26:1;
            unsigned char SPD27:1;
            unsigned char SPD28:1;
            unsigned char SPD29:1;
            unsigned char SPD30:1;
            unsigned char SPD31:1;
        } BIT;
    } SPDR;
    volatile uint8_t   SPSCR;                                  /*  SPSCR           */
    volatile uint8_t   SPSSR;                                  /*  SPSSR           */
    volatile uint8_t   SPBR;                                   /*  SPBR            */
    volatile uint8_t   SPDCR;                                  /*  SPDCR           */
    volatile uint8_t   SPCKD;                                  /*  SPCKD           */
    volatile uint8_t   SSLND;                                  /*  SSLND           */
    volatile uint8_t   SPND;                                   /*  SPND            */
    volatile uint8_t   dummy1[1];                              /*                  */
#define SPCMD_COUNT 4
    volatile uint16_t SPCMD0;                                 /*  SPCMD0          */
    volatile uint16_t SPCMD1;                                 /*  SPCMD1          */
    volatile uint16_t SPCMD2;                                 /*  SPCMD2          */
    volatile uint16_t SPCMD3;                                 /*  SPCMD3          */
    volatile uint8_t   dummy2[8];                              /*                  */
    union
    {
        unsigned char BYTE;
        struct
        {
            unsigned char RXTRG:3;
            unsigned char :1;
            unsigned char TXTRG:2;
            unsigned char RXRST:1;
            unsigned char TXRST:1;
        } BIT;
    } SPBFCR;
    volatile uint8_t   dummy3[1];                              /*                  */
    volatile uint16_t SPBFDR;                                 /*  SPBFDR          */
};


#define RSPI0   (*(struct st_rspi    *)0xE800C800uL) /* RSPI0 */
#define RSPI1   (*(struct st_rspi    *)0xE800D000uL) /* RSPI1 */
#define RSPI2   (*(struct st_rspi    *)0xE800D800uL) /* RSPI2 */
#define RSPI3   (*(struct st_rspi    *)0xE800E000uL) /* RSPI3 */
#define RSPI4   (*(struct st_rspi    *)0xE800E800uL) /* RSPI4 */

#define RSPI(n)     (*(volatile struct st_rspi      *)(0xE800C800uL + 0x800 * n))

/* Start of channnel array defines of RSPI */

/* Channnel array defines of RSPI */
/*(Sample) value = RSPI[ channel ]->SPCR; */
#define RSPI_COUNT  5
#define RSPI_ADDRESS_LIST \
{   /* ->MISRA 11.3 */ /* ->SEC R2.7.1 */ \
    &RSPI0, &RSPI1, &RSPI2, &RSPI3, &RSPI4 \
}   /* <-MISRA 11.3 */ /* <-SEC R2.7.1 */ /* { } is for MISRA 19.4 */

/* End of channnel array defines of RSPI */


#define SPCR_0 RSPI0.SPCR
#define SSLP_0 RSPI0.SSLP
#define SPPCR_0 RSPI0.SPPCR
#define SPSR_0 RSPI0.SPSR
#define SPDR_0   RSPI0.SPDR.UINT32
#define SPDR_0L  RSPI0.SPDR.UINT16[L]
#define SPDR_0H  RSPI0.SPDR.UINT16[H]
#define SPDR_0LL RSPI0.SPDR.UINT8[LL]
#define SPDR_0LH RSPI0.SPDR.UINT8[LH]
#define SPDR_0HL RSPI0.SPDR.UINT8[HL]
#define SPDR_0HH RSPI0.SPDR.UINT8[HH]
#define SPSCR_0 RSPI0.SPSCR
#define SPSSR_0 RSPI0.SPSSR
#define SPBR_0 RSPI0.SPBR
#define SPDCR_0 RSPI0.SPDCR
#define SPCKD_0 RSPI0.SPCKD
#define SSLND_0 RSPI0.SSLND
#define SPND_0 RSPI0.SPND
#define SPCMD0_0 RSPI0.SPCMD0
#define SPCMD1_0 RSPI0.SPCMD1
#define SPCMD2_0 RSPI0.SPCMD2
#define SPCMD3_0 RSPI0.SPCMD3
#define SPBFCR_0 RSPI0.SPBFCR
#define SPBFDR_0 RSPI0.SPBFDR
#define SPCR_1 RSPI1.SPCR
#define SSLP_1 RSPI1.SSLP
#define SPPCR_1 RSPI1.SPPCR
#define SPSR_1 RSPI1.SPSR
#define SPDR_1   RSPI1.SPDR.UINT32
#define SPDR_1L  RSPI1.SPDR.UINT16[L]
#define SPDR_1H  RSPI1.SPDR.UINT16[H]
#define SPDR_1LL RSPI1.SPDR.UINT8[LL]
#define SPDR_1LH RSPI1.SPDR.UINT8[LH]
#define SPDR_1HL RSPI1.SPDR.UINT8[HL]
#define SPDR_1HH RSPI1.SPDR.UINT8[HH]
#define SPSCR_1 RSPI1.SPSCR
#define SPSSR_1 RSPI1.SPSSR
#define SPBR_1 RSPI1.SPBR
#define SPDCR_1 RSPI1.SPDCR
#define SPCKD_1 RSPI1.SPCKD
#define SSLND_1 RSPI1.SSLND
#define SPND_1 RSPI1.SPND
#define SPCMD0_1 RSPI1.SPCMD0
#define SPCMD1_1 RSPI1.SPCMD1
#define SPCMD2_1 RSPI1.SPCMD2
#define SPCMD3_1 RSPI1.SPCMD3
#define SPBFCR_1 RSPI1.SPBFCR
#define SPBFDR_1 RSPI1.SPBFDR
#define SPCR_2 RSPI2.SPCR
#define SSLP_2 RSPI2.SSLP
#define SPPCR_2 RSPI2.SPPCR
#define SPSR_2 RSPI2.SPSR
#define SPDR_2   RSPI2.SPDR.UINT32
#define SPDR_2L  RSPI2.SPDR.UINT16[L]
#define SPDR_2H  RSPI2.SPDR.UINT16[H]
#define SPDR_2LL RSPI2.SPDR.UINT8[LL]
#define SPDR_2LH RSPI2.SPDR.UINT8[LH]
#define SPDR_2HL RSPI2.SPDR.UINT8[HL]
#define SPDR_2HH RSPI2.SPDR.UINT8[HH]
#define SPSCR_2 RSPI2.SPSCR
#define SPSSR_2 RSPI2.SPSSR
#define SPBR_2 RSPI2.SPBR
#define SPDCR_2 RSPI2.SPDCR
#define SPCKD_2 RSPI2.SPCKD
#define SSLND_2 RSPI2.SSLND
#define SPND_2 RSPI2.SPND
#define SPCMD0_2 RSPI2.SPCMD0
#define SPCMD1_2 RSPI2.SPCMD1
#define SPCMD2_2 RSPI2.SPCMD2
#define SPCMD3_2 RSPI2.SPCMD3
#define SPBFCR_2 RSPI2.SPBFCR
#define SPBFDR_2 RSPI2.SPBFDR
#define SPCR_3 RSPI3.SPCR
#define SSLP_3 RSPI3.SSLP
#define SPPCR_3 RSPI3.SPPCR
#define SPSR_3 RSPI3.SPSR
#define SPDR_3   RSPI3.SPDR.UINT32
#define SPDR_3L  RSPI3.SPDR.UINT16[L]
#define SPDR_3H  RSPI3.SPDR.UINT16[H]
#define SPDR_3LL RSPI3.SPDR.UINT8[LL]
#define SPDR_3LH RSPI3.SPDR.UINT8[LH]
#define SPDR_3HL RSPI3.SPDR.UINT8[HL]
#define SPDR_3HH RSPI3.SPDR.UINT8[HH]
#define SPSCR_3 RSPI3.SPSCR
#define SPSSR_3 RSPI3.SPSSR
#define SPBR_3 RSPI3.SPBR
#define SPDCR_3 RSPI3.SPDCR
#define SPCKD_3 RSPI3.SPCKD
#define SSLND_3 RSPI3.SSLND
#define SPND_3 RSPI3.SPND
#define SPCMD0_3 RSPI3.SPCMD0
#define SPCMD1_3 RSPI3.SPCMD1
#define SPCMD2_3 RSPI3.SPCMD2
#define SPCMD3_3 RSPI3.SPCMD3
#define SPBFCR_3 RSPI3.SPBFCR
#define SPBFDR_3 RSPI3.SPBFDR
#define SPCR_4 RSPI4.SPCR
#define SSLP_4 RSPI4.SSLP
#define SPPCR_4 RSPI4.SPPCR
#define SPSR_4 RSPI4.SPSR
#define SPDR_4   RSPI4.SPDR.UINT32
#define SPDR_4L  RSPI4.SPDR.UINT16[L]
#define SPDR_4H  RSPI4.SPDR.UINT16[H]
#define SPDR_4LL RSPI4.SPDR.UINT8[LL]
#define SPDR_4LH RSPI4.SPDR.UINT8[LH]
#define SPDR_4HL RSPI4.SPDR.UINT8[HL]
#define SPDR_4HH RSPI4.SPDR.UINT8[HH]
#define SPSCR_4 RSPI4.SPSCR
#define SPSSR_4 RSPI4.SPSSR
#define SPBR_4 RSPI4.SPBR
#define SPDCR_4 RSPI4.SPDCR
#define SPCKD_4 RSPI4.SPCKD
#define SSLND_4 RSPI4.SSLND
#define SPND_4 RSPI4.SPND
#define SPCMD0_4 RSPI4.SPCMD0
#define SPCMD1_4 RSPI4.SPCMD1
#define SPCMD2_4 RSPI4.SPCMD2
#define SPCMD3_4 RSPI4.SPCMD3
#define SPBFCR_4 RSPI4.SPBFCR
#define SPBFDR_4 RSPI4.SPBFDR
/* <-SEC M1.10.1 */
#endif
