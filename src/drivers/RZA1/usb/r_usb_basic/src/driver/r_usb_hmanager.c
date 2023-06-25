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
 * File Name    : r_usb_hmanager.c
 * Description  : USB Host Control Manager
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "r_usb_basic_if.h"
#include "r_usb_typedef.h"
#include "r_usb_extern.h"
#include "r_usb_bitdefine.h"
#include "r_usb_reg_access.h"
#include "definitions.h"

#if HAVE_OLED
#include "oled.h"
#else
#include "numericdriver.h"
#endif

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Private global variables and functions
 ***********************************************************************************************************************/
static uint16_t usb_shstd_std_request[USB_NUM_USBIP][5];
static uint16_t usb_shstd_dummy_data;
static usb_utr_t usb_shstd_std_req_msg[USB_NUM_USBIP];

/* Condition compilation by the difference of the operating system */
static uint16_t usb_shstd_reg_pointer[USB_NUM_USBIP];
static usb_mgrinfo_t* p_usb_shstd_mgr_msg[USB_NUM_USBIP];
static uint16_t usb_shstd_mgr_msginfo[USB_NUM_USBIP];
static usb_cb_t usb_shstd_mgr_callback[USB_NUM_USBIP];
static uint16_t usb_shstd_suspend_seq[USB_NUM_USBIP];
static uint16_t usb_shstd_resume_seq[USB_NUM_USBIP];
static void usb_hstd_susp_cont(usb_utr_t* ptr, uint16_t devaddr, uint16_t rootport);
static void usb_hstd_resu_cont(usb_utr_t* ptr, uint16_t devaddr, uint16_t rootport);

static uint16_t usb_hstd_chk_device_class(usb_utr_t* ptr, usb_hcdreg_t* driver, uint16_t port);
static void usb_hstd_enumeration_err(uint16_t Rnum);
static uint16_t usb_hstd_cmd_submit(usb_utr_t* ptr, usb_cb_t complete);
static uint16_t usb_hstd_chk_remote(usb_utr_t* ptr);

static void usb_hstd_mgr_rel_mpl(usb_utr_t* ptr, uint16_t n);

/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/
/* Manager */
uint16_t g_usb_hstd_enum_seq[USB_NUM_USBIP]; /* Enumeration request */
uint16_t g_usb_hstd_device_descriptor[USB_NUM_USBIP][USB_DEVICESIZE / 2u];
uint16_t g_usb_hstd_config_descriptor[USB_NUM_USBIP][USB_CONFIGSIZE / 2u];
uint16_t g_usb_hstd_suspend_pipe[USB_NUM_USBIP][USB_MAX_PIPE_NO + 1u];

uint16_t g_usb_hstd_check_enu_result[USB_NUM_USBIP];
uint8_t g_usb_hstd_enu_wait[USB_NUM_USBIP + (USB_NUM_USBIP % 2u)];

uint8_t g_usb_hstd_class_data[USB_NUM_USBIP][CLSDATASIZE];
usb_utr_t g_usb_hstd_class_ctrl[USB_NUM_USBIP];
uint16_t g_usb_hstd_class_request[USB_NUM_USBIP][5];

/* Enumeration Table */
void (*g_usb_hstd_enumaration_process[8])(usb_utr_t*, uint16_t, uint16_t) = {
    usb_hstd_enum_get_descriptor,
    usb_hstd_enum_set_address,
    usb_hstd_enum_get_descriptor,
    usb_hstd_enum_get_descriptor,
    usb_hstd_enum_get_descriptor,
    usb_hstd_enum_get_descriptor,
    usb_hstd_enum_set_configuration,
    usb_hstd_enum_dummy_request,
};

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
uint16_t g_usb_disp_param_set;
usb_compliance_t g_usb_disp_param;
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

/***********************************************************************************************************************
 Exported global variables
 ***********************************************************************************************************************/
/* Enumeration Table */
extern void (*g_usb_hstd_enumaration_process[8])(usb_utr_t*, uint16_t, uint16_t); /* Enumeration Table */

/***********************************************************************************************************************
 Function Name   : usb_hstd_mgr_rel_mpl
 Description     : Release a memory block.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     rootport     : Root port number
 Return          : none
 ***********************************************************************************************************************/
static void usb_hstd_mgr_rel_mpl(usb_utr_t* ptr, uint16_t n)
{
    usb_er_t err;

    err = USB_REL_BLK(USB_MGR_MPL, (usb_mh_t)p_usb_shstd_mgr_msg[ptr->ip]);
    if (USB_OK != err)
    {
        USB_PRINTF1("### USB MGR rel_blk error: %d\n", n);
    }
} /* End of function usb_hstd_mgr_rel_mpl() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_mgr_chgdevst_cb
 Description     : Call the callback function specified by the argument given to
                 : the API usb_hstd_change_device_state.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     rootport     : Port no.
 Return          : none
 ***********************************************************************************************************************/
static void usb_hstd_mgr_chgdevst_cb(usb_utr_t* ptr, uint16_t rootport)
{
    if (0 != usb_shstd_mgr_msginfo[ptr->ip])
    {
        (*usb_shstd_mgr_callback[ptr->ip])(ptr, rootport, usb_shstd_mgr_msginfo[ptr->ip]);
        usb_shstd_mgr_msginfo[ptr->ip] = 0;
    }
} /* End of function usb_hstd_mgr_chgdevst_cb() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_enumeration
 Description     : Execute enumeration on the connected USB device.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return          : uint16_t                  : Enumeration status.
 ***********************************************************************************************************************/
static uint16_t usb_hstd_enumeration(usb_utr_t* ptr)
{
#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
    uint16_t vendor_id;
    uint16_t product_id;
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

    uint16_t md;
    uint16_t flg;
    usb_hcdreg_t* driver;
    uint16_t enume_mode; /* Enumeration mode (device state) */
    uint8_t* descriptor_table;
    uint16_t rootport;
    uint16_t pipenum;
    uint16_t devsel;
    usb_ctrl_t ctrl;

    /* Attach Detect Mode */
    enume_mode = USB_NONDEVICE;

    pipenum = p_usb_shstd_mgr_msg[ptr->ip]->keyword;

    /* Agreement device address */
    devsel = usb_hstd_get_devsel(ptr, pipenum);

    /* Get root port number from device addr */
    rootport = usb_hstd_get_rootport(ptr, devsel);

    /* Manager Mode Change */
    switch (p_usb_shstd_mgr_msg[ptr->ip]->result)
    {
        case USB_CTRL_END:
            enume_mode = USB_DEVICEENUMERATION;
            switch (g_usb_hstd_enum_seq[ptr->ip])
            {
                /* Receive Device Descriptor */
                case 0:
                    break;

                /* Set Address */
                case 1:

                    descriptor_table = (uint8_t*)g_usb_hstd_device_descriptor[ptr->ip];
                    devsel           = (uint16_t)(g_usb_hstd_device_addr[ptr->ip] << USB_DEVADDRBIT);

                    /* Set device speed */
                    usb_hstd_set_dev_addr(ptr, devsel, g_usb_hstd_device_speed[ptr->ip], rootport);
                    g_usb_hstd_dcp_register[ptr->ip][g_usb_hstd_device_addr[ptr->ip]] =
                        (uint16_t)((uint16_t)(descriptor_table[7] & USB_MAXPFIELD) | devsel);

                    break;

                /* Receive Device Descriptor(18) */
                case 2:
                    break;

                /* Receive Configuration Descriptor(9) */
                case 3:
                    break;

                /* Receive Configuration Descriptor(xx) */
                case 4:

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                    descriptor_table = (uint8_t*)g_usb_hstd_device_descriptor[ptr->ip];

                    /* If 'vendor_id' and 'product_id' value is defined by PET, */
                    /*    system works for compliance test mode. */
                    /* Values ??defined by PET is the following. */
                    /* vendor_id : 0x1A0A  */
                    /* product_id : 0x0101 - 0x0108 , 0x0200 */

                    vendor_id = (uint16_t)(descriptor_table[USB_DEV_ID_VENDOR_L]
                                           + ((uint16_t)descriptor_table[USB_DEV_ID_VENDOR_H] << 8));

                    if (0x1A0A == vendor_id)
                    {
                        product_id = (uint16_t)(descriptor_table[USB_DEV_ID_PRODUCT_L]
                                                + ((uint16_t)descriptor_table[USB_DEV_ID_PRODUCT_H] << 8));

                        descriptor_table = (uint8_t*)g_usb_hstd_config_descriptor[ptr->ip];
#if USB_CFG_ELECTRICAL == USB_CFG_ENABLE

                        if ((0x0100 < product_id) && (product_id < 0x0109))
                        {
                            usb_hstd_electrical_test_mode(ptr, product_id, rootport);
                            enume_mode = USB_NOTTPL;
                            break;
                        }

#endif /* USB_CFG_ELECTRICAL == USB_CFG_ENABLE */

                        if (0x0200 == product_id)
                        {
                            g_usb_hstd_enum_seq[ptr->ip]++;
                            break;
                        }
                    }

#endif /* USB_CFG_ELECTRICAL == USB_CFG_ENABLE */

                    /* Device enumeration function */
                    switch (usb_hstd_enum_function1())
                    {
                        /* Driver open */
                        case USB_OK:

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                            g_usb_disp_param_set = USB_OFF;
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

                            for (flg = 0u, md = 0; (md < g_usb_hstd_device_num[ptr->ip]) && (0u == flg); md++)
                            {
                                driver = &g_usb_hstd_device_drv[ptr->ip][md];
                                if (USB_DETACHED == driver->devstate)
                                {
                                    uint16_t retval;
                                    retval = usb_hstd_chk_device_class(ptr, driver, rootport);
                                    g_usb_hstd_check_enu_result[ptr->ip] = USB_OK;

                                    /* In this function, check device class of       */
                                    /*              enumeration flow move to class   */
                                    /* "usb_hstd_return_enu_mgr()" is used to return */
                                    if (USB_OK == retval)
                                    {
                                        usb_shstd_reg_pointer[ptr->ip] = md;
                                        flg                            = 1; /* break; */
                                    }
                                }
                            }

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                            if (USB_ON == g_usb_disp_param_set)
                            {
                                usb_compliance_disp((void*)&g_usb_disp_param);
                            }
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

                            if (1 != flg)
                            {
                                // By Rohan. Means couldn't find an available "driver" for this device. It could be a 2nd hub.
#if HAVE_OLED
                                consoleTextIfAllBootedUp("USB device not recognized");
#else
                                displayPopupIfAllBootedUp("UNKN");
#endif
                                ctrl.address = g_usb_hstd_device_addr[ptr->ip]; /* USB Device address */
                                ctrl.module  = ptr->ip;                         /* Module number setting */
                                usb_set_event(USB_STS_NOT_SUPPORT, &ctrl);      /* Set Event()  */
#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                                g_usb_hstd_enum_seq[ptr->ip] = g_usb_hstd_enum_seq[ptr->ip] + 2;

#else /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */
                                g_usb_hstd_enum_seq[ptr->ip]++;

#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */
                            }

                            break;

                            /* Descriptor error */
                        case USB_ERROR:

                            USB_PRINTF0("### Enumeration is stoped(ClassCode-ERROR)\n");
                            enume_mode = USB_NOTTPL; /* Attach Detect Mode */

                            break;

                        default:

                            enume_mode = USB_NONDEVICE; /* Attach Detect Mode */

                            break;
                    }

                    break;

                    /* Class Check Result */
                case 5:

                    switch (g_usb_hstd_check_enu_result[ptr->ip])
                    {
                        case USB_OK:

                            driver = &g_usb_hstd_device_drv[ptr->ip][usb_shstd_reg_pointer[ptr->ip]];
                            g_usb_hstd_device_info[ptr->ip][g_usb_hstd_device_addr[ptr->ip]][0] =
                                rootport; /* Root port */
                            driver->rootport = rootport;
                            driver->devaddr  = g_usb_hstd_device_addr[ptr->ip];

                            break;

                        case USB_ERROR:

                            enume_mode = USB_NOTTPL;

                            break;

                        default:

                            enume_mode = USB_NONDEVICE;

                            break;
                    }

                    break;

                /* Set Configuration */
                case 6:

                    /* Device enumeration function */
                    if (usb_hstd_enum_function2(&enume_mode) == USB_TRUE)
                    {
                        USB_PRINTF0(" Configured Device\n");
                        for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
                        {
                            driver = &g_usb_hstd_device_drv[ptr->ip][md];
                            if (g_usb_hstd_device_addr[ptr->ip] == driver->devaddr)
                            {
                                /* Device state */
                                g_usb_hstd_device_info[ptr->ip][g_usb_hstd_device_addr[ptr->ip]][1] = USB_CONFIGURED;

                                /* Device speed */
                                g_usb_hstd_device_info[ptr->ip][g_usb_hstd_device_addr[ptr->ip]][4] =
                                    g_usb_hstd_device_speed[ptr->ip];

                                /* Device state */
                                driver->devstate = USB_CONFIGURED;
                                (*driver->devconfig)(
                                    ptr, g_usb_hstd_device_addr[ptr->ip], (uint16_t)USB_NO_ARG); /* Call Back */
                                return (USB_COMPLETEPIPESET);
                            }
                        }
                        enume_mode = USB_COMPLETEPIPESET;
                    }

                    break;

                default:
                    break;
            }
            g_usb_hstd_enum_seq[ptr->ip]++;

            /* Device Enumeration */
            if (USB_DEVICEENUMERATION == enume_mode)
            {
                switch (g_usb_hstd_enum_seq[ptr->ip])
                {
                    case 1:
                        (*g_usb_hstd_enumaration_process[1])(
                            ptr, (uint16_t)USB_DEVICE_0, g_usb_hstd_device_addr[ptr->ip]);

                        break;

                    case 5:
                        break;

                    case 6:

                        descriptor_table = (uint8_t*)g_usb_hstd_config_descriptor[ptr->ip];

                        /* Device state */
                        g_usb_hstd_device_info[ptr->ip][g_usb_hstd_device_addr[ptr->ip]][2] = descriptor_table[5];
                        (*g_usb_hstd_enumaration_process[6])(
                            ptr, g_usb_hstd_device_addr[ptr->ip], (uint16_t)(descriptor_table[5]));

                        break;

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                    case 7:

                        enume_mode = USB_NOTTPL;

                        break;
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

                    default:
                        (*g_usb_hstd_enumaration_process[g_usb_hstd_enum_seq[ptr->ip]])(
                            ptr, g_usb_hstd_device_addr[ptr->ip], g_usb_hstd_enum_seq[ptr->ip]);

                        break;
                }
            }

            break;

        case USB_DATA_ERR:

            USB_PRINTF0("### Enumeration is stoped(SETUP or DATA-ERROR)\n");
            usb_hstd_enumeration_err(g_usb_hstd_enum_seq[ptr->ip]);

            break;

        case USB_DATA_OVR:

            USB_PRINTF0("### Enumeration is stoped(receive data over)\n");
            usb_hstd_enumeration_err(g_usb_hstd_enum_seq[ptr->ip]);

            break;

        case USB_DATA_STALL:

            USB_PRINTF0("### Enumeration is stoped(SETUP or DATA-STALL)\n");
            usb_hstd_enumeration_err(g_usb_hstd_enum_seq[ptr->ip]);

            /* Device enumeration function */
            usb_hstd_enum_function4(&g_usb_hstd_enum_seq[ptr->ip], &enume_mode, g_usb_hstd_device_addr[ptr->ip]);

            break;

        default:

            USB_PRINTF0("### Enumeration is stoped(result error)\n");
            usb_hstd_enumeration_err(g_usb_hstd_enum_seq[ptr->ip]);

            break;
    }
    return (enume_mode);
} /* End of function usb_hstd_enumeration() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_enumeration_err
 Description     : Output error information when enumeration error occurred.
 Argument        : uint16_t     Rnum         : Enumeration sequence
 Return          : none
 ***********************************************************************************************************************/
static void usb_hstd_enumeration_err(uint16_t Rnum)
{
/* Condition compilation by the difference of useful function */
#if defined(USB_DEBUG_ON)
    switch (Rnum)
    {
        case 0:

            USB_PRINTF0(" Get_DeviceDescrip(8)\n");

            break;

        case 1:

            USB_PRINTF0(" Set_Address\n");

            break;

        case 2:

            USB_PRINTF0(" Get_DeviceDescrip(18)\n");

            break;

        case 3:

            USB_PRINTF0(" Get_ConfigDescrip(9)\n");

            break;

        case 4:

            USB_PRINTF0(" Get_ConfigDescrip(xx)\n");

            break;

        case 5: /* Device enumeration function */

            usb_hstd_enum_function5();

            break;

        case 6:

            USB_PRINTF0(" Set_Configuration\n");

            break;

        default:
            break;
    }
#endif /* defined(USB_DEBUG_ON) */
} /* End of function usb_hstd_enumeration_err() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_chk_device_class
 Description     : Interface class search
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : usb_hcdreg_t *driver      : Class driver
                 : uint16_t     port         : Port no.
 Return          : uint16_t                  : USB_OK / USB_ERROR
 ***********************************************************************************************************************/
static uint16_t usb_hstd_chk_device_class(usb_utr_t* ptr, usb_hcdreg_t* driver, uint16_t port)
{
    uint8_t* descriptor_table;
    uint16_t total_length1;
    uint16_t total_length2;
    uint16_t result;
    uint16_t hub_device;
    uint16_t* table[9];
    uint16_t tmp4;
    uint16_t tmp5;
    uint16_t tmp6;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t id_check;
    uint16_t i;
    usb_ctrl_t ctrl;

    descriptor_table = (uint8_t*)g_usb_hstd_device_descriptor[ptr->ip];

    /* Device class check */
    tmp4       = descriptor_table[USB_DEV_B_DEVICE_CLASS];
    tmp5       = descriptor_table[USB_DEV_B_DEVICE_SUBCLASS];
    tmp6       = descriptor_table[USB_DEV_B_DEVICE_PROTOCOL];
    hub_device = USB_OK;
    if (((0xFF == tmp4) && (0xFF == tmp5)) && (0xFF == tmp6))
    {
        USB_PRINTF0("*** Vendor specific device.\n\n");
    }
    else if (((USB_IFCLS_HUB == tmp4) && (0x00 == tmp5)) && (0x00 == tmp6))
    {
        USB_PRINTF0("*** Full-Speed HUB device.\n\n");
        hub_device = USB_FSHUB;
    }
    else if (((USB_IFCLS_HUB == tmp4) && (0x00 == tmp5)) && (0x01 == tmp6))
    {
        USB_PRINTF0("*** High-Speed single TT device.\n\n");
        hub_device = USB_HSHUBS;
    }
    else if (((USB_IFCLS_HUB == tmp4) && (0x00 == tmp5)) && (0x02 == tmp6))
    {
        USB_PRINTF0("*** High-Speed multiple TT device.\n\n");
        hub_device = USB_HSHUBM;
    }
    else if (((0 != tmp4) || (0 != tmp5)) || (0 != tmp6))
    {
        USB_PRINTF0("### Device class information error!\n\n");
    }
    else
    {
        /* Non */
    }

    id_check = USB_ERROR;
    for (i = 0; i < driver->p_tpl[0]; i++)
    {
        vendor_id =
            (uint16_t)(descriptor_table[USB_DEV_ID_VENDOR_L] + ((uint16_t)descriptor_table[USB_DEV_ID_VENDOR_H] << 8));
        product_id = (uint16_t)(descriptor_table[USB_DEV_ID_PRODUCT_L]
                                + ((uint16_t)descriptor_table[USB_DEV_ID_PRODUCT_H] << 8));

        if ((driver->p_tpl[(i * 2) + 2] == USB_NOVENDOR) || (driver->p_tpl[(i * 2) + 2] == vendor_id))
        {

            if ((driver->p_tpl[(i * 2) + 3] == USB_NOPRODUCT) || (driver->p_tpl[(i * 2) + 3] == product_id))
            {
                id_check = USB_OK;
#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                g_usb_disp_param.status = USB_CT_TPL;
                g_usb_disp_param.pid    = product_id;
                g_usb_disp_param.vid    = vendor_id;
                g_usb_disp_param_set    = USB_ON;
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */
            }
        }
    }

    if (id_check == USB_ERROR)
    {
        USB_PRINTF0("### Not support device\n");
#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
        if (descriptor_table[4] == USB_IFCLS_HUB)
        {
            g_usb_disp_param.status = USB_CT_HUB;
            g_usb_disp_param.pid    = product_id;
            g_usb_disp_param.vid    = vendor_id;
            g_usb_disp_param_set    = USB_ON;
        }
        else
        {
            g_usb_disp_param.status = USB_CT_NOTTPL;
            g_usb_disp_param.pid    = product_id;
            g_usb_disp_param.vid    = vendor_id;
            g_usb_disp_param_set    = USB_ON;
        }
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

        //        ctrl.address    = g_usb_hstd_device_addr[ptr->ip];  /* USB Device address */
        //        ctrl.module     = ptr->ip;                          /* Module number setting */
        //        usb_set_event(USB_STS_NOT_SUPPORT, &ctrl);          /* Set Event()  */

        return USB_ERROR;
    }

    descriptor_table = (uint8_t*)g_usb_hstd_config_descriptor[ptr->ip];
    total_length1    = 0;
    total_length2    = (uint16_t)(descriptor_table[USB_DEV_W_TOTAL_LENGTH_L]
                               + ((uint16_t)descriptor_table[USB_DEV_W_TOTAL_LENGTH_H] << 8));

    if (total_length2 > USB_CONFIGSIZE)
    {
        total_length2 = USB_CONFIGSIZE;
    }

    /* Search within configuration total-length */
    while (total_length1 < total_length2)
    {
        switch (descriptor_table[total_length1 + 1])
        {
            /* Configuration Descriptor ? */
            case USB_DT_CONFIGURATION:

                table[1] = (uint16_t*)&descriptor_table[total_length1];

                break;

                /* Interface Descriptor ? */
            case USB_DT_INTERFACE:

                if ((uint16_t)descriptor_table[total_length1 + 5] == driver->ifclass)
                {

                    // If audio class, ensure MIDI subclass. Rohan
                    if (descriptor_table[total_length1 + 5] == USB_IFCLS_AUD
                        && descriptor_table[total_length1 + 6] != 0x03)
                        break;

                    result   = USB_ERROR;
                    table[0] = (uint16_t*)&g_usb_hstd_device_descriptor[ptr->ip];
                    table[2] = (uint16_t*)&descriptor_table[total_length1];
                    table[3] = &result;
                    table[4] = &hub_device;
                    table[5] = &port;
                    table[6] = &g_usb_hstd_device_speed[ptr->ip];
                    table[7] = &g_usb_hstd_device_addr[ptr->ip];
                    table[8] = (uint16_t*)driver->p_pipetbl;
                    (*driver->classcheck)(ptr, (uint16_t**)&table);

                    /* Interface Class */
                    g_usb_hstd_device_info[ptr->ip][g_usb_hstd_device_addr[ptr->ip]][3] =
                        descriptor_table[total_length1 + 5];
                    return result;
                }

                /*          USB_PRINTF2("*** Interface class is 0x%02x (not 0x%02x)\n", 
                 descriptor_table[total_length1 + 5], driver->ifclass);*/

                break;

            default:
                break;
        }
        total_length1 += descriptor_table[total_length1];
        if (0 == descriptor_table[total_length1])
        {
            break;
        }
    }
    return USB_ERROR;
} /* End of function usb_hstd_chk_device_class() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_notif_ator_detach
 Description     : Notify MGR (manager) task that attach or detach occurred.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     result       : Result.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_notif_ator_detach(usb_utr_t* ptr, uint16_t result, uint16_t port)
{
    usb_hstd_mgr_snd_mbx(ptr, (uint16_t)USB_MSG_MGR_AORDETACH, port, result);
} /* End of function usb_hstd_notif_ator_detach() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_ovcr_notifiation
 Description     : Notify MGR (manager) task that overcurrent was generated
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_ovcr_notifiation(usb_utr_t* ptr, uint16_t port)
{
    usb_hstd_mgr_snd_mbx(ptr, (uint16_t)USB_MSG_MGR_OVERCURRENT, port, (uint16_t)0u);
} /* End of function usb_hstd_ovcr_notifiation() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_status_result
 Description     : This is a callback as a result of calling 
                 : usb_hstd_change_device_state. This notifies the MGR (manager) 
                 : task that the change of USB Device status completed.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number.
                 : uint16_t     result       : Result.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_status_result(usb_utr_t* ptr, uint16_t port, uint16_t result)
{
    usb_hstd_mgr_snd_mbx(ptr, (uint16_t)USB_MSG_MGR_STATUSRESULT, port, result);
} /* End of function usb_hstd_status_result() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_submit_result
 Description     : Callback after completion of a standard request.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data1        : Not used.
                 : uint16_t     data2        : Not used.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_submit_result(usb_utr_t* ptr, uint16_t data1, uint16_t data2)
{
    usb_hstd_mgr_snd_mbx(ptr, (uint16_t)USB_MSG_MGR_SUBMITRESULT, ptr->keyword, ptr->status);
} /* End of function usb_hstd_submit_result() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_enum_get_descriptor
 Description     : Send GetDescriptor to the connected USB device.
 Argument        : uint16_t addr             : Device Address
                 : uint16_t cnt_value        : Enumeration sequence
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_enum_get_descriptor(usb_utr_t* ptr, uint16_t addr, uint16_t cnt_value)
{
    uint8_t* data_table;

    switch (cnt_value)
    {
        case 0:
        case 1:
        case 5:

            usb_shstd_std_request[ptr->ip][0] = (USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE);
            usb_shstd_std_request[ptr->ip][1] = (uint16_t)USB_DEV_DESCRIPTOR;
            usb_shstd_std_request[ptr->ip][2] = (uint16_t)0x0000;
            usb_shstd_std_request[ptr->ip][3] = (uint16_t)0x0040;
            if (usb_shstd_std_request[ptr->ip][3] > USB_DEVICESIZE)
            {
                usb_shstd_std_request[ptr->ip][3] = USB_DEVICESIZE;
            }
            usb_shstd_std_req_msg[ptr->ip].p_tranadr = g_usb_hstd_device_descriptor[ptr->ip];

            break;

        case 2:

            usb_shstd_std_request[ptr->ip][0] = (USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE);
            usb_shstd_std_request[ptr->ip][1] = (uint16_t)USB_DEV_DESCRIPTOR;
            usb_shstd_std_request[ptr->ip][2] = (uint16_t)0x0000;
            usb_shstd_std_request[ptr->ip][3] = (uint16_t)0x0012;
            if (usb_shstd_std_request[ptr->ip][3] > USB_DEVICESIZE)
            {
                usb_shstd_std_request[ptr->ip][3] = USB_DEVICESIZE;
            }
            usb_shstd_std_req_msg[ptr->ip].p_tranadr = g_usb_hstd_device_descriptor[ptr->ip];

            break;

        case 3:

            usb_shstd_std_request[ptr->ip][0] = (USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE);
            usb_shstd_std_request[ptr->ip][1] = (uint16_t)USB_CONF_DESCRIPTOR;
            usb_shstd_std_request[ptr->ip][2] = (uint16_t)0x0000;
            usb_shstd_std_request[ptr->ip][3] = (uint16_t)0x0009;
            usb_shstd_std_req_msg[ptr->ip].p_tranadr = g_usb_hstd_config_descriptor[ptr->ip];

            break;

        case 4:

            data_table                        = (uint8_t*)g_usb_hstd_config_descriptor[ptr->ip];
            usb_shstd_std_request[ptr->ip][0] = (USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE);
            usb_shstd_std_request[ptr->ip][1] = (uint16_t)USB_CONF_DESCRIPTOR;
            usb_shstd_std_request[ptr->ip][2] = (uint16_t)0x0000;
            usb_shstd_std_request[ptr->ip][3] = (uint16_t)(((uint16_t)data_table[3] << 8) + (uint16_t)data_table[2]);
            if (usb_shstd_std_request[ptr->ip][3] > USB_CONFIGSIZE)
            {
                usb_shstd_std_request[ptr->ip][3] = USB_CONFIGSIZE;
                USB_PRINTF0("***WARNING Descriptor size over !\n");
            }
            usb_shstd_std_req_msg[ptr->ip].p_tranadr = g_usb_hstd_config_descriptor[ptr->ip];

            break;

        default:

            return;

            break;
    }
    usb_shstd_std_request[ptr->ip][4]       = addr;
    usb_shstd_std_req_msg[ptr->ip].keyword  = (uint16_t)USB_PIPE0;
    usb_shstd_std_req_msg[ptr->ip].tranlen  = (uint32_t)usb_shstd_std_request[ptr->ip][3];
    usb_shstd_std_req_msg[ptr->ip].p_setup  = usb_shstd_std_request[ptr->ip];
    usb_shstd_std_req_msg[ptr->ip].status   = USB_DATA_NONE;
    usb_shstd_std_req_msg[ptr->ip].complete = (usb_cb_t)&usb_hstd_submit_result;
    usb_shstd_std_req_msg[ptr->ip].segment  = USB_TRAN_END;

    usb_shstd_std_req_msg[ptr->ip].ipp = ptr->ipp;
    usb_shstd_std_req_msg[ptr->ip].ip  = ptr->ip;

    usb_hstd_transfer_start(&usb_shstd_std_req_msg[ptr->ip]);
} /* End of function usb_hstd_enum_get_descriptor() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_enum_set_address
 Description     : Send SetAddress to the connected USB device.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device Address.
                 : uint16_t     setaddr      : New address.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_enum_set_address(usb_utr_t* ptr, uint16_t addr, uint16_t setaddr)
{
    usb_shstd_std_request[ptr->ip][0]        = (USB_SET_ADDRESS | USB_HOST_TO_DEV | USB_STANDARD | USB_DEVICE);
    usb_shstd_std_request[ptr->ip][1]        = setaddr;
    usb_shstd_std_request[ptr->ip][2]        = (uint16_t)0x0000;
    usb_shstd_std_request[ptr->ip][3]        = (uint16_t)0x0000;
    usb_shstd_std_request[ptr->ip][4]        = addr;
    usb_shstd_std_req_msg[ptr->ip].keyword   = (uint16_t)USB_PIPE0;
    usb_shstd_std_req_msg[ptr->ip].p_tranadr = (void*)&usb_shstd_dummy_data;
    usb_shstd_std_req_msg[ptr->ip].tranlen   = (uint32_t)usb_shstd_std_request[ptr->ip][3];
    usb_shstd_std_req_msg[ptr->ip].p_setup   = usb_shstd_std_request[ptr->ip];
    usb_shstd_std_req_msg[ptr->ip].status    = USB_DATA_NONE;
    usb_shstd_std_req_msg[ptr->ip].complete  = (usb_cb_t)&usb_hstd_submit_result;
    usb_shstd_std_req_msg[ptr->ip].segment   = USB_TRAN_END;

    usb_shstd_std_req_msg[ptr->ip].ipp = ptr->ipp;
    usb_shstd_std_req_msg[ptr->ip].ip  = ptr->ip;

    usb_hstd_transfer_start(&usb_shstd_std_req_msg[ptr->ip]);
} /* End of function usb_hstd_enum_set_address() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_enum_set_configuration
 Description     : Send SetConfiguration to the connected USB device.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device Address.
                 : uint16_t     confnum      : Configuration number.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_enum_set_configuration(usb_utr_t* ptr, uint16_t addr, uint16_t confnum)
{
    usb_shstd_std_request[ptr->ip][0]        = (USB_SET_CONFIGURATION | USB_HOST_TO_DEV | USB_STANDARD | USB_DEVICE);
    usb_shstd_std_request[ptr->ip][1]        = confnum;
    usb_shstd_std_request[ptr->ip][2]        = (uint16_t)0x0000;
    usb_shstd_std_request[ptr->ip][3]        = (uint16_t)0x0000;
    usb_shstd_std_request[ptr->ip][4]        = addr;
    usb_shstd_std_req_msg[ptr->ip].keyword   = (uint16_t)USB_PIPE0;
    usb_shstd_std_req_msg[ptr->ip].p_tranadr = (void*)&usb_shstd_dummy_data;
    usb_shstd_std_req_msg[ptr->ip].tranlen   = (uint32_t)usb_shstd_std_request[ptr->ip][3];
    usb_shstd_std_req_msg[ptr->ip].p_setup   = usb_shstd_std_request[ptr->ip];
    usb_shstd_std_req_msg[ptr->ip].status    = USB_DATA_NONE;
    usb_shstd_std_req_msg[ptr->ip].complete  = (usb_cb_t)&usb_hstd_submit_result;
    usb_shstd_std_req_msg[ptr->ip].segment   = USB_TRAN_END;

    usb_shstd_std_req_msg[ptr->ip].ipp = ptr->ipp;
    usb_shstd_std_req_msg[ptr->ip].ip  = ptr->ip;

    usb_hstd_transfer_start(&usb_shstd_std_req_msg[ptr->ip]);
} /* End of function usb_hstd_enum_set_configuration() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_enum_dummy_request
 Description     : Dummy function.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device Address.
                 : uint16_t     cnt_value    : Enumeration Sequence
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_enum_dummy_request(usb_utr_t* ptr, uint16_t addr, uint16_t cnt_value)
{
    /* Non */
} /* End of function usb_hstd_enum_dummy_request() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_mgr_suspend
 Description     : Suspend request
 Argument        : uint16_t     info         : Pointer to usb_utr_t structure.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_mgr_suspend(usb_utr_t* ptr, uint16_t info)
{
    uint16_t rootport;
    uint16_t devaddr;
    uint16_t devsel;
    uint16_t j;

    devaddr = p_usb_shstd_mgr_msg[ptr->ip]->keyword;
    devsel  = (uint16_t)(devaddr << USB_DEVADDRBIT);

    /* Get root port number from device addr */
    rootport = usb_hstd_get_rootport(ptr, devsel);

    if (usb_hstd_chk_dev_addr(ptr, devsel, rootport) != USB_NOCONNECT)
    {
        /* PIPE suspend */
        for (j = USB_MIN_PIPE_NO; j <= USB_MAX_PIPE_NO; j++)
        {
            /* Agreement device address */
            if (usb_hstd_get_devsel(ptr, j) == devsel)
            {
                /* PID=BUF ? */
                if (usb_cstd_get_pid(ptr, j) == USB_PID_BUF)
                {
                    usb_cstd_set_nak(ptr, j);
                    g_usb_hstd_suspend_pipe[ptr->ip][j] = USB_SUSPENDED;
                }
            }
        }
        usb_shstd_suspend_seq[ptr->ip] = 0;
        usb_hstd_susp_cont(ptr, (uint16_t)devaddr, (uint16_t)rootport);
        g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_SUSPENDED_PROCESS;
    }
    usb_hstd_mgr_rel_mpl(ptr, info);
} /* End of function usb_hstd_mgr_suspend() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_device_state_ctrl
 Description     : Setup a call to the function usb_hstd_change_device_state to re-
                 : quest the connected USB Device to change status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device Address
                 : uint16_t     msginfo      : Request type.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_device_state_ctrl(usb_utr_t* ptr, uint16_t devaddr, uint16_t msginfo)
{
    switch (devaddr)
    {
        case 0:

            USB_PRINTF0("### usbd_message device address error\n");

            break;

        case USB_DEVICEADDR:

            usb_hstd_hcd_snd_mbx(ptr, msginfo, USB_PORT0, (uint16_t*)0, &usb_hstd_status_result);

            break;

        default:

            if (USB_HUBDPADDR <= devaddr)
            {
                /* Non */
            }

            break;
    }
} /* End of function usb_hstd_device_state_ctrl() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_device_state_ctrl2
 Description     : Setup a call to the function usb_hstd_change_device_state to re-
                 : quest the connected USB Device to change status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : usb_cb_t     complete     : Callback function Pointer
                 : uint16_t     devaddr      : Device address
                 : uint16_t     msginfo      : Request type for HCD
                 : uint16_t     mgr_msginfo  : Request type for MGR
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_device_state_ctrl2(
    usb_utr_t* ptr, usb_cb_t complete, uint16_t devaddr, uint16_t msginfo, uint16_t mgr_msginfo)
{
    usb_shstd_mgr_callback[ptr->ip] = complete;
    usb_shstd_mgr_msginfo[ptr->ip]  = mgr_msginfo;
    usb_hstd_device_state_ctrl(ptr, devaddr, msginfo);
} /* End of function usb_hstd_device_state_ctrl2() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_mgr_reset
 Description     : Request HCD (Host Control Driver) to do a USB bus reset.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device address.
 Return          : none
***********************************************************************************************************************/
void usb_hstd_mgr_reset(usb_utr_t* ptr, uint16_t addr)
{
    usb_hstd_device_state_ctrl(ptr, addr, (uint16_t)USB_MSG_HCD_USBRESET);
    if (USB_DEVICEADDR == addr)
    {
        g_usb_hstd_mgr_mode[ptr->ip][USB_PORT0] = USB_DEFAULT;
    }
    else
    {
        /* Non */
    }
} /* End of function usb_hstd_mgr_reset() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_mgr_reset
 Description     : Request HCD (Host Control Driver) to do a USB bus reset.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     info         : Information.
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_mgr_resume(usb_utr_t* ptr, uint16_t info)
{
    uint16_t rootport;
    uint16_t devaddr;
    uint16_t devsel;

    devaddr = p_usb_shstd_mgr_msg[ptr->ip]->keyword;
    devsel  = (uint16_t)(devaddr << USB_DEVADDRBIT);

    /* Get root port number from device addr */
    rootport = usb_hstd_get_rootport(ptr, devsel);
    if (usb_hstd_chk_dev_addr(ptr, devsel, rootport) != USB_NOCONNECT)
    {
        /* Device resume */
        usb_hstd_device_state_ctrl(ptr, devaddr, p_usb_shstd_mgr_msg[ptr->ip]->msginfo);
        g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_RESUME_PROCESS;
        usb_shstd_resume_seq[ptr->ip]          = 0;
    }
    usb_hstd_mgr_rel_mpl(ptr, info);
} /* End of function usb_hstd_mgr_resume() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_susp_cont
 Description     : Suspend the connected USB Device (Function for nonOS)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device Address
                 : uint16_t     rootport     : Port number.
 Return          : none
 ***********************************************************************************************************************/
static void usb_hstd_susp_cont(usb_utr_t* ptr, uint16_t devaddr, uint16_t rootport)
{
    uint16_t checkerr;

    checkerr = p_usb_shstd_mgr_msg[ptr->ip]->result;

    switch (usb_shstd_suspend_seq[ptr->ip])
    {
        case 0:

            usb_hstd_get_config_desc(ptr, devaddr, (uint16_t)0x09, (usb_cb_t)&usb_hstd_submit_result);
            usb_shstd_suspend_seq[ptr->ip]++;

            break;

        case 1:

            if (usb_hstd_std_req_check(checkerr) == USB_OK)
            {
                if (usb_hstd_chk_remote(ptr) == USB_TRUE)
                {
                    usb_hstd_set_feature(ptr, devaddr, (uint16_t)0xFF, (usb_cb_t)&usb_hstd_submit_result);
                    usb_shstd_suspend_seq[ptr->ip]++;
                }
                else
                {
                    USB_PRINTF0("### Remote wakeup disable\n");
                    usb_hstd_device_state_ctrl(ptr, devaddr, (uint16_t)USB_MSG_HCD_REMOTE);
                    g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_SUSPENDED;
                }
            }

            break;

        case 2:

            if (usb_hstd_std_req_check(checkerr) == USB_OK)
            {
                usb_hstd_device_state_ctrl(ptr, devaddr, (uint16_t)USB_MSG_HCD_REMOTE);
                g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_SUSPENDED;
            }

            break;

        default:
            break;
    }
} /* End of function usb_hstd_susp_cont() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_resu_cont
 Description     : Resume the connected USB Device (Function for nonOS)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device Address
                 : uint16_t     rootport     : Port number.
 Return          : none
 ***********************************************************************************************************************/
static void usb_hstd_resu_cont(usb_utr_t* ptr, uint16_t devaddr, uint16_t rootport)
{
    uint16_t devsel;
    uint16_t j;
    uint16_t md;
    usb_hcdreg_t* driver;
    uint16_t checkerr;

    devsel   = (uint16_t)(devaddr << USB_DEVADDRBIT);
    checkerr = p_usb_shstd_mgr_msg[ptr->ip]->result;

    switch (usb_shstd_resume_seq[ptr->ip])
    {
        case 0:

            usb_hstd_get_config_desc(ptr, devaddr, (uint16_t)0x09, (usb_cb_t)&usb_hstd_submit_result);
            usb_shstd_resume_seq[ptr->ip]++;

            break;

        case 1:

            if (usb_hstd_std_req_check(checkerr) == USB_OK)
            {
                if (usb_hstd_chk_remote(ptr) == USB_TRUE)
                {
                    usb_hstd_clr_feature(ptr, devaddr, (uint16_t)0xFF, (usb_cb_t)&usb_hstd_submit_result);
                    usb_shstd_resume_seq[ptr->ip]++;
                }
                else
                {
                    g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_CONFIGURED;
                }
            }

            break;

        case 2:

            if (usb_hstd_std_req_check(checkerr) == USB_OK)
            {
                g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_CONFIGURED;
            }

            break;

        default:
            break;
    }

    if (USB_CONFIGURED == g_usb_hstd_mgr_mode[ptr->ip][rootport])
    {
        /* PIPE resume */
        for (j = USB_MIN_PIPE_NO; j <= USB_MAX_PIPE_NO; j++)
        {
            /* Agreement device address */
            if (usb_hstd_get_device_address(ptr, j) == devsel)
            {
                if (USB_SUSPENDED == g_usb_hstd_suspend_pipe[ptr->ip][j])
                {
                    usb_cstd_set_buf(ptr, j);
                    g_usb_hstd_suspend_pipe[ptr->ip][j] = USB_OK;
                }
            }
        }

        for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
        {
            driver = &g_usb_hstd_device_drv[ptr->ip][md];
            if ((rootport + USB_DEVICEADDR) == driver->devaddr)
            {
                (*driver->devresume)(ptr, driver->devaddr, (uint16_t)USB_NO_ARG);

                /* Device state */
                g_usb_hstd_device_info[ptr->ip][driver->devaddr][1] = USB_CONFIGURED;

                if (USB_DO_GLOBAL_RESUME == usb_shstd_mgr_msginfo[ptr->ip])
                {
                    usb_hstd_mgr_chgdevst_cb(ptr, rootport);
                }

                /* Device state */
                driver->devstate = USB_CONFIGURED;
            }
        }
    }
} /* End of function usb_hstd_resu_cont() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_chk_remote
 Description     : check remote
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
static uint16_t usb_hstd_chk_remote(usb_utr_t* ptr)
{
    if ((g_usb_hstd_class_data[ptr->ip][7] & USB_CF_RWUPON) != (uint8_t)0)
    {
        return USB_TRUE;
    }
    return USB_FALSE;
} /* End of function usb_hstd_chk_remote() */

/* Condition compilation by the difference of IP */
/***********************************************************************************************************************
 Function Name   : usb_hstd_cmd_submit
 Description     : command submit
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : usb_cb_t     complete     : Callback info
 Return value    : uint16_t                  : USB_OK
 ***********************************************************************************************************************/
static uint16_t usb_hstd_cmd_submit(usb_utr_t* ptr, usb_cb_t complete)
{
    g_usb_hstd_class_ctrl[ptr->ip].p_tranadr = (void*)g_usb_hstd_class_data[ptr->ip];
    g_usb_hstd_class_ctrl[ptr->ip].complete  = complete;
    g_usb_hstd_class_ctrl[ptr->ip].tranlen   = (uint32_t)g_usb_hstd_class_request[ptr->ip][3];
    g_usb_hstd_class_ctrl[ptr->ip].keyword   = USB_PIPE0;
    g_usb_hstd_class_ctrl[ptr->ip].p_setup   = g_usb_hstd_class_request[ptr->ip];
    g_usb_hstd_class_ctrl[ptr->ip].segment   = USB_TRAN_END;

    g_usb_hstd_class_ctrl[ptr->ip].ip  = ptr->ip;
    g_usb_hstd_class_ctrl[ptr->ip].ipp = ptr->ipp;

    usb_hstd_transfer_start(&g_usb_hstd_class_ctrl[ptr->ip]);

    return USB_OK;
} /* End of function usb_hstd_cmd_submit() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_set_feature
 Description     : Set SetFeature
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device Address
                 : uint16_t     epnum        : Endpoint number
                 : usb_cb_t     complete     : Callback function
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hstd_set_feature(usb_utr_t* ptr, uint16_t addr, uint16_t epnum, usb_cb_t complete)
{
    if (0xFF == epnum)
    {
        /* SetFeature(Device) */
        g_usb_hstd_class_request[ptr->ip][0] = USB_SET_FEATURE | USB_HOST_TO_DEV | USB_STANDARD | USB_DEVICE;
        g_usb_hstd_class_request[ptr->ip][1] = USB_DEV_REMOTE_WAKEUP;
        g_usb_hstd_class_request[ptr->ip][2] = (uint16_t)0x0000;
    }
    else
    {
        /* SetFeature(endpoint) */
        g_usb_hstd_class_request[ptr->ip][0] = USB_SET_FEATURE | USB_HOST_TO_DEV | USB_STANDARD | USB_ENDPOINT;
        g_usb_hstd_class_request[ptr->ip][1] = USB_ENDPOINT_HALT;
        g_usb_hstd_class_request[ptr->ip][2] = epnum;
    }
    g_usb_hstd_class_request[ptr->ip][3] = (uint16_t)0x0000;
    g_usb_hstd_class_request[ptr->ip][4] = addr;

    return usb_hstd_cmd_submit(ptr, complete);
} /* End of function usb_hstd_set_feature() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_get_config_desc
 Description     : Set GetConfigurationDescriptor
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device Address
                 : uint16_t     length       : Descriptor length
                 : usb_cb_t     complete     : Callback function
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hstd_get_config_desc(usb_utr_t* ptr, uint16_t addr, uint16_t length, usb_cb_t complete)
{
    uint16_t i;

    /* Get Configuration Descriptor */
    g_usb_hstd_class_request[ptr->ip][0] = USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE;
    g_usb_hstd_class_request[ptr->ip][1] = USB_CONF_DESCRIPTOR;
    g_usb_hstd_class_request[ptr->ip][2] = (uint16_t)0x0000;
    g_usb_hstd_class_request[ptr->ip][3] = length;
    if (g_usb_hstd_class_request[ptr->ip][3] > CLSDATASIZE)
    {
        g_usb_hstd_class_request[ptr->ip][3] = (uint16_t)CLSDATASIZE;
        USB_PRINTF0("***WARNING Descriptor size over !\n");
    }
    g_usb_hstd_class_request[ptr->ip][4] = addr;

    for (i = 0; i < g_usb_hstd_class_request[ptr->ip][3]; i++)
    {
        g_usb_hstd_class_data[ptr->ip][i] = (uint8_t)0xFF;
    }

    return usb_hstd_cmd_submit(ptr, complete);
} /* End of function usb_hstd_get_config_desc() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_std_req_check
 Description     : Sample Standard Request Check
 Arguments       : uint16_t     errcheck     : Error
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hstd_std_req_check(uint16_t errcheck)
{
    if (USB_DATA_TMO == errcheck)
    {
        USB_PRINTF0("*** Standard Request Timeout error !\n");
        return USB_ERROR;
    }
    else if (USB_DATA_STALL == errcheck)
    {
        USB_PRINTF0("*** Standard Request STALL !\n");
        return USB_ERROR;
    }
    else if (USB_CTRL_END != errcheck)
    {
        USB_PRINTF0("*** Standard Request error !\n");
        return USB_ERROR;
    }
    else
    {
        /* Non */
    }
    return USB_OK;
} /* End of function usb_hstd_std_req_check() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_get_string_desc
 Description     : Set GetDescriptor
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     addr         : Device Address
                 : uint16_t     string       : Descriptor index
                 : usb_cb_t     complete     : Callback function
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hstd_get_string_desc(usb_utr_t* ptr, uint16_t addr, uint16_t string, usb_cb_t complete)
{
    uint16_t i;

    if (0 == string)
    {
        g_usb_hstd_class_request[ptr->ip][2] = (uint16_t)0x0000;
        g_usb_hstd_class_request[ptr->ip][3] = (uint16_t)0x0004;
    }
    else
    {
        /* Set LanguageID */
        g_usb_hstd_class_request[ptr->ip][2] = (uint16_t)(g_usb_hstd_class_data[ptr->ip][2]);
        g_usb_hstd_class_request[ptr->ip][2] |= (uint16_t)((uint16_t)(g_usb_hstd_class_data[ptr->ip][3]) << 8);
        g_usb_hstd_class_request[ptr->ip][3] = (uint16_t)CLSDATASIZE;
    }
    g_usb_hstd_class_request[ptr->ip][0] = USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE;
    g_usb_hstd_class_request[ptr->ip][1] = (uint16_t)(USB_STRING_DESCRIPTOR + string);
    g_usb_hstd_class_request[ptr->ip][4] = addr;

    for (i = 0; i < g_usb_hstd_class_request[ptr->ip][3]; i++)
    {
        g_usb_hstd_class_data[ptr->ip][i] = (uint8_t)0xFF;
    }

    return usb_hstd_cmd_submit(ptr, complete);
} /* End of function usb_hstd_get_string_desc() */

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
#if USB_CFG_ELECTRICAL == USB_CFG_ENABLE
/***********************************************************************************************************************
 Function Name   : usb_hstd_electrical_test_mode
 Description     : Host electrical test mode function
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     product_id   : Task Start Code
                 : uint16_t     port         : Rootport number
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_electrical_test_mode(usb_utr_t* ptr, uint16_t product_id, uint16_t port)
{
    uint16_t brdysts;

    switch (product_id)
    {
        case 0x0101: /* Test_SE0_NAK */

            usb_hstd_test_signal(ptr, port, 3);
            while (1)
            {
                /* This loops infinitely until it's reset. */
            }

            break;

        case 0x0102: /* Test_J */

            usb_hstd_test_signal(ptr, port, 1);
            while (1)
            {
                /* This loops infinitely until it's reset. */
            }

            break;

        case 0x0103: /* Test_K */

            usb_hstd_test_signal(ptr, port, 2);
            while (1)
            {
                /* This loops infinitely until it's reset. */
            }

            break;

        case 0x0104: /* Test_Packet */

            usb_hstd_test_signal(ptr, port, 4);
            while (1)
            {
                /* This loops infinitely until it's reset. */
            }

            break;

        case 0x0105: /* Reserved */
            break;

        case 0x0106: /* HS_HOST_PORT_SUSPEND_RESUME */

            usb_cpu_delay_xms(15000); /* wait 15sec */
            usb_hstd_test_suspend(ptr, port);
            usb_cpu_delay_xms(15000); /* wait 15sec */
            usb_hstd_test_resume(ptr, port);

            break;

        case 0x0107: /* SINGLE_STEP_GET_DEV_DESC */

            usb_cpu_delay_xms(15000); /* wait 15sec */
            hw_usb_hwrite_usbreq(ptr, (USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE));
            hw_usb_hset_usbval(ptr, USB_DEV_DESCRIPTOR);
            hw_usb_hset_usbleng(ptr, 0x0012);
            hw_usb_hset_sureq(ptr);

            break;

        case 0x0108: /* SINGLE_STEP_GET_DEV_DESC_DATA */

            hw_usb_hwrite_usbreq(ptr, (USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE));
            hw_usb_hset_usbval(ptr, USB_DEV_DESCRIPTOR);
            hw_usb_hset_usbindx(ptr, 0x0000);
            hw_usb_hset_usbleng(ptr, 0x0012);
            hw_usb_hset_sureq(ptr);
            usb_cpu_delay_xms(15000); /* wait 15sec */

            usb_cstd_set_nak(ptr, USB_PIPE0);
            hw_usb_write_dcpcfg(ptr, 0);

            hw_usb_hwrite_dcpctr(ptr, USB_SQSET);
            usb_hstd_do_sqtgl(ptr, (uint16_t)USB_PIPE0, USB_SQMON);

            hw_usb_rmw_fifosel(ptr, USB_CUSE, (USB_RCNT | USB_PIPE0), (USB_RCNT | USB_ISEL | USB_CURPIPE));
            hw_usb_set_bclr(ptr, USB_CUSE);
            usb_cstd_set_buf(ptr, USB_PIPE0);
            do
            {
                brdysts = hw_usb_read_brdysts(ptr);
            } while (!(brdysts & USB_BRDY0));
            usb_cstd_set_nak(ptr, USB_PIPE0);
            hw_usb_set_sqclr(ptr, USB_PIPE0);
            hw_usb_set_bclr(ptr, USB_CUSE);

            break;

        default:
            break;
    }
} /* End of function usb_hstd_electrical_test_mode() */
#endif /* USB_CFG_ELECTRICAL == USB_CFG_ENABLE */
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

extern usb_msg_t* p_usb_scheduler_add_use;

/***********************************************************************************************************************
 Function Name   : usb_hstd_mgr_task
 Description     : The host manager (MGR) task.
 Argument        : usb_vp_int_t stacd        : Task Start Code
 Return          : none
 ***********************************************************************************************************************/
void usb_hstd_mgr_task(usb_vp_int_t stacd)
{
    usb_utr_t* mess = (usb_utr_t*)p_usb_scheduler_add_use;
    usb_utr_t* ptr;
    usb_er_t err;
    usb_hcdreg_t* driver;
    usb_hcdinfo_t* hp;
    uint16_t rootport;
    uint16_t devaddr;
    uint16_t devsel;
    uint16_t pipenum;
    uint16_t msginfo;
    uint16_t md;
    uint16_t enume_mode; /* Enumeration mode (device state) */
    uint16_t connect_speed;
    usb_ctrl_t ctrl;
#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
    usb_compliance_t disp_param;
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

    p_usb_shstd_mgr_msg[mess->ip] = (usb_mgrinfo_t*)mess;
    rootport                      = p_usb_shstd_mgr_msg[mess->ip]->keyword;
    devaddr                       = p_usb_shstd_mgr_msg[mess->ip]->keyword;
    pipenum                       = p_usb_shstd_mgr_msg[mess->ip]->keyword;
    devsel                        = (uint16_t)(devaddr << USB_DEVADDRBIT);
    hp                            = (usb_hcdinfo_t*)mess;
    ptr                           = mess;

    /* Detach is all device */
    msginfo = p_usb_shstd_mgr_msg[ptr->ip]->msginfo;
    switch (p_usb_shstd_mgr_msg[ptr->ip]->msginfo)
    {
        /* USB-bus control (change device state) */
        case USB_MSG_MGR_STATUSRESULT:
            switch (g_usb_hstd_mgr_mode[ptr->ip][rootport])
            {
                /* End of reset signal */
                case USB_DEFAULT:

                    g_usb_hstd_device_speed[ptr->ip] = p_usb_shstd_mgr_msg[ptr->ip]->result;

                    /* Set device speed */
                    usb_hstd_set_dev_addr(ptr, (uint16_t)USB_DEVICE_0, g_usb_hstd_device_speed[ptr->ip], rootport);
                    g_usb_hstd_dcp_register[ptr->ip][0] = (uint16_t)(USB_DEFPACKET + USB_DEVICE_0);
                    g_usb_hstd_enum_seq[ptr->ip]        = 0;
                    switch (g_usb_hstd_device_speed[ptr->ip])
                    {
                        case USB_HSCONNECT: /* Hi Speed Device Connect */

                            USB_PRINTF0(" Hi-Speed Device\n");
                            (*g_usb_hstd_enumaration_process[0])(ptr, (uint16_t)USB_DEVICE_0, (uint16_t)0);

                            break;

                        case USB_FSCONNECT: /* Full Speed Device Connect */

                            USB_PRINTF0(" Full-Speed Device\n");
                            (*g_usb_hstd_enumaration_process[0])(ptr, (uint16_t)USB_DEVICE_0, (uint16_t)0);

                            break;

                        case USB_LSCONNECT: /* Low Speed Device Connect */

                            USB_PRINTF0(" Low-Speed Device\n");
                            usb_hstd_ls_connect_function(ptr);

                            break;

                        default:

                            USB_PRINTF0(" Device/Detached\n");
                            g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_DETACHED;

                            break;
                    }

                    break;

                /* End of resume signal */
                case USB_CONFIGURED:

                    /* This Resume Sorce is moved to usb_hResuCont() by nonOS */
                    break;

                /* Start of suspended state */
                case USB_SUSPENDED:

                    for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
                    {
                        driver = &g_usb_hstd_device_drv[ptr->ip][md];
                        if ((rootport + USB_DEVICEADDR) == driver->devaddr)
                        {
                            (*driver->devsuspend)(ptr, driver->devaddr, (uint16_t)USB_NO_ARG);

                            if (USB_DO_GLOBAL_SUSPEND == usb_shstd_mgr_msginfo[ptr->ip])
                            {
                                usb_hstd_mgr_chgdevst_cb(ptr, rootport);
                            }

                            /* Device state */
                            g_usb_hstd_device_info[ptr->ip][driver->devaddr][1] = USB_SUSPENDED;

                            /* Device state */
                            driver->devstate = USB_SUSPENDED;
                        }
                    }

                    break;

                /* Continue of resume signal */
                case USB_RESUME_PROCESS:

                    /* Resume Sequence Number is 0 */
                    usb_hstd_resu_cont(ptr, (uint16_t)(USB_DEVICEADDR + rootport), (uint16_t)rootport);

                    break;

                case USB_DETACHED:

                    switch (usb_shstd_mgr_msginfo[ptr->ip])
                    {
                        case USB_PORT_DISABLE:

                            usb_hstd_mgr_chgdevst_cb(ptr, rootport);

                            break;

                        default:
                            break;
                    }

                    break;

                default:
                    break;
            }
            usb_hstd_mgr_rel_mpl(ptr, msginfo);

            break;

        case USB_MSG_MGR_SUBMITRESULT:

            /* Agreement device address */
            devsel = usb_hstd_get_devsel(ptr, pipenum);

            /* Get root port number from device addr */
            rootport = usb_hstd_get_rootport(ptr, devsel);
            switch (g_usb_hstd_mgr_mode[ptr->ip][rootport])
            {
                /* Resume */
                case USB_RESUME_PROCESS:

                    /* Resume Sequence Number is 1 to 2 */
                    usb_hstd_resu_cont(ptr, (uint16_t)(devsel >> USB_DEVADDRBIT), (uint16_t)rootport);

                    break;

                    /* Suspend */
                case USB_SUSPENDED_PROCESS:

                    usb_hstd_susp_cont(ptr, (uint16_t)(devsel >> USB_DEVADDRBIT), (uint16_t)rootport);

                    break;

                    /* Enumeration */
                case USB_DEFAULT:

                    /* Peripheral Device Speed support check */
                    connect_speed = usb_hstd_support_speed_check(ptr, rootport);
                    if (USB_NOCONNECT != connect_speed)
                    {
                        enume_mode = usb_hstd_enumeration(ptr);
                        switch (enume_mode)
                        {
                            /* Detach Mode */
                            case USB_NONDEVICE:

                                USB_PRINTF1("### Enumeration error (address%d)\n", g_usb_hstd_device_addr[ptr->ip]);
                                g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_DETACHED;

                                if ((USB_DO_RESET_AND_ENUMERATION == usb_shstd_mgr_msginfo[ptr->ip])
                                    || (USB_PORT_ENABLE == usb_shstd_mgr_msginfo[ptr->ip]))
                                {
                                    usb_hstd_mgr_chgdevst_cb(ptr, rootport);
                                }

                                break;

                                /* Detach Mode */
                            case USB_NOTTPL:

                                USB_PRINTF1("### Not support device (address%d)\n", g_usb_hstd_device_addr[ptr->ip]);
                                g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_DETACHED;

                                if ((USB_DO_RESET_AND_ENUMERATION == usb_shstd_mgr_msginfo[ptr->ip])
                                    || (USB_PORT_ENABLE == usb_shstd_mgr_msginfo[ptr->ip]))
                                {
                                    usb_hstd_mgr_chgdevst_cb(ptr, rootport);
                                }

                                break;

                            case USB_COMPLETEPIPESET:

                                g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_CONFIGURED;

                                if ((USB_DO_RESET_AND_ENUMERATION == usb_shstd_mgr_msginfo[ptr->ip])
                                    || (USB_PORT_ENABLE == usb_shstd_mgr_msginfo[ptr->ip]))
                                {
                                    usb_hstd_mgr_chgdevst_cb(ptr, rootport);
                                }

                                break;

                            default:
                                break;
                        }
                    }

                    break;

                default:
                    break;
            }
            usb_hstd_mgr_rel_mpl(ptr, msginfo);

            break;

        case USB_MSG_MGR_AORDETACH:

            switch (p_usb_shstd_mgr_msg[ptr->ip]->result)
            {
                case USB_DETACH:

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                    disp_param.status = USB_CT_DETTACH;
                    disp_param.pid    = USB_NULL;
                    disp_param.vid    = USB_NULL;
                    usb_compliance_disp((void*)&disp_param);
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */
                    USB_PRINTF1(" [Detach Device port%d] \n", rootport);
                    g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_DETACHED;
                    g_usb_hstd_device_speed[ptr->ip]       = USB_NOCONNECT;

                    for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
                    {
                        driver = &g_usb_hstd_device_drv[ptr->ip][md];
                        if ((rootport + USB_DEVICEADDR) == driver->devaddr)
                        {
#if defined(USB_CFG_HHID_USE) || defined(USB_CFG_HMIDI_USE)
                            if (USB_DEVICEADDR == driver->devaddr)
                            {
                                g_usb_change_device_state[ptr->ip] = USB_NULL;
                            }
#endif /* defined(USB_CFG_HHID_USE) */

                            (*driver->devdetach)(ptr, driver->devaddr, (uint16_t)USB_NO_ARG);

                            /* Root port */
                            g_usb_hstd_device_info[ptr->ip][driver->devaddr][0] = USB_NOPORT;

                            /* Device state */
                            g_usb_hstd_device_info[ptr->ip][driver->devaddr][1] = USB_DETACHED;

                            /* Not configured */
                            g_usb_hstd_device_info[ptr->ip][driver->devaddr][2] = (uint16_t)0;

                            /* Interface Class : NO class */
                            g_usb_hstd_device_info[ptr->ip][driver->devaddr][3] = (uint16_t)USB_IFCLS_NOT;

                            /* No connect */
                            g_usb_hstd_device_info[ptr->ip][driver->devaddr][4] = (uint16_t)USB_NOCONNECT;

                            /* Root port */
                            driver->rootport = USB_NOPORT;

                            /* Device address */
                            driver->devaddr = USB_NODEVICE;

                            /* Device state */
                            driver->devstate = USB_DETACHED;
                        }
                    }
                    usb_hstd_mgr_rel_mpl(ptr, msginfo);

                    break;

                case USB_ATTACHL:
                case USB_ATTACHF:

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
                    disp_param.status = USB_CT_ATTACH;
                    disp_param.pid    = USB_NULL;
                    disp_param.vid    = USB_NULL;
                    usb_compliance_disp((void*)&disp_param);
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

                    if (USB_DETACHED == g_usb_hstd_mgr_mode[ptr->ip][rootport])
                    {
                        /* enumeration wait setting */
                        g_usb_hstd_enu_wait[ptr->ip]    = (uint8_t)USB_MGR_TSK;
                        g_usb_hstd_device_addr[ptr->ip] = (uint16_t)(rootport + USB_DEVICEADDR);
                        if (USB_MAXDEVADDR < g_usb_hstd_device_addr[ptr->ip])
                        {
                            /* For port1 */
                            USB_PRINTF0("Device address error\n");
                        }
                        else
                        {
                            g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_DEFAULT;

#if USB_CFG_BC == USB_CFG_ENABLE
                            /* Call Back */
                            /*USB_BC_ATTACH(ptr, g_usb_hstd_device_addr[ptr->ip], (uint16_t)g_usb_hstd_bc[ptr->ip].state); */
                            if (USB_BC_STATE_CDP == g_usb_hstd_bc[ptr->ip].state)
                            {
                                ctrl.address = g_usb_hstd_device_addr[ptr->ip]; /* USB Device address */
                                ctrl.module  = ptr->ip;                         /* Module number setting */
                                usb_set_event(USB_STS_BC, &ctrl);               /* Set Event()  */
                            }
#endif /* USB_CFG_BC == USB_CFG_ENABLE */

                            usb_hstd_attach_function();
                            usb_hstd_mgr_reset(ptr, g_usb_hstd_device_addr[ptr->ip]);
                        }
                        usb_hstd_mgr_rel_mpl(ptr, msginfo);
                    }
                    else
                    {
                        /* enumeration wait setting */
                        g_usb_hstd_enu_wait[ptr->ip] = (uint8_t)USB_MGR_TSK;
                        usb_hstd_mgr_rel_mpl(ptr, msginfo);
                    }
                    break;

                default:

                    usb_hstd_mgr_rel_mpl(ptr, msginfo);

                    break;
            }

            break;

        case USB_MSG_MGR_OVERCURRENT:

            ctrl.module = ptr->ip;                     /* Module number setting */
            usb_set_event(USB_STS_OVERCURRENT, &ctrl); /* Set Event()  */

            USB_PRINTF0(" Please detach device \n ");
            USB_PRINTF1("VBUS off port%d\n", rootport);
            usb_hstd_vbus_control(ptr, rootport, (uint16_t)USB_VBOFF);
            g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_DEFAULT;
            for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
            {
                driver = &g_usb_hstd_device_drv[ptr->ip][md];
                if (driver->rootport == rootport)
                {
                    /* Root port */
                    g_usb_hstd_device_info[ptr->ip][driver->devaddr][0] = USB_NOPORT;

                    /* Device state */
                    g_usb_hstd_device_info[ptr->ip][driver->devaddr][1] = USB_DETACHED;

                    /* Not configured */
                    g_usb_hstd_device_info[ptr->ip][driver->devaddr][2] = (uint16_t)0;

                    /* Interface Class : NO class */
                    g_usb_hstd_device_info[ptr->ip][driver->devaddr][3] = (uint16_t)USB_IFCLS_NOT;

                    /* No connect */
                    g_usb_hstd_device_info[ptr->ip][driver->devaddr][4] = (uint16_t)USB_NOCONNECT;

                    /* Root port */
                    driver->rootport = USB_NOPORT;

                    /* Device address */
                    driver->devaddr = USB_NODEVICE;

                    /* Device state */
                    driver->devstate = USB_DETACHED;
                }
            }
            usb_hstd_mgr_rel_mpl(ptr, msginfo);

            break;

        /* USB_MSG_HCD_ATTACH */
        case USB_DO_RESET_AND_ENUMERATION:

            ptr->msginfo = USB_MSG_HCD_ATTACH_MGR;

            if (USB_DEVICEADDR == devaddr)
            {
                g_usb_hstd_mgr_mode[ptr->ip][USB_PORT0] = USB_DETACHED;
            }
            else
            {
                /* Non */
            }

            usb_hstd_device_state_ctrl2(ptr, hp->complete, devaddr, ptr->msginfo, msginfo);
            usb_hstd_mgr_rel_mpl(ptr, msginfo);

            break;

        /* USB_MSG_HCD_VBON */
        case USB_PORT_ENABLE:

            ptr->msginfo = USB_MSG_HCD_VBON;
            if (USB_DEVICEADDR == devaddr)
            {
                g_usb_hstd_mgr_mode[ptr->ip][USB_PORT0] = USB_DETACHED;
            }
            else
            {
                /* Non */
            }
            usb_hstd_device_state_ctrl2(ptr, hp->complete, devaddr, ptr->msginfo, msginfo);
            usb_hstd_mgr_rel_mpl(ptr, msginfo);

            break;

        /* USB_MSG_HCD_VBOFF */
        case USB_PORT_DISABLE:

            /* VBUS is off at the time of the abnormalities in a device */
            ptr->msginfo = USB_MSG_HCD_VBOFF;
            if (USB_DEVICEADDR == devaddr)
            {
                g_usb_hstd_mgr_mode[ptr->ip][USB_PORT0] = USB_DETACHED;
            }
            else
            {
                /* Non */
            }
            usb_hstd_device_state_ctrl2(ptr, hp->complete, devaddr, ptr->msginfo, msginfo);
            usb_hstd_mgr_rel_mpl(ptr, msginfo);

            break;

        /* USB_MSG_HCD_SUSPEND */
        case USB_DO_GLOBAL_SUSPEND:

            ptr->msginfo                    = USB_MSG_HCD_REMOTE;
            usb_shstd_mgr_callback[ptr->ip] = hp->complete;
            usb_shstd_mgr_msginfo[ptr->ip]  = msginfo;
            usb_hstd_mgr_suspend(ptr, msginfo);

            break;

        /* USB_MSG_HCD_SUSPEND */
        case USB_DO_SELECTIVE_SUSPEND:

            ptr->msginfo = USB_MSG_HCD_REMOTE;
            usb_hstd_mgr_suspend(ptr, msginfo);
            usb_hstd_device_state_ctrl2(ptr, hp->complete, devaddr, ptr->msginfo, msginfo);

            break;

        /* USB_MSG_HCD_RESUME */
        case USB_DO_GLOBAL_RESUME:

            ptr->msginfo                    = USB_MSG_HCD_RESUME;
            usb_shstd_mgr_callback[ptr->ip] = hp->complete;
            usb_shstd_mgr_msginfo[ptr->ip]  = msginfo;
            usb_hstd_mgr_resume(ptr, msginfo);

            break;

        /* USB_MSG_HCD_RESUME */
        case USB_MSG_HCD_RESUME:

            usb_shstd_mgr_msginfo[ptr->ip] = msginfo;
            usb_hstd_mgr_resume(ptr, msginfo);

            break;

        /* USB_MSG_HCD_RESUME */
        case USB_DO_SELECTIVE_RESUME:

            ptr->msginfo = USB_MSG_HCD_RESUME;
            usb_hstd_mgr_resume(ptr, msginfo);
            usb_hstd_device_state_ctrl2(ptr, hp->complete, devaddr, ptr->msginfo, msginfo);

            break;

        default:

            usb_hstd_mgr_rel_mpl(ptr, msginfo);

            break;
    }
} /* End of function usb_hstd_mgr_task() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
