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
 * File Name    : r_usb_pstdfunction.c
 * Description  : USB Peripheral standard function code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "r_usb_basic_if.h"
#include "r_usb_typedef.h"
#include "r_usb_extern.h"
#include "r_usb_bitdefine.h"
#include "r_usb_reg_access.h"

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Exported global variables
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Renesas Abstracted Peripheral standard function functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_pstd_set_feature_function
 Description     : Process a SET_FEATURE request.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_set_feature_function(void)
{
    /* Request error */
    usb_pstd_set_stall_pipe0();
} /* End of function usb_pstd_set_feature_function() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_chk_vbsts
 Description     : Return the VBUS status.
 Arguments       : none
 Return          : uint16_t                  : Connection status(ATTACH/DETACH)
 ***********************************************************************************************************************/
uint16_t usb_pstd_chk_vbsts(void)
{
    uint16_t buf1;
    uint16_t buf2;
    uint16_t buf3;
    uint16_t connect_info;

    /* VBUS chattering cut */
    do
    {
        buf1 = hw_usb_read_intsts();
        usb_cpu_delay_1us((uint16_t)10);
        buf2 = hw_usb_read_intsts();
        usb_cpu_delay_1us((uint16_t)10);
        buf3 = hw_usb_read_intsts();
    } while (((buf1 & USB_VBSTS) != (buf2 & USB_VBSTS)) || ((buf2 & USB_VBSTS) != (buf3 & USB_VBSTS)));

    /* VBUS status judge */
    if ((buf1 & USB_VBSTS) != (uint16_t)0)
    {
        /* Attach */
        connect_info = USB_ATTACH;
    }
    else
    {
        /* Detach */
        connect_info = USB_DETACH;
    }
    return connect_info;
} /* End of function usb_pstd_chk_vbsts() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_attach_function
 Description     : Processing for attach detect.(Waiting time of stabilize VBUS.)
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_attach_function(void)
{
    /* Delay about 10ms(Waiting time of stabilize VBUS.) */
    usb_cpu_delay_xms((uint16_t)10);
} /* End of function usb_pstd_attach_function() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_busreset_function
 Description     : Processing for USB bus reset detection.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_busreset_function(void)
{
    /* Non processing. */
} /* End of function usb_pstd_busreset_function() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_suspend_function
 Description     : Processing for suspend signal detection.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_suspend_function(void)
{
    /* Non processing. */
} /* End of function usb_pstd_suspend_function() */

/***********************************************************************************************************************
 Function Name   : usb_pdriver_init
 Description     : USB Peripheral driver initialization
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pdriver_init(usb_ctrl_t* ctrl, usb_cfg_t* cfg)
{
    uint16_t i;

    for (i = 0; i < USB_EVENT_MAX; i++)
    {
        g_usb_cstd_event.code[i] = USB_STS_NONE;
    }

    for (i = USB_PIPE0; i <= USB_MAX_PIPE_NO; i++)
    {
        g_usb_pstd_stall_pipe[i] = USB_FALSE;
        g_p_usb_pipe[i]          = (usb_utr_t*)USB_NULL;
    }

    g_usb_pstd_config_num    = 0;         /* Configuration number */
    g_usb_pstd_remote_wakeup = USB_FALSE; /* Remote wakeup enable flag */

    usb_peri_registration(ctrl, cfg);
} /* End of function usb_pdriver_init() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
