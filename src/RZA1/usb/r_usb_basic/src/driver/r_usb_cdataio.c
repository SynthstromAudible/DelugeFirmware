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
 * File Name    : r_usb_cdataio.c
 * Description  : USB Host and Peripheral low level data I/O code
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

#if defined(USB_CFG_HCDC_USE)
#include "r_usb_hcdc_config.h"
#endif /* defined(USB_CFG_HCDC_USE) */

#if defined(USB_CFG_PCDC_USE)
#include "r_usb_pcdc_config.h"
#endif /* defined(USB_CFG_PCDC_USE) */

#if defined(USB_CFG_HHID_USE)
#include <drivers/usb/userdef/r_usb_hmidi_config.h>
#endif /* defined(USB_CFG_HHID_USE) */

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
#include "drivers/usb/r_usb_basic/src/hw/inc/r_usb_dmac.h"
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

/***********************************************************************************************************************
 Macro definitions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Private global variables and functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Exported global variables
 ***********************************************************************************************************************/
extern uint16_t g_usb_usbmode; /* USB mode HOST/PERI */
extern const uint8_t g_usb_pipe_host[];

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
extern usb_utr_t g_usb_hdata[USB_NUM_USBIP][USB_MAXPIPE_NUM + 1];
extern usb_ctrl_trans_t g_usb_ctrl_request[USB_NUM_USBIP][USB_MAXDEVADDR + 1];
#endif /* ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST) */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
extern usb_utr_t g_usb_pdata[USB_MAXPIPE_NUM + 1];
extern const uint8_t g_usb_pipe_peri[];
#endif /* ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI) */

#if defined(USB_CFG_PCDC_USE)
extern uint8_t g_usb_pcdc_serialstate_table[];
#endif /* defined(USB_CFG_PCDC_USE) */

/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/
/* USB data transfer */
/* PIPEn Buffer counter */
uint32_t g_usb_data_cnt[USB_MAX_PIPE_NO + 1u];

/* PIPEn Buffer pointer(8bit) */
uint8_t* g_p_usb_data[USB_MAX_PIPE_NO + 1u];

/* Message pipe */
usb_utr_t* g_p_usb_pipe[USB_MAX_PIPE_NO + 1u];

/* Callback function of USB Read/USB Write */
void (*g_usb_callback[])(usb_utr_t*, uint16_t, uint16_t) = {
/* PCDC, PCDCC */
#if defined(USB_CFG_PCDC_USE)
    usb_pcdc_read_complete,
    usb_pcdc_write_complete, /* USB_PCDC  (0) */
    USB_NULL,
    usb_pcdc_write_complete, /* USB_PCDCC (1) */
#else
    USB_NULL,
    USB_NULL, /* USB_PCDC  (0) */
    USB_NULL,
    USB_NULL, /* USB_PCDCC (1) */
#endif

/* PHID */
#if defined(USB_CFG_PHID_USE)
    usb_phid_read_complete,
    usb_phid_write_complete, /* USB_PHID  (2) */
#else
    USB_NULL,
    USB_NULL, /* USB_PHID  (2) */
#endif

/* PVNDR */
#if defined(USB_CFG_PVNDR_USE)
    usb_pvndr_read_complete,
    usb_pnvdr_write_complete, /* USB_PVND  (3) */
#else
    USB_NULL,
    USB_NULL, /* USB_PVND  (3) */
#endif

/* HCDC, HCDCC */
#if defined(USB_CFG_HCDC_USE)
    usb_hcdc_read_complete,
    usb_hcdc_write_complete, /* USB_HCDC  (4) */
    usb_hcdc_read_complete,
    USB_NULL, /* USB_HCDCC (5) */
#else
    USB_NULL,
    USB_NULL, /* USB_HCDC  (4) */
    USB_NULL,
    USB_NULL, /* USB_HCDCC (5) */
#endif

/* HHID */
#if defined(USB_CFG_HHID_USE)
    hhid_read_complete,
    hhid_write_complete, /* USB_HHID  (6) */
#else
    USB_NULL,
    USB_NULL, /* USB_HHID  (6) */
#endif

/* HVNDR */
#if defined(USB_CFG_HVNDR_USE)
    usb_hvndr_read_complete,
    usb_hnvdr_write_complete, /* USB_HVND  (7) */
#else
    USB_NULL,
    USB_NULL, /* USB_HVND  (7) */
#endif

    /* HMSC */
    USB_NULL,
    USB_NULL, /* USB_HMSC  (8) */

    /* PMSC */
    USB_NULL,
    USB_NULL, /* USB_PMSC  (9) */
}; /* const void (g_usb_callback[])(usb_utr_t *, uint16_t, uint16_t) */

/***********************************************************************************************************************
 Renesas Abstracted common data I/O functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_cstd_select_nak
 Description     : Set the specified pipe PID to send a NAK if the transfer type is BULK/INT.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_select_nak(usb_utr_t* ptr, uint16_t pipe)
{
    /* Check PIPE TYPE */
    if (usb_cstd_get_pipe_type(ptr, pipe) != USB_TYPFIELD_ISO)
    {
        usb_cstd_set_nak(ptr, pipe);
    }
} /* End of function usb_cstd_select_nak() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_debug_hook
 Description     : Debug hook
 Arguments       : uint16_t     error_code   : error code
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_debug_hook(uint16_t error_code)
{
    while (1)
    {
        /* Non */
    }
} /* End of function usb_cstd_debug_hook() */

/***********************************************************************************************************************
 Function Name   : usb_ctrl_read
 Description     : Receive process for Control transfer
 Arguments       : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
                 : uint8_t      *buf         : Transfer data address
                 : uint32_t     size         : Transfer length
 Return value    : usb_er_t                  : USB_SUCCESS(USB_OK) / USB_ERROR.
 ***********************************************************************************************************************/
usb_er_t usb_ctrl_read(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size)
{
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    usb_er_t err;

    if (USB_HOST == g_usb_usbmode)
    {
        g_usb_read_request_size[p_ctrl->module][USB_PIPE0] = size;
        g_usb_hdata[p_ctrl->module][USB_PIPE0].keyword     = USB_PIPE0; /* Pipe No */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].p_tranadr   = buf;       /* Data address */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].tranlen     = size;      /* Data Size */

        /* Callback function */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].complete             = usb_class_request_complete;
        g_usb_ctrl_request[p_ctrl->module][p_ctrl->address].address = p_ctrl->address;
        g_usb_ctrl_request[p_ctrl->module][p_ctrl->address].setup   = p_ctrl->setup;

        /* Setup message address set */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].p_setup =
            (uint16_t*)&g_usb_ctrl_request[p_ctrl->module][p_ctrl->address];
        g_usb_hdata[p_ctrl->module][USB_PIPE0].segment = USB_TRAN_END;
        g_usb_hdata[p_ctrl->module][USB_PIPE0].ip      = p_ctrl->module;
        g_usb_hdata[p_ctrl->module][USB_PIPE0].ipp     = usb_hstd_get_usb_ip_adr((uint8_t)p_ctrl->module);
        err = usb_hstd_transfer_start(&g_usb_hdata[p_ctrl->module][USB_PIPE0]);
        return err;
    }
#endif

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
    if (USB_PERI == g_usb_usbmode)
    {
        if (USB_ON == g_usb_pstd_pipe0_request)
        {
            return USB_QOVR;
        }
        g_usb_read_request_size[USB_CFG_USE_USBIP][USB_PIPE0] = size;
        usb_pstd_ctrl_write(size, buf);
    }
#endif
    return USB_SUCCESS;
} /* End of function usb_ctrl_read() */

/***********************************************************************************************************************
 Function Name   : usb_ctrl_write
 Description     : Send process for Control transfer
 Arguments       : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
                 : uint8_t      *buf         : Cransfer data address
                 : uint32_t     size         : Transfer length
 Return value    : usb_er_t                  : USB_SUCCESS(USB_OK) / USB_ERROR.
 ***********************************************************************************************************************/
usb_er_t usb_ctrl_write(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size)
{
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    usb_er_t err;

    if (USB_HOST == g_usb_usbmode)
    {
        g_usb_read_request_size[p_ctrl->module][USB_PIPE0] = size;
        g_usb_hdata[p_ctrl->module][USB_PIPE0].keyword     = USB_PIPE0; /* Pipe No */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].p_tranadr   = buf;       /* Data address */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].tranlen     = size;      /* Data Size */

        /* Callback function */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].complete             = usb_class_request_complete;
        g_usb_ctrl_request[p_ctrl->module][p_ctrl->address].address = p_ctrl->address;
        g_usb_ctrl_request[p_ctrl->module][p_ctrl->address].setup   = p_ctrl->setup;

        /* Setup message address set */
        g_usb_hdata[p_ctrl->module][USB_PIPE0].p_setup =
            (uint16_t*)&g_usb_ctrl_request[p_ctrl->module][p_ctrl->address];
        g_usb_hdata[p_ctrl->module][USB_PIPE0].segment = USB_TRAN_END;
        g_usb_hdata[p_ctrl->module][USB_PIPE0].ip      = p_ctrl->module;
        g_usb_hdata[p_ctrl->module][USB_PIPE0].ipp     = usb_hstd_get_usb_ip_adr((uint8_t)p_ctrl->module);
        err = usb_hstd_transfer_start(&g_usb_hdata[p_ctrl->module][USB_PIPE0]);
        return err;
    }
#endif

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
    usb_ctrl_t ctrl;

    if (USB_PERI == g_usb_usbmode)
    {
        if ((USB_NULL == buf) && (USB_NULL == size))
        {
            if (USB_ACK == p_ctrl->status)
            {
                usb_cstd_set_buf(USB_NULL, (uint16_t)USB_PIPE0); /* Set BUF */
            }
            else /* USB_STALL */
            {
                usb_pstd_set_stall_pipe0();
            }

            ctrl.setup  = p_ctrl->setup; /* Save setup data. */
            ctrl.module = USB_CFG_USE_USBIP;
            ctrl.size   = 0;
            ctrl.status = USB_ACK;
            ctrl.type   = USB_REQUEST;
            usb_set_event(USB_STS_REQUEST_COMPLETE, &ctrl);
        }
        else
        {
            if (USB_ON == g_usb_pstd_pipe0_request)
            {
                return USB_QOVR;
            }
            usb_pstd_ctrl_read(size, buf);
        }
    }
#endif
    return USB_SUCCESS;
} /* End of function usb_ctrl_write() */

/***********************************************************************************************************************
 Function Name   : usb_ctrl_stop
 Description     : Stop of USB Control transfer.
 Arguments       : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
 Return value    : usb_er_t                  : USB_OK / USB_ERROR.
 ***********************************************************************************************************************/
usb_er_t usb_ctrl_stop(usb_ctrl_t* p_ctrl)
{
    usb_er_t err;
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    usb_utr_t utr;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

    if ((p_ctrl->type) > USB_PVND)
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        utr.ip  = p_ctrl->module;
        utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip);
        err     = usb_hstd_transfer_end(&utr, USB_PIPE0, (uint16_t)USB_DATA_STOP);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    else
    {
        /* Peripheral only */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        err = usb_pstd_transfer_end(USB_PIPE0);
#endif
    }
    return err;
} /* End of function usb_ctrl_stop() */

/***********************************************************************************************************************
 Function Name   : usb_data_read
 Description     :
 Arguments       : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
                 : uint8_t      *buf         : Transfer data address
                 : uint32_t     size         : Transfer length
 Return value    : usb_er_t                  : USB_OK / USB_ERROR.
 ***********************************************************************************************************************/
usb_er_t usb_data_read(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size)
{
    uint8_t pipe;
    usb_er_t err;

    pipe = usb_get_usepipe(p_ctrl, USB_READ);

    if ((p_ctrl->type) > USB_PVND)
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        g_usb_read_request_size[p_ctrl->module][pipe] = size;
        g_usb_hdata[p_ctrl->module][pipe].keyword     = pipe;                             /* Pipe No */
        g_usb_hdata[p_ctrl->module][pipe].p_tranadr   = buf;                              /* Data address */
        g_usb_hdata[p_ctrl->module][pipe].tranlen     = size;                             /* Data Size */
        g_usb_hdata[p_ctrl->module][pipe].complete    = g_usb_callback[p_ctrl->type * 2]; /* Callback function */
        g_usb_hdata[p_ctrl->module][pipe].segment     = USB_TRAN_END;
        g_usb_hdata[p_ctrl->module][pipe].ip          = p_ctrl->module;
        g_usb_hdata[p_ctrl->module][pipe].ipp         = usb_hstd_get_usb_ip_adr((uint8_t)p_ctrl->module);
        err                                           = usb_hstd_transfer_start(&g_usb_hdata[p_ctrl->module][pipe]);
#endif
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        g_usb_read_request_size[USB_CFG_USE_USBIP][pipe] = size;
        g_usb_pdata[pipe].keyword                        = pipe;                 /* Pipe No */
        g_usb_pdata[pipe].p_tranadr                      = buf;                  /* Data address */
        g_usb_pdata[pipe].tranlen                        = size;                 /* Data Size */
        g_usb_pdata[pipe].complete = (usb_cb_t)g_usb_callback[p_ctrl->type * 2]; /* Callback function */
        err                        = usb_pstd_transfer_start(&g_usb_pdata[pipe]);
#endif
    }
    return err;
} /* End of function usb_data_read() */

/***********************************************************************************************************************
 Function Name   : usb_data_write
 Description     :
 Arguments       : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
                 : uint8_t      *buf         : Transfer data address
                 : uint32_t     size         : Transfer length
 Return value    : usb_er_t                  : USB_OK / USB_ERROR.
 ***********************************************************************************************************************/
usb_er_t usb_data_write(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size)
{
    uint8_t pipe;
    usb_er_t err;

    pipe = usb_get_usepipe(p_ctrl, USB_WRITE);

    if ((p_ctrl->type) > USB_PVND)
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        g_usb_hdata[p_ctrl->module][pipe].keyword   = pipe;                                   /* Pipe No */
        g_usb_hdata[p_ctrl->module][pipe].p_tranadr = buf;                                    /* Data address */
        g_usb_hdata[p_ctrl->module][pipe].tranlen   = size;                                   /* Data Size */
        g_usb_hdata[p_ctrl->module][pipe].complete  = g_usb_callback[(p_ctrl->type * 2) + 1]; /* Callback function */
        g_usb_hdata[p_ctrl->module][pipe].segment   = USB_TRAN_END;
        g_usb_hdata[p_ctrl->module][pipe].ip        = p_ctrl->module;
        g_usb_hdata[p_ctrl->module][pipe].ipp       = usb_hstd_get_usb_ip_adr((uint8_t)p_ctrl->module);
        err                                         = usb_hstd_transfer_start(&g_usb_hdata[p_ctrl->module][pipe]);
#endif
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if defined(USB_CFG_PCDC_USE)
        if (USB_CFG_PCDC_INT_IN != pipe)
        {
            g_usb_pdata[pipe].p_tranadr = buf;  /* Data address */
            g_usb_pdata[pipe].tranlen   = size; /* Data Size */
        }
        else
        {
            g_usb_pcdc_serialstate_table[8] = buf[0];
            g_usb_pcdc_serialstate_table[9] = buf[1];
            g_usb_pdata[pipe].p_tranadr     = g_usb_pcdc_serialstate_table; /* Data address */
            g_usb_pdata[pipe].tranlen       = 10;                           /* Data Size */
        }

#else
        g_usb_pdata[pipe].p_tranadr = buf;  /* Data address */
        g_usb_pdata[pipe].tranlen   = size; /* Data Size */
#endif                                                                                 /* defined(USB_CFG_PCDC_USE) */
        g_usb_pdata[pipe].keyword  = pipe;                                             /* Pipe No */
        g_usb_pdata[pipe].complete = (usb_cb_t)g_usb_callback[(p_ctrl->type * 2) + 1]; /* Callback function */
        err                        = usb_pstd_transfer_start(&g_usb_pdata[pipe]);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    return err;
} /* End of function usb_data_write() */

/***********************************************************************************************************************
 Function Name   : usb_data_stop
 Description     :
 Arguments       : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
                 : uint16_t     type         : Read(0)/Write(1)
 Return value    : usb_er_t                  : USB_OK / USB_ERROR.
 ***********************************************************************************************************************/
usb_er_t usb_data_stop(usb_ctrl_t* p_ctrl, uint16_t type)
{
    uint8_t pipe;
    usb_er_t err = USB_ERROR;
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    usb_utr_t utr;
#endif

    pipe = usb_get_usepipe(p_ctrl, type);

    if (USB_NULL == pipe)
    {
        return USB_ERROR;
    }

    if ((p_ctrl->type) > USB_PVND)
    {
        /* Host only */
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        utr.ip  = p_ctrl->module;
        utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip);
        err     = usb_hstd_transfer_end(&utr, pipe, (uint16_t)USB_DATA_STOP);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    else
    {
        /* Peripheral only */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        err = usb_pstd_transfer_end(pipe);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    return err;
} /* End of function usb_data_stop() */

/***********************************************************************************************************************
 Function Name   : usb_get_usepipe
 Description     : Get pipe number for USB Read/USB Write
 Arguments       : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
                 : uint8_t      dir          : Read(0)/Write(1)
 Return value    : Bitmap of Use pipe
 ***********************************************************************************************************************/
uint8_t usb_get_usepipe(usb_ctrl_t* p_ctrl, uint8_t dir)
{
    uint8_t pipe = USB_NULL;
    uint8_t idx;

    if ((p_ctrl->type) > USB_PVND)
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        /* Host */
        idx  = (((p_ctrl->type - USB_HCDC) * 8) + ((p_ctrl->address - 1) * 2)) + dir;
        pipe = g_usb_pipe_host[idx];
#endif
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        /* Peripheral */
        idx  = (p_ctrl->type * 2) + dir;
        pipe = g_usb_pipe_peri[idx];
#endif
    }
    return pipe;
} /* End of function usb_get_usepipe() */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
