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
 *******************************************************************************/
/*******************************************************************************
 * File Name     : bsc_userdef.h
 * Device(s)     : RZ/A1H (R7S721001)
 * Tool-Chain    : GNUARM-NONEv14.02-EABI
 * H/W Platform  : RSK+RZA1H CPU Board
 * Description   : Sample Program - Bus State Controller User file
 *******************************************************************************/
/*******************************************************************************
 * History       : DD.MM.YYYY Version Description
 *               : 21.10.2014 1.00
 *******************************************************************************/

/* Multiple inclusion prevention macro */
#ifndef BSC_USERDEF_H
#define BSC_USERDEF_H

/******************************************************************************
Macro definitions
******************************************************************************/
/* CS0 */
#define BSC_AREA_CS0 (0x01)
/* CS1 */
#define BSC_AREA_CS1 (0x02)
/* CS2 */
#define BSC_AREA_CS2 (0x04)
/* CS3 */
#define BSC_AREA_CS3 (0x08)
/* CS4 */
#define BSC_AREA_CS4 (0x10)
/* CS5 */
#define BSC_AREA_CS5 (0x20)

/******************************************************************************
Functions Prototypes
******************************************************************************/
// void BSC_Init (uint8_t area);

void userdef_bsc_cs0_init(void);
void userdef_bsc_cs1_init(void);
void userdef_bsc_cs2_init(uint8_t);
void userdef_bsc_cs3_init(void);
void userdef_bsc_cs4_init(void);
void userdef_bsc_cs5_init(void);

/* BSC_USERDEF_H */
#endif

/* End of File */
