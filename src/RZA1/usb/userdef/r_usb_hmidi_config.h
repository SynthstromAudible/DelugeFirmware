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
 * File Name    : r_usb_hhid_config_reference.h
 * Description  : USB Host HID configuration
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 04.01.2014 1.00 First Release
 *         : 30.01.2015 1.01    Added RX71M.
 ***********************************************************************************************************************/

#ifndef R_USB_HMIDI_CONFIG_H
#define R_USB_HMIDI_CONFIG_H

/*****************************************************************************
 Macro definitions (USER DEFINE)
 ******************************************************************************/
/** [Setting pipe to be used]
 * USB_CFG_HHID_INT_IN      : Pipe number (USB_PIPE6 to USB_PIPE9) 1st Device HID Interrupt In Pipe
 * USB_CFG_HHID_INT_IN2     : Pipe number (USB_PIPE6 to USB_PIPE9) 2nd Device HID Interrupt In Pipe
 * USB_CFG_HHID_INT_IN3     : Pipe number (USB_PIPE6 to USB_PIPE9) 3rd Device HID Interrupt In Pipe
 */
#define USB_CFG_HMIDI_BULK_SEND (USB_PIPE1)
#define USB_CFG_HMIDI_INT_SEND  (USB_PIPE6)

#define USB_CFG_HMIDI_BULK_RECV_MIN (USB_PIPE2)
#define USB_CFG_HMIDI_BULK_RECV_MAX (USB_PIPE5)

#define USB_CFG_HMIDI_INT_RECV_MIN (USB_PIPE7)
#define USB_CFG_HMIDI_INT_RECV_MAX (USB_PIPE8)

// Hub uses PIPE9

#endif /* R_USB_HMIDI_CONFIG_H */
/******************************************************************************
 End  Of File
 ******************************************************************************/
