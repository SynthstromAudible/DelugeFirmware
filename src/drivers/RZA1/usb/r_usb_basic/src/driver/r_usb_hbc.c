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
 * File Name    : r_usb_hbc.c
 * Description  : This is the USB host battery charging code.
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "r_usb_basic_if.h"
#include "r_usb_typedef.h"
#include "r_usb_extern.h"
#include "r_usb_bitdefine.h"
#include "r_usb_reg_access.h"

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
#if USB_CFG_BC == USB_CFG_ENABLE
/***********************************************************************************************************************
 Macro definitions
 ***********************************************************************************************************************/
/* PD Detect Define */
#define USB_BC_NODET (0x00u)
#define USB_BC_PDDET (0x01u)

/***********************************************************************************************************************
 Typedef definitions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Private global variables and functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Exported global variables and functions (to be accessed by other files)
 ***********************************************************************************************************************/
/* variables */
usb_bc_status_t g_usb_hstd_bc[2u];

/* functions */
void usb_hstd_pddetint_process(usb_utr_t* ptr, uint16_t port);

/* BC State change function table */
void (*usb_hstd_bc_func[USB_BC_STATE_MAX][USB_BC_EVENT_MAX])(usb_utr_t* ptr, uint16_t port) = {
    /* VBUS_ON              ATTACH              DETACH */
    {usb_hstd_bc_init_vb, usb_hstd_bc_err, usb_hstd_bc_err}, /* USB_BC_STATE_INIT */
    {usb_hstd_bc_err, usb_hstd_bc_det_at, usb_hstd_bc_err},  /* USB_BC_STATE_DET */
    {usb_hstd_bc_err, usb_hstd_bc_err, usb_hstd_bc_cdp_dt},  /* USB_BC_STATE_CDP */
    {usb_hstd_bc_err, usb_hstd_bc_err, usb_hstd_bc_sdp_dt},  /* USB_BC_STATE_SDP */
    {usb_hstd_bc_err, usb_hstd_bc_err, usb_hstd_bc_err}      /* USB_BC_STATE_DCP */
};

/***********************************************************************************************************************
 Renesas Abstracted USB host battery charging driver functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_hstd_pddetint_process
 Description     : PDDETINT process
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_pddetint_process(usb_utr_t* ptr, uint16_t port)
{
    uint16_t buf[3];

    /* PDDETSTS chattering cut */
    do
    {
        buf[0] = hw_usb_read_bcctrl(ptr);
        usb_cpu_delay_xms(1);
        buf[1] = hw_usb_read_bcctrl(ptr);
        usb_cpu_delay_xms(1);
        buf[2] = hw_usb_read_bcctrl(ptr);
    } while (
        ((buf[0] & USB_PDDETSTS) != (buf[1] & USB_PDDETSTS)) || ((buf[1] & USB_PDDETSTS) != (buf[2] & USB_PDDETSTS)));

    if (USB_PDDETSTS == (buf[0] & USB_PDDETSTS)) /* VDPSRC Detect */
    {
        if (USB_VDMSRCE != (buf[0] & USB_VDMSRCE))
        {
            hw_usb_set_vdmsrce(ptr);
        }
    }
    else /* VDPSRC Not detect */
    {
        if (USB_VDMSRCE == (buf[0] & USB_VDMSRCE))
        {
            hw_usb_clear_vdmsrce(ptr);
            g_usb_hstd_bc[ptr->ip].pd_detect = USB_BC_PDDET;
        }
    }
} /* End of function usb_hstd_pddetint_process() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_err
 Description     : BC State change function [ERROR]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_err(usb_utr_t* ptr, uint16_t port)
{
    /* Nothing */
} /* End of function usb_hstd_bc_err() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_init_vb
 Description     : BC State change function [INIT] [VbusOn]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_init_vb(usb_utr_t* ptr, uint16_t port)
{
#if USB_CFG_DCP == USB_CFG_ENABLE
    g_usb_hstd_bc[ptr->ip].state = USB_BC_STATE_DCP;
    usb_hstd_bc_dcp_entry(ptr, port);
#else
    g_usb_hstd_bc[ptr->ip].state = USB_BC_STATE_DET;
    usb_hstd_bc_det_entry(ptr, port);
#endif /* USB_CFG_DCP == USB_CFG_ENABLE */
} /* End of function usb_hstd_bc_init_vb() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_det_at
 Description     : BC State change function [DET] [Attach]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_det_at(usb_utr_t* ptr, uint16_t port)
{
    usb_hstd_bc_det_exit(ptr, port);

    if (g_usb_hstd_bc[ptr->ip].pd_detect)
    {
        g_usb_hstd_bc[ptr->ip].state = USB_BC_STATE_CDP;
        usb_hstd_bc_cdp_entry(ptr, port);
    }
    else
    {
        g_usb_hstd_bc[ptr->ip].state = USB_BC_STATE_SDP;
        usb_hstd_bc_sdp_entry(ptr, port);
    }
} /* End of function usb_hstd_bc_det_at() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_cdp_dt
 Description     : BC State change function [CDP] [Detach]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_cdp_dt(usb_utr_t* ptr, uint16_t port)
{
    usb_hstd_bc_cdp_exit(ptr, port);
    g_usb_hstd_bc[ptr->ip].state = USB_BC_STATE_DET;
    usb_hstd_bc_det_entry(ptr, port);
} /* End of function usb_hstd_bc_cdp_dt() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_sdp_dt
 Description     : BC State change function [SDP] [Detach]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_sdp_dt(usb_utr_t* ptr, uint16_t port)
{
    usb_hstd_bc_sdp_exit(ptr, port);
    g_usb_hstd_bc[ptr->ip].state = USB_BC_STATE_DET;
    usb_hstd_bc_det_entry(ptr, port);
} /* End of function usb_hstd_bc_sdp_dt() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_det_entry
 Description     : BC State entry function [DET]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_det_entry(usb_utr_t* ptr, uint16_t port)
{
    hw_usb_set_idpsinke(ptr);
    hw_usb_hclear_sts_pddetint(ptr);
    hw_usb_hset_enb_pddetinte(ptr);
} /* End of function usb_hstd_bc_det_entry() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_det_exit
 Description     : BC State exit function [DET]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_det_exit(usb_utr_t* ptr, uint16_t port)
{
    hw_usb_hset_enb_pddetinte(ptr);
    hw_usb_hclear_sts_pddetint(ptr);
    hw_usb_clear_idpsinke(ptr);
} /* End of function usb_hstd_bc_det_exit() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_cdp_entry
 Description     : BC State entry function [CDP]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_cdp_entry(usb_utr_t* ptr, uint16_t port)
{
} /* End of function usb_hstd_bc_cdp_entry() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_cdp_exit
 Description     : BC State exit function [CDP]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_cdp_exit(usb_utr_t* ptr, uint16_t port)
{
    g_usb_hstd_bc[ptr->ip].pd_detect = USB_BC_NODET;
} /* End of function usb_hstd_bc_cdp_exit() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_sdp_entry
 Description     : BC State entry function [SDP]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_sdp_entry(usb_utr_t* ptr, uint16_t port)
{
} /* End of function usb_hstd_bc_sdp_entry() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_sdp_exit
 Description     : BC State exit function [SDP]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_sdp_exit(usb_utr_t* ptr, uint16_t port)
{
    /* Nothing */
} /* End of function usb_hstd_bc_sdp_exit() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bc_dcp_entry
 Description     : BC State entry function [DCP]
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_bc_dcp_entry(usb_utr_t* ptr, uint16_t port)
{
    hw_usb_clear_drpd(ptr, port);
    hw_usb_hset_dcpmode(ptr);
} /* End of function usb_hstd_bc_dcp_entry() */

#endif /* USB_CFG_BC == USB_CFG_ENABLE */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
