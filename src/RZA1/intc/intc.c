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
 * File Name   : intc.c
 * $Rev: 809 $
 * $Date:: 2014-04-09 15:06:36 +0900#$
 * Description : INTC driver
 *******************************************************************************/

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "RZA1/intc/devdrv_intc.h" /* INTC Driver Header */
#include "RZA1/system/dev_drv.h"   /* Device Driver common header */
#include "RZA1/system/iodefine.h"
#include "RZA1/system/r_typedefs.h"

/******************************************************************************
Typedef definitions
******************************************************************************/

/******************************************************************************
Macro definitions
******************************************************************************/
/* ==== Total number of registers ==== */
#define INTC_ICDISR_REG_TOTAL  (((uint16_t)INTC_ID_TOTAL / 32) + 1) /* ICDISR  */
#define INTC_ICDICFR_REG_TOTAL (((uint16_t)INTC_ID_TOTAL / 16) + 1) /* ICDICFR */
#define INTC_ICDIPR_REG_TOTAL  (((uint16_t)INTC_ID_TOTAL / 4) + 1)  /* ICDIPR  */
#define INTC_ICDIPTR_REG_TOTAL (((uint16_t)INTC_ID_TOTAL / 4) + 1)  /* ICDIPTR */
#define INTC_ICDISER_REG_TOTAL (((uint16_t)INTC_ID_TOTAL / 32) + 1) /* ICDISER */
#define INTC_ICDICER_REG_TOTAL (((uint16_t)INTC_ID_TOTAL / 32) + 1) /* ICDICER */

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/

/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/

/******************************************************************************
Private global variables and functions
******************************************************************************/
/* ==== Global variable ==== */
static uint32_t intc_icdicfrn_table[] = /* Initial value table of Interrupt Configuration Registers */
    {
        /*           Interrupt ID */
        0xAAAAAAAA, /* ICDICFR0  :  15 to   0 */
        0x00000055, /* ICDICFR1  :  19 to  16 */
        0xFFFD5555, /* ICDICFR2  :  47 to  32 */
        0x555FFFFF, /* ICDICFR3  :  63 to  48 */
        0x55555555, /* ICDICFR4  :  79 to  64 */
        0x55555555, /* ICDICFR5  :  95 to  80 */
        0x55555555, /* ICDICFR6  : 111 to  96 */
        0x55555555, /* ICDICFR7  : 127 to 112 */
        0x5555F555, /* ICDICFR8  : 143 to 128 */
        0x55555555, /* ICDICFR9  : 159 to 144 */
        0x55555555, /* ICDICFR10 : 175 to 160 */
        0xF5555555, /* ICDICFR11 : 191 to 176 */
        0xF555F555, /* ICDICFR12 : 207 to 192 */
        0x5555F555, /* ICDICFR13 : 223 to 208 */
        0x55555555, /* ICDICFR14 : 239 to 224 */
        0x55555555, /* ICDICFR15 : 255 to 240 */
        0x55555555, /* ICDICFR16 : 271 to 256 */
        0xFD555555, /* ICDICFR17 : 287 to 272 */
        0x55555557, /* ICDICFR18 : 303 to 288 */
        0x55555555, /* ICDICFR19 : 319 to 304 */
        0xFFD55555, /* ICDICFR20 : 335 to 320 */
        0x5F55557F, /* ICDICFR21 : 351 to 336 */
        0xFD55555F, /* ICDICFR22 : 367 to 352 */
        0x55555557, /* ICDICFR23 : 383 to 368 */
        0x55555555, /* ICDICFR24 : 399 to 384 */
        0x55555555, /* ICDICFR25 : 415 to 400 */
        0x55555555, /* ICDICFR26 : 431 to 416 */
        0x55555555, /* ICDICFR27 : 447 to 432 */
        0x55555555, /* ICDICFR28 : 463 to 448 */
        0x55555555, /* ICDICFR29 : 479 to 464 */
        0x55555555, /* ICDICFR30 : 495 to 480 */
        0x55555555, /* ICDICFR31 : 511 to 496 */
        0x55555555, /* ICDICFR32 : 527 to 512 */
        0x55555555, /* ICDICFR33 : 543 to 528 */
        0x55555555, /* ICDICFR34 : 559 to 544 */
        0x55555555, /* ICDICFR35 : 575 to 560 */
        0x00155555  /* ICDICFR36 : 586 to 576 */
};

/******************************************************************************
 * Function Name: R_INTC_RegistIntFunc
 * Description  : Registers the function specified by the func to the element
 *              : specified by the int_id in the INTC interrupt handler function
 *              : table.
 * Arguments    : uint16_t int_id         : Interrupt ID
 *              : void (* func)(uint32_t) : Function to be registered to INTC
 *              :                         : interrupt hander table
 * Return Value : DEVDRV_SUCCESS          : Success of registration of INTC
 *              :                         : interrupt handler function
 *              : DEVDRV_ERROR            : Failure of registration of INTC
 *              :                         : interrupt handler function
 ******************************************************************************/
int32_t R_INTC_RegistIntFunc(uint16_t int_id, void (*func)(uint32_t int_sense))
{
    /* ==== Argument check ==== */
    if (int_id >= INTC_ID_TOTAL)
    {
        return DEVDRV_ERROR; /* Argument error */
    }

    Userdef_INTC_RegistIntFunc(int_id, func); /* Register specified interrupt functions */

    return DEVDRV_SUCCESS;
}

/******************************************************************************
 * Function Name: R_INTC_Init
 * Description  : Executes initial setting for the INTC.
 *              : The interrupt mask level is set to 31 to receive interrupts
 *              : with the interrupt priority level 0 to 30.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void R_INTC_Init(void)
{
    uint16_t offset;
    volatile uint32_t* addr;

    /* ==== Initial setting 1 to receive GIC interrupt request ==== */
    /* Interrupt Security Registers setting */
    addr = (volatile uint32_t*)&INTC.ICDISR0;
    for (offset = 0; offset < INTC_ICDISR_REG_TOTAL; offset++)
    {
        *(addr + offset) = 0x00000000uL; /* Set all interrupts to be secured */
    }

    /* Interrupt Configuration Registers setting */
    addr = (volatile uint32_t*)&INTC.ICDICFR0;
    for (offset = 0; offset < INTC_ICDICFR_REG_TOTAL; offset++)
    {
        *(addr + offset) = intc_icdicfrn_table[offset];
    }

    /* Interrupt Priority Registers setting */
    addr = (volatile uint32_t*)&INTC.ICDIPR0;
    for (offset = 0; offset < INTC_ICDIPR_REG_TOTAL; offset++)
    {
        *(addr + offset) = 0xF8F8F8F8uL; /* Set the priority for all interrupts to 31 */
    }

    /* Interrupt Processor Targets Registers setting */
    /* Initialize ICDIPTR8 to ICDIPTRn                     */
    /* (n = The number of interrupt sources / 4)           */
    /*   - ICDIPTR0 to ICDIPTR4 are dedicated for main CPU */
    /*   - ICDIPTR5 is dedicated for sub CPU               */
    /*   - ICDIPTR6 to 7 are reserved                      */
    addr = (volatile uint32_t*)&INTC.ICDIPTR0;
    for (offset = 8; offset < INTC_ICDIPTR_REG_TOTAL; offset++) /* Do not initialize ICDIPTR0 to ICDIPTR7 */
    {
        *(addr + offset) = 0x01010101uL; /* Set the target for all interrupts to main CPU */
    }

    /* Interrupt Clear-Enable Registers setting */
    addr = (volatile uint32_t*)&INTC.ICDICER0;
    for (offset = 0; offset < INTC_ICDICER_REG_TOTAL; offset++)
    {
        *(addr + offset) = 0xFFFFFFFFuL; /* Set all interrupts to be disabled */
    }

    /* ==== Initial setting for CPU interface ==== */
    /* Interrupt Priority Mask Register setting */
    R_INTC_SetMaskLevel(31); /* Enable priorities for all interrupts */

    /* Binary Point Register setting */
    INTC.ICCBPR = 0x00000002uL; /* Group priority field [7:3], Subpriority field [2:0](Do not use) */

    /* CPU Interface Control Register setting */
    INTC.ICCICR = 0x00000003uL;

    /* ==== Initial setting 2 to receive GIC interrupt request ==== */
    /* Distributor Control Register setting */
    INTC.ICDDCR = 0x00000001uL;
}

/******************************************************************************
 * Function Name: R_INTC_Enable
 * Description  : Enables interrupt of the ID specified by the int_id.
 * Arguments    : uint16_t int_id : Interrupt ID
 * Return Value : DEVDRV_SUCCESS  : Success to enable INTC interrupt
 *              : DEVDRV_ERROR    : Failure to enable INTC interrupt
 ******************************************************************************/
int32_t R_INTC_Enable(uint16_t int_id)
{
    uint32_t mask;
    volatile uint32_t* addr;

    /* ==== Argument check ==== */
    if (int_id >= INTC_ID_TOTAL)
    {
        // return DEVDRV_ERROR;        /* Argument error */
    }

    /* ICDISERn has 32 sources in the 32 bits               */
    /* The n can be calculated by int_id / 32               */
    /* The bit field width is 1 bit                         */
    /* The target bit can be calclated by (int_id % 32) * 1 */
    /* ICDICERn does not effect on writing "0"              */
    /* The bits except for the target write "0"             */
    addr = (volatile uint32_t*)&INTC.ICDISER0;
    mask = 1 << (int_id & 31); /* Create mask data */

    *(addr + (int_id >> 5)) = mask; /* Write ICDISERn   */

    return DEVDRV_SUCCESS;
}

/******************************************************************************
 * Function Name: R_INTC_Disable
 * Description  : Disables interrupt of the ID specified by the int_id.
 * Arguments    : uint16_t int_id : Interrupt ID
 * Return Value : DEVDRV_SUCCESS  : Success to disable INTC interrupt
 *              : DEVDRV_ERROR    : Failure to disable INTC interrupt
 ******************************************************************************/
int32_t R_INTC_Disable(uint16_t int_id)
{
    uint32_t mask;
    volatile uint32_t* addr;

    /* ==== Argument check ==== */
    if (int_id >= INTC_ID_TOTAL)
    {
        // return DEVDRV_ERROR;        /* Argument error */
    }

    /* ICDICERn has 32 sources in the 32 bits               */
    /* The n can be calculated by int_id / 32               */
    /* The bit field width is 1 bit                         */
    /* The targe bit can be calculated by (int_id % 32) * 1 */
    /* ICDICERn does no effect on writing "0"               */
    /* Other bits except for the target write "0"           */
    addr = (volatile uint32_t*)&INTC.ICDICER0;
    mask = 1 << (int_id & 31); /* Create mask data */

    *(addr + (int_id >> 5)) = mask; /* Write ICDICERn   */

    return DEVDRV_SUCCESS;
}

uint8_t R_INTC_Enabled(uint16_t int_id)
{
    if (int_id >= INTC_ID_TOTAL)
    {
        return false;
    }

    volatile uint32_t* addr = (volatile uint32_t*)&INTC.ICDICER0;
    uint32_t mask           = 1 << (int_id & 31); /* Create mask data */

    return ((*(addr + (int_id >> 5)) & mask) == mask) ? 1 : 0;
}

/******************************************************************************
 * Function Name: R_INTC_SetPriority
 * Description  : Sets the priority level of the ID specified by the int_id to
 *              : the priority level specified by the priority.
 * Arguments    : uint16_t int_id   : Interrupt ID
 *              : uint8_t  priority : Interrupt priority level (0 to 31)
 * Return Value : DEVDRV_SUCCESS    : Success of INTC interrupt priority level setting
 *              : DEVDRV_ERROR      : Failure of INTC interrupt priority level setting
 ******************************************************************************/
int32_t R_INTC_SetPriority(uint16_t int_id, uint8_t priority)
{
    uint32_t icdipr;
    uint32_t mask;
    volatile uint32_t* addr;

    /* ==== Argument check ==== */
    if ((int_id >= INTC_ID_TOTAL) || priority >= 32)
    {
        return DEVDRV_ERROR; /* Argument error */
    }

    priority = priority << 3; /* Priority[7:3] of ICDIPRn is valid bit */

    /* ICDIPRn has 4 sources in the 32 bits                 */
    /* The n can be calculated by int_id / 4                */
    /* The bit field width is 8 bits                        */
    /* The target bit can be calculated by (int_id % 4) * 8 */
    addr = (volatile uint32_t*)&INTC.ICDIPR0;

    icdipr = *(addr + (int_id / 4)); /* Read ICDIPRn */

    mask = (uint32_t)0x000000FFuL;     /* ---- Mask ----      */
    mask = mask << ((int_id % 4) * 8); /* Shift to target bit */
    icdipr &= ~mask;                   /* Clear priority      */
    mask = (uint32_t)priority;         /* ---- Priority ----  */
    mask = mask << ((int_id % 4) * 8); /* Shift to target bit */
    icdipr |= mask;                    /* Set priority        */

    *(addr + (int_id / 4)) = icdipr; /* Write ICDIPRn */

    return DEVDRV_SUCCESS;
}

/******************************************************************************
 * Function Name: R_INTC_SetMaskLevel
 * Description  : Sets the interrupt mask level specified by the mask_level.
 * Arguments    : uint8_t mask_level : Interrupt mask level (0 to 31)
 * Return Value : DEVDRV_SUCCESS     : Success of INTC interrupt mask level setting
 *              : DEVDRV_ERROR       : Failure of INTC interrupt mask level setting
 ******************************************************************************/
int32_t R_INTC_SetMaskLevel(uint8_t mask_level)
{
    volatile uint8_t dummy_buf_8b;

    /* ==== Argument check ==== */
    if (mask_level >= 32)
    {
        return DEVDRV_ERROR; /* Argument error */
    }

    mask_level   = mask_level << 3; /* ICCPMR[7:3] is valid bit */
    INTC.ICCPMR  = mask_level;      /* Write ICCPMR             */
    dummy_buf_8b = INTC.ICCPMR;

    return DEVDRV_SUCCESS;
}

/******************************************************************************
 * Function Name: R_INTC_GetMaskLevel
 * Description  : Obtains the setting value of the interrupt mask level, and
 *              : returns the obtained value to the mask_level.
 * Arguments    : uint8_t * mask_level : Interrupt mask level (0 to 31)
 * Return Value : none
 ******************************************************************************/
void R_INTC_GetMaskLevel(uint8_t* mask_level)
{
    *mask_level = INTC.ICCPMR;      /* Read ICCPMR              */
    *mask_level = *mask_level >> 3; /* ICCPMR[7:3] is valid bit */
}

/******************************************************************************
 * Function Name: R_INTC_GetPendingStatus
 * Description  : Obtains the interrupt state of the interrupt specified by
 *              : int_id, and returns the obtained value to the *icdicpr.
 * Arguments    : uint16_t int_id    : Interrupt ID
 *              : uint32_t * icdicpr : Interrupt state of the interrupt
 *              :                    : specified by int_id
 *              :                    :   1 : Pending or active and pending
 *              :                    :   0 : Not pending
 * Return Value : DEVDRV_SUCCESS : Success to obtaine interrupt pending status
 *              : DEVDRV_ERROR   : Failure to obtaine interrupt pending status
 ******************************************************************************/
int32_t R_INTC_GetPendingStatus(uint16_t int_id, uint32_t* icdicpr)
{
    volatile uint32_t* addr;

    /* ==== Argument check ==== */
    if (int_id >= INTC_ID_TOTAL)
    {
        return DEVDRV_ERROR; /* Argument error */
    }

    /* ICDICPRn has 32 sources in the 32 bits               */
    /* The n can be calculated by int_id / 32               */
    /* The bit field width is 1 bit                         */
    /* The targe bit can be calculated by (int_id % 32) * 1 */
    addr     = (volatile uint32_t*)&INTC.ICDICPR0;
    *icdicpr = *(addr + (int_id / 32)); /* Read ICDICPRn */
    *icdicpr = (*icdicpr >> (int_id % 32)) & 0x00000001;

    return DEVDRV_SUCCESS;
}

/******************************************************************************
 * Function Name: R_INTC_SetConfiguration
 * Description  : Sets the interrupt detection mode of the ID specified by the
 *              : int_id to the detection mode specified by the int_sense.
 * Arguments    : uint16_t int_id    : Interrupt ID (INTC_ID_TINT0 to INTC_ID_TINT170)
 *              : uint32_t int_sense : Interrupt detection
 *              :                    :   INTC_LEVEL_SENSITIVE : Level sense
 *              :                    :   INTC_EDGE_TRIGGER    : Edge trigger
 * Return Value : DEVDRV_SUCCESS : Success of INTC interrupt configuration setting
 *              : DEVDRV_ERROR   : Failure of INTC interrupt configuration setting
 ******************************************************************************/
int32_t R_INTC_SetConfiguration(uint16_t int_id, uint32_t int_sense)
{
    uint32_t icdicfr;
    uint32_t mask;
    volatile uint32_t* addr;

    /* ==== Argument check ==== */
    if (int_id < INTC_ID_TINT0 || int_id >= INTC_ID_TOTAL || int_sense > INTC_EDGE_TRIGGER)
    {
        return DEVDRV_ERROR; /* Argument error */
    }

    /* ICDICFRn has 16 sources in the 32 bits                          */
    /* The n can be calculated by int_id / 16                          */
    /* The bit field width is 2 bits                                   */
    /* Interrupt configuration bit assigned higher 1 bit in the 2 bits */
    /* The targe bit can be calculated by ((int_id % 16) * 2) + 1      */
    addr = (volatile uint32_t*)&INTC.ICDICFR0;

    icdicfr = *(addr + (int_id / 16)); /* Read ICDICFRn        */

    mask = 0x00000001uL;
    mask <<= (((int_id % 16) * 2) + 1); /* Shift to target bit  */
    if (INTC_LEVEL_SENSITIVE == int_sense)
    {
        icdicfr &= ~mask; /* Level sense setting  */
    }
    else
    {
        icdicfr |= mask; /* Edge trigger setting */
    }

    *(addr + (int_id / 16)) = icdicfr; /* Write ICDICFRn       */

    return DEVDRV_SUCCESS;
}

/* END of File */
