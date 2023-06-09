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
 * File Name    : r_usb_hmidi.h
 * Description  : USB Host HID class driver header
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 04.01.2014 1.00 First Release
 *         : 30.01.2015 1.01    Support Multi device.
 ***********************************************************************************************************************/
#ifndef R_USB_HMIDI_H
#define R_USB_HMIDI_H

#include "r_usb_basic_if.h"

/*****************************************************************************
 Macro definitions
 ******************************************************************************/

#define USB_HMIDI_CLSDATASIZE (512)

/* Host HID Task */                   // TODO: should these be changed if I re-introduce actual HID?
#define USB_HMIDI_TSK (USB_TID_4)     /* Task ID */
#define USB_HMIDI_MBX (USB_HMIDI_TSK) /* Mailbox ID */
#define USB_HMIDI_MPL (USB_HMIDI_TSK) /* Memorypool ID */

/*****************************************************************************
 Enumerated Types
 ******************************************************************************/
/* Host HID Task Command */
typedef enum {
    USB_HHID_TCMD_OPEN,
    USB_HHID_TCMD_SEND,
    USB_HHID_TCMD_RECEIVE,
    USB_HHID_TCMD_CLASS_REQ,
    USB_HHID_TCMD_DATA_TRANS
} usb_hhid_tcmd_t;

/* Enumeration Sequence */
typedef enum {
    /* Enumeration Sequence Complete */
    USB_HHID_ENUM_COMPLETE = 0,

    /* Enumeration Sequence String Descriptor #0 receive request */
    USB_HHID_ENUM_STR_DT0_REQ,

    /* Enumeration Sequence String Descriptor #0 Receive complete */
    USB_HHID_ENUM_STR_DT0_WAIT,

    /* StringDescriptor iProduct Receive complete */
    USB_HHID_ENUM_STR_IPRODUCT_WAIT
} usb_hhid_enum_seq_t;

/*****************************************************************************
 Struct definition
 ******************************************************************************/
typedef struct
{
    uint16_t devadr;
    usb_regadr_t ipp; /* IP Address(USB0 or USB1)*/
    uint16_t ip;      /* IP number(0 or 1) */
    uint16_t brequest_code;
    void* p_tranadr;  /* Transfer data Start address */
    uint32_t tranlen; /* Transfer data length */
    uint16_t index;
    uint16_t duration;
    uint8_t set_protocol;
    usb_cb_t complete; /* Call Back Function Info */
} usb_hhid_class_request_parm_t;

/******************************************************************************
 Exported global variables
 ******************************************************************************/
extern uint16_t g_usb_hmidi_maxps[USB_NUM_USBIP][USB_MAXDEVADDR]; /* Max Packet Size */
extern uint16_t g_usb_hmidi_devaddr[];                            /* Device Address */
extern uint16_t g_usb_hmidi_speed[];                              /* Device speed */
extern uint16_t g_usb_hmidi_enum_seq[];                           /* Enumeration Sequence */
extern uint16_t* g_p_usb_hmidi_pipe_table[];                      /* Pipe Table(DefEP) */
extern uint8_t* g_p_usb_hmidi_interface_table[];                  /* Interface Descriptor Table */
extern uint8_t* g_p_usb_hmidi_device_table[];                     /* Device Descriptor Table */
extern uint8_t* g_p_usb_hmidi_config_table[];                     /* Configuration Descriptor Table */

/*****************************************************************************
 Public Functions
 ******************************************************************************/
/* Functions */
void usb_hmidi_task(usb_vp_int_t stacd);
uint16_t usb_hmidi_pipe_info(usb_utr_t* ptr, uint8_t* table, uint16_t speed, uint16_t length);
uint16_t usb_hmidi_get_string_desc(usb_utr_t* ptr, uint16_t addr, uint16_t string, usb_cb_t complete);
void hmidi_configured(usb_utr_t* ptr, uint16_t dev_addr, uint16_t data2);
void hmidi_detach(usb_utr_t* ptr, uint16_t devadr, uint16_t data2);
void hmidi_resume_complete(usb_utr_t* ptr, uint16_t devadr, uint16_t data2);

#endif /* R_USB_HMIDI_H */

/******************************************************************************
 End Of File
 ******************************************************************************/
