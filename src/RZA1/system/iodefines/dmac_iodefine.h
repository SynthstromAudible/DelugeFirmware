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
* File Name : dmac_iodefine.h
* $Rev: 2444 $
* $Date:: 2014-10-14 21:15:39 +0100#$
* Description : Definition of I/O Register (V1.00a)
******************************************************************************/
#ifndef DMAC_IODEFINE_H
#define DMAC_IODEFINE_H
/* ->QAC 0639 : Over 127 members (C90) */
/* ->SEC M1.10.1 : Not magic number */

#include "r_typedefs.h"

struct st_dmac
{                                                          /* DMAC             */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_0;                                 /*  N0SA_0          */
    volatile uint32_t  N0DA_0;                                 /*  N0DA_0          */
    volatile uint32_t  N0TB_0;                                 /*  N0TB_0          */
    volatile uint32_t  N1SA_0;                                 /*  N1SA_0          */
    volatile uint32_t  N1DA_0;                                 /*  N1DA_0          */
    volatile uint32_t  N1TB_0;                                 /*  N1TB_0          */
    volatile uint32_t  CRSA_0;                                 /*  CRSA_0          */
    volatile uint32_t  CRDA_0;                                 /*  CRDA_0          */
    volatile uint32_t  CRTB_0;                                 /*  CRTB_0          */
    volatile uint32_t  CHSTAT_0;                               /*  CHSTAT_0        */
    volatile uint32_t  CHCTRL_0;                               /*  CHCTRL_0        */
    volatile uint32_t  CHCFG_0;                                /*  CHCFG_0         */
    volatile uint32_t  CHITVL_0;                               /*  CHITVL_0        */
    volatile uint32_t  CHEXT_0;                                /*  CHEXT_0         */
    volatile uint32_t  NXLA_0;                                 /*  NXLA_0          */
    volatile uint32_t  CRLA_0;                                 /*  CRLA_0          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_1;                                 /*  N0SA_1          */
    volatile uint32_t  N0DA_1;                                 /*  N0DA_1          */
    volatile uint32_t  N0TB_1;                                 /*  N0TB_1          */
    volatile uint32_t  N1SA_1;                                 /*  N1SA_1          */
    volatile uint32_t  N1DA_1;                                 /*  N1DA_1          */
    volatile uint32_t  N1TB_1;                                 /*  N1TB_1          */
    volatile uint32_t  CRSA_1;                                 /*  CRSA_1          */
    volatile uint32_t  CRDA_1;                                 /*  CRDA_1          */
    volatile uint32_t  CRTB_1;                                 /*  CRTB_1          */
    volatile uint32_t  CHSTAT_1;                               /*  CHSTAT_1        */
    volatile uint32_t  CHCTRL_1;                               /*  CHCTRL_1        */
    volatile uint32_t  CHCFG_1;                                /*  CHCFG_1         */
    volatile uint32_t  CHITVL_1;                               /*  CHITVL_1        */
    volatile uint32_t  CHEXT_1;                                /*  CHEXT_1         */
    volatile uint32_t  NXLA_1;                                 /*  NXLA_1          */
    volatile uint32_t  CRLA_1;                                 /*  CRLA_1          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_2;                                 /*  N0SA_2          */
    volatile uint32_t  N0DA_2;                                 /*  N0DA_2          */
    volatile uint32_t  N0TB_2;                                 /*  N0TB_2          */
    volatile uint32_t  N1SA_2;                                 /*  N1SA_2          */
    volatile uint32_t  N1DA_2;                                 /*  N1DA_2          */
    volatile uint32_t  N1TB_2;                                 /*  N1TB_2          */
    volatile uint32_t  CRSA_2;                                 /*  CRSA_2          */
    volatile uint32_t  CRDA_2;                                 /*  CRDA_2          */
    volatile uint32_t  CRTB_2;                                 /*  CRTB_2          */
    volatile uint32_t  CHSTAT_2;                               /*  CHSTAT_2        */
    volatile uint32_t  CHCTRL_2;                               /*  CHCTRL_2        */
    volatile uint32_t  CHCFG_2;                                /*  CHCFG_2         */
    volatile uint32_t  CHITVL_2;                               /*  CHITVL_2        */
    volatile uint32_t  CHEXT_2;                                /*  CHEXT_2         */
    volatile uint32_t  NXLA_2;                                 /*  NXLA_2          */
    volatile uint32_t  CRLA_2;                                 /*  CRLA_2          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_3;                                 /*  N0SA_3          */
    volatile uint32_t  N0DA_3;                                 /*  N0DA_3          */
    volatile uint32_t  N0TB_3;                                 /*  N0TB_3          */
    volatile uint32_t  N1SA_3;                                 /*  N1SA_3          */
    volatile uint32_t  N1DA_3;                                 /*  N1DA_3          */
    volatile uint32_t  N1TB_3;                                 /*  N1TB_3          */
    volatile uint32_t  CRSA_3;                                 /*  CRSA_3          */
    volatile uint32_t  CRDA_3;                                 /*  CRDA_3          */
    volatile uint32_t  CRTB_3;                                 /*  CRTB_3          */
    volatile uint32_t  CHSTAT_3;                               /*  CHSTAT_3        */
    volatile uint32_t  CHCTRL_3;                               /*  CHCTRL_3        */
    volatile uint32_t  CHCFG_3;                                /*  CHCFG_3         */
    volatile uint32_t  CHITVL_3;                               /*  CHITVL_3        */
    volatile uint32_t  CHEXT_3;                                /*  CHEXT_3         */
    volatile uint32_t  NXLA_3;                                 /*  NXLA_3          */
    volatile uint32_t  CRLA_3;                                 /*  CRLA_3          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_4;                                 /*  N0SA_4          */
    volatile uint32_t  N0DA_4;                                 /*  N0DA_4          */
    volatile uint32_t  N0TB_4;                                 /*  N0TB_4          */
    volatile uint32_t  N1SA_4;                                 /*  N1SA_4          */
    volatile uint32_t  N1DA_4;                                 /*  N1DA_4          */
    volatile uint32_t  N1TB_4;                                 /*  N1TB_4          */
    volatile uint32_t  CRSA_4;                                 /*  CRSA_4          */
    volatile uint32_t  CRDA_4;                                 /*  CRDA_4          */
    volatile uint32_t  CRTB_4;                                 /*  CRTB_4          */
    volatile uint32_t  CHSTAT_4;                               /*  CHSTAT_4        */
    volatile uint32_t  CHCTRL_4;                               /*  CHCTRL_4        */
    volatile uint32_t  CHCFG_4;                                /*  CHCFG_4         */
    volatile uint32_t  CHITVL_4;                               /*  CHITVL_4        */
    volatile uint32_t  CHEXT_4;                                /*  CHEXT_4         */
    volatile uint32_t  NXLA_4;                                 /*  NXLA_4          */
    volatile uint32_t  CRLA_4;                                 /*  CRLA_4          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_5;                                 /*  N0SA_5          */
    volatile uint32_t  N0DA_5;                                 /*  N0DA_5          */
    volatile uint32_t  N0TB_5;                                 /*  N0TB_5          */
    volatile uint32_t  N1SA_5;                                 /*  N1SA_5          */
    volatile uint32_t  N1DA_5;                                 /*  N1DA_5          */
    volatile uint32_t  N1TB_5;                                 /*  N1TB_5          */
    volatile uint32_t  CRSA_5;                                 /*  CRSA_5          */
    volatile uint32_t  CRDA_5;                                 /*  CRDA_5          */
    volatile uint32_t  CRTB_5;                                 /*  CRTB_5          */
    volatile uint32_t  CHSTAT_5;                               /*  CHSTAT_5        */
    volatile uint32_t  CHCTRL_5;                               /*  CHCTRL_5        */
    volatile uint32_t  CHCFG_5;                                /*  CHCFG_5         */
    volatile uint32_t  CHITVL_5;                               /*  CHITVL_5        */
    volatile uint32_t  CHEXT_5;                                /*  CHEXT_5         */
    volatile uint32_t  NXLA_5;                                 /*  NXLA_5          */
    volatile uint32_t  CRLA_5;                                 /*  CRLA_5          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_6;                                 /*  N0SA_6          */
    volatile uint32_t  N0DA_6;                                 /*  N0DA_6          */
    volatile uint32_t  N0TB_6;                                 /*  N0TB_6          */
    volatile uint32_t  N1SA_6;                                 /*  N1SA_6          */
    volatile uint32_t  N1DA_6;                                 /*  N1DA_6          */
    volatile uint32_t  N1TB_6;                                 /*  N1TB_6          */
    volatile uint32_t  CRSA_6;                                 /*  CRSA_6          */
    volatile uint32_t  CRDA_6;                                 /*  CRDA_6          */
    volatile uint32_t  CRTB_6;                                 /*  CRTB_6          */
    volatile uint32_t  CHSTAT_6;                               /*  CHSTAT_6        */
    volatile uint32_t  CHCTRL_6;                               /*  CHCTRL_6        */
    volatile uint32_t  CHCFG_6;                                /*  CHCFG_6         */
    volatile uint32_t  CHITVL_6;                               /*  CHITVL_6        */
    volatile uint32_t  CHEXT_6;                                /*  CHEXT_6         */
    volatile uint32_t  NXLA_6;                                 /*  NXLA_6          */
    volatile uint32_t  CRLA_6;                                 /*  CRLA_6          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_7;                                 /*  N0SA_7          */
    volatile uint32_t  N0DA_7;                                 /*  N0DA_7          */
    volatile uint32_t  N0TB_7;                                 /*  N0TB_7          */
    volatile uint32_t  N1SA_7;                                 /*  N1SA_7          */
    volatile uint32_t  N1DA_7;                                 /*  N1DA_7          */
    volatile uint32_t  N1TB_7;                                 /*  N1TB_7          */
    volatile uint32_t  CRSA_7;                                 /*  CRSA_7          */
    volatile uint32_t  CRDA_7;                                 /*  CRDA_7          */
    volatile uint32_t  CRTB_7;                                 /*  CRTB_7          */
    volatile uint32_t  CHSTAT_7;                               /*  CHSTAT_7        */
    volatile uint32_t  CHCTRL_7;                               /*  CHCTRL_7        */
    volatile uint32_t  CHCFG_7;                                /*  CHCFG_7         */
    volatile uint32_t  CHITVL_7;                               /*  CHITVL_7        */
    volatile uint32_t  CHEXT_7;                                /*  CHEXT_7         */
    volatile uint32_t  NXLA_7;                                 /*  NXLA_7          */
    volatile uint32_t  CRLA_7;                                 /*  CRLA_7          */
/* end of struct st_dmac_n */
    volatile uint8_t   dummy187[256];                          /*                  */
/* start of struct st_dmaccommon_n */
    volatile uint32_t  DCTRL_0_7;                              /*  DCTRL_0_7       */
    volatile uint8_t   dummy188[12];                           /*                  */
    volatile uint32_t  DSTAT_EN_0_7;                           /*  DSTAT_EN_0_7    */
    volatile uint32_t  DSTAT_ER_0_7;                           /*  DSTAT_ER_0_7    */
    volatile uint32_t  DSTAT_END_0_7;                          /*  DSTAT_END_0_7   */
    volatile uint32_t  DSTAT_TC_0_7;                           /*  DSTAT_TC_0_7    */
    volatile uint32_t  DSTAT_SUS_0_7;                          /*  DSTAT_SUS_0_7   */
/* end of struct st_dmaccommon_n */
    volatile uint8_t   dummy189[220];                          /*                  */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_8;                                 /*  N0SA_8          */
    volatile uint32_t  N0DA_8;                                 /*  N0DA_8          */
    volatile uint32_t  N0TB_8;                                 /*  N0TB_8          */
    volatile uint32_t  N1SA_8;                                 /*  N1SA_8          */
    volatile uint32_t  N1DA_8;                                 /*  N1DA_8          */
    volatile uint32_t  N1TB_8;                                 /*  N1TB_8          */
    volatile uint32_t  CRSA_8;                                 /*  CRSA_8          */
    volatile uint32_t  CRDA_8;                                 /*  CRDA_8          */
    volatile uint32_t  CRTB_8;                                 /*  CRTB_8          */
    volatile uint32_t  CHSTAT_8;                               /*  CHSTAT_8        */
    volatile uint32_t  CHCTRL_8;                               /*  CHCTRL_8        */
    volatile uint32_t  CHCFG_8;                                /*  CHCFG_8         */
    volatile uint32_t  CHITVL_8;                               /*  CHITVL_8        */
    volatile uint32_t  CHEXT_8;                                /*  CHEXT_8         */
    volatile uint32_t  NXLA_8;                                 /*  NXLA_8          */
    volatile uint32_t  CRLA_8;                                 /*  CRLA_8          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_9;                                 /*  N0SA_9          */
    volatile uint32_t  N0DA_9;                                 /*  N0DA_9          */
    volatile uint32_t  N0TB_9;                                 /*  N0TB_9          */
    volatile uint32_t  N1SA_9;                                 /*  N1SA_9          */
    volatile uint32_t  N1DA_9;                                 /*  N1DA_9          */
    volatile uint32_t  N1TB_9;                                 /*  N1TB_9          */
    volatile uint32_t  CRSA_9;                                 /*  CRSA_9          */
    volatile uint32_t  CRDA_9;                                 /*  CRDA_9          */
    volatile uint32_t  CRTB_9;                                 /*  CRTB_9          */
    volatile uint32_t  CHSTAT_9;                               /*  CHSTAT_9        */
    volatile uint32_t  CHCTRL_9;                               /*  CHCTRL_9        */
    volatile uint32_t  CHCFG_9;                                /*  CHCFG_9         */
    volatile uint32_t  CHITVL_9;                               /*  CHITVL_9        */
    volatile uint32_t  CHEXT_9;                                /*  CHEXT_9         */
    volatile uint32_t  NXLA_9;                                 /*  NXLA_9          */
    volatile uint32_t  CRLA_9;                                 /*  CRLA_9          */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_10;                                /*  N0SA_10         */
    volatile uint32_t  N0DA_10;                                /*  N0DA_10         */
    volatile uint32_t  N0TB_10;                                /*  N0TB_10         */
    volatile uint32_t  N1SA_10;                                /*  N1SA_10         */
    volatile uint32_t  N1DA_10;                                /*  N1DA_10         */
    volatile uint32_t  N1TB_10;                                /*  N1TB_10         */
    volatile uint32_t  CRSA_10;                                /*  CRSA_10         */
    volatile uint32_t  CRDA_10;                                /*  CRDA_10         */
    volatile uint32_t  CRTB_10;                                /*  CRTB_10         */
    volatile uint32_t  CHSTAT_10;                              /*  CHSTAT_10       */
    volatile uint32_t  CHCTRL_10;                              /*  CHCTRL_10       */
    volatile uint32_t  CHCFG_10;                               /*  CHCFG_10        */
    volatile uint32_t  CHITVL_10;                              /*  CHITVL_10       */
    volatile uint32_t  CHEXT_10;                               /*  CHEXT_10        */
    volatile uint32_t  NXLA_10;                                /*  NXLA_10         */
    volatile uint32_t  CRLA_10;                                /*  CRLA_10         */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_11;                                /*  N0SA_11         */
    volatile uint32_t  N0DA_11;                                /*  N0DA_11         */
    volatile uint32_t  N0TB_11;                                /*  N0TB_11         */
    volatile uint32_t  N1SA_11;                                /*  N1SA_11         */
    volatile uint32_t  N1DA_11;                                /*  N1DA_11         */
    volatile uint32_t  N1TB_11;                                /*  N1TB_11         */
    volatile uint32_t  CRSA_11;                                /*  CRSA_11         */
    volatile uint32_t  CRDA_11;                                /*  CRDA_11         */
    volatile uint32_t  CRTB_11;                                /*  CRTB_11         */
    volatile uint32_t  CHSTAT_11;                              /*  CHSTAT_11       */
    volatile uint32_t  CHCTRL_11;                              /*  CHCTRL_11       */
    volatile uint32_t  CHCFG_11;                               /*  CHCFG_11        */
    volatile uint32_t  CHITVL_11;                              /*  CHITVL_11       */
    volatile uint32_t  CHEXT_11;                               /*  CHEXT_11        */
    volatile uint32_t  NXLA_11;                                /*  NXLA_11         */
    volatile uint32_t  CRLA_11;                                /*  CRLA_11         */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_12;                                /*  N0SA_12         */
    volatile uint32_t  N0DA_12;                                /*  N0DA_12         */
    volatile uint32_t  N0TB_12;                                /*  N0TB_12         */
    volatile uint32_t  N1SA_12;                                /*  N1SA_12         */
    volatile uint32_t  N1DA_12;                                /*  N1DA_12         */
    volatile uint32_t  N1TB_12;                                /*  N1TB_12         */
    volatile uint32_t  CRSA_12;                                /*  CRSA_12         */
    volatile uint32_t  CRDA_12;                                /*  CRDA_12         */
    volatile uint32_t  CRTB_12;                                /*  CRTB_12         */
    volatile uint32_t  CHSTAT_12;                              /*  CHSTAT_12       */
    volatile uint32_t  CHCTRL_12;                              /*  CHCTRL_12       */
    volatile uint32_t  CHCFG_12;                               /*  CHCFG_12        */
    volatile uint32_t  CHITVL_12;                              /*  CHITVL_12       */
    volatile uint32_t  CHEXT_12;                               /*  CHEXT_12        */
    volatile uint32_t  NXLA_12;                                /*  NXLA_12         */
    volatile uint32_t  CRLA_12;                                /*  CRLA_12         */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_13;                                /*  N0SA_13         */
    volatile uint32_t  N0DA_13;                                /*  N0DA_13         */
    volatile uint32_t  N0TB_13;                                /*  N0TB_13         */
    volatile uint32_t  N1SA_13;                                /*  N1SA_13         */
    volatile uint32_t  N1DA_13;                                /*  N1DA_13         */
    volatile uint32_t  N1TB_13;                                /*  N1TB_13         */
    volatile uint32_t  CRSA_13;                                /*  CRSA_13         */
    volatile uint32_t  CRDA_13;                                /*  CRDA_13         */
    volatile uint32_t  CRTB_13;                                /*  CRTB_13         */
    volatile uint32_t  CHSTAT_13;                              /*  CHSTAT_13       */
    volatile uint32_t  CHCTRL_13;                              /*  CHCTRL_13       */
    volatile uint32_t  CHCFG_13;                               /*  CHCFG_13        */
    volatile uint32_t  CHITVL_13;                              /*  CHITVL_13       */
    volatile uint32_t  CHEXT_13;                               /*  CHEXT_13        */
    volatile uint32_t  NXLA_13;                                /*  NXLA_13         */
    volatile uint32_t  CRLA_13;                                /*  CRLA_13         */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_14;                                /*  N0SA_14         */
    volatile uint32_t  N0DA_14;                                /*  N0DA_14         */
    volatile uint32_t  N0TB_14;                                /*  N0TB_14         */
    volatile uint32_t  N1SA_14;                                /*  N1SA_14         */
    volatile uint32_t  N1DA_14;                                /*  N1DA_14         */
    volatile uint32_t  N1TB_14;                                /*  N1TB_14         */
    volatile uint32_t  CRSA_14;                                /*  CRSA_14         */
    volatile uint32_t  CRDA_14;                                /*  CRDA_14         */
    volatile uint32_t  CRTB_14;                                /*  CRTB_14         */
    volatile uint32_t  CHSTAT_14;                              /*  CHSTAT_14       */
    volatile uint32_t  CHCTRL_14;                              /*  CHCTRL_14       */
    volatile uint32_t  CHCFG_14;                               /*  CHCFG_14        */
    volatile uint32_t  CHITVL_14;                              /*  CHITVL_14       */
    volatile uint32_t  CHEXT_14;                               /*  CHEXT_14        */
    volatile uint32_t  NXLA_14;                                /*  NXLA_14         */
    volatile uint32_t  CRLA_14;                                /*  CRLA_14         */
/* end of struct st_dmac_n */
/* start of struct st_dmac_n */
    volatile uint32_t  N0SA_15;                                /*  N0SA_15         */
    volatile uint32_t  N0DA_15;                                /*  N0DA_15         */
    volatile uint32_t  N0TB_15;                                /*  N0TB_15         */
    volatile uint32_t  N1SA_15;                                /*  N1SA_15         */
    volatile uint32_t  N1DA_15;                                /*  N1DA_15         */
    volatile uint32_t  N1TB_15;                                /*  N1TB_15         */
    volatile uint32_t  CRSA_15;                                /*  CRSA_15         */
    volatile uint32_t  CRDA_15;                                /*  CRDA_15         */
    volatile uint32_t  CRTB_15;                                /*  CRTB_15         */
    volatile uint32_t  CHSTAT_15;                              /*  CHSTAT_15       */
    volatile uint32_t  CHCTRL_15;                              /*  CHCTRL_15       */
    volatile uint32_t  CHCFG_15;                               /*  CHCFG_15        */
    volatile uint32_t  CHITVL_15;                              /*  CHITVL_15       */
    volatile uint32_t  CHEXT_15;                               /*  CHEXT_15        */
    volatile uint32_t  NXLA_15;                                /*  NXLA_15         */
    volatile uint32_t  CRLA_15;                                /*  CRLA_15         */
/* end of struct st_dmac_n */
    volatile uint8_t   dummy190[256];                          /*                  */
/* start of struct st_dmaccommon_n */
    volatile uint32_t  DCTRL_8_15;                             /*  DCTRL_8_15      */
    volatile uint8_t   dummy191[12];                           /*                  */
    volatile uint32_t  DSTAT_EN_8_15;                          /*  DSTAT_EN_8_15   */
    volatile uint32_t  DSTAT_ER_8_15;                          /*  DSTAT_ER_8_15   */
    volatile uint32_t  DSTAT_END_8_15;                         /*  DSTAT_END_8_15  */
    volatile uint32_t  DSTAT_TC_8_15;                          /*  DSTAT_TC_8_15   */
    volatile uint32_t  DSTAT_SUS_8_15;                         /*  DSTAT_SUS_8_15  */
/* end of struct st_dmaccommon_n */
    volatile uint8_t   dummy192[350095580];                    /*                  */
    volatile uint32_t  DMARS[8]; // Modified by Rohan
};


struct st_dmaccommon_n
{
    volatile uint32_t  DCTRL_0_7;                              /*  DCTRL_0_7       */
    volatile uint8_t   dummy1[12];                             /*                  */
    volatile uint32_t  DSTAT_EN_0_7;                           /*  DSTAT_EN_0_7    */
    volatile uint32_t  DSTAT_ER_0_7;                           /*  DSTAT_ER_0_7    */
    volatile uint32_t  DSTAT_END_0_7;                          /*  DSTAT_END_0_7   */
    volatile uint32_t  DSTAT_TC_0_7;                           /*  DSTAT_TC_0_7    */
    volatile uint32_t  DSTAT_SUS_0_7;                          /*  DSTAT_SUS_0_7   */
};


struct st_dmac_n
{
    volatile uint32_t  N0SA_n;                                 /*  N0SA_n          */
    volatile uint32_t  N0DA_n;                                 /*  N0DA_n          */
    volatile uint32_t  N0TB_n;                                 /*  N0TB_n          */
    volatile uint32_t  N1SA_n;                                 /*  N1SA_n          */
    volatile uint32_t  N1DA_n;                                 /*  N1DA_n          */
    volatile uint32_t  N1TB_n;                                 /*  N1TB_n          */
    volatile uint32_t  CRSA_n;                                 /*  CRSA_n          */
    volatile uint32_t  CRDA_n;                                 /*  CRDA_n          */
    volatile uint32_t  CRTB_n;                                 /*  CRTB_n          */
    volatile uint32_t  CHSTAT_n;                               /*  CHSTAT_n        */
    volatile uint32_t  CHCTRL_n;                               /*  CHCTRL_n        */
    volatile uint32_t  CHCFG_n;                                /*  CHCFG_n         */
    volatile uint32_t  CHITVL_n;                               /*  CHITVL_n        */
    volatile uint32_t  CHEXT_n;                                /*  CHEXT_n         */
    volatile uint32_t  NXLA_n;                                 /*  NXLA_n          */
    volatile uint32_t  CRLA_n;                                 /*  CRLA_n          */
};

struct st_dmac_n_non_volatile // By Rohan
{
    uint32_t  N0SA_n;                                 /*  N0SA_n          */
    uint32_t  N0DA_n;                                 /*  N0DA_n          */
    uint32_t  N0TB_n;                                 /*  N0TB_n          */
    uint32_t  N1SA_n;                                 /*  N1SA_n          */
    uint32_t  N1DA_n;                                 /*  N1DA_n          */
    uint32_t  N1TB_n;                                 /*  N1TB_n          */
    uint32_t  CRSA_n;                                 /*  CRSA_n          */
    uint32_t  CRDA_n;                                 /*  CRDA_n          */
    uint32_t  CRTB_n;                                 /*  CRTB_n          */
    uint32_t  CHSTAT_n;                               /*  CHSTAT_n        */
    uint32_t  CHCTRL_n;                               /*  CHCTRL_n        */
    uint32_t  CHCFG_n;                                /*  CHCFG_n         */
    uint32_t  CHITVL_n;                               /*  CHITVL_n        */
    uint32_t  CHEXT_n;                                /*  CHEXT_n         */
    uint32_t  NXLA_n;                                 /*  NXLA_n          */
    uint32_t  CRLA_n;                                 /*  CRLA_n          */
};


#define DMAC    (*(struct st_dmac    *)0xE8200000uL) /* DMAC */

// These all by Rohan
#define DMACn(n)			(*(struct st_dmac_n *)(0xE8200000uL + (n) * 64 + (((n) >= 8) ? 0x200 : 0)))
#define DMACnNonVolatile(n)	(*(struct st_dmac_n_non_volatile *)(0xE8200000uL + (n) * 64 + (((n) >= 8) ? 0x200 : 0)))
#define DCTRLn(n)			(*(((n) < 8) ? &DMAC.DCTRL_0_7 : &DMAC.DCTRL_8_15))
#define DMARSnAddress(n)	(&DMAC.DMARS[0] + ((n) >> 1))


#endif
