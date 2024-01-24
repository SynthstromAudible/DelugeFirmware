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
 * Copyright (C) 2017 Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/
/***********************************************************************************************************************
 * File Name    : r_usb_pmsc_apl.c
 * Device(s)    : RZ/A1H
 * Tool-Chain   : ARM Development Studio�iDS-5TM�j Version 5.26, KPIT GNUARM-RZ v14.01
 * Description  : USB Peripheral Mass-Storage Sample Code.
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version  Description
 *         : 29.09.2017 1.00     First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
Includes   <System Includes> , "Project Includes"
***********************************************************************************************************************/
#include "RZA1/system/r_typedefs.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"

#include "deluge/drivers/uart/uart.h"

#define WHICH_USB_MODULE USB_IP0

extern uint8_t g_midi_device[];
extern uint8_t g_midi_configuration[];
extern uint8_t* g_midi_string_table[];

static usb_descriptor_t usb_descriptor = {
    (uint8_t*)g_midi_device,        /* Pointer to the device descriptor */
    (uint8_t*)g_midi_configuration, /* Pointer to the configuration descriptor for Full-speed */
    (uint8_t*)0,                    /* Pointer to the configuration descriptor for High-speed
                                     * Does not work in FS mode on most devices if this is a copy of the FS config.
                                     * It's unused under FS so set to null
                                     */

    (uint8_t*)0,                   // Don't think we need this /* Pointer to the qualifier descriptor */
    (uint8_t**)g_midi_string_table /* Pointer to the string descriptor table */
};

extern uint8_t usbCurrentlyInitialized;

// Check lock and lock it before calling any of these!

void openUSBHost(void)
{
    usb_ctrl_t ctrl;
    usb_cfg_t cfg;

    ctrl.module   = WHICH_USB_MODULE;
    ctrl.type     = USB_HHID;
    cfg.usb_speed = USB_FS;
    cfg.usb_mode  = USB_HOST;
    usb_err_t err = R_USB_Open(&ctrl, &cfg);
    if (USB_SUCCESS != err)
    {
        uartPrintln("Failed to open USB0\n");
    }
    usbCurrentlyInitialized = true;
}

void closeUSBHost(void)
{

    usb_ctrl_t ctrl;
    ctrl.module = WHICH_USB_MODULE;
    ctrl.type   = USB_HHID;

    R_USB_Close(&ctrl);

    usbCurrentlyInitialized = false;
}

void openUSBPeripheral(void)
{
    usb_ctrl_t ctrl;
    usb_cfg_t cfg;

    ctrl.module        = WHICH_USB_MODULE;
    ctrl.type          = USB_PMSC;
    cfg.usb_speed      = USB_FS;
    cfg.usb_mode       = USB_PERI;
    cfg.p_usb_reg      = &usb_descriptor;
    usb_err_t ret_code = R_USB_Open(&ctrl, &cfg); /* Initializes the USB module */

    usbCurrentlyInitialized = true;
}

void closeUSBPeripheral(void) // You can't immediately close this after opening it - you'd better wait a little while.
{
    usb_ctrl_t ctrl;
    ctrl.module = WHICH_USB_MODULE;
    ctrl.type   = USB_PMSC;

    usb_err_t ret_code = R_USB_Close(&ctrl);
    if (USB_SUCCESS != ret_code)
    {
        uartPrintln("Failed to close peripheral");
    }

    usbCurrentlyInitialized = false;
}
