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
 * File Name    : r_usb_preg_abs.c
 * Description  : Call USB Peripheral register access function
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

/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/
uint16_t g_usb_cstd_suspend_mode = USB_NORMAL_MODE;

#include "RZA1/system/iodefines/dmac_iodefine.h"
#include "definitions.h"

extern uint32_t timeLastBRDY[];

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Function Name   : usb_pstd_interrupt_handler
 Description     : Determine which USB interrupt occurred and report results to
                 : the usb_utr_t argument's ipp, type, and status members.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
int usb_pstd_interrupt_handler(uint16_t* type, uint16_t* status)
{
    uint16_t intsts0;

    /* Register Save */
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    intsts0 = USB200.INTSTS0;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */

#if USB_CFG_USE_USBIP == USB_CFG_IP1
    intsts0 = USB201.INTSTS0;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

    *type   = USB_INT_UNKNOWN;
    *status = 0;

    // Do quick check - it might be that no interrupt type is showing at all.
    if ((intsts0 & (USB_VBINT | USB_RESM | USB_SOFR | USB_DVST | USB_CTRT | USB_BEMP | USB_NRDY | USB_BRDY)) == 0u)
    {
        return 1; // All dealt with
    }

    uint16_t intenb0;

#if USB_CFG_USE_USBIP == USB_CFG_IP0
    intenb0 = USB200.INTENB0;
#else
    intenb0 = USB201.INTENB0;
#endif

    /* Interrupt status get */
    uint16_t ists0 = (uint16_t)(intsts0 & intenb0);

    /***** Processing PIPE data *****/

    // BEMP interrupt - is this buffer empty, as in we've finished sending a transfer - I think? Rohan
    if (ists0 & USB_BEMP)
    {

        uint16_t bempsts;
        // uint16_t    bempenb;

#if USB_CFG_USE_USBIP == USB_CFG_IP0
        bempsts = USB200.BEMPSTS;
        // bempenb = USB200.BEMPENB;
#else
        bempsts = USB201.BEMPSTS;
        // bempenb = USB201.BEMPENB;
#endif

        // uint16_t ests  = (uint16_t)(bempsts & bempenb);
        uint16_t ests = bempsts;

        // Pipe 0
        if (ests & USB_BEMP0)
        {
#if USB_CFG_USE_USBIP == USB_CFG_IP0
            USB200.BEMPSTS = (uint16_t)~USB_BEMP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            USB201.BEMPSTS = (uint16_t)~USB_BEMP0;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            *type   = USB_INT_BEMP0;
            *status = USB_BEMP0;
        }

        // Other pipes
        else
        {
#if USB_CFG_USE_USBIP == USB_CFG_IP0
            USB200.BEMPSTS = (uint16_t)~ests;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            USB201.BEMPSTS = (uint16_t)~ests;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            *type   = USB_INT_BEMP;
            *status = ests;
        }
    }

    // BRDY interrupt. Usually means a data receive is complete. Rohan
    else if (ists0 & USB_BRDY)
    {

        uint16_t brdysts;
        // uint16_t    brdyenb;

#if USB_CFG_USE_USBIP == USB_CFG_IP0
        brdysts = USB200.BRDYSTS;
        // brdyenb = USB200.BRDYENB;
#else
        brdysts = USB201.BRDYSTS;
        // brdyenb = USB201.BRDYENB;
#endif

        // uint16_t bsts  = (uint16_t)(brdysts & brdyenb);
        uint16_t bsts = brdysts;

        // Pipe 0
        if (bsts & USB_BRDY0)
        {
#if USB_CFG_USE_USBIP == USB_CFG_IP0
            USB200.BRDYSTS = (uint16_t)~USB_BRDY0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            USB201.BRDYSTS = (uint16_t)~USB_BRDY0;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            *type   = USB_INT_BRDY0;
            *status = USB_BRDY0;
        }

        // Other pipes
        else
        {

            // brdyOccurred(0); // Records exact time of message receipt
            timeLastBRDY[0] = DMACnNonVolatile(SSI_TX_DMA_CHANNEL).CRSA_n;

#if USB_CFG_USE_USBIP == USB_CFG_IP0
            USB200.BRDYSTS = (uint16_t)~bsts;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            USB201.BRDYSTS = (uint16_t)~bsts;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
            *type   = USB_INT_BRDY;
            *status = bsts;
        }
    }

    // NRDY interrupt. I think this gets generated each time the host requests some data, but we just happen not to have
    // any to send. Rohan
    else if (ists0 & USB_NRDY)
    {
        uint16_t nrdysts;
        // uint16_t    nrdyenb;

#if USB_CFG_USE_USBIP == USB_CFG_IP0
        nrdysts = USB200.NRDYSTS;
        // nrdyenb = USB200.NRDYENB;
#else
        nrdysts = USB201.NRDYSTS;
        // nrdyenb = USB201.NRDYENB;
#endif
        // uint16_t nsts  = (uint16_t)(nrdysts & nrdyenb);
        uint16_t nsts = nrdysts;

        // Ok, disregard everything else below - NRDY interrupts never actually need dealing with - unless we have ISO
        // endpoints, which MIDI never does. We'll just clear the interrupt and get out. I've actually disabled NRDY
        // interrupts for my own code, but it seems many other parts of the USB library enable them.
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.NRDYSTS = (uint16_t)~nsts;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        USB201.NRDYSTS = (uint16_t)~nsts;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */

        return 1; // All dealt with

#if 0
    	// Pipe0
    	if (nsts & USB_NRDY0) {
#if USB_CFG_USE_USBIP == USB_CFG_IP0
			USB200.NRDYSTS = (uint16_t)~USB_NRDY0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
			USB201.NRDYSTS = (uint16_t)~USB_NRDY0;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
			*type   = USB_INT_NRDY;
			*status = USB_NRDY0;
    	}

    	// Other pipes
    	else {
#if USB_CFG_USE_USBIP == USB_CFG_IP0
			USB200.NRDYSTS = (uint16_t)~nsts;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
			USB201.NRDYSTS = (uint16_t)~nsts;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
			*type   = USB_INT_NRDY;
			*status = nsts;


			// Rohan's insane thing: OK, so I *think*, possibly, this gets generated each time the host requests some data, but we just happen not to have any to send - we're "not ready".
			// We can very often dismiss this right away, and should do so because these can otherwise stack up faster than we'll deal with them when audio processing is heavy.
			// Should I maybe just deactivate NRDY interrupts? They seem to come all the time...

			int allDealtWithHere = 1; // Until otherwise decided, below

			// Actually I'll just deactivate all of this - the only time anything needs dealing with later is when using ISO endpoints, which MIDI doesn't use.
#if 0
			// Logic from usb_pstd_nrdy_pipe_process();
			uint16_t    buffer;
			uint16_t    i;

			for (i = USB_MIN_PIPE_NO; i <= USB_MAX_PIPE_NO; i++)
			{
				if ((nsts & USB_BITSET(i)) != 0)
				{
					/* Interrupt check */
					if (USB_NULL != g_p_usb_pipe[i])
					{
						if (usb_cstd_get_pipe_type_from_memory(i) == USB_TYPFIELD_ISO)
						{
							allDealtWithHere = 0;
						}
						else
						{
							/* Non processing. */
							*status &= (1 << i); // Remember that this pipe is all sorted
						}
					}
				}
			}
#endif
			return allDealtWithHere;
    	}
#endif
    }

    /***** Processing USB bus signal *****/
    /***** Resume signal *****/
    else if (ists0 & USB_RESM)
    {
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTSTS0 = (uint16_t)~USB_RESM;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        USB201.INTSTS0 = (uint16_t)~USB_RESM;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        *type = USB_INT_RESM;
    }
    /***** Vbus change *****/
    else if (ists0 & USB_VBINT)
    {
        /* Status clear */
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTSTS0 = (uint16_t)~USB_VBINT;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        USB201.INTSTS0 = (uint16_t)~USB_VBINT;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        *type = USB_INT_VBINT;
    }
    /***** SOFR change *****/
    else if (ists0 & USB_SOFR)
    {
        /* SOFR Clear */
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTSTS0 = (uint16_t)~USB_SOFR;
#else             /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        USB201.INTSTS0 = (uint16_t)~USB_SOFR;
#endif            /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        return 1; // SOFR interrupts don't result in any action when processed. Rohan
        *type = USB_INT_SOFR;
    }

    /***** Processing device state *****/
    /***** DVST change *****/
    else if (ists0 & USB_DVST)
    {
        /* DVST clear */
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTSTS0 = (uint16_t)~USB_DVST;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        USB201.INTSTS0 = (uint16_t)~USB_DVST;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        *type   = USB_INT_DVST;
        *status = intsts0;
    }

    /***** Processing setup transaction *****/
    else if (ists0 & USB_CTRT)
    {
        /* CTSQ bit changes later than CTRT bit for ASSP. */
        /* CTSQ reloading */
        *status = hw_usb_read_intsts();
        /* USB_CTRT clear */
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTSTS0 = (uint16_t)~USB_CTRT;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        USB201.INTSTS0 = (uint16_t)~USB_CTRT;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
        *type = USB_INT_CTRT;

        if (USB_CS_SQER == (uint8_t)((*status) & USB_CTSQ))
        {
            hw_usb_pclear_sts_valid();
            return 1; // By Rohan
            *type   = USB_INT_UNKNOWN;
            *status = 0;
        }
    }

    else
    {
        /* Non processing. */
        return 1; // By Rohan
    }

    return 0;
} /* End of function usb_pstd_interrupt_handler() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_save_request
 Description     : Save received USB command.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_save_request(void)
{
    /* Valid clear */
    hw_usb_pclear_sts_valid();

    g_usb_pstd_req_type   = hw_usb_read_usbreq();
    g_usb_pstd_req_value  = hw_usb_read_usbval();
    g_usb_pstd_req_index  = hw_usb_read_usbindx();
    g_usb_pstd_req_length = hw_usb_read_usbleng();
} /* End of function usb_pstd_save_request() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_chk_configured
 Description     : Check if USB Device is in a CONFIGURED state.
 Arguments       : none
 Return value    : Configuration state (YES/NO)
 ***********************************************************************************************************************/
uint16_t usb_pstd_chk_configured(void)
{
    uint16_t buf;

    buf = hw_usb_read_intsts();

    /* Device Status - Configured check */
    if (USB_DS_CNFG == (buf & USB_DVSQ))
    {
        /* Configured */
        return USB_TRUE;
    }
    else
    {
        /* not Configured */
        return USB_FALSE;
    }
} /* End of function usb_pstd_chk_configured() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_remote_wakeup
 Description     : Set the USB peripheral to implement remote wake up.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_remote_wakeup(void)
{
    uint16_t buf;

    /* Support remote wakeup ? */
    if (USB_TRUE == g_usb_pstd_remote_wakeup)
    {
        /* RESM interrupt disable */
        hw_usb_pclear_enb_rsme();

        /* RESM status read */
        buf = hw_usb_read_intsts(); /* usb_creg_read_intsts(); */
        if ((uint16_t)0 != (buf & USB_RESM))
        {
            /* RESM status clear */
            hw_usb_pclear_sts_resm();
        }
        else
        {
            if ((uint16_t)0 != (buf & USB_DS_SUSP))
            {
                /* Remote wakeup set */
                hw_usb_pset_wkup();
            }
        }
    }
} /* End of function usb_pstd_remote_wakeup() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_test_mode
 Description     : USB Peripheral test mode function.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_test_mode(void)
{
    switch ((uint16_t)(g_usb_pstd_test_mode_select & USB_TEST_SELECT))
    {
        case USB_TEST_J:
        case USB_TEST_K:
        case USB_TEST_SE0_NAK:
        case USB_TEST_PACKET:

            hw_usb_set_utst(USB_NULL, 0);
            hw_usb_set_utst(USB_NULL, (uint16_t)(g_usb_pstd_test_mode_select >> 8));

            break;

        case USB_TEST_FORCE_ENABLE:
        default:
            break;
    }
} /* End of function usb_pstd_test_mode() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_resume_process
 Description     : Set USB registers to implement resume processing.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_resume_process(void)
{
    /* RESM status clear */
    hw_usb_pclear_sts_resm();

    /* RESM interrupt disable */
    hw_usb_pclear_enb_rsme();
} /* End of function usb_pstd_resume_process() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_set_stall
 Description     : Set the specified pipe's PID to STALL.
 Arguments       : uint16_t     pipe         : Pipe Number
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_set_stall(uint16_t pipe)
{
    /* PIPE control reg set */
    hw_usb_set_pid(USB_NULL, pipe, USB_PID_STALL);
} /* End of function usb_pstd_set_stall() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_set_stall_pipe0
 Description     : Set pipe "0" PID to STALL.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_set_stall_pipe0(void)
{
    /* PIPE control reg set */
    hw_usb_set_pid(USB_NULL, USB_PIPE0, USB_PID_STALL);
} /* End of function usb_pstd_set_stall_pipe0() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_write_fifo
 Description     : Write specified amount of data to specified USB FIFO.
 Arguments       : uint16_t     count        : Write size.
                 : uint16_t     pipemode     : The mode of CPU/DMA(D0)/DMA(D1).
                 : uint16_t     *write_p     : Address of buffer of data to write.
 Return value    : The incremented address of last argument (write_p).
 ***********************************************************************************************************************/
uint8_t* usb_pstd_write_fifo(uint16_t count, uint16_t pipemode, uint8_t* write_p)
{
    uint16_t even;
    uint16_t odd;

    for (even = (uint16_t)(count >> 2); (even != 0); --even)
    {
        /* 32bit access */
        hw_usb_write_fifo32(USB_NULL, pipemode, *((uint32_t*)write_p));

        /* Renewal write pointer */
        write_p += sizeof(uint32_t);
    }
    odd = count & 3;
    if (odd)
    {
        if ((odd & (uint16_t)0x0002u) != 0u)
        {
            /* 16bit access */
            /* Change FIFO access width */
            hw_usb_set_mbw(USB_NULL, pipemode, USB_MBW_16);
            /* FIFO write */
            hw_usb_write_fifo16(USB_NULL, pipemode, *((uint16_t*)write_p));

            /* Renewal write pointer */
            write_p += sizeof(uint16_t);
        }
        if ((odd & (uint16_t)0x0001u) != 0u)
        {
            /* 8bit access */
            /* count == odd */
            /* Change FIFO access width */
            hw_usb_set_mbw(USB_NULL, pipemode, USB_MBW_8);

            /* FIFO write */
            hw_usb_write_fifo8(USB_NULL, pipemode, *write_p);

            /* Renewal write pointer */
            write_p++;
        }
        /* Return FIFO access width */
        hw_usb_set_mbw(USB_NULL, pipemode, USB_MBW_32);
    }

    return write_p;

} /* End of function usb_pstd_write_fifo() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_read_fifo
 Description     : Read specified buffer size from the USB FIFO.
 Arguments       : uint16_t     count        : Read size.
                 : uint16_t     pipemode     : The mode of CPU/DMA(D0)/DMA(D1).
                 : uint16_t     *write_p     : Address of buffer to store the read data.
 Return value    : Pointer to a buffer that contains the data to be read next.
 ***********************************************************************************************************************/
uint8_t* usb_pstd_read_fifo(uint16_t count, uint16_t pipemode, uint8_t* read_p)
{
    uint16_t even;
    uint16_t odd;
    uint32_t odd_byte_data_temp;

    for (even = (uint16_t)(count >> 2); (even != 0); --even)
    {
        /* 32bit FIFO access */
        *(uint32_t*)read_p = hw_usb_read_fifo32(USB_NULL, pipemode);

        /* Renewal read pointer */
        read_p += sizeof(uint32_t);
    }
    odd = count & 3;

    if (odd != 0)
    {
        /* 32bit FIFO access */
        odd_byte_data_temp = hw_usb_read_fifo32(USB_NULL, pipemode);
        /* Condition compilation by the difference of the endian */
        do
        {
            *read_p            = (uint8_t)(odd_byte_data_temp & 0x000000ff);
            odd_byte_data_temp = odd_byte_data_temp >> 8;
            /* Renewal read pointer */
            read_p += sizeof(uint8_t);
            odd--;
        } while (odd != 0);
    }

    return read_p;
} /* End of function usb_pstd_read_fifo() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_forced_termination
 Description     : Terminate data transmission and reception.
 Arguments       : uint16_t     pipe         : Pipe Number
                 : uint16_t     status       : Transfer status type
 Return value    : none
 Note            : In the case of timeout status, it does not call back.
 ***********************************************************************************************************************/
void usb_pstd_forced_termination(uint16_t pipe, uint16_t status)
{
    uint16_t useport;

    /* PID = NAK */
    usb_cstd_set_nak(USB_NULL, pipe); /* Set NAK */

    /* Disable Interrupt */
    hw_usb_clear_brdyenb(USB_NULL, pipe); /* Disable Ready Interrupt */
    hw_usb_clear_nrdyenb(USB_NULL, pipe); /* Disable Not Ready Interrupt */
    hw_usb_clear_bempenb(USB_NULL, pipe); /* Disable Empty Interrupt */

    usb_cstd_clr_transaction_counter(USB_NULL, pipe);

    /* Pipe number to FIFO port select */
    useport = usb_pstd_pipe2fport(pipe);
    /* Check use FIFO access */
    switch (useport)
    {
        case USB_CUSE: /* CFIFO use */

#if USB_CFG_USE_USBIP == USB_CFG_IP0
            hw_usb_set_mbw(USB_NULL, USB_CUSE, USB0_CFIFO_MBW);
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
            hw_usb_set_mbw(USB_NULL, USB_CUSE, USB1_CFIFO_MBW);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
            /* Changes the FIFO port by the pipe. */
            usb_cstd_chg_curpipe(USB_NULL, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);

            break;

        default:
            break;
    }

    /* Do Aclr */
    usb_cstd_do_aclrm(USB_NULL, pipe);

    /* FIFO buffer SPLIT transaction initialized */
    usb_cstd_chg_curpipe(USB_NULL, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_NOUSE);
    hw_usb_set_csclr(USB_NULL, pipe);

    /* Call Back */
    if (USB_NULL != g_p_usb_pipe[pipe])
    {
        /* Transfer information set */
        g_p_usb_pipe[pipe]->tranlen = g_usb_data_cnt[pipe];
        g_p_usb_pipe[pipe]->status  = status;
        g_p_usb_pipe[pipe]->pipectr = hw_usb_read_pipectr(USB_NULL, pipe);

        if (USB_NULL != (g_p_usb_pipe[pipe]->complete))
        {
            (g_p_usb_pipe[pipe]->complete)(g_p_usb_pipe[pipe], USB_NULL, USB_NULL);
        }

        g_p_usb_pipe[pipe] = (usb_utr_t*)USB_NULL;
    }
} /* End of function usb_pstd_forced_termination() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_interrupt_clock
 Description     : Not processed as the functionality is provided by R8A66597(ASSP).
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_interrupt_clock(void)
{
    if (USB_NORMAL_MODE != g_usb_cstd_suspend_mode)
    {
        hw_usb_set_suspendm(); /* UTMI Normal Mode (Not Suspend Mode) */
        usb_cpu_delay_1us(100);
        g_usb_cstd_suspend_mode = USB_NORMAL_MODE;
    }
} /* End of function usb_pstd_interrupt_clock() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_self_clock
 Description     : Not processed as the functionality is provided by R8A66597(ASSP).
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_self_clock(void)
{
    if (USB_NORMAL_MODE != g_usb_cstd_suspend_mode)
    {
        hw_usb_set_suspendm(); /* UTMI Normal Mode (Not Suspend Mode) */
        usb_cpu_delay_1us(100);
        g_usb_cstd_suspend_mode = USB_NORMAL_MODE;
    }
} /* End of function usb_pstd_self_clock() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_stop_clock
 Description     : Not processed as the functionality is provided by R8A66597(ASSP).
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_stop_clock(void)
{
    g_usb_cstd_suspend_mode = USB_SUSPEND_MODE;
    hw_usb_clear_suspm(); /* UTMI Suspend Mode */
} /* End of function usb_pstd_stop_clock() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
