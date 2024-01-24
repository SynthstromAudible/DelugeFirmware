/***********************************************************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
 * other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
 * applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
 * EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
 * SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
 * SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
 * this software. By using this software, you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Copyright (C) 2016 Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/
/***********************************************************************************************************************
 * File Name    : r_usb_rz_mcu.c
 * Description  : RZ MCU processing
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 *         : 30.09.2016 1.20    DMA add
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/intc/devdrv_intc.h" /* INTC Driver Header   */
#include "RZA1/system/iodefine.h"
#include "RZA1/system/iodefines/mtu2_iodefine.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "deluge/deluge.h"
#include "deluge/util/cfunctions.h"
#include <stdbool.h>

// Additions by Rohan
#include "RZA1/mtu/mtu.h"
#include "definitions.h"

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_dmac.h"
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

/***********************************************************************************************************************
External variables and functions
***********************************************************************************************************************/
extern uint16_t g_usb_usbmode;
#if USB_CFG_DMA == USB_CFG_ENABLE
extern usb_dma_int_t g_usb_cstd_dma_int; /* DMA Interrupt Info */
extern void usb_cstd_dxfifo_handler(usb_utr_t* ptr, uint16_t useport);
#endif /* USB_CFG_DMA == USB_CFG_ENABLE */

/***********************************************************************************************************************
 Private global variables and functions
 ***********************************************************************************************************************/
static bool g_usb_is_opened[2] = {false, false};

/*=== Interrupt =============================================================*/
void usb_cpu_usbint_init(uint8_t ip_type);
void usb_cpu_usb_int_hand(uint32_t int_sense);
void usb2_cpu_usb_int_hand(uint32_t int_sense);
void usb_cpu_dmaint0_hand(uint32_t int_sense);
void usb_cpu_dmaint1_hand(uint32_t int_sense);
void usb_cpu_dmaint2_hand(uint32_t int_sense);
void usb_cpu_dmaint3_hand(uint32_t int_sense);

#if USB_CFG_DMA == USB_CFG_ENABLE
void usb_cstd_dmaint0_handler(void);
void usb_cstd_dmaint1_handler(void);
void usb_cstd_dmaint2_handler(void);
void usb_cstd_dmaint3_handler(void);
#endif

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void usb_cpu_int_enable(usb_utr_t* ptr);
void usb_cpu_int_disable(usb_utr_t* ptr);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
/*=== TIMER =================================================================*/
void usb_cpu_delay_1us(uint16_t time);
void usb_cpu_delay_xms(uint16_t time);

/***********************************************************************************************************************
 Renesas Abstracted RSK functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_module_start
 Description     : USB module start
 Arguments       : uint8_t      ip_type      : USB_IP0/USB_IP1
 Return value    : none
 ***********************************************************************************************************************/
usb_err_t usb_module_start(uint8_t ip_type)
{
    volatile uint8_t dummy_buf;

    if (USB_IP0 == ip_type)
    {
        if (true == g_usb_is_opened[USB_IP0])
        {
            return USB_ERR_BUSY; /* USB_ERR_OPENED */
        }

        /* The clock of USB0 modules is permitted */
        CPG.STBCR7 &= 0xFD;
        dummy_buf = CPG.STBCR7;

        g_usb_is_opened[USB_IP0] = true;
    }
    else
    {
        if (true == g_usb_is_opened[USB_IP1])
        {
            return USB_ERR_BUSY; /* USB_ERR_OPENED */
        }

        /* The clock of USB1 modules is permitted */
        CPG.STBCR7 &= 0xFE;
        dummy_buf = CPG.STBCR7;

        g_usb_is_opened[USB_IP1] = true;
    }

    return USB_SUCCESS;
} /* End of function usb_module_start() */

/***********************************************************************************************************************
 Function Name   : usb_module_stop
 Description     : USB module stop
 Arguments       : uint8_t      ip_type      : USB_IP0/USB_IP1
 Return value    : none
 ***********************************************************************************************************************/
usb_err_t usb_module_stop(uint8_t ip_type)
{
    volatile uint8_t dummy_buf;

    if (USB_IP0 == ip_type)
    {
        if (g_usb_is_opened[ip_type] == false)
        {
            return USB_ERR_NOT_OPEN;
        }

        CPG.STBCR7 |= 0x02;
        dummy_buf = CPG.STBCR7;
    }
    else if (USB_IP1 == ip_type)
    {
        if (g_usb_is_opened[ip_type] == false)
        {
            return USB_ERR_NOT_OPEN;
        }

        CPG.STBCR7 |= 0x01;
        dummy_buf = CPG.STBCR7;
    }
    else
    {
        return USB_ERR_PARA;
    }
    g_usb_is_opened[ip_type] = false;

    return USB_SUCCESS;
} /* End of function usb_module_stop() */

/***********************************************************************************************************************
Interrupt function
***********************************************************************************************************************/
/***********************************************************************************************************************
 Function Name   : usb_cpu_usbint_init
 Description     : USB interrupt Initialize
 Arguments       : uint8_t      ip_type      : USB_IP0/USB_IP1
 Return value    : void
 ***********************************************************************************************************************/
// No longer gets called - I got the caller doing the setup instead - Rohan
void usb_cpu_usbint_init(uint8_t ip_type)
{
    if (USB_IP0 == ip_type)
    {
        R_INTC_RegistIntFunc(INTC_ID_USBI0, usb_cpu_usb_int_hand);
        R_INTC_SetPriority(INTC_ID_USBI0, 9);
        R_INTC_Enable(INTC_ID_USBI0);
    }

#if USB_NUM_USBIP == 2
    if (USB_IP1 == ip_type)
    {
        R_INTC_RegistIntFunc(INTC_ID_USBI1, usb2_cpu_usb_int_hand);
        R_INTC_SetPriority(INTC_ID_USBI1, 9);
        R_INTC_Enable(INTC_ID_USBI1);
    }
#endif

#if USB_CFG_DMA == USB_CFG_ENABLE
    R_INTC_RegistIntFunc(INTC_ID_DMAINT0, usb_cpu_dmaint0_hand);
    R_INTC_SetPriority(INTC_ID_DMAINT0, 0);
    R_INTC_Enable(INTC_ID_DMAINT0);

    R_INTC_RegistIntFunc(INTC_ID_DMAINT1, usb_cpu_dmaint1_hand);
    R_INTC_SetPriority(INTC_ID_DMAINT1, 0);
    R_INTC_Enable(INTC_ID_DMAINT1);

    R_INTC_RegistIntFunc(INTC_ID_DMAINT2, usb_cpu_dmaint2_hand);
    R_INTC_SetPriority(INTC_ID_DMAINT2, 0);
    R_INTC_Enable(INTC_ID_DMAINT2);

    R_INTC_RegistIntFunc(INTC_ID_DMAINT3, usb_cpu_dmaint3_hand);
    R_INTC_SetPriority(INTC_ID_DMAINT3, 0);
    R_INTC_Enable(INTC_ID_DMAINT3);
#endif
} /* End of function usb_cpu_usbint_init() */

/************************************************************************************************************************
 * Function Name: usb_cpu_usb_int_hand
 * Description  : Interrupt service routine
 * Arguments    : none
 * Return Value : none
 ************************************************************************************************************************/
void usb_cpu_usb_int_hand(uint32_t int_sense) // This function actually gets called as a CPU interrupt, for real! Rohan
{

    // uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];

    /* Call USB interrupt routine */
    if (USB_HOST == g_usb_usbmode)
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        usb_hstd_usb_handler(0); /* Call interrupt routine */
#endif                           /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        usb_pstd_usb_handler(0); /* Call interrupt routine */
#endif                           /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }

    /*
    uint16_t endTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
    uint16_t duration = endTime - startTime;
    uint32_t timePassedNS = superfastTimerCountToNS(duration);
    uartPrint("interrupt duration, nSec: ");
    uartPrintNumber(timePassedNS);
    */
} /* End of function usb_cpu_usb_int_hand() */

/************************************************************************************************************************
 * Function Name: usb2_cpu_usb_int_hand
 * Description  : Interrupt service routine
 * Arguments    : none
 * Return Value : none
 ************************************************************************************************************************/
void usb2_cpu_usb_int_hand(uint32_t int_sense)
{
#if USB_NUM_USBIP == 2
    /* Condition compilation by the difference of USB function */
    if (USB_HOST == g_usb_usbmode)
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        usb2_hstd_usb_handler(); /* Call interrupt routine */
#endif                           /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        usb_pstd_usb_handler();
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
#endif
} /* End of function usb2_cpu_usb_int_hand() */

/***********************************************************************************************************************
TIMER function
***********************************************************************************************************************/
/***********************************************************************************************************************
 Function Name   : usb_cpu_delay_1us
 Description     : 1us Delay timer
 Arguments       : uint16_t     time         : Delay time(*1us)
 Return value    : none
 Note            : Please change for your MCU
 ***********************************************************************************************************************/

#define MTU_TIMER_CNT 33

#include "definitions.h"
#include "deluge/drivers/mtu/mtu.h"

void usb_cpu_delay_1us(uint16_t time) // Modified by Rohan
{
    uint16_t startTime = *TCNT[TIMER_SYSTEM_FAST];
    uint16_t stopTime  = startTime + usToFastTimerCount(time);
    do
    {
        if (time >= 40)
            routineForSD();
    } while ((int16_t)(*TCNT[TIMER_SYSTEM_FAST] - stopTime) < 0);
} /* End of function usb_cpu_delay_1us() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_delay_xms
 Description     : xms Delay timer
 Arguments       : uint16_t         time     : Delay time(*1ms)
 Return value    : void
 Note            : Please change for your MCU
 ***********************************************************************************************************************/
void usb_cpu_delay_xms(uint16_t time) // Modified by Rohan
{
    uint16_t startTime = *TCNT[TIMER_SYSTEM_SLOW];
    uint16_t stopTime  = startTime + msToSlowTimerCount(time);
    do
    {
        routineForSD();
    } while ((int16_t)(*TCNT[TIMER_SYSTEM_SLOW] - stopTime) < 0);
} /* End of function usb_cpu_delay_xms() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : usb_cpu_int_enable
 Description     : USB Interrupt Enable
 Arguments       : usb_utr_t    *ptr         : USB internal structure. Selects USB channel.
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_int_enable(usb_utr_t* ptr)
{
    if (USB_USBIP_0 == ptr->ip)
    {
        R_INTC_Enable(INTC_ID_USBI0); /* Enable USB0 interrupt */
#if USB_CFG_DMA == USB_CFG_ENABLE
        R_INTC_Enable(INTC_ID_DMAINT1);
        R_INTC_Enable(INTC_ID_DMAINT2);
#endif /* USB_CFG_DMA == USB_CFG_ENABLE */
    }

    if (USB_USBIP_1 == ptr->ip)
    {
        R_INTC_Enable(INTC_ID_USBI1); /* Enable USBA interrupt */
#if USB_CFG_DMA == USB_CFG_ENABLE
        R_INTC_Enable(INTC_ID_DMAINT3);
        R_INTC_Enable(INTC_ID_DMAINT4);
#endif /* USB_CFG_DMA == USB_CFG_ENABLE */
    }
} /* End of function usb_cpu_int_enable() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_int_disable
 Description     : USB Interrupt disable
 Arguments       : usb_utr_t    *ptr         : USB internal structure. Selects USB channel.
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_int_disable(usb_utr_t* ptr)
{
    if (USB_USBIP_0 == ptr->ip)
    {
        R_INTC_Disable(INTC_ID_USBI0); /* Disnable USB0 interrupt */
#if USB_CFG_DMA == USB_CFG_ENABLE
        R_INTC_Disable(INTC_ID_DMAINT1);
        R_INTC_Disable(INTC_ID_DMAINT2);
#endif /* USB_CFG_DMA == USB_CFG_ENABLE */
    }

    if (USB_USBIP_1 == ptr->ip)
    {
        R_INTC_Disable(INTC_ID_USBI1); /* Disnable USBA interrupt */
#if USB_CFG_DMA == USB_CFG_ENABLE
        R_INTC_Disable(INTC_ID_DMAINT3);
        R_INTC_Disable(INTC_ID_DMAINT4);
#endif /* USB_CFG_DMA == USB_CFG_ENABLE */
    }
} /* End of function usb_cpu_int_disable() */

/***********************************************************************************************************************
 Function Name   : usb_chattaring
 Description     :
 Arguments       :
 Return value    :
 ***********************************************************************************************************************/
uint16_t usb_chattaring(volatile uint16_t* syssts)
{
    uint16_t lnst[3];

    while (1)
    {
        lnst[0] = *syssts & USB_LNST;
        usb_cpu_delay_xms((uint16_t)1); /* 1ms wait */
        lnst[1] = *syssts & USB_LNST;
        usb_cpu_delay_xms((uint16_t)1); /* 1ms wait */
        lnst[2] = *syssts & USB_LNST;
        if ((lnst[0] == lnst[1]) && (lnst[0] == lnst[2]))
        {
            break;
        }
    }
    return lnst[0];
} /* End of function usb_chattaring() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dmaint0_hand
 Description     :
 Arguments       :
 Return value    :
 ***********************************************************************************************************************/
void usb_cpu_dmaint0_hand(uint32_t int_sense)
{
#if USB_CFG_DMA == USB_CFG_ENABLE
    usb_cstd_dmaint0_handler();
#endif
} /* End of function usb_cpu_dmaint0_hand() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dmaint1_hand
 Description     :
 Arguments       :
 Return value    :
 ***********************************************************************************************************************/
void usb_cpu_dmaint1_hand(uint32_t int_sense)
{
#if USB_CFG_DMA == USB_CFG_ENABLE
    usb_cstd_dmaint1_handler();
#endif
} /* End of function usb_cpu_dmaint1_hand() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dmaint1_hand
 Description     :
 Arguments       :
 Return value    :
 ***********************************************************************************************************************/
void usb_cpu_dmaint2_hand(uint32_t int_sense)
{
#if USB_CFG_DMA == USB_CFG_ENABLE
    usb_cstd_dmaint2_handler();
#endif
} /* End of function usb_cpu_dmaint1_hand() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dmaint1_hand
 Description     :
 Arguments       :
 Return value    :
 ***********************************************************************************************************************/
void usb_cpu_dmaint3_hand(uint32_t int_sense)
{
#if USB_CFG_DMA == USB_CFG_ENABLE
    usb_cstd_dmaint3_handler();
#endif
} /* End of function usb_cpu_dmaint1_hand() */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
