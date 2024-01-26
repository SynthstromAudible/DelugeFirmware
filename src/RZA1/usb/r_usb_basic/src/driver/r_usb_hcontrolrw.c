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
 * Copyright (C) 2014(2015) Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/
/***********************************************************************************************************************
 * File Name    : r_usb_hcontrolrw.c
 * Description  : USB Host Control read/write
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 04.01.2014 1.00 First Release
 *         : 30.01.2015 1.01    Added RX71M.
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
 Renesas USB FIFO Read/Write Host Driver API functions
 ***********************************************************************************************************************/

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
uint16_t g_usb_hstd_responce_counter;
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

/***********************************************************************************************************************
 Function Name   : usb_hstd_ctrl_write_start
 Description     : Start data stage of Control Write transfer.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint32_t     Bsize        : Data Size
                 : uint8_t      *Table       : Data Table Address
 Return          : uint16_t                  : USB_WRITEEND / USB_WRITING
                 :                           : USB_WRITESHRT / USB_FIFOERROR
 ***********************************************************************************************************************/
uint16_t usb_hstd_ctrl_write_start(usb_utr_t* ptr, uint32_t Bsize, uint8_t* Table)
{
    uint16_t end_flag;
    uint16_t toggle;

    /* PID=NAK & clear STALL */
    usb_cstd_clr_stall(ptr, (uint16_t)USB_PIPE0);
    g_usb_data_cnt[USB_PIPE0] = Bsize; /* Transfer size set */
    g_p_usb_data[USB_PIPE0]   = Table; /* Transfer address set */

    /* DCP Configuration Register  (0x5C) */
    hw_usb_write_dcpcfg(ptr, (USB_CNTMDFIELD | USB_DIRFIELD));
    hw_usb_set_sqset(ptr, USB_PIPE0); /* SQSET=1, PID=NAK */
    if (USB_DATAWRCNT == g_usb_hstd_ctsq[ptr->ip])
    {
        /* Next stage is Control read data stage */
        toggle = g_p_usb_pipe[USB_PIPE0]->pipectr;
        usb_hstd_do_sqtgl(ptr, (uint16_t)USB_PIPE0, toggle); /* Do pipe SQTGL */
    }

    hw_usb_clear_status_bemp(ptr, USB_PIPE0);

    /* Ignore count clear */
    g_usb_hstd_ignore_cnt[ptr->ip][USB_PIPE0] = (uint16_t)0;

    /* Host Control sequence */
    end_flag = usb_hstd_write_data(ptr, USB_PIPE0, USB_CUSE);

    switch (end_flag)
    {
        /* End of data write */
        case USB_WRITESHRT:

            /* Next stage is Control write status stage */
            g_usb_hstd_ctsq[ptr->ip] = USB_STATUSWR;

            /* Enable Empty Interrupt */
            hw_usb_set_bempenb(ptr, (uint16_t)USB_PIPE0);

            /* Enable Not Ready Interrupt */
            // usb_cstd_nrdy_enable(ptr, (uint16_t)USB_PIPE0);
            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
            //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver
            //  (in 2019).

            /* Set BUF */
            usb_cstd_set_buf(ptr, (uint16_t)USB_PIPE0);

            break;

        /* End of data write (not null) */
        case USB_WRITEEND:
        /* Continue of data write */
        case USB_WRITING:

            if (USB_SETUPWR == g_usb_hstd_ctsq[ptr->ip])
            {
                /* Next stage is Control read data stage */
                /* Next stage is Control write data stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_DATAWR;
            }
            else
            {
                /* Next stage is Control read data stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_DATAWRCNT;
            }
            /* Enable Empty Interrupt */
            hw_usb_set_bempenb(ptr, (uint16_t)USB_PIPE0);

            /* Enable Not Ready Interrupt */
            // usb_cstd_nrdy_enable(ptr, (uint16_t)USB_PIPE0);
            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
            //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver
            //  (in 2019).

            /* Set BUF */
            usb_cstd_set_buf(ptr, (uint16_t)USB_PIPE0);

            break;

        /* FIFO access error */
        case USB_FIFOERROR:
            break;

        default:
            break;
    }

    /* End or Err or Continue */
    return (end_flag);
} /* End of function usb_hstd_ctrl_write_start() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_ctrl_read_start
 Description     : Start data stage of Control Read transfer.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint32_t     Bsize        : Data Size
                 : uint8_t      *Table       : Data Table Address
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_ctrl_read_start(usb_utr_t* ptr, uint32_t Bsize, uint8_t* Table)
{
    uint16_t toggle;

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
    g_usb_hstd_responce_counter = 0;

    hw_usb_clear_sts_sofr(ptr);
    hw_usb_set_intenb(ptr, USB_SOFE);
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

    /* PID=NAK & clear STALL */
    usb_cstd_clr_stall(ptr, (uint16_t)USB_PIPE0);

    /* Transfer size set */
    g_usb_data_cnt[USB_PIPE0] = Bsize;

    /* Transfer address set */
    g_p_usb_data[USB_PIPE0] = Table;

    /* DCP Configuration Register  (0x5C) */
    hw_usb_write_dcpcfg(ptr, USB_SHTNAKFIELD);
    hw_usb_hwrite_dcpctr(ptr, USB_SQSET); /* SQSET=1, PID=NAK */
    if (USB_DATARDCNT == g_usb_hstd_ctsq[ptr->ip])
    {
        /* Next stage is Control read data stage */
        toggle = g_p_usb_pipe[USB_PIPE0]->pipectr;
        usb_hstd_do_sqtgl(ptr, (uint16_t)USB_PIPE0, toggle);
    }

    /* Host Control sequence */
    if (USB_SETUPRD == g_usb_hstd_ctsq[ptr->ip])
    {
        /* Next stage is Control read data stage */
        /* Next stage is Control read data stage */
        g_usb_hstd_ctsq[ptr->ip] = USB_DATARD;
    }
    else
    {
        /* Next stage is Control read data stage */
        g_usb_hstd_ctsq[ptr->ip] = USB_DATARDCNT;
    }

    /* Ignore count clear */
    g_usb_hstd_ignore_cnt[ptr->ip][USB_PIPE0] = (uint16_t)0;

    /* Interrupt enable */
    hw_usb_set_brdyenb(ptr, (uint16_t)USB_PIPE0); /* Enable Ready Interrupt */

    // usb_cstd_nrdy_enable(ptr, (uint16_t)USB_PIPE0); /* Enable Not Ready Interrupt */
    //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was causing
    //  freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in 2019).

    usb_cstd_set_buf(ptr, (uint16_t)USB_PIPE0); /* Set BUF */
} /* End of function usb_hstd_ctrl_read_start() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_status_start
 Description     : Start status stage of Control Command.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_status_start(usb_utr_t* ptr)
{
    uint16_t end_flag;
    uint8_t buf1[16];

    /* Interrupt Disable */
    /* BEMP0 Disable */
    hw_usb_clear_bempenb(ptr, (uint16_t)USB_PIPE0);

    /* BRDY0 Disable */
    hw_usb_clear_brdyenb(ptr, (uint16_t)USB_PIPE0);

    /* Transfer size set */
    g_p_usb_pipe[USB_PIPE0]->tranlen = g_usb_data_cnt[USB_PIPE0];

    /* Branch by the Control transfer stage management */
    switch (g_usb_hstd_ctsq[ptr->ip])
    {
        /* Control Read Data */
        case USB_DATARD:
        /* Control Read Data */
        case USB_DATARDCNT:

            /* Control read Status */
            g_usb_hstd_ctsq[ptr->ip] = USB_DATARD;
            /* Control write start */
            end_flag = usb_hstd_ctrl_write_start(ptr, (uint32_t)0, (uint8_t*)&buf1);
            if (USB_FIFOERROR == end_flag)
            {
                USB_PRINTF0("### FIFO access error \n");
                usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_ERR); /* Control Read/Write End */
            }
            else
            {
                /* Host Control sequence */
                /* Next stage is Control read status stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_STATUSRD;
            }

            break;

        /* Control Write Data */
        case USB_STATUSWR:
        /* NoData Control */
        case USB_SETUPNDC:

            /* Control Read Status */
            usb_hstd_ctrl_read_start(ptr, (uint32_t)0, (uint8_t*)&buf1);
            /* Host Control sequence */
            /* Next stage is Control write status stage */
            g_usb_hstd_ctsq[ptr->ip] = USB_STATUSWR;

            break;

        default:
            break;
    }
} /* End of function usb_hstd_status_start() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_ctrl_end
 Description     : Call the user registered callback function that notifies
                 : completion of a control transfer.
                 : Command.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     status       : Transfer status
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_ctrl_end(usb_utr_t* ptr, uint16_t status)
{
    /* Interrupt Disable */
    hw_usb_clear_bempenb(ptr, (uint16_t)USB_PIPE0); /* BEMP0 Disable */
    hw_usb_clear_brdyenb(ptr, (uint16_t)USB_PIPE0); /* BRDY0 Disable */
    hw_usb_clear_nrdyenb(ptr, (uint16_t)USB_PIPE0); /* NRDY0 Disable */

    usb_cstd_clr_stall(ptr, (uint16_t)USB_PIPE0); /* PID=NAK & clear STALL */
    if (USB_USBIP_0 == ptr->ip)
    {
        hw_usb_set_mbw(ptr, USB_CUSE, USB0_CFIFO_MBW);
    }
    else if (USB_USBIP_1 == ptr->ip)
    {
        hw_usb_set_mbw(ptr, USB_CUSE, USB1_CFIFO_MBW);
    }
    else
    {
        /* Non */
    }

    /* CSCLR=1, SUREQ=1, SQCLR=1, PID=NAK */
    hw_usb_hwrite_dcpctr(ptr, (uint16_t)(USB_CSCLR | USB_SUREQCLR | USB_SQCLR));

    /* CFIFO buffer clear */
    usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);
    hw_usb_set_bclr(ptr, USB_CUSE); /* Clear BVAL */
    usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, (uint16_t)USB_ISEL);
    hw_usb_set_bclr(ptr, USB_CUSE); /* Clear BVAL */

    /* Host Control sequence */
    if ((USB_CTRL_READING != status) && (USB_CTRL_WRITING != status))
    {
        /* Next stage is idle */
        g_usb_hstd_ctsq[ptr->ip] = USB_IDLEST;
    }

    g_p_usb_pipe[USB_PIPE0]->status  = status;
    g_p_usb_pipe[USB_PIPE0]->pipectr = hw_usb_read_pipectr(ptr, (uint16_t)USB_PIPE0);
    // g_p_usb_pipe[USB_PIPE0]->errcnt   = (uint8_t)g_usb_hstd_ignore_cnt[ptr->ip][USB_PIPE0];
    g_p_usb_pipe[USB_PIPE0]->ipp = ptr->ipp;
    g_p_usb_pipe[USB_PIPE0]->ip  = ptr->ip;

    /* Callback */
    if (USB_NULL != g_p_usb_pipe[USB_PIPE0])
    {
        if (USB_NULL != (g_p_usb_pipe[USB_PIPE0]->complete))
        {
            /* Process Done Callback */
            (g_p_usb_pipe[USB_PIPE0]->complete)(g_p_usb_pipe[USB_PIPE0], 0, 0);
        }
    }
    g_p_usb_pipe[USB_PIPE0] = (usb_utr_t*)USB_NULL;

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
    hw_usb_clear_enb_sofe(ptr);
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */
} /* End of function usb_hstd_ctrl_end() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_setup_start
 Description     : Start control transfer setup stage. (Set global function re-
                 : quired to start control transfer, and USB register).
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_setup_start(usb_utr_t* ptr)
{
    uint16_t segment;
    uint16_t dir;
    uint16_t setup_req;
    uint16_t setup_val;
    uint16_t setup_indx;
    uint16_t setup_leng;
    uint16_t* p_setup;

    segment = (uint16_t)(g_p_usb_pipe[USB_PIPE0]->segment);
    p_setup = g_p_usb_pipe[USB_PIPE0]->p_setup;

    setup_req  = *p_setup++; /* Set Request data */
    setup_val  = *p_setup++; /* Set wValue data */
    setup_indx = *p_setup++; /* Set wIndex data */
    setup_leng = *p_setup++; /* Set wLength data */

    /* Max Packet Size + Device Number select */
    hw_usb_write_dcpmxps(ptr, g_usb_hstd_dcp_register[ptr->ip][*p_setup]);

    /* Transfer Length check */

    /* Check Last flag */
    if ((uint16_t)USB_TRAN_END == segment)
    {
        if (g_p_usb_pipe[USB_PIPE0]->tranlen < setup_leng)
        {
            setup_leng = (uint16_t)g_p_usb_pipe[USB_PIPE0]->tranlen;
        }
    }
    if (setup_leng < g_p_usb_pipe[USB_PIPE0]->tranlen)
    {
        g_p_usb_pipe[USB_PIPE0]->tranlen = (uint32_t)setup_leng;
    }

    /* Control sequence setting */
    dir = (uint16_t)(setup_req & USB_BMREQUESTTYPEDIR);
    /* Check wLength field */
    if (0 == setup_leng)
    {
        /* Check Dir field */
        if (0 == dir)
        {
            /* Check Last flag */
            if ((uint16_t)USB_TRAN_END == segment)
            {
                /* Next stage is NoData control status stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_SETUPNDC;
            }
            else
            {
                /* Error */
                g_usb_hstd_ctsq[ptr->ip] = USB_IDLEST;
            }
        }
        else
        {
            /* Error */
            g_usb_hstd_ctsq[ptr->ip] = USB_IDLEST;
        }
    }
    else
    {
        /* Check Dir field */
        if (0 == dir)
        {
            /* Check Last flag */
            if ((uint16_t)USB_TRAN_END == segment)
            {
                /* Next stage is Control Write data stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_SETUPWR;
            }
            else
            {
                /* Next stage is Control Write data stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_SETUPWRCNT;
            }
        }
        else
        {
            /* Check Last flag */
            if ((uint16_t)USB_TRAN_END == segment)
            {
                /* Next stage is Control read data stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_SETUPRD;
            }
            else
            {
                /* Next stage is Control read data stage */
                g_usb_hstd_ctsq[ptr->ip] = USB_SETUPRDCNT;
            }
        }
    }

    /* Control transfer idle stage? */
    if (USB_IDLEST == g_usb_hstd_ctsq[ptr->ip])
    {
        /* Control Read/Write End */
        usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_STOP);
    }
    else
    {
        /* SETUP request set */
        /* Set Request data */
        hw_usb_hwrite_usbreq(ptr, setup_req);

        hw_usb_hset_usbval(ptr, setup_val);
        hw_usb_hset_usbindx(ptr, setup_indx);
        hw_usb_hset_usbleng(ptr, setup_leng);

        /* Ignore count clear */
        g_usb_hstd_ignore_cnt[ptr->ip][USB_PIPE0] = (uint16_t)0;

        hw_usb_hclear_sts_sign(ptr);
        hw_usb_hclear_sts_sack(ptr);
        hw_usb_hset_enb_signe(ptr);
        hw_usb_hset_enb_sacke(ptr);
        hw_usb_hset_sureq(ptr);
    }
} /* End of function usb_hstd_setup_start() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
