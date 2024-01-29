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
 * Copyright (C) 2012 - 2014 Renesas Electronics Corporation. All rights reserved.
 *******************************************************************************/
/*******************************************************************************
 * File Name   : intc_handler.c
 * $Rev: 819 $
 * $Date:: 2014-04-18 17:03:54 +0900#$
 * Description : INTC Driver - Handler process
 *******************************************************************************/

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "RZA1/intc/intc_handler.h"
#include "RZA1/compiler/asm/inc/asm.h"
#include "RZA1/intc/devdrv_intc.h" /* INTC Driver Header */
#include "RZA1/system/iodefine.h"
#include "RZA1/system/r_typedefs.h"
#include "deluge/drivers/uart/uart.h"

#ifdef __ICCARM__
#include <intrinsics.h>
#endif
#ifdef __GNUC__
// #include "irq.h"
#endif

#ifdef __CC_ARM
#pragma arm section code   = "CODE_HANDLER"
#pragma arm section rodata = "CONST_HANDLER"
#pragma arm section rwdata = "DATA_HANDLER"
#pragma arm section zidata = "BSS_HANDLER"
#endif

/******************************************************************************
Typedef definitions
******************************************************************************/

/******************************************************************************
Macro definitions
******************************************************************************/

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/

/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/

/******************************************************************************
 * Function Name: INTC_Handler_Interrupt
 * Description  : This function is the INTC interrupt handler processing called
 *              : by the irq_handler. Executes the handler processing which
 *              : corresponds to the INTC interrupt source ID specified by the
 *              : icciar by calling the Userdef_INTC_HandlerExe function. The IRQ
 *              : multiple interrupts are enabled. The processing for unsupported
 *              : interrupt ID is executed by calling Userdef_INTC_UndefId function.
 *              : In the interrupt handler processing, when the int_sense shows
 *              : "INTC_LEVEL_SENSITIVE", clear the interrupt source because it
 *              : means a level sense interrupt.
 * Arguments    : uint32_t icciar : Interrupt ID (value of ICCIAR register)
 * Return Value : none
 ******************************************************************************/
void INTC_Handler_Interrupt(uint32_t icciar)
{
    /* Stacks are restored by ASM with the top level to correspond to multiple interrupts */
    uint32_t mask;
    uint32_t int_sense;
    uint16_t int_id;
    uint32_t volatile* addr;

    int_id = (uint16_t)(icciar & 0x000003FFuL); /* Obtain interrupt ID */

    /*
     * If an interrupt ID value read from the interrupt acknowledge register
     * (ICCIAR) is 1022 or 1023, return from interrupt processing after
     * writing the same value as the setting value to the interrupt priority
     * register 0 (ICDIPR0).
     */
    if (int_id >= 0x3fe) /* In case of unsupported interrupt ID */
    {
        addr = (volatile uint32_t*)&INTC.ICDIPR0;

        *addr = icciar;
        return;
    }

    if (int_id >= INTC_ID_TOTAL) /* In case of unsupported interrupt ID */
    {
        // Insane thing that keeps happening - we get here somehow, with int_id 1023.
        // FREEZE_WITH_ERROR("i029");
        uartPrintln("i029 ----------------------------------------------------!!");
        return; // Just keep running - it seems to work at least most of the time?
        // Userdef_INTC_UndefId(int_id); // Previously, it'd just go in here and freeze.
    }

    /* ==== Interrupt handler call ==== */
    __enable_irq(); /* IRQ interrupt enabled */

    /* ICDICFRn has 16 sources in the 32 bits                                  */
    /* The n can be calculated by int_id / 16                                  */
    /* The upper 1 bit out of 2 bits for the bit field width is the target bit */
    /* The target bit can be calculated by ((int_id % 16) * 2) + 1             */
    addr = (volatile uint32_t*)&INTC.ICDICFR0;
    mask = (uint32_t)(1 << (((int_id % 16) * 2) + 1));
    if (0 == (*(addr + (int_id / 16)) & mask)) /* In the case of level sense */
    {
        int_sense = INTC_LEVEL_SENSITIVE;
    }
    else /* In the case of edge trigger */
    {
        int_sense = INTC_EDGE_TRIGGER;
    }

    Userdef_INTC_HandlerExe(int_id, int_sense); /* Call interrupt handler */

    __disable_irq(); /* IRQ interrupt disabled */
}

/*******************************************************************************
 * Function Name: FiqHandler_Interrupt
 * Description  : This function is the INTC interrupt handler processing called by
 *              : the fiq_handler.
 * Arguments    : none
 * Return Value : none
 *******************************************************************************/
void fiq_handler_interrupt(void)
{
    Userdef_FIQ_HandlerExe();
}

/* END of File */
