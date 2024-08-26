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
 * File Name    : r_usb_hreg_abs.c
 * Description  : Call USB Host register access function
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

#include "deluge/deluge.h"

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : usb_hstd_set_hub_port
 Description     : Set up-port hub
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device address
                 : uint16_t     upphub       : Up-port hub address
                 : uint16_t     hubport      : Hub port number
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_set_hub_port(usb_utr_t* ptr, uint16_t addr, uint16_t upphub, uint16_t hubport)
{
    hw_usb_hrmw_devadd(ptr, addr, (upphub | hubport), (uint16_t)(USB_UPPHUB | USB_HUBPORT));
} /* End of function usb_hstd_set_hub_port */

uint8_t anythingEverAttachedAsUSBHost = 0;

#include "definitions.h"
extern uint32_t timeLastBRDY[];

/***********************************************************************************************************************
 Function Name   : usb_hstd_interrupt_handler
 Description     : Analyzes which USB interrupt is generated
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return          : none
 ***********************************************************************************************************************/
// Returns whether everything was just taken care of here. Rohan
int usb_hstd_interrupt_handler(usb_utr_t* ptr)
{
    uint16_t intsts0;
    uint16_t intenb0;
    uint16_t ists0;

    intsts0 = ptr->ipp->INTSTS0;
    intenb0 = ptr->ipp->INTENB0;

    /* Interrupt Status Get */
    ptr->keyword = USB_INT_UNKNOWN;
    ptr->status  = 0;

    ists0 = (uint16_t)(intsts0 & intenb0);
    /*  ists2 = (uint16_t)(intsts2 & intenb2);*/

    /***** Processing PIPE0-MAX_PIPE_NO data *****/
    if (ists0 & USB_BRDY) /***** EP0-7 BRDY *****/ // Usually means a data receive is complete. Rohan
    {

        uint16_t brdysts;
        uint16_t brdyenb;
        uint16_t bsts;

        brdysts = ptr->ipp->BRDYSTS;
        // brdyenb = ptr->ipp->BRDYENB;
        bsts = brdysts; //(uint16_t)(brdysts & brdyenb);

        // Pipe 0
        if (bsts & USB_BRDY0)
        {
            ptr->ipp->BRDYSTS = (uint16_t)(~USB_BRDY0);
            ptr->keyword      = USB_INT_BRDY0;
            ptr->status       = USB_BRDY0;
        }

        // Other pipes
        else
        {
            // brdyOccurred(0); // Records exact time of message receipt
            timeLastBRDY[0] = DMACnNonVolatile(SSI_TX_DMA_CHANNEL).CRSA_n;

            ptr->ipp->BRDYSTS = (uint16_t)(~bsts & BRDYSTS_MASK);
            ptr->keyword      = USB_INT_BRDY;
            ptr->status       = bsts;
        }
    }
    else if (ists0 & USB_BEMP) /***** EP0-7 BEMP *****/
    {

        uint16_t bempsts;
        uint16_t bempenb;
        uint16_t ests;

        bempsts = ptr->ipp->BEMPSTS;
        // bempenb = ptr->ipp->BEMPENB;
        ests = bempsts; //(uint16_t)(bempsts & bempenb);

        // Pipe 0
        if (ests & USB_BEMP0)
        {
            ptr->ipp->BEMPSTS = (uint16_t)(~USB_BEMP0);
            ptr->keyword      = USB_INT_BEMP0;
            ptr->status       = USB_BEMP0;
        }

        // Other pipes
        else
        {
            ptr->ipp->BEMPSTS = (uint16_t)(~ests & BEMPSTS_MASK);
            ptr->keyword      = USB_INT_BEMP;
            ptr->status       = ests;
        }
    }
    else if (ists0 & USB_NRDY) /***** EP0-7 NRDY *****/
    {
        uint16_t nrdysts;
        uint16_t nrdyenb;
        uint16_t nsts;

        nrdysts = ptr->ipp->NRDYSTS;
        nrdyenb = ptr->ipp->NRDYENB;
        nsts    = (uint16_t)(nrdysts & nrdyenb);

        ptr->ipp->NRDYSTS = (uint16_t)(~nsts & NRDYSTS_MASK);
        ptr->keyword      = USB_INT_NRDY;
        ptr->status       = nsts;
    }

    /***** Processing VBUS/SOF *****/
    else if (ists0 & USB_VBINT) /***** VBUS change *****/
    {
        /* Status Clear */
        ptr->ipp->INTSTS0 = (uint16_t)~USB_VBINT;
        ptr->keyword      = USB_INT_VBINT;
    }
    else if (ists0 & USB_SOFR) /***** SOFR change *****/
    {
        /* SOFR Clear */
        ptr->ipp->INTSTS0 = (uint16_t)~USB_SOFR;
        ptr->keyword      = USB_INT_SOFR;
    }

    else
    {

        uint16_t intsts1;
        uint16_t intenb1;
        uint16_t ists1;

        intsts1 = ptr->ipp->INTSTS1;
        intenb1 = ptr->ipp->INTENB1;
        ists1   = (uint16_t)(intsts1 & intenb1);

        /***** Processing Setup transaction *****/
        if (ists1 & USB_SACK)
        {
            /***** Setup ACK *****/
            /* SACK Clear */
            ptr->ipp->INTSTS1 = (uint16_t)(~USB_SACK & INTSTS1_MASK);
            /* Setup Ignore,Setup Acknowledge disable */
            ptr->ipp->INTENB1 &= (uint16_t)~(USB_SIGNE | USB_SACKE);
            ptr->keyword = USB_INT_SACK;
        }
        else if (ists1 & USB_SIGN)
        {
            /***** Setup Ignore *****/
            /* SIGN Clear */
            ptr->ipp->INTSTS1 = (uint16_t)~USB_SIGN;
            /* Setup Ignore,Setup Acknowledge disable */
            ptr->ipp->INTENB1 &= (uint16_t)(~USB_SIGN & INTSTS1_MASK);
            ptr->keyword = USB_INT_SIGN;
        }

        /***** Processing rootport0 *****/
#if 0
		else if (ists1 & USB_OVRCR)     /***** OVER CURRENT *****/
		{
			/* OVRCR Clear */
			ptr->ipp->INTSTS1 = (uint16_t)~USB_OVRCR;
			ptr->keyword = USB_INT_OVRCR0;
		}
#endif
        else if (ists1 & USB_ATTCH) /***** ATTCH INT *****/
        {
            /* DTCH  interrupt disable */
            usb_hstd_bus_int_disable(ptr, (uint16_t)USB_PORT0);
            ptr->keyword                  = USB_INT_ATTCH0;
            anythingEverAttachedAsUSBHost = 1; // By Rohan. This is the first notification that something's been
                                               // attached - even if it's not working yet
        }
        else if (ists1 & USB_EOFERR) /***** EOFERR INT *****/
        {
            /* EOFERR Clear */
            ptr->ipp->INTSTS1 = (uint16_t)(~USB_EOFERR & INTSTS1_MASK);
            ptr->keyword      = USB_INT_EOFERR0;
        }
        else if (ists1 & USB_BCHG) /***** BCHG INT *****/
        {
            /* BCHG  interrupt disable */
            usb_hstd_bchg_disable(ptr, (uint16_t)USB_PORT0);
            ptr->keyword = USB_INT_BCHG0;
        }
        else if (ists1 & USB_DTCH) /***** DETACH *****/
        {
            /* DTCH  interrupt disable */
            usb_hstd_bus_int_disable(ptr, (uint16_t)USB_PORT0);
            ptr->keyword = USB_INT_DTCH0;
        }
#if USB_CFG_BC == USB_CFG_ENABLE
        else if (ists1 & USB_PDDETINT) /***** PDDETINT INT *****/
        {
            if (ptr->ip == USB_USBIP_1)
            {
                /* PDDETINT  interrupt disable */
                ptr->ipp->INTSTS1 = (uint16_t)~USB_PDDETINT;
                ptr->keyword      = USB_INT_PDDETINT0;
            }
        }
#endif
        else
        {
            return 1;
        }
    }

    return 0; // Everything's *not* taken care of here - need to use the messaging system to take care of it later.
              // Rohan
} /* End of function of usb_hstd_interrupt_handler */

/***********************************************************************************************************************
 Function Name   : usb_hstd_chk_attach
 Description     : Checks whether USB Device is attached or not and return USB speed
                 : of USB Device
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
 Return value    : uint16_t                  ; connection status
                 :                           ; (USB_ATTACHF/USB_ATTACHL/USB_DETACH/USB_OK)
 Note            : Please change for your SYSTEM
 ***********************************************************************************************************************/
uint16_t usb_hstd_chk_attach(usb_utr_t* ptr, uint16_t port)
{
    uint16_t buf[3];

    usb_hstd_read_lnst(ptr, port, buf);

    if ((uint16_t)(buf[1] & USB_RHST) == USB_UNDECID)
    {
        if ((buf[0] & USB_LNST) == USB_FS_JSTS)
        {
            /* High/Full speed device */
            USB_PRINTF0(" Detect FS-J\n");
            usb_hstd_set_hse(ptr, port, g_usb_hstd_hs_enable[ptr->ip]);
            return USB_ATTACHF;
        }
        else if ((buf[0] & USB_LNST) == USB_LS_JSTS)
        {
            /* Low speed device */
            USB_PRINTF0(" Attach LS device\n");
            usb_hstd_set_hse(ptr, port, USB_HS_DISABLE);
            return USB_ATTACHL;
        }
        else if ((buf[0] & USB_LNST) == USB_SE0)
        {
            consoleTextIfAllBootedUp("DETACH"); // By Rohan
            USB_PRINTF0(" Detach device\n");
        }
        else
        {
            USB_PRINTF0(" Attach unknown speed device\n");
        }
    }
    else
    {
        USB_PRINTF0(" Already device attached\n");
        return USB_OK;
    }
    return USB_DETACH;
} /* End of function of usb_hstd_chk_attach */

/***********************************************************************************************************************
 Function Name   : usb_hstd_chk_clk
 Description     : Checks SOF sending setting when USB Device is detached or suspended
                 : , BCHG interrupt enable setting and clock stop processing
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
                 : uint16_t     event        ; Device state
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_chk_clk(usb_utr_t* ptr, uint16_t port, uint16_t event)
{
    if ((g_usb_hstd_mgr_mode[ptr->ip][USB_PORT0] == USB_DETACHED)
        || (g_usb_hstd_mgr_mode[ptr->ip][USB_PORT0] == USB_SUSPENDED))
    {
        usb_hstd_chk_sof(ptr, (uint16_t)USB_PORT0);
        /* Enable port BCHG interrupt */
        usb_hstd_bchg_enable(ptr, (uint16_t)USB_PORT0);
    }
} /* End of function of usb_hstd_chk_clk */

/***********************************************************************************************************************
 Function Name   : usb_hstd_detach_process
 Description     : Handles the require processing when USB device is detached
                 : (Data transfer forcibly termination processing to the connected USB Device,
                 : the clock supply stop setting and the USB interrupt dissable setteing etc)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_detach_process(usb_utr_t* ptr, uint16_t port)
{
    uint16_t connect_inf;
    uint16_t md;
    uint16_t i;
    uint16_t addr;

    /* ATTCH interrupt disable */
    usb_hstd_attch_disable(ptr, port);

    /* DTCH  interrupt disable */
    usb_hstd_dtch_disable(ptr, port);
    usb_hstd_bchg_disable(ptr, (uint16_t)USB_PORT0);

    for (md = 1u; md < (USB_MAXDEVADDR + 1u); md++)
    {
        addr = (uint16_t)(md << USB_DEVADDRBIT);
        if (usb_hstd_chk_dev_addr(ptr, addr, port) != USB_NOCONNECT)
        {
            if (USB_IDLEST != g_usb_hstd_ctsq[ptr->ip])
            {
                /* Control Read/Write End */
                usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_ERR);
            }
            for (i = USB_MIN_PIPE_NO; i <= USB_MAX_PIPE_NO; i++)
            {
                /* Not control transfer */
                /* Agreement device address */
                if (usb_hstd_get_devsel(ptr, i) == addr)
                {
                    /* PID=BUF ? */
                    if (usb_cstd_get_pid(ptr, i) == USB_PID_BUF)
                    {
                        /* End of data transfer (IN/OUT) */
                        usb_hstd_forced_termination(ptr, i, (uint16_t)USB_DATA_STOP);
                    }
                    usb_cstd_clr_pipe_cnfg(ptr, i);
                }
            }
            usb_hstd_set_dev_addr(ptr, addr, USB_OK, USB_OK);
            usb_hstd_set_hub_port(ptr, addr, USB_OK, USB_OK);
            USB_PRINTF1("*** Device address %d clear.\n", md);
        }
    }
    /* Decide USB Line state (ATTACH) */
    connect_inf = usb_hstd_chk_attach(ptr, port);
    switch (connect_inf)
    {
        case USB_ATTACHL:

            usb_hstd_attach(ptr, connect_inf, port);

            break;

        case USB_ATTACHF:

            usb_hstd_attach(ptr, connect_inf, port);

            break;

        case USB_DETACH:

            /* USB detach */
            usb_hstd_detach(ptr, port);
            /* Check clock */
            usb_hstd_chk_clk(ptr, port, (uint16_t)USB_DETACHED);

            break;

        default:

            /* USB detach */
            usb_hstd_detach(ptr, port);
            /* Check clock */
            usb_hstd_chk_clk(ptr, port, (uint16_t)USB_DETACHED);

            break;
    }
} /* End of function of usb_hstd_detach_process */

/***********************************************************************************************************************
 Function Name   : usb_hstd_read_lnst
 Description     : Reads LNST register two times, checks whether these values
                 : are equal and returns the value of DVSTCTR register that correspond to
                 : the port specified by 2nd argument.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
                 : uint16_t     *buf         ; Pointer to the buffer to store DVSTCTR register
 Return value    : none
 Note            : Please change for your SYSTEM
 ***********************************************************************************************************************/
void usb_hstd_read_lnst(usb_utr_t* ptr, uint16_t port, uint16_t* buf)
{
    do
    {
        buf[0] = hw_usb_read_syssts(ptr, port);
        /* 30ms wait */
        usb_cpu_delay_xms((uint16_t)30);
        buf[1] = hw_usb_read_syssts(ptr, port);
        if ((buf[0] & USB_LNST) == (buf[1] & USB_LNST))
        {
            /* 20ms wait */
            usb_cpu_delay_xms((uint16_t)20);
            buf[1] = hw_usb_read_syssts(ptr, port);
        }
    } while ((buf[0] & USB_LNST) != (buf[1] & USB_LNST));
    buf[1] = hw_usb_read_dvstctr(ptr, port);
} /* End of function of usb_hstd_read_lnst */

/***********************************************************************************************************************
 Function Name   : usb_hstd_attach_process
 Description     : Interrupt disable setting when USB Device is attached and
                 : handles the required interrupt disable setting etc when USB device
                 : is attached.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
 Return value    : none
 Note            : Please change for your SYSTEM
 ***********************************************************************************************************************/
void usb_hstd_attach_process(usb_utr_t* ptr, uint16_t port)
{
    uint16_t connect_inf;

    /* ATTCH interrupt disable */
    usb_hstd_attch_disable(ptr, port);

    /* DTCH  interrupt disable */
    usb_hstd_dtch_disable(ptr, port);
    usb_hstd_bchg_disable(ptr, (uint16_t)USB_PORT0);

    /* Decide USB Line state (ATTACH) */
    connect_inf = usb_hstd_chk_attach(ptr, port);
    switch (connect_inf)
    {
        case USB_ATTACHL:

            usb_hstd_attach(ptr, connect_inf, port);

            break;

        case USB_ATTACHF:

            usb_hstd_attach(ptr, connect_inf, port);

            break;

        case USB_DETACH:

            /* USB detach */
            usb_hstd_detach(ptr, port);
            /* Check clock */
            usb_hstd_chk_clk(ptr, port, (uint16_t)USB_DETACHED);

            break;

        default:

            usb_hstd_attach(ptr, (uint16_t)USB_ATTACHF, port);

            break;
    }
} /* End of function of usb_hstd_attach_process */

/***********************************************************************************************************************
 Function Name   : usb_hstd_chk_sof
 Description     : Checks whether SOF is sended or not
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_chk_sof(usb_utr_t* ptr, uint16_t port)
{
    usb_cpu_delay_1us((uint16_t)1); /* Wait 640ns */
} /* End of function of usb_hstd_chk_sof */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bus_reset
 Description     : Setting USB register when BUS Reset
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_bus_reset(usb_utr_t* ptr, uint16_t port)
{
    uint16_t buf;
    uint16_t i;

    /* USBRST=1, UACT=0 */
    hw_usb_rmw_dvstctr(ptr, port, USB_USBRST, (USB_USBRST | USB_UACT));

    /* Wait 50ms */
    usb_cpu_delay_xms((uint16_t)50);
    if (ptr->ip == USB_USBIP_1)
    {
        /* USBRST=0 */
        hw_usb_clear_dvstctr(ptr, USB_PORT0, USB_USBRST); /* for UTMI */
        usb_cpu_delay_1us(300);                           /* for UTMI */
    }
    /* USBRST=0, RESUME=0, UACT=1 */
    usb_hstd_set_uact(ptr, port);
    /* Wait 10ms or more (USB reset recovery) */
    usb_cpu_delay_xms((uint16_t)20);
    for (i = 0, buf = USB_HSPROC; (i < 3) && (USB_HSPROC == buf); ++i)
    {
        /* DeviceStateControlRegister - ResetHandshakeStatusCheck */
        buf = hw_usb_read_dvstctr(ptr, port);
        buf = (uint16_t)(buf & USB_RHST);
        if (USB_HSPROC == buf)
        {
            /* Wait */
            usb_cpu_delay_xms((uint16_t)10);
        }
    }
    /* 30ms wait */
    usb_cpu_delay_xms((uint16_t)30);
} /* End of function of usb_hstd_bus_reset */

/***********************************************************************************************************************
 Function Name   : usb_hstd_resume_process
 Description     : Setting USB register when RESUME signal is detected
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_resume_process(usb_utr_t* ptr, uint16_t port)
{
    usb_hstd_bchg_disable(ptr, port);

    /* RESUME=1, RWUPE=0 */
    hw_usb_rmw_dvstctr(ptr, port, USB_RESUME, (USB_RESUME | USB_RWUPE));

    /* Wait */
    usb_cpu_delay_xms((uint16_t)20);

    /* USBRST=0, RESUME=0, UACT=1 */
    usb_hstd_set_uact(ptr, port);

    /* Wait */
    usb_cpu_delay_xms((uint16_t)5);
} /* End of function of usb_hstd_resume_process */

/***********************************************************************************************************************
 Function Name   : usb_hstd_support_speed_check
 Description     : Get USB-speed of the specified port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         ; Port number
 Return value    : uint16_t                  : HSCONNECT : Hi-Speed
                 :                           : FSCONNECT : Full-Speed
                 :                           : LSCONNECT : Low-Speed
                 :                           : NOCONNECT : not connect
 ***********************************************************************************************************************/
uint16_t usb_hstd_support_speed_check(usb_utr_t* ptr, uint16_t port)
{
    uint16_t buf;
    uint16_t conn_inf;

    buf = hw_usb_read_dvstctr(ptr, port);

    /* Reset handshake status get */
    buf = (uint16_t)(buf & USB_RHST);

    switch (buf)
    {
        /* Get port speed */
        case USB_HSMODE:

            conn_inf = USB_HSCONNECT;

            break;

        case USB_FSMODE:

            conn_inf = USB_FSCONNECT;

            break;

        case USB_LSMODE:

            conn_inf = USB_LSCONNECT;

            break;

        case USB_HSPROC:

            conn_inf = USB_NOCONNECT;

            break;

        default:

            conn_inf = USB_NOCONNECT;

            break;
    }

    return (conn_inf);
} /* End of function of usb_hstd_support_speed_check */

/***********************************************************************************************************************
 Function Name   : usb_hstd_write_fifo
 Description     : Write specified amount of data to specified USB FIFO.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     count        : Write size.
                 : uint16_t     pipemode     : The mode of CPU/DMA(D0)/DMA(D1).
                 : uint16_t     *write_p     : Address of buffer of data to write.
 Return value    : The incremented address of last argument (write_p).
 ***********************************************************************************************************************/
uint8_t* usb_hstd_write_fifo(usb_utr_t* ptr, uint16_t count, uint16_t pipemode, uint8_t* write_p)
{
    uint16_t even;
    uint16_t odd;

    for (even = (uint16_t)(count >> 2); (even != 0); --even)
    {
        /* 32bit access */
        hw_usb_write_fifo32(ptr, pipemode, *((uint32_t*)write_p));

        /* Renewal write pointer */
        write_p += sizeof(uint32_t);
    }
    odd = count % 4;
    if ((odd & (uint16_t)0x0002u) != 0u)
    {
        /* 16bit access */
        /* Change FIFO access width */
        hw_usb_set_mbw(ptr, pipemode, USB_MBW_16);
        /* FIFO write */
        hw_usb_write_fifo16(ptr, pipemode, *((uint16_t*)write_p));

        /* Renewal write pointer */
        write_p += sizeof(uint16_t);
    }
    if ((odd & (uint16_t)0x0001u) != 0u)
    {
        /* 8bit access */
        /* count == odd */
        /* Change FIFO access width */
        hw_usb_set_mbw(ptr, pipemode, USB_MBW_8);

        /* FIFO write */
        hw_usb_write_fifo8(ptr, pipemode, *write_p);

        /* Renewal write pointer */
        write_p++;
    }
    /* Return FIFO access width */
    hw_usb_set_mbw(ptr, pipemode, USB_MBW_32);

    return write_p;

} /* End of function usb_hstd_write_fifo() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_read_fifo
 Description     : Read specified buffer size from the USB FIFO.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     count        : Read size.
                 : uint16_t     pipemode     : The mode of CPU/DMA(D0)/DMA(D1).
                 : uint16_t     *write_p     : Address of buffer to store the read data.
 Return value    : Pointer to a buffer that contains the data to be read next.
 ***********************************************************************************************************************/
uint8_t* usb_hstd_read_fifo(usb_utr_t* ptr, uint16_t count, uint16_t pipemode, uint8_t* read_p)
{
    uint16_t even;
    uint16_t odd;
    uint32_t odd_byte_data_temp;

    for (even = (uint16_t)(count >> 2); (even != 0); --even)
    {
        /* 32bit FIFO access */
        *(uint32_t*)read_p = hw_usb_read_fifo32(ptr, pipemode);

        /* Renewal read pointer */
        read_p += sizeof(uint32_t);
    }
    odd = count % 4;
    if (count < 4)
    {
        odd = count;
    }
    if (odd != 0)
    {
        /* 32bit FIFO access */
        odd_byte_data_temp = hw_usb_read_fifo32(ptr, pipemode);
        /* Condition compilation by the difference of the little endian */
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
} /* End of function usb_hstd_read_fifo() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_forced_termination
 Description     : Terminate data transmission and reception.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe Number
                 : uint16_t     status       : Transfer status type
 Return value    : none
 Note            : In the case of timeout status, it does not call back.
 ***********************************************************************************************************************/
void usb_hstd_forced_termination(usb_utr_t* ptr, uint16_t pipe, uint16_t status)
{
    uint16_t buffer;

    /* PID = NAK */
    /* Set NAK */
    usb_cstd_set_nak(ptr, pipe);

    /* Disable Interrupt */
    /* Disable Ready Interrupt */
    hw_usb_clear_brdyenb(ptr, pipe);

    /* Disable Not Ready Interrupt */
    hw_usb_clear_nrdyenb(ptr, pipe);

    /* Disable Empty Interrupt */
    hw_usb_clear_bempenb(ptr,
        pipe); // This isn't necessary, and I've removed it from when there's been a successful transaction. However,
               // removing it didn't solve occasional i029 on disconnecting / reconnecting hub and many devices.

    usb_cstd_clr_transaction_counter(ptr, pipe);

    /* Clear D1FIFO-port */
    buffer = hw_usb_read_fifosel(ptr, USB_CUSE);
    if ((buffer & USB_CURPIPE) == pipe)
    {
        if (ptr->ip == USB_USBIP_0)
        {
            hw_usb_set_mbw(ptr, USB_CUSE, USB0_CFIFO_MBW);
        }
        else if (ptr->ip == USB_USBIP_1)
        {
            hw_usb_set_mbw(ptr, USB_CUSE, USB1_CFIFO_MBW);
        }
        else
        {
            /* Non */
        }

        /* Changes the FIFO port by the pipe. */
        usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);
    }

    /* Changes the FIFO port by the pipe. */
    usb_cstd_chg_curpipe(ptr, pipe, (uint16_t)USB_CUSE, USB_FALSE);
    buffer = hw_usb_read_fifoctr(ptr, USB_CUSE);
    if ((uint16_t)(buffer & USB_FRDY) == USB_FRDY)
    {
        /* Clear BVAL */
        hw_usb_set_bclr(ptr, USB_CUSE);
    }

    /* FIFO buffer SPLIT transaction initialized */
    usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);
    hw_usb_set_csclr(ptr, pipe);

    /* Call Back */
    if (g_p_usb_pipe[pipe] != USB_NULL)
    {
        /* Transfer information set */
        g_p_usb_pipe[pipe]->tranlen = g_usb_data_cnt[pipe];
        g_p_usb_pipe[pipe]->status  = status;
        g_p_usb_pipe[pipe]->pipectr = hw_usb_read_pipectr(ptr, pipe);
        // g_p_usb_pipe[pipe]->errcnt  = (uint8_t)g_usb_hstd_ignore_cnt[ptr->ip][pipe];
        g_p_usb_pipe[pipe]->ipp = ptr->ipp;
        g_p_usb_pipe[pipe]->ip  = ptr->ip;
        if (USB_NULL != (g_p_usb_pipe[pipe]->complete))
        {
            (g_p_usb_pipe[pipe]->complete)(g_p_usb_pipe[pipe], 0, 0);
        }
        g_p_usb_pipe[pipe] = (usb_utr_t*)USB_NULL;
    }
} /* End of function usb_hstd_forced_termination() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_get_usb_ip_adr
 Description     : Get base address of the selected USB channel's peripheral
                 : registers.
 Argument        : uint16_t     ipnum        : USB_USBIP_0 (0), or USB_USBIP_1 (1).
 Return          : usb_regadr_t              : A pointer to the USB_597IP register
                                             : structure USB_REGISTER containing all USB
                                             : channel's registers.
 ***********************************************************************************************************************/
usb_regadr_t usb_hstd_get_usb_ip_adr(uint16_t ipnum)
{
    usb_regadr_t ptr;

    if (ipnum == USB_USBIP_0)
    {
        ptr = (usb_regadr_t)&USB200;
    }
    else if (ipnum == USB_USBIP_1)
    {
        ptr = (usb_regadr_t)&USB201;
    }
    else
    {
        USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE1);
    }

    return ptr;
} /* End of function usb_hstd_get_usb_ip_adr() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_nrdy_endprocess
 Description     : NRDY interrupt processing. (Forced termination of data trans-
                 : mission and reception of specified pipe.)
  Arguments      : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe No
 Return value    : none
 Note            : none
 ***********************************************************************************************************************/
void usb_hstd_nrdy_endprocess(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t buffer;

    /* Host Function */
    buffer = usb_cstd_get_pid(ptr, pipe);

    /* STALL ? */
    if ((buffer & USB_PID_STALL) == USB_PID_STALL)
    {
        /*USB_PRINTF1("### STALL Pipe %d\n", pipe);*/
        /* @4 */
        /* End of data transfer */
        usb_hstd_forced_termination(ptr, pipe, USB_DATA_STALL);
    }
    else
    {
        /* Wait for About 60ns */
        buffer = hw_usb_read_syssts(ptr, USB_PORT0);
        /* @3 */
        g_usb_hstd_ignore_cnt[ptr->ip][pipe]++;
        /*USB_PRINTF2("### IGNORE Pipe %d is %d times \n", pipe, g_usb_hstd_ignore_cnt[ptr->ip][pipe]);*/
        if (g_usb_hstd_ignore_cnt[ptr->ip][pipe] == USB_PIPEERROR)
        {
            /* Data Device Ignore X 3 call back */
            /* End of data transfer */
            usb_hstd_forced_termination(ptr, pipe, USB_DATA_ERR);
        }
        else
        {
            /* 5ms wait */
            usb_cpu_delay_xms(5);
            /* PIPEx Data Retry */
            usb_cstd_set_buf(ptr, pipe);
        }
    }
} /* End of function usb_hstd_nrdy_endprocess() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
