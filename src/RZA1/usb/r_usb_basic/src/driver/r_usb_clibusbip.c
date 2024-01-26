/**********************************************************************************************************************
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
 *********************************************************************************************************************/
/**********************************************************************************************************************
 * File Name    : r_usb_clibusbip.c
 * Description  : USB IP Host and Peripheral low level library
 *********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 *********************************************************************************************************************/

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_reg_access.h"

// Added by Rohan
#include "RZA1/mtu/mtu.h"
#include "RZA1/usb/r_usb_hmidi/src/inc/r_usb_hmidi.h"
#include "definitions.h"

#if defined(USB_CFG_HCDC_USE)
#include "r_usb_hcdc_if.h"
#endif /* defined(USB_CFG_PCDC_USE) */

#if defined(USB_CFG_HHID_USE)
#include <drivers/usb/r_usb_hmidi/r_usb_hmidi_if.h>
#endif /* defined(USB_CFG_HMSC_USE) */

#if defined(USB_CFG_HMSC_USE)
#include "r_usb_hmsc_if.h"
#endif /* defined(USB_CFG_HMSC_USE) */

#if defined(USB_CFG_PCDC_USE)
#include "r_usb_pcdc_if.h"
#endif /* defined(USB_CFG_PCDC_USE) */

#if defined(USB_CFG_PMSC_USE)
#include "drivers/usb/r_usb_pmsc/r_usb_pmsc_if.h"
#endif /* defined(USB_CFG_PMSC_USE) */

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
#include "drivers/usb/r_usb_basic/src/hw/inc/r_usb_dmac.h"
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

/**********************************************************************************************************************
 Exported global variables
 *********************************************************************************************************************/
extern uint16_t g_usb_usbmode;

#if defined(USB_CFG_HMSC_USE)
extern uint8_t g_drive_search_lock;
extern uint8_t g_drive_search_que[];
extern uint8_t g_drive_search_que_cnt;
#endif /* defined(USB_CFG_HMSC_USE) */

/**********************************************************************************************************************
 Renesas Abstracted Driver API functions
 *********************************************************************************************************************/

/**********************************************************************************************************************
 Function Name   : usb_cstd_nrdy_enable
 Description     : Enable NRDY interrupt of the specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : none
 *********************************************************************************************************************/
void usb_cstd_nrdy_enable(usb_utr_t* ptr, uint16_t pipe)
{
    /* Enable NRDY */
    hw_usb_set_nrdyenb(ptr, pipe);
} /* End of function usb_pstd_DriverRelease() */

/**********************************************************************************************************************
 Function Name   : usb_cstd_get_pid
 Description     : Fetch specified pipe's PID.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t PID-bit status
 *********************************************************************************************************************/
uint16_t usb_cstd_get_pid(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t buf;

    /* PIPE control reg read */
    buf = hw_usb_read_pipectr(ptr, pipe);

    return (uint16_t)(buf & USB_PID);
} /* End of function usb_pstd_DriverRelease() */

extern uint16_t pipeMaxPs[];

/**********************************************************************************************************************
 Function Name   : usb_cstd_get_maxpacket_size
 Description     : Fetch MaxPacketSize of the specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t MaxPacketSize
 *********************************************************************************************************************/
uint16_t usb_cstd_get_maxpacket_size(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t size;
    uint16_t buffer;

    if (USB_PIPE0 == pipe)
    {
        buffer = hw_usb_read_dcpmaxp(ptr);
    }
    else
    {
        buffer = pipeMaxPs[pipe];
    }

    /* Max Packet Size */
    size = (uint16_t)(buffer & USB_MXPS);

    return size;
} /* End of function usb_pstd_DriverRelease() */

/**********************************************************************************************************************
 Function Name   : usb_cstd_get_pipe_dir
 Description     : Get PIPE DIR
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t                  : Pipe direction.
 *********************************************************************************************************************/
uint16_t usb_cstd_get_pipe_dir(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t buffer;

    /* Pipe select */
    hw_usb_write_pipesel(ptr, pipe);

    /* Read Pipe direction */
    buffer = hw_usb_read_pipecfg(ptr);
    return (uint16_t)(buffer & USB_DIRFIELD);
} /* End of function usb_pstd_DriverRelease() */

/**********************************************************************************************************************
 Function Name   : usb_cstd_get_pipe_type
 Description     : Fetch and return PIPE TYPE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : uint16_t                   : Pipe type
 *********************************************************************************************************************/
uint16_t usb_cstd_get_pipe_type(usb_utr_t* ptr, uint16_t pipe)
{
    uint16_t buffer;

    /* Pipe select */
    hw_usb_write_pipesel(ptr, pipe);

    /* Read Pipe direction */
    buffer = hw_usb_read_pipecfg(ptr);

    return (uint16_t)(buffer & USB_TYPFIELD);
} /* End of function usb_pstd_DriverRelease() */

extern uint16_t pipeCfgs[];
uint16_t usb_cstd_get_pipe_type_from_memory(uint16_t pipe)
{ // Function by Rohan
    return pipeCfgs[pipe] & USB_TYPFIELD;
}

uint16_t usb_cstd_get_pipe_dir_from_memory(uint16_t pipe)
{ // Function by Rohan
    return pipeCfgs[pipe] & USB_DIRFIELD;
}

/**********************************************************************************************************************
 Function Name   : usb_cstd_do_aclrm
 Description     : Set the ACLRM-bit (Auto Buffer Clear Mode) of the specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : none
 *********************************************************************************************************************/
void usb_cstd_do_aclrm(usb_utr_t* ptr, uint16_t pipe)
{
    /* Control ACLRM */
    hw_usb_set_aclrm(ptr, pipe);
    hw_usb_clear_aclrm(ptr, pipe);
} /* End of function usb_pstd_DriverRelease() */

/**********************************************************************************************************************
 Function Name   : usb_cstd_set_buf
 Description     : Set PID (packet ID) of the specified pipe to BUF.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : none
 *********************************************************************************************************************/
void usb_cstd_set_buf(usb_utr_t* ptr, uint16_t pipe)
{
    /* PIPE control reg set */
    hw_usb_set_pid(ptr, pipe, USB_PID_BUF);
} /* End of function usb_pstd_DriverRelease() */

/**********************************************************************************************************************
 Function Name   : usb_cstd_clr_stall
 Description     : Set up to NAK the specified pipe, and clear the STALL-bit set
                 : to the PID of the specified pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
 Return value    : none
 Note            : PID is set to NAK.
 *********************************************************************************************************************/
void usb_cstd_clr_stall(usb_utr_t* ptr, uint16_t pipe)
{
    /* Set NAK */
    usb_cstd_set_nak(ptr, pipe);

    /* Clear STALL */
    hw_usb_clear_pid(ptr, pipe, USB_PID_STALL);
} /* End of function usb_pstd_DriverRelease() */

/**********************************************************************************************************************
 Function Name   : usb_cstd_port_speed
 Description     : Get USB-speed of the specified port.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Root port.
 Return value    : uint16_t                  : HSCONNECT : Hi-Speed
                 :                           : FSCONNECT : Full-Speed
                 :                           : LSCONNECT : Low-Speed
                 :                           : NOCONNECT : not connect
 *********************************************************************************************************************/
uint16_t usb_cstd_port_speed(usb_utr_t* ptr, uint16_t port)
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
} /* End of function usb_pstd_DriverRelease() */

/**********************************************************************************************************************
 Function Name   : usb_set_event
 Description     : Set event.
 Arguments       : uint16_t     event        : Event code.
                 : usb_ctrl_t   *p_ctrl      : Control structure for USB API.
 Return value    : none
 *********************************************************************************************************************/
void usb_set_event(uint16_t event, usb_ctrl_t* ctrl)
{
    g_usb_cstd_event.code[g_usb_cstd_event.write_pointer] = event;
    g_usb_cstd_event.ctrl[g_usb_cstd_event.write_pointer] = *ctrl;
    g_usb_cstd_event.write_pointer++;
    if (g_usb_cstd_event.write_pointer >= USB_EVENT_MAX)
    {
        g_usb_cstd_event.write_pointer = 0;
    }
} /* End of function usb_pstd_DriverRelease() */

extern uint8_t usb_scheduler_id_use;
extern uint8_t usb_scheduler_schedule_flag;
/**********************************************************************************************************************
 Function Name   : usb_cstd_usb_task
 Description     : USB driver main loop processing.
 Arguments       : none
 Return value    : none
 *********************************************************************************************************************/
void usb_cstd_usb_task(void)
{
    if (USB_HOST == g_usb_usbmode)
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
#if defined(USB_CFG_HMSC_USE)
        do
        {
#endif /* defined(USB_CFG_HMSC_USE) */
doMoreIfAnyMore:
            {
            }
            // uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];

            usb_cstd_scheduler();                        /* Scheduler */
            if (USB_FLGSET == usb_cstd_check_schedule()) /* Check for any task processing requests flags. */
            {
                switch (usb_scheduler_id_use)
                {
                    case USB_HCD_MBX:
                        usb_hstd_hcd_task((usb_vp_int_t)0); /* HCD Task */
                        break;

                    case USB_MGR_MBX:
                        usb_hstd_mgr_task((usb_vp_int_t)0); /* MGR Task */
                        break;

                    case USB_HUB_MBX:
                        usb_hhub_task((usb_vp_int_t)0); /* HUB Task */
                        break;

                    case USB_HMIDI_MBX:
                        usb_class_task();
                        break;
                }

                /*
            uint16_t endTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
            uint16_t duration = endTime - startTime;
            uint32_t timePassedNS = superfastTimerCountToNS(duration);
            uartPrint("host task duration, nSec: ");
            uartPrintNumber(timePassedNS);
            */
                goto doMoreIfAnyMore; // By Rohan. Want to process everything immediately - especially cos of that bug
                                      // which means all data reads must be done before any data writes. (Wait, does
                                      // that still exist?)
            }
#if defined(USB_CFG_HMSC_USE)
        } while (USB_FALSE != g_drive_search_lock);
#endif /* defined(USB_CFG_HMSC_USE) */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        usb_pstd_pcd_task();
#if defined(USB_CFG_PMSC_USE)
        usb_pmsc_task();
#endif /* defined(USB_CFG_PMSC_USE) */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
} /* End of function usb_cstd_usb_task() */

/**********************************************************************************************************************
 Function Name   : usb_class_task
 Description     :
 Arguments       : none
 Return value    : none
 *********************************************************************************************************************/
void usb_class_task(void)
{
#if defined(USB_CFG_HMSC_USE)
    usb_utr_t utr;
    uint16_t addr;

    R_USB_HmscTask();          /* USB Host MSC driver task */
    R_USB_HmscStrgDriveTask(); /* HSTRG Task */

    if (USB_FALSE == g_drive_search_lock)
    {
        if (g_drive_search_que_cnt > 0)
        {
            g_drive_search_lock = g_drive_search_que[0];

            utr.ip = USB_IP0;
            if ((g_drive_search_lock & USB_IP_MASK) == USBA_ADDRESS_OFFSET)
            {
                utr.ip = USB_IP1;
            }

            addr    = g_drive_search_lock & USB_ADDRESS_MASK;
            utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip); /* Get the USB IP base address. */

            /* Storage drive search. */
            R_USB_HmscStrgDriveSearch(&utr, addr, (usb_cb_t)usb_hmsc_drive_complete);
        }
    }
#endif /* defined(USB_CFG_HMSC_USE) */

#if defined(USB_CFG_HCDC_USE)
    R_USB_HcdcTask((usb_vp_int_t)0); /* USB Host CDC driver task */
#endif                               /* defined(USB_CFG_HCDC_USE) */

#if defined(USB_CFG_HHID_USE)
    R_USB_HhidTask((usb_vp_int_t)0); /* USB Host CDC driver task */
#endif                               /* defined(USB_CFG_HCDC_USE) */

#if defined(USB_CFG_HMIDI_USE)
    usb_hmidi_task(0);
#endif /* defined(USB_CFG_HCDC_USE) */

} /* End of function usb_class_task */

/**********************************************************************************************************************
 End Of File
 *********************************************************************************************************************/
