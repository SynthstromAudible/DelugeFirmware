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
 * File Name    : r_usb_pcontrolrw.c
 * Description  : USB Peripheral control transfer API code
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

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Renesas Abstracted Peripheral Control RW API functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_pstd_ctrl_read
 Description     : Called by R_USB_PstdCtrlRead, see it for description.
 Arguments       : uint32_t     bsize        : Read size in bytes.
                 : uint8_t      *table       : Start address of read data buffer.
 Return value    : uint16_t                  : USB_WRITESHRT/USB_WRITE_END/USB_WRITING/USB_FIFOERROR.
 ***********************************************************************************************************************/
uint16_t usb_pstd_ctrl_read(uint32_t bsize, uint8_t* table)
{
    uint16_t end_flag;

    g_usb_pstd_pipe0_request = USB_ON;

    g_usb_data_cnt[USB_PIPE0] = bsize;
    g_p_usb_data[USB_PIPE0]   = table;

    usb_cstd_chg_curpipe(USB_NULL, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, (uint16_t)USB_ISEL);

    /* Buffer clear */
    hw_usb_set_bclr(USB_NULL, USB_CUSE);

    hw_usb_clear_status_bemp(USB_NULL, USB_PIPE0);

    /* Peripheral Control sequence */
    end_flag = usb_pstd_write_data(USB_PIPE0, USB_CUSE);

    /* Peripheral control sequence */
    switch (end_flag)
    {
        /* End of data write */
        case USB_WRITESHRT:

            /* Enable not ready interrupt */
            // usb_cstd_nrdy_enable(USB_NULL, (uint16_t)USB_PIPE0);
            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
            //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver
            //  (in 2019).

            /* Set PID=BUF */
            usb_cstd_set_buf(USB_NULL, (uint16_t)USB_PIPE0);

            break;

        /* End of data write (not null) */
        case USB_WRITEEND:
        /* Continue of data write */
        case USB_WRITING:

            /* Enable empty interrupt */
            hw_usb_set_bempenb(USB_NULL, (uint16_t)USB_PIPE0);

            /* Enable not ready interrupt */
            // usb_cstd_nrdy_enable(USB_NULL, (uint16_t)USB_PIPE0);
            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
            //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver
            //  (in 2019).

            /* Set PID=BUF */
            usb_cstd_set_buf(USB_NULL, (uint16_t)USB_PIPE0);

            break;

        /* FIFO access error */
        case USB_FIFOERROR:
            break;

        default:
            break;
    }
    return (end_flag); /* End or error or continue */
} /* End of function usb_pstd_ctrl_read() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_ctrl_write
 Description     : Called by R_USB_PstdCtrlWrite, see it for description.
 Arguments       : uint32_t     bsize        : Write size in bytes.
                 : uint8_t      *table       : Start address of write data buffer.
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_ctrl_write(uint32_t bsize, uint8_t* table)
{
    g_usb_pstd_pipe0_request = USB_ON;

    g_usb_data_cnt[USB_PIPE0] = bsize;
    g_p_usb_data[USB_PIPE0]   = table;

    usb_cstd_chg_curpipe(USB_NULL, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);

    /* Buffer clear */
    hw_usb_set_bclr(USB_NULL, USB_CUSE);

    /* Interrupt enable */
    /* Enable ready interrupt */
    hw_usb_set_brdyenb(USB_NULL, (uint16_t)USB_PIPE0);

    /* Enable not ready interrupt */
    // usb_cstd_nrdy_enable(USB_NULL, (uint16_t)USB_PIPE0);
    //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was causing
    //  freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in 2019).

    /* Set PID=BUF */
    usb_cstd_set_buf(USB_NULL, (uint16_t)USB_PIPE0);
} /* End of function usb_pstd_ctrl_write() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_ctrl_end
 Description     : End control transfer
 Arguments       : uint16_t     status       : Transfer end status
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_ctrl_end(uint16_t status)
{
    g_usb_pstd_pipe0_request = USB_OFF;

    /* Interrupt disable */
    /* BEMP0 disable */
    hw_usb_clear_bempenb(USB_NULL, (uint16_t)USB_PIPE0);

    /* BRDY0 disable */
    hw_usb_clear_brdyenb(USB_NULL, (uint16_t)USB_PIPE0);

    /* NRDY0 disable */
    hw_usb_clear_nrdyenb(USB_NULL, (uint16_t)USB_PIPE0);

#if USB_CFG_USE_USBIP == USB_CFG_IP0
    hw_usb_set_mbw(USB_NULL, USB_CUSE, USB0_CFIFO_MBW);
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
    hw_usb_set_mbw(USB_NULL, USB_CUSE, USB1_CFIFO_MBW);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

    if ((USB_DATA_ERR == status) || (USB_DATA_OVR == status))
    {
        /* Request error */
        usb_pstd_set_stall_pipe0();
    }
    else if (USB_DATA_STOP == status)
    {
        /* Pipe stop */
        usb_cstd_set_nak(USB_NULL, (uint16_t)USB_PIPE0);
    }
    else
    {
        /* Set CCPL bit */
        hw_usb_pset_ccpl();
    }
} /* End of function usb_pstd_ctrl_end() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
