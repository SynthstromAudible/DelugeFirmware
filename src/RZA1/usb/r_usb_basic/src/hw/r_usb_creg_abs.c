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
 * File Name    : r_usb_creg_abs.c
 * Description  : Call USB register access function
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/system/iodefine.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_reg_access.h"
#include "deluge/drivers/uart/uart.h"

/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
uint16_t
    g_usb_hstd_use_pipe[USB_NUM_USBIP]; // Keeps a record of which pipes are in use, with one bit representing each pipe
#endif                                  /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

// Added by Rohan
#include "RZA1/mtu/mtu.h"
#include "definitions.h"
extern uint16_t pipeBufs[];
extern uint16_t pipeMaxPs[];
extern uint16_t pipeCfgs[];

/***********************************************************************************************************************
 Function Name   : usb_cstd_get_buf_size
 Description     : Return buffer size, or max packet size, of specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t                  : FIFO buffer size or max packet size.
 ***********************************************************************************************************************/
uint16_t usb_cstd_get_buf_size(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t size;
    uint16_t buffer;

    if (USB_PIPE0 == pipe)
    {
        buffer = hw_usb_read_dcpcfg(ptr);
        if ((buffer & USB_CNTMDFIELD) == USB_CFG_CNTMDON)
        {
            /* Continuation transmit */
            /* Buffer Size */
            size = USB_PIPE0BUF;
        }
        else
        {
            /* Not continuation transmit */
            buffer = hw_usb_read_dcpmaxp(ptr);

            /* Max Packet Size */
            size = (uint16_t)(buffer & USB_MAXP);
        }
    }
    else
    {
        /* Read CNTMD */
        if (pipeCfgs[pipe] & USB_CNTMDFIELD == USB_CFG_CNTMDON)
        {
            /* Buffer Size */
            size = (uint16_t)((uint16_t)((pipeBufs[pipe] >> USB_BUFSIZE_BIT) + 1) * USB_PIPEXBUF);
        }
        else
        {
            /* Max Packet Size */
            size = (uint16_t)(pipeMaxPs[pipe] & USB_MXPS);
        }
    }

    return (size);
} /* End of function usb_cstd_get_buf_size() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_pipe_init
 Description     : Initialization of registers associated with specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe Number
                 : uint16_t     *tbl         : Pointer to the endpoint table
                 : uint16_t     ofs          : Endpoint table offset
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_pipe_init(usb_utr_t* ptr, uint16_t pipe, uint16_t* tbl, uint16_t ofs)
{
    uint16_t useport = USB_CUSE;
    int ip; // Bugfix added by Rohan - we need this in case ptr is supplied as NULL, which happens always in peripheral
            // mode

    if (USB_NULL == ptr)
    {
        ip = USB_CFG_USE_USBIP; // Bugfix added by Rohan - we need this in case ptr is supplied as NULL, which happens
                                // always in peripheral mode
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        g_p_usb_pipe[pipe] = (usb_utr_t*)USB_NULL;
        useport            = usb_pstd_pipe2fport(pipe);

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    else
    {
        ip = ptr->ip; // Bugfix added by Rohan - we need this in case ptr is supplied as NULL, which happens always in
                      // peripheral mode
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        g_p_usb_pipe[pipe] = (usb_utr_t*)USB_NULL;
        useport            = usb_hstd_pipe2fport(ptr, pipe);

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    /* Interrupt Disable */
    /* Ready         Int Disable */
    hw_usb_clear_brdyenb(ptr, pipe);

    /* NotReady      Int Disable */
    hw_usb_clear_nrdyenb(ptr, pipe);

    /* Empty/SizeErr Int Disable */
    hw_usb_clear_bempenb(ptr, pipe);

    /* PID=NAK & clear STALL */
    usb_cstd_clr_stall(ptr, pipe);

    /* PIPE Configuration */
    hw_usb_write_pipesel(ptr, pipe);

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    /* Update use pipe no info */
    if (USB_NULL != tbl[ofs + 1])
    {
        g_usb_hstd_use_pipe[ip] |= ((uint16_t)1 << pipe); // Bugfix added by Rohan - we need this in case ptr is
                                                          // supplied as NULL, which happens always in peripheral mode
    }
    else
    {
        g_usb_hstd_use_pipe[ip] &=
            (~((uint16_t)1 << pipe)); // Bugfix added by Rohan - we need this in case ptr is supplied as NULL, which
                                      // happens always in peripheral mode
    }

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

    if ((USB_D0DMA == useport) || (USB_D1DMA == useport))
    {
        tbl[ofs + 1] |= USB_BFREON;
    }

    hw_usb_write_pipecfg(ptr, tbl[ofs + 1], pipe);
    hw_usb_write_pipebuf(ptr, tbl[ofs + 2], pipe);
    hw_usb_write_pipemaxp(ptr, tbl[ofs + 3], pipe);
    hw_usb_write_pipeperi(ptr, tbl[ofs + 4]);

    /* FIFO buffer DATA-PID initialized */
    hw_usb_write_pipesel(ptr, USB_PIPE0);

    /* SQCLR */
    hw_usb_set_sqclr(ptr, pipe);

    /* ACLRM */
    usb_cstd_do_aclrm(ptr, pipe);

    /* CSSTS */
    hw_usb_set_csclr(ptr, pipe);

    /* Interrupt status clear */
    /* Ready         Int Clear */
    hw_usb_clear_sts_brdy(ptr, pipe);

    /* NotReady      Int Clear */
    hw_usb_clear_status_nrdy(ptr, pipe);

    /* Empty/SizeErr Int Clear */
    hw_usb_clear_status_bemp(ptr, pipe);

    // hw_usb_set_bempenb(USB_NULL, pipe); // Added by Rohan - we now just enable this here once, rather than at the
    // start of every transfer. Actually no, doesn't work... though I thought it did for a bit...
} /* End of function usb_cstd_pipe_init() */

// Function by Rohan obviously
void change_destination_of_send_pipe(usb_utr_t* ptr, uint16_t pipe, uint16_t* tbl, int sq)
{

    /* PIPE Configuration */
    hw_usb_write_pipesel(NULL, pipe);

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
    uint16_t useport = USB_CUSE;
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    g_p_usb_pipe[pipe] = (usb_utr_t*)USB_NULL;
    useport            = usb_hstd_pipe2fport(ptr, pipe);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

    if ((USB_D0DMA == useport) || (USB_D1DMA == useport))
    {
        tbl[1] |= USB_BFREON;
    }
#endif

    hw_usb_write_pipecfg(NULL, tbl[1], pipe);
    // We won't set the PIPEBUF, cos I (Rohan) have made it so each pipe number always has the same buffer number, and
    // we're still on the same pipe.
    hw_usb_write_pipemaxp(
        NULL, tbl[3], pipe); // This sets the destination USB peripheral address, so yes we need to change this.
    hw_usb_write_pipeperi(NULL,
        tbl[4]); // Probably worth doing. This sets stuff relating to "flushing" and "intervals" - possibly more
                 // relevant to ISO or interrupt endpoints, but this driver does indeed work out a value to put for this
                 // in the table, so we'd better grab it

    /* SQCLR */
    volatile uint16_t* p_reg = ((uint16_t*)&(USB200.PIPE1CTR) + (pipe - 1));
    (*p_reg) |= (USB_SQCLR >> sq);

} /* End of function usb_cstd_pipe_init() */

// #include "drivers/usb/userdef/r_usb_hmidi_config.h"

/***********************************************************************************************************************
 Function Name   : usb_cstd_clr_pipe_cnfg
 Description     : Clear specified pipe configuration register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe_no      : pipe number
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_clr_pipe_cnfg(usb_utr_t* ptr, uint16_t pipe_no)
{

    // if (pipe_no == USB_CFG_HMIDI_BULK_SEND || pipe_no == USB_CFG_HMIDI_INT_SEND) return; // Don't deconfigure the
    // send-pipe cos it's shared - Rohan

    uartPrint("clearing config for pipe ");
    uartPrintNumber(pipe_no);

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        g_p_usb_pipe[pipe_no] = (usb_utr_t*)USB_NULL;
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        g_p_usb_pipe[pipe_no] = (usb_utr_t*)USB_NULL;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    /* PID=NAK & clear STALL */
    usb_cstd_clr_stall(ptr, pipe_no);

    /* Interrupt disable */
    /* Ready         Int Disable */
    hw_usb_clear_brdyenb(ptr, pipe_no);

    /* NotReady      Int Disable */
    hw_usb_clear_nrdyenb(ptr, pipe_no);

    /* Empty/SizeErr Int Disable */
    hw_usb_clear_bempenb(ptr, pipe_no);

    /* PIPE Configuration */
    usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);
    hw_usb_write_pipesel(ptr, pipe_no);

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    /* Clear use pipe no info. Added by Rohan - I can't see why this wouldn't have already been done, and the USB
     * library doesn't even make use of this info anyway, so modification should be fine. */
    g_usb_hstd_use_pipe[USB_CFG_USE_USBIP] &=
        (~((uint16_t)1 << pipe_no)); // Uses "hard-coded" USB_CFG_USE_USBIP because ptr is sometimes NULL
#endif                               /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

    hw_usb_write_pipecfg(ptr, 0, pipe_no);
    hw_usb_write_pipebuf(ptr, 0, pipe_no);
    hw_usb_write_pipemaxp(ptr, 0, pipe_no);
    hw_usb_write_pipeperi(ptr, 0);
    hw_usb_write_pipesel(ptr, 0);

    /* FIFO buffer DATA-PID initialized */
    /* SQCLR */
    hw_usb_set_sqclr(ptr, pipe_no);

    /* ACLRM */
    usb_cstd_do_aclrm(ptr, pipe_no);

    /* CSSTS */
    hw_usb_set_csclr(ptr, pipe_no);
    usb_cstd_clr_transaction_counter(ptr, pipe_no);

    /* Interrupt status clear */
    /* Ready         Int Clear */
    hw_usb_clear_sts_brdy(ptr, pipe_no);

    /* NotReady      Int Clear */
    hw_usb_clear_status_nrdy(ptr, pipe_no);

    /* Empty/SizeErr Int Clear */
    hw_usb_clear_status_bemp(ptr, pipe_no);
} /* End of function usb_cstd_clr_pipe_cnfg() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_set_nak
 Description     : Set up to NAK the specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe Number
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_set_nak(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t buf;
    uint16_t n;

    /* Set NAK */
    hw_usb_clear_pid(ptr, pipe, (uint16_t)USB_PID_BUF);

    /* The state of PBUSY continues while transmitting the packet when it is a detach. */
    /* 1ms comes off when leaving because the packet duration might not exceed 1ms.  */
    /* Whether it is PBUSY release or 1ms passage can be judged. */
    for (n = 0; n < 0xFFFFu; ++n)
    {
        /* PIPE control reg read */
        buf = hw_usb_read_pipectr(ptr, pipe);
        if ((uint16_t)(buf & USB_PBUSY) == 0)
        {
            n = 0xFFFEu;
        }
    }
} /* End of function usb_cstd_set_nak() */

// For non-zero pipe; no pointer
void usb_cstd_set_nak_fast_rohan(uint16_t pipe)
{
    uint16_t n;

    /* Set NAK */
    volatile uint16_t* p_reg = ((uint16_t*)&(USB200.PIPE1CTR) + (pipe - 1));
    (*p_reg) &= (~(uint16_t)USB_PID_BUF);

    // A bit of a weird one... my copying and simplifying all this stuff into one function caused i029 (unknown
    // interrupt) error when sending MIDI. It seems that we actually need a tiny ~1uS delay here. I couldn't work out
    // just why that would be required. And the kinda cryptic original comment in Renesas's code didn't really help.
#if 0
	uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
	while (true) {
		uint16_t stopTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
		uint16_t duration = stopTime - startTime;
		if (duration >= 40) break; // 30 is too low
	}
#endif

    // And then there seems no need to check PBUSY anymore...
#if 0
    /* The state of PBUSY continues while transmitting the packet when it is a detach. */
    /* 1ms comes off when leaving because the packet duration might not exceed 1ms.  */
    /* Whether it is PBUSY release or 1ms passage can be judged. */
	for (n = 0; n < 0xFFFFu; ++n)
	{
        uint16_t    buf;
		/* PIPE control reg read */
		buf = *p_reg;
		if ((uint16_t)(buf & USB_PBUSY) == 0) break;
	}
#endif
}

// I've optimized this copy much better than the original.
// usb_cstd_is_set_frdy(USB_NULL, pipe, USB_CUSE, USB_FALSE);
uint16_t usb_cstd_is_set_frdy_rohan(uint16_t pipe)
{

    /* Changes the FIFO port by the pipe. */
    // usb_cstd_chg_curpipe(USB_NULL, pipe, USB_CUSE, USB_FALSE);
    usb_cstd_chg_curpipe_rohan_fast(pipe);

    volatile uint16_t* p_reg;
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    p_reg = (void*)&(USB200.CFIFOCTR);
#else
    p_reg = (void*)&(USB201.CFIFOCTR);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

    // I drastically rearranged the code from usb_cstd_is_set_frdy(), here. Basically, it's meant to time out with error
    // if not FRDY within 100ns.
    uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];

    uint16_t buffer;

    while (true)
    {
        buffer = *p_reg;
        if (buffer & USB_FRDY)
        {
            return buffer;
        }
        uint16_t timeNow    = *TCNT[TIMER_SYSTEM_SUPERFAST];
        uint16_t timePassed = timeNow - startTime;
        if (timePassed >= 5)
        { // Definitely more than 100ns then. (It's 29.5928 nanoseconds per tick.)
            break;
        }
    }

    return USB_FIFOERROR;
}

/***********************************************************************************************************************
 Function Name   : usb_cstd_is_set_frdy
 Description     : Changes the specified FIFO port by the specified pipe.
 : Please change the wait time for your MCU.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe Number
                 : uint16_t     fifosel      : FIFO select
                 : uint16_t     isel         : ISEL bit status
 Return value    : FRDY status
 ***********************************************************************************************************************/
uint16_t usb_cstd_is_set_frdy(usb_utr_t* ptr, uint16_t pipe, uint16_t fifosel, uint16_t isel)
{
    uint16_t buffer;
    uint16_t i;

    /* Changes the FIFO port by the pipe. */
    usb_cstd_chg_curpipe(ptr, pipe, fifosel, isel);

    for (i = 0; i < 4; i++)
    {
        buffer = hw_usb_read_fifoctr(ptr, fifosel);

        if ((uint16_t)(buffer & USB_FRDY) == USB_FRDY)
        {
            return (buffer);
        }
        USB_PRINTF1("*** FRDY wait pipe = %d\n", pipe);

        /* Caution!!!
         * Depending on the external bus speed of CPU, you may need to wait
         * for 100ns here.
         * For details, please look at the data sheet.   */
        /***** The example of reference. *****/
        buffer = hw_usb_read_syscfg(ptr, USB_PORT0);
        buffer = hw_usb_read_syssts(ptr, USB_PORT0);

        /*************************************/
    }
    return (USB_FIFOERROR);
} /* End of function usb_cstd_is_set_frdy() */

extern uint16_t fifoSels[];

/***********************************************************************************************************************
 Function Name   : usb_cstd_chg_curpipe
 Description     : Switch FIFO and pipe number.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     fifosel      : FIFO selected (CPU, D0, D1..)
                 : uint16_t     isel         : CFIFO Port Access Direction.(Pipe1 to 9:Set to 0)
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_chg_curpipe(usb_utr_t* ptr, uint16_t pipe, uint16_t fifosel, uint16_t isel)
{
    uint16_t buffer;

    /* Select FIFO */
    switch (fifosel)
    {
        case USB_CUSE: /* CFIFO use */

            if ((fifoSels[USB_CUSE] & (uint16_t)(USB_ISEL | USB_CURPIPE)) == (uint16_t)(isel | pipe))
                break;

            /* ISEL=1, CURPIPE=0 */
            hw_usb_rmw_fifosel(ptr, USB_CUSE, ((USB_RCNT | isel) | pipe), ((USB_RCNT | USB_ISEL) | USB_CURPIPE));
            do
            {
                buffer = hw_usb_read_fifosel(ptr, USB_CUSE);
            } while ((buffer & (uint16_t)(USB_ISEL | USB_CURPIPE)) != (uint16_t)(isel | pipe));

            break;

        case USB_D0USE: /* D0FIFO use */

            /* continue */

            break;

        case USB_D1USE: /* D1FIFO use */

            /* continue */

            break;

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))

        case USB_D0DMA: /* D0FIFO DMA */

            /* D0FIFO pipe select */
            hw_usb_set_curpipe(ptr, USB_D0DMA, pipe);
            do
            {
                buffer = hw_usb_read_fifosel(ptr, USB_D0DMA);
            } while ((uint16_t)(buffer & USB_CURPIPE) != pipe);

            break;

        case USB_D1DMA: /* D1FIFO DMA */

            /* D1FIFO pipe select */
            hw_usb_set_curpipe(ptr, USB_D1DMA, pipe);

            do
            {
                buffer = hw_usb_read_fifosel(ptr, USB_D1DMA);
            } while ((uint16_t)(buffer & USB_CURPIPE) != pipe);

            break;
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

        default:
            break;
    }
} /* End of function usb_cstd_chg_curpipe() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_set_transaction_counter
 Description     : Set specified Pipe Transaction Counter Register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     trnreg       : Pipe number
                 : uint16_t     trncnt       : Transaction counter
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_set_transaction_counter(usb_utr_t* ptr, uint16_t trnreg, uint16_t trncnt)
{
    hw_usb_set_trclr(ptr, trnreg);
    hw_usb_write_pipetrn(ptr, trnreg, trncnt);
    hw_usb_set_trenb(ptr, trnreg);
} /* End of function usb_cstd_set_transaction_counter() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_clr_transaction_counter
 Description     : Clear specified Pipe Transaction Counter Register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     trnreg       : Pipe Number
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_clr_transaction_counter(usb_utr_t* ptr, uint16_t trnreg)
{
    hw_usb_clear_trenb(ptr, trnreg);
    hw_usb_set_trclr(ptr, trnreg);
} /* End of function usb_cstd_clr_transaction_counter() */

/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
