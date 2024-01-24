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
 * File Name    : r_usb_hlibusbip.c
 * Description  : USB IP Host library.
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

// Added by Rohan
#include "RZA1/usb/userdef/r_usb_hmidi_config.h"

#include "definitions.h"
#include "deluge/io/midi/midi_device_manager.h"

#include "deluge/drivers/uart/uart.h"
#include "deluge/io/midi/midi_engine.h"

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
#include "drivers/usb/r_usb_basic/src/hw/inc/r_usb_dmac.h"
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Renesas Abstracted Host Lib IP functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_hstd_set_dev_addr
 Description     : Set USB speed (Full/Hi) of the connected USB Device.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device Address
                 : uint16_t     speed        : Device speed
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_set_dev_addr(usb_utr_t* ptr, uint16_t addr, uint16_t speed, uint16_t port)
{
    if (USB_DEVICE_0 == addr)
    {
        hw_usb_write_dcpmxps(ptr, (uint16_t)(USB_DEFPACKET + USB_DEVICE_0));
    }
    hw_usb_hset_usbspd(ptr, addr, (speed | port));
} /* End of function usb_hstd_set_dev_addr() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bchg_enable
 Description     : Enable BCHG interrupt for the specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_bchg_enable(usb_utr_t* ptr, uint16_t port)
{
    hw_usb_hclear_sts_bchg(ptr, port);
    hw_usb_hset_enb_bchge(ptr, port);
} /* End of function usb_hstd_bchg_enable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bchg_disable
 Description     : Disable BCHG interrupt for specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_bchg_disable(usb_utr_t* ptr, uint16_t port)
{
    hw_usb_hclear_sts_bchg(ptr, port);
    hw_usb_hclear_enb_bchge(ptr, port);
} /* End of function usb_hstd_bchg_disable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_set_uact
 Description     : Start sending SOF to the connected USB device.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_set_uact(usb_utr_t* ptr, uint16_t port)
{
    hw_usb_rmw_dvstctr(ptr, port, USB_UACT, ((USB_USBRST | USB_RESUME) | USB_UACT));
} /* End of function usb_hstd_set_uact() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_ovrcr_enable
 Description     : Enable OVRCR interrupt of the specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_ovrcr_enable(usb_utr_t* ptr, uint16_t port)
{
    hw_usb_hclear_sts_ovrcr(ptr, port);
    hw_usb_hset_enb_ovrcre(ptr, port);
} /* End of function usb_hstd_ovrcr_enable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_ovrcr_disable
 Description     : Disable OVRCR interrupt of the specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_ovrcr_disable(usb_utr_t* ptr, uint16_t port)
{
    /* OVRCR Clear(INT_N edge sense) */
    hw_usb_hclear_sts_ovrcr(ptr, port);

    /* Over-current disable */
    hw_usb_hclear_enb_ovrcre(ptr, port);
} /* End of function usb_hstd_ovrcr_disable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_attch_enable
 Description     : Enable ATTCH (attach) interrupt of the specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_attch_enable(usb_utr_t* ptr, uint16_t port)
{
    /* ATTCH status Clear */
    hw_usb_hclear_sts_attch(ptr, port);

    /* Attach enable */
    hw_usb_hset_enb_attche(ptr, port);
} /* End of function usb_hstd_attch_enable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_attch_disable
 Description     : Disable ATTCH (attach) interrupt of the specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_attch_disable(usb_utr_t* ptr, uint16_t port)
{
    /* ATTCH Clear(INT_N edge sense) */
    hw_usb_hclear_sts_attch(ptr, port);

    /* Attach disable */
    hw_usb_hclear_enb_attche(ptr, port);
} /* End of function usb_hstd_attch_disable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_dtch_enable
 Description     : Enable DTCH (detach) interrupt of the specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_dtch_enable(usb_utr_t* ptr, uint16_t port)
{
    /* DTCH Clear */
    hw_usb_hclear_sts_dtch(ptr, port);

    /* Detach enable */
    hw_usb_hset_enb_dtche(ptr, port);
} /* End of function usb_hstd_dtch_enable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_dtch_disable
 Description     : Disable DTCH (detach) interrupt of the specified USB port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_dtch_disable(usb_utr_t* ptr, uint16_t port)
{
    /* DTCH Clear(INT_N edge sense) */
    hw_usb_hclear_sts_dtch(ptr, port);

    /* Detach disable */
    hw_usb_hclear_enb_dtche(ptr, port);
} /* End of function usb_hstd_dtch_disable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_set_pipe_register
 Description     : Set up USB registers to use specified pipe (given in infor-
                 : mation table).
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe_no      : Pipe number
                 : uint16_t     *tbl         : Pipe information table
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_set_pipe_register(usb_utr_t* ptr, uint16_t pipe_no, uint16_t* tbl)
{
    uint16_t i;
    uint16_t pipe;
    uint16_t buf;

    /* PIPE USE check */
    if (USB_USEPIPE == pipe_no)
    {
        /* EP Table loop */
        for (i = 0; USB_PDTBLEND != tbl[i]; i += USB_EPL)
        {
            /* PipeNo Number */
            pipe = (uint16_t)(tbl[i + 0] & USB_CURPIPE);

            /* Current FIFO port Clear */
            buf = hw_usb_read_fifosel(ptr, USB_CUSE);
            if ((buf & USB_CURPIPE) == pipe)
            {
                usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);
            }
            buf = hw_usb_read_fifosel(ptr, USB_D0DMA);
            if ((buf & USB_CURPIPE) == pipe)
            {
                usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_D0USE, USB_FALSE);
            }
            buf = hw_usb_read_fifosel(ptr, USB_D1DMA);
            if ((buf & USB_CURPIPE) == pipe)
            {
                usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_D1USE, USB_FALSE);
            }

            /* PIPE Setting */
            usb_cstd_pipe_init(ptr, pipe, tbl, i);
        }
    }
    else
    {
        /* Current FIFO port Clear */
        usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_CUSE, USB_FALSE);
        buf = hw_usb_read_fifosel(ptr, USB_D0USE);
        if ((buf & USB_CURPIPE) == pipe_no)
        {
            usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_D0USE, USB_FALSE);
        }
        buf = hw_usb_read_fifosel(ptr, USB_D1USE);
        if ((buf & USB_CURPIPE) == pipe_no)
        {
            usb_cstd_chg_curpipe(ptr, (uint16_t)USB_PIPE0, (uint16_t)USB_D1USE, USB_FALSE);
        }

        /* EP Table loop */
        for (i = 0; USB_PDTBLEND != tbl[i]; i += USB_EPL)
        {
            /* PipeNo Number */
            pipe = (uint16_t)(tbl[i + 0] & USB_CURPIPE);
            if (pipe == pipe_no)
            {
                /* PIPE Setting */
                usb_cstd_pipe_init(ptr, pipe, tbl, i);
            }
        }
    }
} /* End of function usb_hstd_set_pipe_register() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_get_rootport
 Description     : Get USB port no. set in the USB register based on the speci-
                 : fied USB Device address.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device address
 Return value    : uint16_t                  : Root port number
 ***********************************************************************************************************************/
uint16_t usb_hstd_get_rootport(usb_utr_t* ptr, uint16_t addr)
{
    uint16_t buffer;

    /* Get device address configuration register from device address */
    buffer = hw_usb_hread_devadd(ptr, addr);
    if (USB_ERROR != buffer)
    {
        /* Return root port number */
        return (uint16_t)(buffer & USB_RTPORT);
    }
    return USB_ERROR;
} /* End of function usb_hstd_get_rootport() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_chk_dev_addr
 Description     : Get USB speed set in USB register based on the specified USB
                 : Device address and USB port no.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device address
                 : uint16_t     rootport     : Root port
 Return value    : uint16_t                  : USB speed etc
 ***********************************************************************************************************************/
uint16_t usb_hstd_chk_dev_addr(usb_utr_t* ptr, uint16_t addr, uint16_t rootport)
{
    uint16_t buffer;

    /* Get device address configuration register from device address */
    buffer = hw_usb_hread_devadd(ptr, addr);
    if (USB_ERROR != buffer)
    {
        if ((uint16_t)(buffer & USB_RTPORT) == rootport)
        {
            /* Return Address check result */
            return (uint16_t)(buffer & USB_USBSPD);
        }
    }
    return USB_NOCONNECT;
} /* End of function usb_hstd_chk_dev_addr() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_get_dev_speed
 Description     : Get USB speed set in USB register based on the specified USB
                 : Device address.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device address
 Return value    : uint16_t                  : device speed
 Note            : Use also to a connection check is possible
 ***********************************************************************************************************************/
uint16_t usb_hstd_get_dev_speed(usb_utr_t* ptr, uint16_t addr)
{
    uint16_t buffer;

    /* Get device address configuration register from device address */
    buffer = hw_usb_hread_devadd(ptr, addr);
    if (USB_ERROR != buffer)
    {
        /* Return device speed */
        return (uint16_t)(buffer & USB_USBSPD);
    }
    return USB_NOCONNECT;
} /* End of function usb_hstd_get_dev_speed() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_pipe_to_epadr
 Description     : Get the associated endpoint value of the specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint8_t                   : OK    : Endpoint nr + direction.
                 :                           : ERROR : Error.
 ***********************************************************************************************************************/
uint8_t usb_hstd_pipe_to_epadr(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t buffer;
    uint16_t direp;

    /* Pipe select */
    hw_usb_write_pipesel(ptr, pipe);

    /* Read Pipe direction */
    buffer = hw_usb_read_pipecfg(ptr);
    direp  = (uint16_t)((((buffer & USB_DIRFIELD) ^ USB_DIRFIELD) << 3) + (buffer & USB_EPNUMFIELD));
    return (uint8_t)(direp);
} /* End of function usb_hstd_get_dev_speed() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_pipe2fport
 Description     : Get port No. from the specified pipe No. by argument
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t                  : FIFO port selector.
 ***********************************************************************************************************************/
uint16_t usb_hstd_pipe2fport(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t fifo_mode = USB_CUSE;

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
    if (USB_PIPE1 == pipe)
    {
        fifo_mode = USB_D0DMA;
    }
    if (USB_PIPE2 == pipe)
    {
        fifo_mode = USB_D1DMA;
    }
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

    return fifo_mode;
} /* End of function usb_hstd_pipe2fport() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_set_hse
 Description     : Set/clear the HSE-bit of the specified port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port.
                 : uint16_t     speed        : HS_ENABLE/HS_DISABLE.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_set_hse(usb_utr_t* ptr, uint16_t port, uint16_t speed)
{
    if (speed == USB_HS_DISABLE)
    {
        /* HSE = disable */
        hw_usb_clear_hse(ptr, port);
    }
    else
    {
        /* HSE = enable */
        hw_usb_set_hse(ptr, port);
    }
} /* End of function usb_hstd_set_hse() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_berne_enable
 Description     : Enable BRDY/NRDY/BEMP interrupt.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_berne_enable(usb_utr_t* ptr)
{
    /* Enable BEMP, NRDY, BRDY */
    hw_usb_set_intenb(ptr, ((USB_BEMPE | USB_NRDYE) | USB_BRDYE));
} /* End of function usb_hstd_berne_enable() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_sw_reset
 Description     : Request USB IP software reset
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_sw_reset(usb_utr_t* ptr)
{
    /* USB Enable */
    hw_usb_set_usbe(ptr);

    /* USB Reset */
    hw_usb_clear_usbe(ptr);

    /* USB Enable */
    hw_usb_set_usbe(ptr);
} /* End of function usb_hstd_sw_reset() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_do_sqtgl
 Description     : Toggle setting of the toggle-bit for the specified pipe by
                 : argument.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     toggle       : Current toggle status.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_do_sqtgl(usb_utr_t* ptr, uint16_t pipe, uint16_t toggle)
{
    /* Check toggle */
    if ((toggle & USB_SQMON) == USB_SQMON)
    {
        /* Do pipe SQSET */
        hw_usb_set_sqset(ptr, pipe);
    }
    else
    {
        /* Do pipe SQCLR */
        hw_usb_set_sqclr(ptr, pipe);
    }
} /* End of function usb_hstd_do_sqtgl() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_get_devsel
 Description     : Get device address from pipe number
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t DEVSEL-bit status
 ***********************************************************************************************************************/
uint16_t usb_hstd_get_devsel(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t devsel;
    uint16_t buffer;

    if (USB_PIPE0 == pipe)
    {
        buffer = hw_usb_read_dcpmaxp(ptr);
    }
    else
    {
        /* Pipe select */
        hw_usb_write_pipesel(ptr, pipe);
        buffer = hw_usb_read_pipemaxp(ptr);
    }

    /* Device address */
    devsel = (uint16_t)(buffer & USB_DEVSEL);

    return devsel;
} /* End of function usb_hstd_get_devsel() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_get_device_address
 Description     : Get the device address associated with the specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t DEVSEL-bit status
 ***********************************************************************************************************************/
uint16_t usb_hstd_get_device_address(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t buffer;
    uint16_t i;
    uint16_t md;
    usb_hcdreg_t* pdriver;

    /* Host */
    if (USB_PIPE0 == pipe)
    {
        buffer = hw_usb_read_dcpmaxp(ptr);

        /* Device address */
        return (uint16_t)(buffer & USB_DEVSEL);
    }
    else
    {
        for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
        {
            if ((USB_IFCLS_NOT != g_usb_hstd_device_drv[ptr->ip][md].ifclass)
                && (USB_NODEVICE != g_usb_hstd_device_drv[ptr->ip][md].devaddr))
            {
                pdriver = (usb_hcdreg_t*)&g_usb_hstd_device_drv[ptr->ip][md];

                /* EP table loop */
                for (i = 0; USB_PDTBLEND != pdriver->p_pipetbl[i]; i += USB_EPL)
                {
                    if (pdriver->p_pipetbl[i] == pipe)
                    {
                        buffer = pdriver->p_pipetbl[i + 3];

                        /* Device address */
                        return (uint16_t)(buffer & USB_DEVSEL);
                    }
                }
            }
        }
    }

    return USB_ERROR;
} /* End of function usb_hstd_get_device_address() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_buf2fifo
 Description     : Set USB registers as required to write from data buffer to USB
                 : FIFO, to have USB FIFO to write data to bus.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     useport      : Port number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_buf2fifo(usb_utr_t* ptr, uint16_t pipe, uint16_t useport)
{
    uint16_t end_flag;

    /* Disable Ready Interrupt */
    hw_usb_clear_brdyenb(ptr, pipe);

    /* Ignore count clear */
    g_usb_hstd_ignore_cnt[ptr->ip][pipe] = (uint16_t)0;

    end_flag = usb_hstd_write_data(ptr, pipe, useport);

    /* Check FIFO access sequence */
    switch (end_flag)
    {
        case USB_WRITING:

            /* Continue of data write */
            /* Enable Ready Interrupt */
            hw_usb_set_brdyenb(ptr, pipe);

            /* Enable Not Ready Interrupt */
            // usb_cstd_nrdy_enable(ptr, pipe);
            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
            //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver
            //  (in 2019).

            break;

        case USB_WRITEEND:

            /* End of data write */
            /* continue */

        case USB_WRITESHRT:

            /* End of data write */
            /* Enable Empty Interrupt */
            hw_usb_set_bempenb(ptr, pipe);

            /* Enable Not Ready Interrupt */
            // usb_cstd_nrdy_enable(ptr, pipe);
            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
            //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver
            //  (in 2019).

            break;

        case USB_FIFOERROR:

            /* FIFO access error */
            USB_PRINTF0("### FIFO access error \n");
            usb_hstd_forced_termination(ptr, pipe, (uint16_t)USB_DATA_ERR);

            break;

        default:

            usb_hstd_forced_termination(ptr, pipe, (uint16_t)USB_DATA_ERR);

            break;
    }
} /* End of function usb_hstd_buf2fifo() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_write_data
 Description     : Switch PIPE, request the USB FIFO to write data, and manage
                 : the size of written data.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : uint16_t end_flag
 ***********************************************************************************************************************/
uint16_t usb_hstd_write_data(usb_utr_t* ptr, uint16_t pipe, uint16_t pipemode)
{
    uint16_t size;
    uint16_t count;
    uint16_t buffer;
    uint16_t mxps;
    uint16_t end_flag;

    /* Changes FIFO port by the pipe. */
    if ((USB_CUSE == pipemode) && (USB_PIPE0 == pipe))
    {
        buffer = usb_cstd_is_set_frdy(ptr, pipe, (uint16_t)USB_CUSE, (uint16_t)USB_ISEL);
    }
    else
    {
        buffer = usb_cstd_is_set_frdy(ptr, pipe, (uint16_t)pipemode, USB_FALSE);
    }

    /* Check error */
    if (USB_FIFOERROR == buffer)
    {
        /* FIFO access error */
        return (USB_FIFOERROR);
    }

    /* Data buffer size */
    size = usb_cstd_get_buf_size(ptr, pipe);

    /* Max Packet Size */
    mxps = usb_cstd_get_maxpacket_size(ptr, pipe);

    /* Data size check */
    if (g_usb_data_cnt[pipe] <= (uint32_t)size)
    {
        count = (uint16_t)g_usb_data_cnt[pipe];

        /* Data count check */
        if (0 == count)
        {
            /* Null Packet is end of write */
            end_flag = USB_WRITESHRT;
        }
        else if ((count % mxps) != 0)
        {
            /* Short Packet is end of write */
            end_flag = USB_WRITESHRT;
        }
        else
        {
            if (USB_PIPE0 == pipe)
            {
                /* Just Send Size */
                end_flag = USB_WRITING;
            }
            else
            {
                /* Write continues */
                end_flag = USB_WRITEEND;
            }
        }
    }
    else
    {
        /* Write continues */
        end_flag = USB_WRITING;
        count    = size;
    }

    g_p_usb_data[pipe] = usb_hstd_write_fifo(ptr, count, pipemode, g_p_usb_data[pipe]);

    /* Check data count to remain */
    if (g_usb_data_cnt[pipe] < (uint32_t)size)
    {
        /* Clear data count */
        g_usb_data_cnt[pipe] = (uint32_t)0u;
        buffer               = hw_usb_read_fifoctr(ptr, pipemode); /* Read CFIFOCTR */

        /* Check BVAL */
        if ((buffer & USB_BVAL) == 0u)
        {
            /* Short Packet */
            hw_usb_set_bval(ptr, pipemode);
        }
    }
    else
    {
        /* Total data count - count */
        g_usb_data_cnt[pipe] -= count;
    }

    uartPrintln("sent");

    /* End or Err or Continue */
    return end_flag;
} /* End of function usb_hstd_write_data() */

// Special fast copy by Rohan. For both host and peripheral now.
void usb_receive_start_rohan_midi(uint16_t pipe)
{
    usb_utr_t* pp;
    uint32_t length;
    uint16_t mxps;
    uint16_t useport;
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
    uint16_t dma_ch;
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

    /* Evacuation pointer */
    pp     = g_p_usb_pipe[pipe];
    length = pp->tranlen;

#if 0
    /* Check transfer count */
    if (USB_TRAN_CONT == pp->segment) // Wait, can this actually happen? Rohan
    {
        /* Sequence toggle */
        //usb_hstd_do_sqtgl(ptr, pipe, pp->pipectr);

        /* Check toggle */
        if (pp->pipectr & USB_SQMON)
        {
            /* Do pipe SQSET */
            hw_usb_set_sqset(ptr, pipe);
        }
        else
        {
            /* Do pipe SQCLR */
            hw_usb_set_sqclr(ptr, pipe);
        }
    }
#endif

    /* Select NAK */
    // usb_cstd_select_nak(ptr, pipe);
    // usb_cstd_set_nak_fast_rohan(pipe);

    g_usb_data_cnt[pipe] = length;                  /* Set data count */
    g_p_usb_data[pipe]   = (uint8_t*)pp->p_tranadr; /* Set data pointer */

    /* Ignore count clear */
    g_usb_hstd_ignore_cnt[USB_CFG_USE_USBIP][pipe] =
        (uint16_t)0u; // Was only included in host code originally - not peripheral

    /* Pipe number to FIFO port select */
    // useport = USB_CUSE; // usb_hstd_pipe2fport(ptr, pipe);

    /* Changes the FIFO port by the pipe. */
    // usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);
    usb_cstd_chg_curpipe_rohan_fast(pipe);

    // Corner cut here vs original code - it'll always be less than 1 packet, just go.

    // usb_cstd_set_transaction_counter(ptr, pipe, 1);
    { // Seems this wasn't necessary either!
      // hw_usb_set_trclr(NULL, pipe);
      // hw_usb_write_pipetrn(NULL, pipe, 1);
      // hw_usb_set_trenb(NULL, pipe);
    }

    // usb_cstd_set_buf(ptr, pipe); /* Set BUF */
    // hw_usb_set_pid(NULL, pipe, USB_PID_BUF);
    hw_usb_set_pid_nonzero_pipe_rohan(
        pipe, USB_PID_BUF); // I guess this is how it says "request me an incoming data transfer"?

    /* Enable Ready Interrupt */
    hw_usb_set_brdyenb(NULL, pipe);

    /* Enable Not Ready Interrupt */
    // usb_cstd_nrdy_enable(ptr, pipe);
    // hw_usb_set_nrdyenb(NULL, pipe);
    //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was causing
    //  freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in 2019).

    // Commenting that out seems to work fine now, I've tested and that's good, but previous comment was:
    //	"Generally not needed and removing this works fine, except one case - after reconnecting a device to a hub (the
    // Targus USB2 one at least), MIDI won't get to us, every 2nd reconnect!"
}

/***********************************************************************************************************************
 Function Name   : usb_hstd_receive_start
 Description     : Start data reception using CPU/DMA transfer to USB Host/USB
                 : device.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_receive_start(usb_utr_t* ptr, uint16_t pipe)
{
    usb_utr_t* pp;
    uint32_t length;
    uint16_t mxps;
    uint16_t useport;
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
    uint16_t dma_ch;
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

    /* Evacuation pointer */
    pp     = g_p_usb_pipe[pipe];
    length = pp->tranlen;

    /* Check transfer count */
    if (USB_TRAN_CONT == pp->segment) // Wait, can this actually happen? Rohan
    {
        /* Sequence toggle */
        // usb_hstd_do_sqtgl(ptr, pipe, pp->pipectr);

        /* Check toggle */
        if (pp->pipectr & USB_SQMON)
        {
            /* Do pipe SQSET */
            hw_usb_set_sqset(ptr, pipe);
        }
        else
        {
            /* Do pipe SQCLR */
            hw_usb_set_sqclr(ptr, pipe);
        }
    }

    /* Select NAK */
    // usb_cstd_select_nak(ptr, pipe);
    usb_cstd_set_nak_fast_rohan(pipe);

    g_usb_data_cnt[pipe] = length;                  /* Set data count */
    g_p_usb_data[pipe]   = (uint8_t*)pp->p_tranadr; /* Set data pointer */

    /* Ignore count clear */
    g_usb_hstd_ignore_cnt[ptr->ip][pipe] = (uint16_t)0u;

    /* Pipe number to FIFO port select */
    useport = USB_CUSE; // usb_hstd_pipe2fport(ptr, pipe);

    /* Check use FIFO access */
    switch (useport)
    {
        /* D0FIFO use */
        case USB_D0USE:

            /* D0 FIFO access is NG */
            USB_PRINTF1("### USB-ITRON is not support(RCV-D0USE:pipe%d)\n", pipe);
            usb_hstd_forced_termination(ptr, pipe, (uint16_t)USB_DATA_ERR);

            break;

        /* CFIFO use */
        case USB_CUSE:
        /* D1FIFO use */
        case USB_D1USE:

            /* Changes the FIFO port by the pipe. */
            usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);
            mxps = usb_cstd_get_maxpacket_size(ptr, pipe); /* Max Packet Size */
            if ((uint32_t)0u != length)
            {
                /* Data length check */
                if ((length % mxps) == (uint32_t)0u)
                {
                    /* Set Transaction counter */
                    usb_cstd_set_transaction_counter(ptr, pipe, (uint16_t)(length / mxps));
                }
                else
                {
                    /* Set Transaction counter */
                    usb_cstd_set_transaction_counter(ptr, pipe, (uint16_t)((length / mxps) + (uint32_t)1u));
                }
            }

            usb_cstd_set_buf(ptr, pipe); /* Set BUF */

            /* Enable Ready Interrupt */
            hw_usb_set_brdyenb(ptr, pipe);

            /* Enable Not Ready Interrupt */
            // usb_cstd_nrdy_enable(ptr, pipe);
            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
            //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver
            //  (in 2019).

            break;

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
        /* D1FIFO DMA */
        case USB_D1DMA:
        /* D0FIFO DMA */
        case USB_D0DMA:

            if (USB_IP0 == ptr->ip)
            {
                dma_ch = USB_CFG_USB0_DMA_RX;
            }
            else
            {
                dma_ch = USB_CFG_USB1_DMA_RX;
            }

            usb_dma_set_ch_no(ptr->ip, useport, dma_ch);

            /* Setting for use PIPE number */
            g_usb_cstd_dma_pipe[ptr->ip][dma_ch] = pipe;
            /* PIPE direction */
            g_usb_cstd_dma_dir[ptr->ip][dma_ch] = usb_cstd_get_pipe_dir(ptr, pipe);
            /* Buffer size */
            g_usb_cstd_dma_fifo[ptr->ip][dma_ch] = usb_cstd_get_buf_size(ptr, pipe);

            /* Transfer data size */
            g_usb_cstd_dma_size[ptr->ip][dma_ch] = g_usb_hstd_data_cnt[ptr->ip][pipe];
            usb_cstd_dxfifo2buf_start_dma(ptr, pipe, useport, length);

            break;

#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

        default:

            USB_PRINTF1("### USB-ITRON is not support(RCV-else:pipe%d)\n", pipe);
            usb_hstd_forced_termination(ptr, pipe, (uint16_t)USB_DATA_ERR);

            break;
    }
} /* End of function usb_hstd_receive_start() */

#if 0
// I now just call the PSTD one instead of this - it does the same.
/***********************************************************************************************************************
 Function Name   : usb_hstd_read_data
 Description     : Request to read data from USB FIFO, and manage the size of 
                 : the data read.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     pipemode     : Pipe mode (CFIFO/D0FIFO/D1FIFO)
 Return value    : USB_READING / USB_READEND / USB_READSHRT / USB_READOVER
 ***********************************************************************************************************************/
uint16_t usb_hstd_read_data(usb_utr_t *ptr, uint16_t pipe, uint16_t pipemode)
{
    uint16_t    count;
    uint16_t    buffer;
    uint16_t    mxps;
    uint16_t    dtln;
    uint16_t    end_flag;

    /* Changes FIFO port by the pipe. */
    buffer = usb_cstd_is_set_frdy(ptr, pipe, (uint16_t) pipemode, USB_FALSE);
    if (USB_FIFOERROR == buffer)
    {
        /* FIFO access error */
        return (USB_FIFOERROR);
    }
    dtln = (uint16_t) (buffer & USB_DTLN);

    /* Max Packet Size */
    mxps = usb_cstd_get_maxpacket_size(ptr, pipe);

    if (g_usb_data_cnt[pipe] < dtln)
    {
        /* Buffer Over ? */
        end_flag = USB_READOVER;
        usb_cstd_set_nak(ptr, pipe); /* Set NAK */
        count = (uint16_t) g_usb_data_cnt[pipe];
        g_usb_data_cnt[pipe] = dtln;
    }
    else if (g_usb_data_cnt[pipe] == dtln)
    {
        /* Just Receive Size */
        count = dtln;
        end_flag = USB_READEND;
        usb_cstd_select_nak(ptr, pipe); /* Set NAK */
    }
    else
    {
        /* Continus Receive data */
        count = dtln;
        end_flag = USB_READING;
        if ((0 == count) || (0 != (count % mxps)))
        {
            /* Null Packet receive */
            end_flag = USB_READSHRT;
            usb_cstd_select_nak(ptr, pipe); /* Select NAK */
        }
    }

    if (0 == dtln)
    {
        /* 0 length packet */
        /* Clear BVAL */
        hw_usb_set_bclr(ptr, pipemode);
    }
    else
    {
    	g_p_usb_data[pipe] = usb_hstd_read_fifo(ptr, count, pipemode,
    			g_p_usb_data[pipe]);
    }
    g_usb_data_cnt[pipe] -= count;

    /* End or Err or Continue */
    return (end_flag);
} /* End of function usb_hstd_read_data() */
#endif

/***********************************************************************************************************************
 Function Name   : usb_hstd_data_end
 Description     : Set USB registers as appropriate after data transmission/re-
                 : ception, and call the callback function as transmission/recep-
                 : tion is complete.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     status       : Transfer status type.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_data_end(usb_utr_t* ptr, uint16_t pipe, uint16_t status)
{
    uint16_t useport;
    uint16_t ip;

    if (USB_NULL != ptr)
    {
        ip = ptr->ip;
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#else  /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
        ip = USB_NULL;
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }

    /* PID = NAK */
    /* Set NAK */
    usb_cstd_select_nak(ptr, pipe);

    /* Pipe number to FIFO port select */
    useport = usb_hstd_pipe2fport(ptr, pipe);

    /* Disable Interrupt */
    /* Disable Ready Interrupt */
    hw_usb_clear_brdyenb(ptr, pipe);

    /* Disable Not Ready Interrupt */
    hw_usb_clear_nrdyenb(ptr, pipe);

    /* Disable Empty Interrupt */
    hw_usb_clear_bempenb(ptr, pipe);

    /* Disable Transaction count */
    usb_cstd_clr_transaction_counter(ptr, pipe);

    /* Check use FIFO */
    switch (useport)
    {
        /* CFIFO use */
        case USB_CUSE:
            break;

        /* D0FIFO use */
        case USB_D0USE:
            break;

        /* D1FIFO use */
        case USB_D1USE:
            break;

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
        /* D0FIFO DMA */
        case USB_D0DMA:

            /* DMA buffer clear mode clear */
            hw_usb_clear_dclrm(ptr, USB_D0DMA);
            if (USB_USBIP_0 == ip)
            {
                hw_usb_set_mbw(ptr, USB_D0DMA, USB0_D0FIFO_MBW);
            }
            else if (USB_USBIP_1 == ip)
            {
                hw_usb_set_mbw(ptr, USB_D0DMA, USB1_D0FIFO_MBW);
            }
            else
            {
                /* None */
            }

            break;

        /* D1FIFO DMA */
        case USB_D1DMA:

            /* DMA buffer clear mode clear */
            hw_usb_clear_dclrm(ptr, USB_D1DMA);
            if (USB_USBIP_0 == ip)
            {
                hw_usb_set_mbw(ptr, USB_D1DMA, USB0_D1FIFO_MBW);
            }
            else if (USB_USBIP_1 == ip)
            {
                hw_usb_set_mbw(ptr, USB_D1DMA, USB1_D1FIFO_MBW);
            }
            else
            {
                /* None */
            }

            break;
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

        default:
            break;
    }

    /* Call Back */
    if (USB_NULL != g_p_usb_pipe[pipe])
    {
        /* Check PIPE TYPE */
        if (USB_TYPFIELD_ISO != usb_cstd_get_pipe_type(ptr, pipe))
        {
            /* Transfer information set */
            g_p_usb_pipe[pipe]->tranlen = g_usb_data_cnt[pipe];
            g_p_usb_pipe[pipe]->status  = status;
            g_p_usb_pipe[pipe]->pipectr = hw_usb_read_pipectr(ptr, pipe);
            // g_p_usb_pipe[pipe]->errcnt     = (uint8_t) g_usb_hstd_ignore_cnt[ip][pipe];
            g_p_usb_pipe[pipe]->ipp = usb_hstd_get_usb_ip_adr(ip);
            g_p_usb_pipe[pipe]->ip  = ip;

            usb_utr_t* temp = g_p_usb_pipe[pipe];
            g_p_usb_pipe[pipe] =
                (usb_utr_t*)USB_NULL; // Moved by Rohan - set this to NULL before the callback so the callback can do
                                      // another transfer, which sets this to something else
            (temp->complete)(temp, 0, 0); // This does our callback on our outgoing transfers. Rohan
        }
        else
        {
            /* Transfer information set */
            g_p_usb_pipe[pipe]->tranlen = g_usb_data_cnt[pipe];
            g_p_usb_pipe[pipe]->pipectr = hw_usb_read_pipectr(ptr, pipe);
            // g_p_usb_pipe[pipe]->errcnt     = (uint8_t) g_usb_hstd_ignore_cnt[ip][pipe];
            g_p_usb_pipe[pipe]->ipp = usb_hstd_get_usb_ip_adr(ip);
            g_p_usb_pipe[pipe]->ip  = ip;

            /* Data Transfer (restart) */
            if (usb_cstd_get_pipe_dir(ptr, pipe) == USB_BUF2FIFO)
            {
                /* OUT Transfer */
                g_p_usb_pipe[pipe]->status = USB_DATA_WRITING;
                (g_p_usb_pipe[pipe]->complete)(g_p_usb_pipe[pipe], 0, 0);
            }
            else
            {
                /* IN Transfer */
                g_p_usb_pipe[pipe]->status = USB_DATA_READING;
                (g_p_usb_pipe[pipe]->complete)(g_p_usb_pipe[pipe], 0, 0);
            }
        }
    }
} /* End of function usb_hstd_data_end() */

extern usb_utr_t g_usb_midi_recv_utr[][MAX_NUM_USB_MIDI_DEVICES];

// For when data has been received, as host. Hub stuff is on PIPE9
void usb_hstd_brdy_pipe_process_rohan_midi_and_hub(usb_utr_t* ptr, uint16_t bitsts)
{
    // uint16_t    useport;
    uint16_t pipe;
    // uint16_t    ip;
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
    uint16_t buffer;
    uint16_t maxps;
    uint16_t set_dtc_brock_cnt;
    uint16_t trans_dtc_block_cnt;
    uint16_t dma_ch;
    uint16_t status;
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

    // ip = ptr->ip;
    for (pipe = USB_CFG_HMIDI_BULK_RECV_MIN; pipe <= USB_PIPE9; pipe++)
    { // We'll just do the receive pipes we actually sometimes use for MIDI, plus PIPE9 for hubs. We could also do it by
      // USB device that we know is connected...
        if (pipe == USB_CFG_HMIDI_BULK_RECV_MAX + 1)
            pipe = USB_CFG_HMIDI_INT_RECV_MIN; // Skip that one pipe

        if (bitsts & USB_BITSET(pipe))
        {
            /* Interrupt check */
            // hw_usb_clear_status_bemp(NULL, pipe);

            if (true || USB_NULL != g_p_usb_pipe[pipe])
            { // I think this'll be fine now
                /* Pipe number to FIFO port select */
                // useport = USB_CUSE;//usb_hstd_pipe2fport(ptr, i);

                /* FIFO to Buffer data read */
                // usb_hstd_fifo_to_buf(ptr, pipe, useport);
                {
                    uint16_t end_flag;

                    /* Ignore count clear */
                    g_usb_hstd_ignore_cnt[USB_CFG_USE_USBIP][pipe] = (uint16_t)0;

                    end_flag = usb_read_data_fast_rohan(
                        pipe); // Modified by Rohan to call the PSTD one, which does the exact same now

                    if (end_flag == USB_READEND)
                    { // I condensed USB_READSHRT into this too
#if 1
                        if (pipe == USB_PIPE9)
                        { // For hub - use original function. Without this, reconnecting devices to hub doesn't work,
                          // even though they do if initially connected.
                            usb_hstd_data_end(ptr, pipe, USB_DATA_OK);
                        }
                        else
#endif
                        { // For MIDI - use this optimized code

#if 0
							    /* PID = NAK */
							    /* Set NAK */
							    //usb_cstd_select_nak(ptr, pipe);
								usb_cstd_set_nak_fast_rohan(pipe);

							    /* Disable Interrupt */
							    /* Disable Ready Interrupt */
							    hw_usb_clear_brdyenb(NULL, pipe);

							    /* Disable Not Ready Interrupt */
							    hw_usb_clear_nrdyenb(NULL, pipe);

							    /* Disable Empty Interrupt */
							    hw_usb_clear_bempenb(NULL, pipe);

							    /* Disable Transaction count */
							    //usb_cstd_clr_transaction_counter(ptr, pipe);
							    hw_usb_clear_trenb(NULL, pipe);
							    hw_usb_set_trclr(NULL, pipe);
#endif

                            /* Call Back */
                            if (true || USB_NULL != g_p_usb_pipe[pipe]) // Checked above already
                            {
                                /* Check PIPE TYPE */
                                if (true || USB_TYPFIELD_ISO != usb_cstd_get_pipe_type(NULL, pipe))
                                {
                                    /* Transfer information set */
                                    /* // Doesn't get read, anyway
                                        g_p_usb_pipe[pipe]->tranlen    = g_usb_data_cnt[pipe];
                                        g_p_usb_pipe[pipe]->status     = newStatus;
                                        g_p_usb_pipe[pipe]->pipectr    = hw_usb_read_pipectr(NULL, pipe);
                                        g_p_usb_pipe[pipe]->errcnt     = (uint8_t)
                                       g_usb_hstd_ignore_cnt[USB_CFG_USE_USBIP][pipe]; g_p_usb_pipe[pipe]->ipp        =
                                       usb_hstd_get_usb_ip_adr(USB_CFG_USE_USBIP); g_p_usb_pipe[pipe]->ip         =
                                       USB_CFG_USE_USBIP;
                                        */
                                    usb_utr_t* temp = g_p_usb_pipe[pipe];
                                    // g_p_usb_pipe[pipe] = (usb_utr_t*) USB_NULL; // Moved by Rohan - set this to NULL
                                    // before the callback so the callback can do another transfer, which sets this to
                                    // something else (temp->complete)(temp, 0, 0); // This does our callback on our
                                    // outgoing transfers. Rohan

                                    unsigned int deviceNum = temp - &g_usb_midi_recv_utr[0][0];

                                    if (deviceNum < MAX_NUM_USB_MIDI_DEVICES)
                                    { // Gotta check this - I can totally see something going wrong, with all the other
                                      // checks I'm now skipping!
                                        connectedUSBMIDIDevices[0][deviceNum].numBytesReceived =
                                            64 - g_usb_data_cnt[pipe];
                                        // Warning - sometimes (with a Teensy, e.g. my knob box), length will be 0. Not
                                        // sure why - but we need to cope with that case.

                                        connectedUSBMIDIDevices[0][deviceNum].currentlyWaitingToReceive =
                                            0; // Take note that we need to set up another receive
                                    }
                                }
                            }
                        }
                    }
                    else
                    { // USB_FIFOERROR and formerly USB_READOVER
                        usb_hstd_forced_termination(ptr, pipe, USB_DATA_ERR);
                    }
                }
            }
        }
    }
}

/***********************************************************************************************************************
 Function Name   : usb_hstd_brdy_pipe_process
 Description     : Search for the PIPE No. that BRDY interrupt occurred, and
                 : request data transmission/reception from the PIPE
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     bitsts       : BRDYSTS Register & BRDYENB Register
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_brdy_pipe_process(usb_utr_t* ptr, uint16_t bitsts)
{
    uint16_t useport;
    uint16_t i;
    uint16_t ip;
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
    uint16_t buffer;
    uint16_t maxps;
    uint16_t set_dtc_brock_cnt;
    uint16_t trans_dtc_block_cnt;
    uint16_t dma_ch;
    uint16_t status;
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

    ip = ptr->ip;
    for (i = USB_PIPE1; i <= USB_MAX_PIPE_NO; i++)
    {
        if ((bitsts & USB_BITSET(i)) != 0)
        {
            /* Interrupt check */
            hw_usb_clear_status_bemp(ptr, i);

            if (USB_NULL != g_p_usb_pipe[i])
            {
                /* Pipe number to FIFO port select */
                useport = usb_hstd_pipe2fport(ptr, i);
                if ((USB_D0DMA == useport) || (USB_D1DMA == useport))
                {
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
                    dma_ch = usb_dma_ref_ch_no(ip, useport);

                    maxps = g_usb_cstd_dma_fifo[ip][dma_ch];

                    /* DMA Transfer request disable */
                    hw_usb_clear_dreqe(ptr, useport);

                    /* DMA stop */
                    usb_dma_stop_dxfifo(ptr->ip, useport);

                    /* Changes FIFO port by the pipe. */
                    buffer = usb_cstd_is_set_frdy(ptr, i, useport, USB_FALSE);

                    set_dtc_brock_cnt = (uint16_t)((g_usb_hstd_data_cnt[ip][g_usb_cstd_dma_pipe[ip][dma_ch]] - 1)
                                                   / g_usb_cstd_dma_fifo[ip][dma_ch])
                                        + 1;
                    trans_dtc_block_cnt = usb_dma_get_crtb(dma_ch);

                    /* Get D0fifo Receive Data Length */
                    g_usb_cstd_dma_size[ip][dma_ch] = usb_dma_get_n0tb(dma_ch) - usb_dma_get_crtb(dma_ch);
                    g_usb_cstd_dma_size[ip][dma_ch] -= g_usb_cstd_dma_size[ip][dma_ch] % maxps;
                    if (g_usb_cstd_dma_size[ip][dma_ch] >= maxps)
                    {
                        g_usb_cstd_dma_size[ip][dma_ch] -= maxps;
                    }
                    g_usb_cstd_dma_size[ip][dma_ch] += (uint32_t)(buffer & USB_DTLN);

                    /* Check data count */
                    if (g_usb_cstd_dma_size[ip][dma_ch] == g_usb_hstd_data_cnt[ptr->ip][i])
                    {
                        status = USB_DATA_OK;
                    }
                    else if (g_usb_cstd_dma_size[ip][dma_ch] > g_usb_hstd_data_cnt[ip][i])
                    {
                        status = USB_DATA_OVR;
                    }
                    else
                    {
                        status = USB_DATA_SHT;
                    }
                    /* D0FIFO access DMA stop */
                    usb_cstd_dxfifo_stop(ptr, useport);
                    /* End of data transfer */
                    usb_hstd_data_end(ptr, i, status);
                    /* Set BCLR */
                    hw_usb_set_bclr(ptr, useport);

#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */
                }
                else
                {
                    if (usb_cstd_get_pipe_dir(ptr, i) == USB_BUF2FIFO)
                    {
                        /* Buffer to FIFO data write */
                        usb_hstd_buf2fifo(ptr, i, useport);
                    }
                    else
                    {
                        /* FIFO to Buffer data read */
                        usb_hstd_fifo_to_buf(ptr, i, useport);
                    }
                }
            }
        }
    }
} /* End of function usb_hstd_brdy_pipe_process() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_nrdy_pipe_process
 Description     : Search for PIPE No. that occurred NRDY interrupt, and execute
                 : the process for PIPE when NRDY interrupt occurred
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     bitsts       : NRDYSTS Register & NRDYENB Register
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_nrdy_pipe_process(usb_utr_t* ptr, uint16_t bitsts)
{
    uint16_t buffer;
    uint16_t i;

    for (i = USB_MIN_PIPE_NO; i <= USB_MAX_PIPE_NO; i++)
    {
        if ((bitsts & USB_BITSET(i)) != 0)
        {
            /* Interrupt check */
            if (USB_NULL != g_p_usb_pipe[i])
            {
                if (usb_cstd_get_pipe_type(ptr, i) == USB_TYPFIELD_ISO)
                {
                    /* Wait for About 60ns */
                    buffer = hw_usb_read_frmnum(ptr);
                    if ((buffer & USB_OVRN) == USB_OVRN)
                    {
                        /* @1 */
                        /* End of data transfer */
                        usb_hstd_forced_termination(ptr, i, (uint16_t)USB_DATA_OVR);
                        USB_PRINTF1("###ISO OVRN %d\n", g_usb_hstd_data_cnt[ptr->ip][i]);
                    }
                    else
                    {
                        /* @2 */
                        /* End of data transfer */
                        usb_hstd_forced_termination(ptr, i, (uint16_t)USB_DATA_ERR);
                    }
                }
                else
                {
                    usb_hstd_nrdy_endprocess(ptr, i);
                }
            }
        }
    }
} /* End of function usb_hstd_brdy_pipe_process() */

void usb_hstd_bemp_pipe_process_rohan_midi(usb_utr_t* ptr, uint16_t bitsts)
{
    uint16_t buffer;
    uint16_t pipe;
    uint16_t useport;

    // We only have one SEND pipe for BULK endpoints (standard for MIDI), and one for INTERRUPT endpoints (which our
    // dumb Actition pedal uses). It's not necessary to check PIPE9 for the hub - I guess we must only receive, not
    // send, on that pipe.

    // Bulk send pipe
    pipe = USB_CFG_HMIDI_BULK_SEND;

goAgain:
    if ((bitsts & USB_BITSET(pipe)) != 0)
    {
        /* Interrupt check */
        if (true || USB_NULL != g_p_usb_pipe[pipe]) // No real harm if this somehow was NULL
        {
            volatile uint16_t* p_reg;
#if USB_CFG_USE_USBIP == USB_CFG_IP0
            p_reg = (uint16_t*)&(USB200.PIPE1CTR) + (pipe - 1);
#else
            p_reg = (uint16_t*)&(USB201.PIPE1CTR) + (pipe - 1);
#endif
            buffer = *p_reg; // usb_cstd_get_pid(ptr, pipe);

            /* MAX packet size error ? */
            if (buffer & USB_PID_STALL) // Probably couldn't happen...
            {
                USB_PRINTF1("### STALL Pipe %d\n", pipe);
                usb_hstd_forced_termination(ptr, pipe, (uint16_t)USB_DATA_STALL);
            }
            else
            {
                if (pipe == USB_CFG_HMIDI_INT_SEND
                    || !(buffer & USB_INBUFM)) // For some reason, the original code only needs this check for BULK, not
                                               // INTERRUPT
                {
                    /* End of data transfer */
                    // usb_hstd_data_end(ptr, pipe, (uint16_t) USB_DATA_NONE);
                    {

                        // This stuff not needed after all. We were getting occasional i029 when disconnecting /
                        // reconnecting many devices with hub, even before this change, and still now.
#if 0
					    /* PID = NAK */
					    /* Set NAK */
					    //usb_cstd_select_nak(ptr, pipe);
						usb_cstd_set_nak_fast_rohan(pipe);

					    /* Disable Interrupt */
					    /* Disable Ready Interrupt */
					    hw_usb_clear_brdyenb(NULL, pipe);

					    /* Disable Not Ready Interrupt */
					    hw_usb_clear_nrdyenb(NULL, pipe);

					    /* Disable Empty Interrupt */
					    hw_usb_clear_bempenb(NULL, pipe);

					    /* Disable Transaction count */
					    //usb_cstd_clr_transaction_counter(ptr, pipe);
					    hw_usb_clear_trenb(USB_NULL, pipe);
					    hw_usb_set_trclr(USB_NULL, pipe);
#endif

                        /* Call Back */
                        if (true || USB_NULL != g_p_usb_pipe[pipe]) // Already checked above
                        {
                            /* Check PIPE TYPE */
                            if (true || USB_TYPFIELD_ISO != usb_cstd_get_pipe_type(ptr, pipe))
                            {
                                /* Transfer information set */
                                /* // This doesn't get ready anyway
                                g_p_usb_pipe[pipe]->tranlen    = g_usb_data_cnt[pipe];
                                g_p_usb_pipe[pipe]->status     = USB_DATA_NONE;
                                g_p_usb_pipe[pipe]->pipectr    = hw_usb_read_pipectr(ptr, pipe);
                                g_p_usb_pipe[pipe]->errcnt     = (uint8_t)
                                g_usb_hstd_ignore_cnt[USB_CFG_USE_USBIP][pipe]; g_p_usb_pipe[pipe]->ipp        =
                                usb_hstd_get_usb_ip_adr(USB_CFG_USE_USBIP); g_p_usb_pipe[pipe]->ip         =
                                USB_CFG_USE_USBIP;
                                */

                                usb_utr_t* temp    = g_p_usb_pipe[pipe];
                                g_p_usb_pipe[pipe] = (usb_utr_t*)
                                    USB_NULL; // Moved by Rohan - set this to NULL before the callback so the callback
                                              // can do another transfer, which sets this to something else
                                //(temp->complete)(temp, 0, 0); // This does our callback on our outgoing transfers.
                                // Rohan
                                usbSendCompleteAsHost(USB_CFG_USE_USBIP);
                            }
                        }
                    }
                }
            }
        }
    }

    if (pipe == USB_CFG_HMIDI_BULK_SEND)
    {
        // Interrupt send pipe
        pipe = USB_CFG_HMIDI_INT_SEND;
        goto goAgain;
    }
}

/***********************************************************************************************************************
 Function Name   : usb_hstd_bemp_pipe_process
 Description     : Search for PIPE No. that BEMP interrupt occurred, and complete data transmission for the PIPE
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     bitsts       : BEMPSTS Register & BEMPENB Register
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_bemp_pipe_process(usb_utr_t* ptr, uint16_t bitsts)
{
    uint16_t buffer;
    uint16_t i;
    uint16_t useport;

    for (i = USB_MIN_PIPE_NO; i <= USB_PIPE5; i++)
    {
        if ((bitsts & USB_BITSET(i)) != 0)
        {
            /* Interrupt check */
            if (USB_NULL != g_p_usb_pipe[i])
            {
                buffer = usb_cstd_get_pid(ptr, i);

                /* MAX packet size error ? */
                if ((buffer & USB_PID_STALL) == USB_PID_STALL)
                {
                    USB_PRINTF1("### STALL Pipe %d\n", i);
                    usb_hstd_forced_termination(ptr, i, (uint16_t)USB_DATA_STALL);
                }
                else
                {
                    if ((hw_usb_read_pipectr(ptr, i) & USB_INBUFM) != USB_INBUFM)
                    {
                        /* Pipe number to FIFO port select */
                        useport = usb_hstd_pipe2fport(ptr, i);

                        if (USB_D0DMA == useport)
                        {
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
                            hw_usb_clear_status_bemp(ptr, i);
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */
                        }

                        /* End of data transfer */
                        usb_hstd_data_end(ptr, i, (uint16_t)USB_DATA_NONE);
                    }
                }
            }
        }
    }
    for (i = USB_PIPE6; i <= USB_MAX_PIPE_NO; i++)
    {
        /* Interrupt check */
        if ((bitsts & USB_BITSET(i)) != 0)
        {
            if (USB_NULL != g_p_usb_pipe[i])
            {
                buffer = usb_cstd_get_pid(ptr, i);

                /* MAX packet size error ? */
                if ((buffer & USB_PID_STALL) == USB_PID_STALL)
                {
                    /*USB_PRINTF1("### STALL Pipe %d\n", i);*/
                    usb_hstd_forced_termination(ptr, i, (uint16_t)USB_DATA_STALL);
                }
                else
                {
                    /* End of data transfer */
                    usb_hstd_data_end(ptr, i, (uint16_t)USB_DATA_NONE);
                }
            }
        }
    }
} /* End of function usb_hstd_bemp_pipe_process() */

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
