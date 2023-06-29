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
* Copyright (C) 2013 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
* File Name : uahv_iodefine.h
* Description : Definition of I/O Register (V0.50j)
******************************************************************************/
#ifndef UAHV_IODEFINE_H
#define UAHV_IODEFINE_H

struct st_uahv
{                                                          /* UAHV             */
    volatile    uint8_t        URTH0CTL0;                              /*  URTH0CTL0       */
    volatile    uint8_t        dummy156[3];                            /*                  */
    volatile    uint8_t        URTH0OPT1;                              /*  URTH0OPT1       */
    volatile    uint8_t        dummy157[3];                            /*                  */
    volatile    uint16_t       URTH0OPT2;                              /*  URTH0OPT2       */
    volatile    uint8_t        dummy158[2];                            /*                  */
    volatile    uint8_t        URTH0TRG;                               /*  URTH0TRG        */
    volatile    uint8_t        dummy159[3];                            /*                  */
    volatile    uint8_t        URTH0STR0;                              /*  URTH0STR0       */
    volatile    uint8_t        dummy160[3];                            /*                  */
    volatile    uint8_t        URTH0STR1;                              /*  URTH0STR1       */
    volatile    uint8_t        dummy161[3];                            /*                  */
    volatile    uint8_t        URTH0STC;                               /*  URTH0STC        */
    volatile    uint8_t        dummy162[3];                            /*                  */
    volatile    uint8_t        URTH0RX;                                /*  URTH0RX         */
    volatile    uint8_t        dummy163[3];                            /*                  */
    volatile    uint16_t       URTH0RXHL;                              /*  URTH0RXHL       */
    volatile    uint8_t        dummy164[2];                            /*                  */
    volatile    uint8_t        URTH0ERX;                               /*  URTH0ERX        */
    volatile    uint8_t        dummy165[3];                            /*                  */
    volatile    uint32_t       URTH0ERXW;                              /*  URTH0ERXW       */
    volatile    uint8_t        URTH0TX;                                /*  URTH0TX         */
    volatile    uint8_t        dummy166[3];                            /*                  */
    volatile    uint16_t       URTH0TXHL;                              /*  URTH0TXHL       */
    volatile    uint8_t        dummy167[2];                            /*                  */
    volatile    uint8_t        URTH0ETX;                               /*  URTH0ETX        */
    volatile    uint8_t        dummy168[3];                            /*                  */
    volatile    uint32_t       URTH0ETXW;                              /*  URTH0ETXW       */
    volatile    uint8_t        dummy169[4];                            /*                  */
    volatile    uint16_t       URTH0CTL1;                              /*  URTH0CTL1       */
    volatile    uint8_t        dummy170[2];                            /*                  */
    volatile    uint16_t       URTH0CTL2;                              /*  URTH0CTL2       */
    volatile    uint8_t        dummy171[2];                            /*                  */
    volatile    uint16_t       URTH0OPT0;                              /*  URTH0OPT0       */
};


#define UAHV    (*(volatile struct st_uahv    *)0xE8030000uL) /* UAHV */


#define UAHVURTH0CTL0 UAHV.URTH0CTL0
#define UAHVURTH0OPT1 UAHV.URTH0OPT1
#define UAHVURTH0OPT2 UAHV.URTH0OPT2
#define UAHVURTH0TRG UAHV.URTH0TRG
#define UAHVURTH0STR0 UAHV.URTH0STR0
#define UAHVURTH0STR1 UAHV.URTH0STR1
#define UAHVURTH0STC UAHV.URTH0STC
#define UAHVURTH0RX UAHV.URTH0RX
#define UAHVURTH0RXHL UAHV.URTH0RXHL
#define UAHVURTH0ERX UAHV.URTH0ERX
#define UAHVURTH0ERXW UAHV.URTH0ERXW
#define UAHVURTH0TX UAHV.URTH0TX
#define UAHVURTH0TXHL UAHV.URTH0TXHL
#define UAHVURTH0ETX UAHV.URTH0ETX
#define UAHVURTH0ETXW UAHV.URTH0ETXW
#define UAHVURTH0CTL1 UAHV.URTH0CTL1
#define UAHVURTH0CTL2 UAHV.URTH0CTL2
#define UAHVURTH0OPT0 UAHV.URTH0OPT0
#endif
