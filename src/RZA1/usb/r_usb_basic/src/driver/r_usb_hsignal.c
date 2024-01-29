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
 * File Name    : r_usb_hsignal.c
 * Description  : Host USB signalling
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_reg_access.h"

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Renesas Abstracted Host Signal functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_hstd_vbus_control
 Description     : USB VBUS ON/OFF setting.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
                 : uint16_t     command      : ON / OFF.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_vbus_control(usb_utr_t* ptr, uint16_t port, uint16_t command)
{
    if (USB_VBON == command)
    {
        hw_usb_set_vbout(ptr, port);
#if USB_CFG_BC == USB_CFG_ENABLE
        //        if (USB_BC_SUPPORT_IP == ptr->ip)
        //        {
        usb_hstd_bc_func[g_usb_hstd_bc[ptr->ip].state][USB_BC_EVENT_VB](ptr, port);
//        }
#endif
    }
    else
    {
        hw_usb_clear_vbout(ptr, port);
    }
} /* End of function usb_hstd_vbus_control() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_suspend_process
 Description     : Set USB registers as required when USB Device status is moved
                 : to "Suspend".
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_suspend_process(usb_utr_t* ptr, uint16_t port)
{
    /* SUSPENDED check */
    if (USB_SUSPENDED == g_usb_hstd_remort_port[port])
    {
        /* SOF OFF */
        hw_usb_hclear_uact(ptr, port);

        /* Wait */
        usb_cpu_delay_xms((uint16_t)1);
        usb_hstd_chk_sof(ptr, port);

        /* RWUPE=1, UACT=0 */
        hw_usb_hset_rwupe(ptr, port);

        /* Enable port BCHG interrupt */
        usb_hstd_bchg_enable(ptr, port);

        /* Wait */
        usb_cpu_delay_xms((uint16_t)5);
    }
    else
    {
        /* SOF OFF */
        hw_usb_hclear_uact(ptr, port);
        /* Wait */
        usb_cpu_delay_xms((uint16_t)5);
    }
} /* End of function usb_hstd_suspend_process() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_attach
 Description     : Set USB registers as required when USB device is attached,
                 : and notify MGR (manager) task that attach event occurred.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     result       : Result.
                 : uint16_t     port         : Port number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_attach(usb_utr_t* ptr, uint16_t result, uint16_t port)
{
    /* DTCH  interrupt enable */
    usb_hstd_dtch_enable(ptr, port);

    /* Interrupt Enable */
    usb_hstd_berne_enable(ptr);

    /* USB Mng API */
    usb_hstd_notif_ator_detach(ptr, result, port);
#if USB_CFG_BC == USB_CFG_ENABLE
    //    if(USB_BC_SUPPORT_IP == ptr->ip)
    //    {
    usb_hstd_bc_func[g_usb_hstd_bc[ptr->ip].state][USB_BC_EVENT_AT](ptr, port);
//    }
#endif /* USB_CFG_BC == USB_CFG_ENABLE */
} /* End of function usb_hstd_attach() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_detach
 Description     : Set USB register as required when USB device is detached, and
                 : notify MGR (manager) task that detach occurred.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_detach(usb_utr_t* ptr, uint16_t port)
{
#if USB_CFG_BC == USB_CFG_ENABLE
    //    if(USB_BC_SUPPORT_IP == ptr->ip)
    //    {
    usb_hstd_bc_func[g_usb_hstd_bc[ptr->ip].state][USB_BC_EVENT_DT](ptr, port);
//    }
#endif /* USB_CFG_BC == USB_CFG_ENABLE */

    /* DVSTCTR clear */
    hw_usb_clear_dvstctr(ptr, port, (uint16_t)(USB_RWUPE | USB_USBRST | USB_RESUME | USB_UACT));

    /* ATTCH interrupt enable */
    usb_hstd_attch_enable(ptr, port);

    /* USB Mng API */
    usb_hstd_notif_ator_detach(ptr, (uint16_t)USB_DETACH, port);
} /* End of function usb_hstd_detach() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
