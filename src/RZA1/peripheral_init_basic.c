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
 * File Name     : peripheral_init_basic.c
 * Device(s)     : RZ/A1H (R7S721001)
 * Tool-Chain    : GNUARM-NONEv14.02-EABI
 * H/W Platform  : RSK+RZA1H CPU Board
 * Description   : Sample Program - Initialise peripheral function sample
 *******************************************************************************/
/*******************************************************************************
 * History       : DD.MM.YYYY Version Description
 *               : 21.10.2014 1.00
 *******************************************************************************/

/*******************************************************************************
Includes   <System Includes> , "Project Includes"
*******************************************************************************/
/* Default  type definition header */
#include "RZA1/system/r_typedefs.h"
#include "RZA1/system/rza_io_regrw.h"
/* Common Driver header */
/* I/O Register root header */
#include "RZA1/system/iodefine.h"

/*******************************************************************************
Private global variables and functions
*******************************************************************************/
static void CPG_Init(void);

/*******************************************************************************
 * Function Name: Peripheral_Basic_Init
 * Description  : Calls the CPG_Init and the CS0_PORT_Init of sample functions
 *              : and the BSC_Init of API function. Sets the CPG, enables
 *              : writing to the data-retention RAM area, and executes the
 *              : port settings for the CS0 and the CS1 spaces and the BSC
 *              : setting.
 * Arguments    : none
 * Return Value : none
 *******************************************************************************/
void Peripheral_Basic_Init(void)
{
    /* ==== CPG setting ====*/
    CPG_Init();
}

/*******************************************************************************
 * Function Name: CPG_Init
 * Description  : Executes initial setting for the CPG.
 *              : In the sample code, the internal clock ratio is set to be
 *              : I:G:B:P1:P0 = 30:20:10:5:5/2 in the state that the
 *              : clock mode is 0. The frequency is set to be as below when the
 *              : input clock is 13.33MHz.
 *              : CPU clock (I clock)              : 400MHz
 *              : Image processing clock (G clock) : 266.67MHz
 *              : Internal bus clock (B clock)     : 133.33MHz
 *              : Peripheral clock1 (P1 clock)     : 66.67MHz
 *              : Peripheral clock0 (P0 clock)     : 33.33MHz
 *              : Sets the data-retention RAM area (H'2000 0000 to H'2001 FFFF)
 *              : to be enabled for writing.
 * Arguments    : none
 * Return Value : none
 *******************************************************************************/
static void CPG_Init(void)
{
    volatile uint32_t dummy = 0;

    UNUSED_VARIABLE(dummy);

    /* standby_mode_en bit of Power Control Register setting */
    (*(volatile uint32_t*)(0x3fffff80)) = 0x00000001;
    dummy                               = (*(volatile uint32_t*)(0x3fffff80));

    /* ==== CPG Settings ==== */

    /* PLL(x30), I:G:B:P1:P0 = 30:20:10:5:5/2 */
    CPG.FRQCR = 0x1035u;

    /* CKIO:Output at time usually output     *
     * when bus right is opened output at     *
     * standby "L"                            *
     * Clockin  = 13.33MHz, CKIO = 66.67MHz,  *
     * I  Clock = 400.00MHz,                  *
     * G  Clock = 266.67MHz,                  *
     * B  Clock = 133.33MHz,                  *
     * P1 Clock =  66.67MHz,                  *
     * P0 Clock =  33.33MHz                   */
    CPG.FRQCR2 = 0x0001u;

    /* ----  Writing to On-Chip Data-Retention RAM is enabled. ---- */
    CPG.SYSCR3 = 0x0Fu;
    dummy      = CPG.SYSCR3;
}
