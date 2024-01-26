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
 * File Name    : r_usb_hhubsys.c
 * Description  : USB Host Hub system code
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
#include "definitions.h"
#include "deluge/drivers/uart/uart.h"

#include "deluge/deluge.h"

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Macro definitions
 ***********************************************************************************************************************/
/* Condition compilation by the difference of the devices */
#define USB_MAXHUB ((uint16_t)1u)

#define USB_HUB_CLSDATASIZE ((uint16_t)512u)
#define USB_HUB_QOVR        ((uint16_t)0xFFFEu)

#define USB_BIT_PORT_CONNECTION     (0x00000001u)
#define USB_BIT_PORT_ENABLE         (0x00000002u)
#define USB_BIT_PORT_SUSPEND        (0x00000004u)
#define USB_BIT_PORT_OVER_CURRENT   (0x00000008u)
#define USB_BIT_PORT_RESET          (0x00000010u)
#define USB_BIT_PORT_POWER          (0x00000100u)
#define USB_BIT_PORT_LOW_SPEED      (0x00000200u)
#define USB_BIT_C_PORT_CONNECTION   (0x00010000u)
#define USB_BIT_C_PORT_ENABLE       (0x00020000u)
#define USB_BIT_C_PORT_SUSPEND      (0x00040000u)
#define USB_BIT_C_PORT_OVER_CURRENT (0x00080000u)
#define USB_BIT_C_PORT_RESET        (0x00100000u)

/* HUB down port */
#define USB_HUBDOWNPORT                                                                                                \
    (127u) /* HUB downport (MAX15) */ // Set by Rohan. Having a high number here doesn't use any extra memory (possibly
                                      // it might have before I did my other code edits).

/***********************************************************************************************************************
Typedef definitions
***********************************************************************************************************************/
typedef struct USB_HUB_INFO
{
    uint16_t up_addr;     /* Up-address  */
    uint16_t up_port_num; /* Up-port num */
    uint16_t port_num;    /* Port number */
    uint16_t pipe_num;    /* Pipe number */
} usb_hub_info_t;

/***********************************************************************************************************************
 Private global variables and functions
 ***********************************************************************************************************************/
static uint16_t usb_hhub_chk_config(uint16_t** table, uint16_t spec);
static uint16_t usb_hhub_chk_interface(uint16_t** table, uint16_t spec);
static uint16_t usb_hhub_pipe_info(usb_utr_t* ptr, uint8_t* table, uint16_t offset, uint16_t speed, uint16_t length);
static void usb_hhub_port_detach(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum);
static void usb_hhub_selective_detach(usb_utr_t* ptr, uint16_t devaddr);
static void usb_hhub_trans_start(usb_utr_t* ptr, uint16_t hubaddr, uint32_t size, uint8_t* table, usb_cb_t complete);
static void usb_hhub_trans_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
static uint16_t usb_hhub_get_new_devaddr(usb_utr_t* ptr);
static uint16_t usb_hhub_get_hubaddr(usb_utr_t* ptr, uint16_t pipenum);
static uint16_t usb_hhub_get_cnn_devaddr(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum);
static uint16_t usb_hhub_chk_tbl_indx1(usb_utr_t* ptr, uint16_t hubaddr);
static uint16_t usb_hhub_chk_tbl_indx2(usb_utr_t* ptr, uint16_t hubaddr);
static uint16_t usb_hhub_port_set_feature(
    usb_utr_t* ptr, uint16_t hubaddr, uint16_t port, uint16_t command, usb_cb_t complete);
static uint16_t usb_hhub_port_clr_feature(
    usb_utr_t* ptr, uint16_t hubaddr, uint16_t port, uint16_t command, usb_cb_t complete);
static void usb_hhub_init_down_port(usb_utr_t* ptr, uint16_t hubaddr, usb_clsinfo_t* mess);
static void usb_hhub_new_connect(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum, usb_clsinfo_t* mess);
static uint16_t usb_hhub_port_attach(uint16_t hubaddr, uint16_t portnum, usb_clsinfo_t* mess);
static void usb_hhub_port_reset(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum, usb_clsinfo_t* mess);
static void usb_hhub_enumeration(usb_clsinfo_t* mess);
static void usb_hhub_event(usb_clsinfo_t* mess);
static void usb_hhub_specified_path(usb_clsinfo_t* mess);
static void usb_hhub_specified_path_wait(usb_clsinfo_t* mess, uint16_t times);
static void usb_hhub_class_request_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
static void usb_hhub_check_request(usb_utr_t* ptr, uint16_t result);
static void usb_hhub_initial(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
static uint16_t usb_hhub_request_result(uint16_t checkerr);
static void usb_hhub_device_descrip_info(usb_utr_t* ptr);
static void usb_hhub_config_descrip_info(usb_utr_t* ptr);
static void usb_hhub_check_class(usb_utr_t* ptr, uint16_t** table);
#ifdef USB_DEBUG_ON
static void usb_hhub_endp_descrip_info(uint8_t* tbl);
#endif

/***********************************************************************************************************************
Private global variables and functions
***********************************************************************************************************************/
/* Control transfer message */
usb_utr_t g_usb_shhub_ctrl_mess[USB_NUM_USBIP];

/* Data transfer message */
usb_utr_t g_usb_shhub_data_mess[USB_NUM_USBIP][USB_MAXDEVADDR + 1u];

/* HUB descriptor */
uint8_t g_usb_hhub_descriptor[USB_NUM_USBIP][USB_CONFIGSIZE];

/* HUB status data */
uint8_t g_usb_hhub_data[USB_NUM_USBIP][USB_MAXDEVADDR + 1u][8];

/* HUB downport status */
uint16_t g_usb_shhub_down_port[USB_NUM_USBIP][USB_MAXDEVADDR + 1u];

/* Downport remotewakeup */
uint16_t g_usb_shhub_remote[USB_NUM_USBIP][USB_MAXDEVADDR + 1u];

/* Up-hubaddr, up-hubport, portnum, pipenum */
usb_hub_info_t g_usb_shhub_info_data[USB_NUM_USBIP][USB_MAXDEVADDR + 1u];
uint16_t g_usb_shhub_number[USB_NUM_USBIP];
uint16_t g_usb_shhub_class_request[USB_NUM_USBIP][5];

/* Condition compilation by the difference of USB function */
#if USB_NUM_USBIP == 2
uint16_t g_usb_shhub_class_seq[USB_NUM_USBIP]  = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_init_seq[USB_NUM_USBIP]   = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_init_port[USB_NUM_USBIP]  = {USB_HUB_P1, USB_HUB_P1};
uint16_t g_usb_shhub_event_seq[USB_NUM_USBIP]  = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_event_port[USB_NUM_USBIP] = {USB_HUB_P1, USB_HUB_P1};
uint16_t g_usb_shhub_attach_seq[USB_NUM_USBIP] = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_reset_seq[USB_NUM_USBIP]  = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_state[USB_NUM_USBIP]      = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_info[USB_NUM_USBIP]       = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_hub_addr[USB_NUM_USBIP]   = {USB_SEQ_0, USB_SEQ_0};
uint16_t g_usb_shhub_process[USB_NUM_USBIP]    = {USB_SEQ_0, USB_SEQ_0};
#else  /* USB_NUM_USBIP == 2 */
uint16_t g_usb_shhub_class_seq[USB_NUM_USBIP]  = {USB_SEQ_0};
uint16_t g_usb_shhub_init_seq[USB_NUM_USBIP]   = {USB_SEQ_0};
uint16_t g_usb_shhub_init_port[USB_NUM_USBIP]  = {USB_HUB_P1};
uint16_t g_usb_shhub_event_seq[USB_NUM_USBIP]  = {USB_SEQ_0};
uint16_t g_usb_shhub_event_port[USB_NUM_USBIP] = {USB_HUB_P1};
uint16_t g_usb_shhub_attach_seq[USB_NUM_USBIP] = {USB_SEQ_0};
uint16_t g_usb_shhub_reset_seq[USB_NUM_USBIP]  = {USB_SEQ_0};
uint16_t g_usb_shhub_state[USB_NUM_USBIP]      = {USB_SEQ_0};
uint16_t g_usb_shhub_info[USB_NUM_USBIP]       = {USB_SEQ_0};
uint16_t g_usb_shhub_hub_addr[USB_NUM_USBIP]   = {USB_SEQ_0};
uint16_t g_usb_shhub_process[USB_NUM_USBIP]    = {USB_SEQ_0};
#endif /* USB_NUM_USBIP == 2 */

uint8_t* g_p_usb_shhub_device_table[USB_NUM_USBIP];
uint8_t* g_p_usb_shhub_config_table[USB_NUM_USBIP];
uint8_t* g_p_usb_shhub_interface_table[USB_NUM_USBIP];
uint16_t g_usb_shhub_spec[USB_NUM_USBIP];
uint16_t g_usb_shhub_root[USB_NUM_USBIP];
uint16_t g_usb_shhub_speed[USB_NUM_USBIP];
uint16_t g_usb_shhub_dev_addr[USB_NUM_USBIP];
uint16_t g_usb_shhub_index[USB_NUM_USBIP];

const uint16_t g_usb_hhub_tpl[4] = {
    USB_CFG_HUB_TPLCNT, /* Number of tpl table */
    0,                  /* Reserved */
    USB_CFG_HUB_TPL     /* Vendor ID, Product ID */
};

/* Host hub Pipe Information Table (endpoint table) definition */
uint16_t g_usb_hhub_def_ep_tbl[USB_NUM_USBIP][USB_EPL + 1] = {
    {
        /* PIPE9 Definition */
        USB_PIPE9,
        USB_NULL | USB_BFREOFF | USB_CFG_DBLBOFF | USB_CFG_CNTMDOFF | USB_CFG_SHTNAKOFF | USB_NULL | USB_NULL,
        (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(7u), // PIPE9 must use buffer 7, according to manual!
        USB_NULL,
        USB_NULL,
        USB_CUSE,

        /* Pipe end */
        USB_PDTBLEND,
    },
#if USB_NUM_USBIP == 2
    {
        /* PIPE9 Definition */
        USB_PIPE9,
        USB_NULL | USB_BFREOFF | USB_CFG_DBLBOFF | USB_CFG_CNTMDOFF | USB_CFG_SHTNAKOFF | USB_NULL | USB_NULL,
        (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(7u),
        USB_NULL,
        USB_NULL,
        USB_CUSE,

        /* Pipe end */
        USB_PDTBLEND,
    }
#endif /* USB_NUM_USBIP == 2 */
};

/* Host hub temporary Pipe Information Table ("endpoint table") definition */
uint16_t g_usb_hhub_tmp_ep_tbl[USB_NUM_USBIP][USB_EPL + 1] = {
    {
        /* PIPE9 Definition */
        USB_PIPE9,
        USB_NULL | USB_BFREOFF | USB_CFG_DBLBOFF | USB_CFG_CNTMDOFF | USB_CFG_SHTNAKOFF | USB_NULL | USB_NULL,
        (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(7u), // PIPE9 must use buffer 7, according to manual!
        USB_NULL,
        USB_NULL,
        USB_CUSE,

        /* Pipe end */
        USB_PDTBLEND,
    },
#if USB_NUM_USBIP == 2
    {
        /* PIPE9 Definition */
        USB_PIPE9,
        USB_NULL | USB_BFREOFF | USB_CFG_DBLBOFF | USB_CFG_CNTMDOFF | USB_CFG_SHTNAKOFF | USB_NULL | USB_NULL,
        (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(7u),
        USB_NULL,
        USB_NULL,
        USB_CUSE,

        /* Pipe end */
        USB_PDTBLEND,
    }
#endif /* USB_NUM_USBIP == 2 */
};
/***********************************************************************************************************************
 Exported global variables
 ***********************************************************************************************************************/
/* Enumeration request */
/*extern uint16_t   g_usb_hstd_enum_seq[2u];*/
extern uint8_t g_usb_hstd_class_data[][512];

extern void (*g_usb_hstd_enumaration_process[])(usb_utr_t*, uint16_t, uint16_t);

/***********************************************************************************************************************
Renesas Abstracted Hub Driver API functions
***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_hhub_open
 Description     : HUB sys open
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device Address
                 : uint16_t     data2        : Not use
 Return value    : usb_er_t                  : Error Info
 ***********************************************************************************************************************/
void usb_hhub_open(usb_utr_t* ptr, uint16_t devaddr, uint16_t data2)
{
    usb_er_t err, err2;
    usb_mh_t p_blf;
    usb_mgrinfo_t* mp;
    uint16_t hubaddr, index;

    err     = USB_ERROR;
    hubaddr = (uint16_t)(devaddr << USB_DEVADDRBIT);
    index   = usb_hhub_chk_tbl_indx1(ptr, devaddr);

    if (USB_MAXHUB != g_usb_shhub_number[ptr->ip])
    {
        /* Wait 10ms */
        usb_cpu_delay_xms((uint16_t)10);
        err = USB_PGET_BLK(USB_HUB_MPL, &p_blf);
        if (USB_OK == err)
        {
            mp          = (usb_mgrinfo_t*)p_blf;
            mp->msghead = (usb_mh_t)USB_NULL;
            mp->msginfo = USB_MSG_CLS_INIT;
            mp->keyword = devaddr;

            mp->ipp = ptr->ipp;
            mp->ip  = ptr->ip;

            /* Send message */
            err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)p_blf);
            if (USB_OK != err)
            {
                /* Send Message failure */
                USB_PRINTF1("### hHubOpen snd_msg error (%ld)\n", err);
                err2 = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)p_blf);
                if (USB_OK != err2)
                {
                    USB_PRINTF1("### hHubOpen rel_blk error (%ld)\n", err2);
                }
            }
        }
        else
        {
            /* Release memory block failure */
            USB_PRINTF1("### hHubOpen pget_blk error (%ld)\n", err);
            while (1)
            {
                /* Non */
            }
        }
        /* Pipe number set */
        g_usb_shhub_info_data[ptr->ip][devaddr].pipe_num = g_usb_hhub_tmp_ep_tbl[ptr->ip][index];
        /* HUB downport status */
        g_usb_shhub_down_port[ptr->ip][devaddr] = 0;
        /* Downport remotewakeup */
        g_usb_shhub_remote[ptr->ip][devaddr] = 0;
        g_usb_hhub_tmp_ep_tbl[ptr->ip][index + 3] |= hubaddr;
        usb_hstd_set_pipe_info(
            &g_usb_hhub_def_ep_tbl[ptr->ip][index], &g_usb_hhub_tmp_ep_tbl[ptr->ip][index], (uint16_t)USB_EPL);

        g_usb_shhub_process[ptr->ip] = USB_MSG_CLS_INIT;
        usb_hstd_set_pipe_registration(
            ptr, (uint16_t*)&g_usb_hhub_def_ep_tbl[ptr->ip], g_usb_hhub_def_ep_tbl[ptr->ip][index]);

        g_usb_shhub_number[ptr->ip]++;
    }
} /* End of function usb_hhub_open() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_close
 Description     : HUB sys close
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     data2        : Not use
 Return value    : usb_er_t                  : Error Info
 ***********************************************************************************************************************/
void usb_hhub_close(usb_utr_t* ptr, uint16_t hubaddr, uint16_t data2)
{
    uint16_t md, i;
    usb_hcdreg_t* driver;
    uint16_t devaddr, index;

    for (i = 1; i <= g_usb_shhub_info_data[ptr->ip][hubaddr].port_num; i++)
    {
        /* Now downport device search */
        devaddr = usb_hhub_get_cnn_devaddr(ptr, hubaddr, i);
        if (0 != devaddr)
        {
            /* HUB down port selective disconnect */
            usb_hhub_selective_detach(ptr, devaddr);
            for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
            {
                driver = &g_usb_hstd_device_drv[ptr->ip][md];
                if (devaddr == driver->devaddr)
                {
                    (*driver->devdetach)(ptr, driver->devaddr, (uint16_t)USB_NO_ARG);
                    driver->rootport = USB_NOPORT;   /* Root port */
                    driver->devaddr  = USB_NODEVICE; /* Device devaddress */
                    driver->devstate = USB_DETACHED; /* Device state */
                }
            }
        }
    }

    g_usb_shhub_number[ptr->ip]--;
    index = usb_hhub_chk_tbl_indx2(ptr, hubaddr);

    /* Set pipe information */
    for (i = 1; i <= USB_MAXDEVADDR; i++)
    {
        g_usb_shhub_info_data[ptr->ip][i].up_addr     = 0; /* Up-address clear */
        g_usb_shhub_info_data[ptr->ip][i].up_port_num = 0; /* Up-port num clear */
        g_usb_shhub_info_data[ptr->ip][i].port_num    = 0; /* Port number clear */
        g_usb_shhub_info_data[ptr->ip][i].pipe_num    = 0; /* Pipe number clear */
    }

    g_usb_shhub_down_port[ptr->ip][hubaddr] = 0;
    g_usb_shhub_remote[ptr->ip][hubaddr]    = 0;
    g_usb_shhub_attach_seq[ptr->ip]         = 0;

    g_usb_hhub_def_ep_tbl[ptr->ip][index + 1] = USB_NULL;
    g_usb_hhub_def_ep_tbl[ptr->ip][index + 3] = USB_NULL;
    g_usb_hhub_def_ep_tbl[ptr->ip][index + 4] = USB_NULL;
    g_usb_hhub_tmp_ep_tbl[ptr->ip][index + 1] = USB_NULL;
    g_usb_hhub_tmp_ep_tbl[ptr->ip][index + 3] = USB_NULL;
    g_usb_hhub_tmp_ep_tbl[ptr->ip][index + 4] = USB_NULL;

} /* End of function usb_hhub_close() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_registration
 Description     : HUB driver
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : usb_hcdreg_t *callback    : Pointer ot usb_hcdreg_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hhub_registration(usb_utr_t* ptr, usb_hcdreg_t* callback)
{
    usb_hcdreg_t driver;

    /* Driver registration */
    if (USB_NULL == callback)
    {
        driver.p_tpl     = (uint16_t*)&g_usb_hhub_tpl[0];              /* Target peripheral list */
        driver.p_pipetbl = (uint16_t*)&g_usb_hhub_def_ep_tbl[ptr->ip]; /* Pipe Information Table Define address */
    }
    else
    {
        driver.p_tpl     = callback->p_tpl;     /* Target peripheral list */
        driver.p_pipetbl = callback->p_pipetbl; /* Pipe Define Table address */
    }

    driver.ifclass    = (uint16_t)USB_IFCLS_HUB;   /* Interface Class */
    driver.classinit  = &usb_hhub_initial;         /* Driver init */
    driver.classcheck = &usb_hhub_check_class;     /* Driver check */
    driver.devconfig  = (usb_cb_t)&usb_hhub_open;  /* Device configured */
    driver.devdetach  = (usb_cb_t)&usb_hhub_close; /* Device detach */
    driver.devsuspend = &usb_hstd_dummy_function;  /* Device suspend */
    driver.devresume  = &usb_hstd_dummy_function;  /* Device resume */

    usb_hstd_driver_registration(ptr, (usb_hcdreg_t*)&driver);
} /* End of function usb_hhub_registration() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_hub_information
 Description     : Read HUB-Descriptor
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : usb_cb_t     complete     : Callback function
 Return value    : uint16_t                  : DONE/ERROR
 ***********************************************************************************************************************/
uint16_t usb_hhub_get_hub_information(usb_utr_t* ptr, uint16_t hubaddr, usb_cb_t complete)
{
    usb_er_t qerr;

    /* Request */
    g_usb_shhub_class_request[ptr->ip][0] = USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_CLASS | USB_DEVICE;
    g_usb_shhub_class_request[ptr->ip][1] = USB_HUB_DESCRIPTOR;
    g_usb_shhub_class_request[ptr->ip][2] = 0;
    g_usb_shhub_class_request[ptr->ip][3] = 0x0047;
    g_usb_shhub_class_request[ptr->ip][4] = hubaddr; /* Device address */

    /* HUB Descriptor */
    g_usb_shhub_ctrl_mess[ptr->ip].keyword   = USB_PIPE0;
    g_usb_shhub_ctrl_mess[ptr->ip].p_tranadr = (void*)&g_usb_hhub_descriptor[ptr->ip][0];
    g_usb_shhub_ctrl_mess[ptr->ip].tranlen   = (uint32_t)g_usb_shhub_class_request[ptr->ip][3];
    g_usb_shhub_ctrl_mess[ptr->ip].p_setup   = g_usb_shhub_class_request[ptr->ip];
    g_usb_shhub_ctrl_mess[ptr->ip].segment   = USB_TRAN_END;
    g_usb_shhub_ctrl_mess[ptr->ip].complete  = complete;

    g_usb_shhub_ctrl_mess[ptr->ip].ipp = ptr->ipp;
    g_usb_shhub_ctrl_mess[ptr->ip].ip  = ptr->ip;

    /* Transfer start */
    qerr = usb_hstd_transfer_start(&g_usb_shhub_ctrl_mess[ptr->ip]);
    if (USB_QOVR == qerr)
    {
        return USB_HUB_QOVR;
    }

    return USB_OK;
} /* End of function usb_hhub_get_hub_information() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_port_information
 Description     : GetStatus request
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     port         : Down port number
                 : usb_cb_t     complete     : Callback function
 Return value    : uint16_t                  : DONE/ERROR
 ***********************************************************************************************************************/
uint16_t usb_hhub_get_port_information(usb_utr_t* ptr, uint16_t hubaddr, uint16_t port, usb_cb_t complete)
{
    usb_er_t qerr;

    /* Request */
    g_usb_shhub_class_request[ptr->ip][0] = USB_GET_STATUS | USB_DEV_TO_HOST | USB_CLASS | USB_OTHER;
    g_usb_shhub_class_request[ptr->ip][1] = 0;
    g_usb_shhub_class_request[ptr->ip][2] = port; /* Port number */
    g_usb_shhub_class_request[ptr->ip][3] = 4;
    g_usb_shhub_class_request[ptr->ip][4] = hubaddr; /* Device address */

    /* Port GetStatus */
    g_usb_shhub_ctrl_mess[ptr->ip].keyword   = USB_PIPE0;
    g_usb_shhub_ctrl_mess[ptr->ip].p_tranadr = (void*)&g_usb_hhub_data[ptr->ip][hubaddr][0];
    g_usb_shhub_ctrl_mess[ptr->ip].tranlen   = (uint32_t)g_usb_shhub_class_request[ptr->ip][3];
    g_usb_shhub_ctrl_mess[ptr->ip].p_setup   = g_usb_shhub_class_request[ptr->ip];
    g_usb_shhub_ctrl_mess[ptr->ip].segment   = USB_TRAN_END;
    g_usb_shhub_ctrl_mess[ptr->ip].complete  = complete;

    g_usb_shhub_ctrl_mess[ptr->ip].ipp = ptr->ipp;
    g_usb_shhub_ctrl_mess[ptr->ip].ip  = ptr->ip;

    /* Transfer start */
    qerr = usb_hstd_transfer_start(&g_usb_shhub_ctrl_mess[ptr->ip]);
    if (USB_QOVR == qerr)
    {
        return USB_HUB_QOVR;
    }

    return USB_OK;
} /* End of function usb_hhub_get_port_information() */

extern usb_msg_t* p_usb_scheduler_add_use;

/***********************************************************************************************************************
 Function Name   : usb_hhub_task
 Description     : HUB task
 Arguments       : usb_vp_int_t stacd        : Start Code of Hub Task
 Return value    : none
 ***********************************************************************************************************************/
void usb_hhub_task(usb_vp_int_t stacd)
{
    usb_utr_t* mess = (usb_utr_t*)p_usb_scheduler_add_use;
    usb_er_t err;

    switch (mess->msginfo)
    {
        case USB_MSG_CLS_CHECKREQUEST:

            /* USB HUB Class Enumeration */
            usb_hhub_enumeration((usb_clsinfo_t*)mess);
            err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)mess);
            if (USB_OK != err)
            {
                /* Release Memoryblock failure */
                USB_PRINTF0("### USB HUB Task rel_blk error\n");
            }

            break;

        case USB_MSG_CLS_INIT:

            /* Down port initialize */
            usb_hhub_init_down_port(mess, (uint16_t)0, (usb_clsinfo_t*)mess);

            break;

        /* Enumeration waiting of other device */
        case USB_MSG_CLS_WAIT:

            mess->msginfo = USB_MSG_MGR_AORDETACH;
            err           = USB_SND_MSG(USB_MGR_MBX, (usb_msg_t*)mess);
            if (USB_OK != err)
            {
                /* Send Message failure */
                USB_PRINTF0("### USB HUB enuwait snd_msg error\n");
            }

            break;

        case USB_MSG_HUB_EVENT:

            usb_hhub_event((usb_clsinfo_t*)mess);

            break;

        case USB_MSG_HUB_ATTACH:

            /* Hub Port attach */
            usb_hhub_port_attach((uint16_t)0, (uint16_t)0, (usb_clsinfo_t*)mess);

            break;

        case USB_MSG_HUB_RESET:

            /* Hub Reset */
            usb_hhub_port_reset(mess, (uint16_t)0, (uint16_t)0, (usb_clsinfo_t*)mess);

            break;

        default:

            err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)mess);
            if (USB_OK != err)
            {
                USB_PRINTF0("### USB HUB rel_blk error\n");
            }

            break;
    }
} /* End of function usb_hhub_task() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_enumeration
 Description     : USB HUB Class Enumeration
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_enumeration(usb_clsinfo_t* ptr)
{
/* Condition compilation by the difference of useful function */
#if defined(USB_DEBUG_ON)
    uint8_t pdata[32];
    uint16_t j;
#endif /* USB_DEBUG_ON */
    uint16_t checkerr;
    uint16_t retval;
    uint8_t string;
    uint16_t* table[8];

    checkerr = ptr->result;
    table[0] = (uint16_t*)g_p_usb_shhub_device_table[ptr->ip];
    table[1] = (uint16_t*)g_p_usb_shhub_config_table[ptr->ip];
    table[2] = (uint16_t*)g_p_usb_shhub_interface_table[ptr->ip];

    /* Manager Mode Change */
    switch (g_usb_shhub_class_seq[ptr->ip])
    {
        case USB_SEQ_0:

            checkerr = USB_OK;
            /* Descriptor check */
            retval = usb_hhub_chk_config((uint16_t**)&table, (uint16_t)g_usb_shhub_spec[ptr->ip]);
            if (USB_ERROR == retval)
            {
                USB_PRINTF0("### Configuration descriptor error !\n");
                checkerr = USB_ERROR;
                break;
            }
            /* Interface Descriptor check */
            retval = usb_hhub_chk_interface((uint16_t**)&table, (uint16_t)g_usb_shhub_spec[ptr->ip]);
            if (USB_ERROR == retval)
            {
                USB_PRINTF0("### Interface descriptor error !\n");
                checkerr = USB_ERROR;
                break;
            }

            /* String descriptor */
            g_usb_shhub_process[ptr->ip] = USB_MSG_CLS_CHECKREQUEST;
            usb_hhub_get_string_descriptor1(
                ptr, g_usb_shhub_dev_addr[ptr->ip], (uint16_t)15, (usb_cb_t)&usb_hhub_class_request_complete);

            break;

        case USB_SEQ_1:

            /* String descriptor check */
            retval = usb_hhub_get_string_descriptor1check(checkerr);
            if (USB_OK == retval)
            {
                string = g_p_usb_shhub_device_table[ptr->ip][15];
                /* String descriptor */
                g_usb_shhub_process[ptr->ip] = USB_MSG_CLS_CHECKREQUEST;
                usb_hhub_get_string_descriptor2(
                    ptr, g_usb_shhub_dev_addr[ptr->ip], (uint16_t)string, (usb_cb_t)&usb_hhub_class_request_complete);
            }
            else
            {
                /* If USB_ERROR, Go case 3 (checkerr==USB_ERROR) */
                g_usb_shhub_class_seq[ptr->ip] = USB_SEQ_2;
                /* Next sequence */
                usb_hhub_check_request(ptr, USB_ERROR);
                checkerr = USB_OK;
            }

            break;

        case USB_SEQ_2:

            /* String descriptor check */
            retval = usb_hhub_get_string_descriptor_to_check(checkerr);
            if (USB_OK == retval)
            {
                /* Next sequence */
                usb_hhub_check_request(ptr, checkerr);
            }

            break;

        case USB_SEQ_3:

            /* String descriptor check */
            if (USB_OK == checkerr)
            {
                if (g_usb_hstd_class_data[ptr->ip][0] < (uint8_t)((32 * 2) + 2))
                {
                    g_usb_hstd_class_data[ptr->ip][0] = (uint8_t)(g_usb_hstd_class_data[ptr->ip][0] / 2);
                    g_usb_hstd_class_data[ptr->ip][0] = (uint8_t)(g_usb_hstd_class_data[ptr->ip][0] - 1);
                }
                else
                {
                    g_usb_hstd_class_data[ptr->ip][0] = (uint8_t)32;
                }
                /* Condition compilation by the difference of useful function */
#if defined(USB_DEBUG_ON)
                for (j = (uint16_t)0; j < g_usb_hstd_class_data[ptr->ip][0]; j++)
                {
                    pdata[j] = g_usb_hstd_class_data[ptr->ip][j * (uint16_t)2 + (uint16_t)2];
                }
                pdata[g_usb_hstd_class_data[ptr->ip][0]] = 0;
                uartPrint("    Product name : ");
                uartPrintln(pdata);
#endif /* USB_DEBUG_ON */
            }
            else
            {
                USB_PRINTF0("*** Product name error\n");
                checkerr = USB_OK;
            }

            /* Get HUB descriptor */
            g_usb_shhub_process[ptr->ip] = USB_MSG_CLS_CHECKREQUEST;
            checkerr =
                usb_hhub_get_hub_information(ptr, g_usb_shhub_dev_addr[ptr->ip], usb_hhub_class_request_complete);
            /* Submit overlap error */
            if (USB_HUB_QOVR == checkerr)
            {
                usb_hhub_specified_path_wait(ptr, (uint16_t)10);
            }

            break;

        case USB_SEQ_4:

            /* Get HUB descriptor Check */

            // By Rohan: It seems some hubs don't send that descriptor back. If it stalled, just make a guess about how
            // many ports it had - it'll still work.
            if (checkerr == USB_DATA_STALL)
            {
                g_usb_hhub_descriptor[ptr->ip][1] = USB_DT_HUBDESCRIPTOR;
                g_usb_hhub_descriptor[ptr->ip][2] = USB_HUBDOWNPORT;
                checkerr                          = USB_CTRL_END; // Pretend everything was fine
            }

            retval = usb_hhub_request_result(checkerr);
            if (USB_OK == retval)
            {
                usb_hhub_check_request(ptr, checkerr); /* Next sequence */
            }

            break;

        case USB_SEQ_5:

            /* Get HUB descriptor Check */
            if (USB_OK == checkerr)
            {
                retval = usb_hhub_check_descriptor(g_usb_hhub_descriptor[ptr->ip], (uint16_t)USB_DT_HUBDESCRIPTOR);
                if (USB_ERROR == retval)
                {
                    USB_PRINTF0("### HUB descriptor error !\n");
                    checkerr = USB_ERROR;
                }
                else if (g_usb_hhub_descriptor[ptr->ip][2] > USB_HUBDOWNPORT)
                {
                    USB_PRINTF0("### HUB Port number over\n");

                    // Edited by Rohan: If more hub ports than we support, just use the first few - don't render the
                    // whole hub useless
                    g_usb_hhub_descriptor[ptr->ip][2] = USB_HUBDOWNPORT;
                    // checkerr = USB_ERROR;
                }
                else
                {
                    USB_PRINTF1("    Attached %d port HUB\n", g_usb_hhub_descriptor[ptr->ip][2]);
                }
            }
            else
            {
                USB_PRINTF0("### HUB Descriptor over\n");
                checkerr = USB_ERROR;
            }

            /* Pipe Information table set */
            switch (g_usb_shhub_spec[ptr->ip])
            {
                case USB_FSHUB: /* Full Speed Hub */

                    if (USB_FSCONNECT == g_usb_shhub_speed[ptr->ip])
                    {
                        retval = usb_hhub_pipe_info(ptr, (uint8_t*)g_p_usb_shhub_interface_table[ptr->ip],
                            g_usb_shhub_index[ptr->ip], g_usb_shhub_speed[ptr->ip],
                            (uint16_t)g_p_usb_shhub_config_table[ptr->ip][2]);
                        if (USB_ERROR == retval)
                        {
                            USB_PRINTF0("### Device information error(HUB) !\n");
                            checkerr = USB_ERROR;
                        }
                    }
                    else
                    {
                        USB_PRINTF0("### HUB Descriptor speed error\n");
                        checkerr = USB_ERROR;
                    }

                    break;

                case USB_HSHUBS: /* Hi Speed Hub(Single) */

                    if (USB_HSCONNECT == g_usb_shhub_speed[ptr->ip])
                    {
                        retval = usb_hhub_pipe_info(ptr, (uint8_t*)g_p_usb_shhub_interface_table[ptr->ip],
                            g_usb_shhub_index[ptr->ip], g_usb_shhub_speed[ptr->ip],
                            (uint16_t)g_p_usb_shhub_config_table[ptr->ip][2]);
                        if (USB_ERROR == retval)
                        {
                            USB_PRINTF0("### Device information error(HUB) !\n");
                            checkerr = USB_ERROR;
                        }
                    }
                    else
                    {
                        USB_PRINTF0("### HUB Descriptor speed error\n");
                        checkerr = USB_ERROR;
                    }

                    break;

                case USB_HSHUBM: /* Hi Speed Hub(Multi) */

                    if (USB_HSCONNECT == g_usb_shhub_speed[ptr->ip])
                    {
                        /* Set pipe information */
                        retval = usb_hhub_pipe_info(ptr, (uint8_t*)g_p_usb_shhub_interface_table[ptr->ip],
                            g_usb_shhub_index[ptr->ip], g_usb_shhub_speed[ptr->ip],
                            (uint16_t)g_p_usb_shhub_config_table[ptr->ip][2]);
                        if (USB_ERROR == retval)
                        {
                            USB_PRINTF0("### Device information error(HUB) !\n");
                            checkerr = USB_ERROR;
                        }
                        /* Set pipe information */
                        retval = usb_hhub_pipe_info(ptr, (uint8_t*)g_p_usb_shhub_interface_table[ptr->ip],
                            g_usb_shhub_index[ptr->ip], g_usb_shhub_speed[ptr->ip],
                            (uint16_t)g_p_usb_shhub_config_table[ptr->ip][2]);
                        if (USB_ERROR == retval)
                        {
                            USB_PRINTF0("### Device information error(HUB) !\n");
                            checkerr = USB_ERROR;
                        }
                    }
                    else
                    {
                        USB_PRINTF0("### HUB Descriptor speed error\n");
                        checkerr = USB_ERROR;
                    }

                    break;

                default:

                    checkerr = USB_ERROR;

                    break;
            }
            /* Port number set */
            g_usb_shhub_info_data[ptr->ip][g_usb_shhub_dev_addr[ptr->ip]].port_num = g_usb_hhub_descriptor[ptr->ip][2];
            g_usb_shhub_process[ptr->ip]                                           = USB_NULL;
            usb_hstd_return_enu_mgr((usb_utr_t*)ptr, checkerr); /* Return to MGR */

            uartPrint("num ports: ");
            uartPrintNumber(g_usb_shhub_info_data[ptr->ip][g_usb_shhub_dev_addr[ptr->ip]].port_num);

            break;

        default:
            break;
    }

    switch (checkerr)
    {
        /* Driver open */
        case USB_OK:

            g_usb_shhub_class_seq[ptr->ip]++;

            break;

        /* Submit overlap error */
        case USB_HUB_QOVR:
            break;

        /* Descriptor error */
        case USB_ERROR:

            USB_PRINTF0("### Enumeration is stoped(ClassCode-ERROR)\n");
            g_usb_shhub_process[ptr->ip] = USB_NULL;
            usb_hstd_return_enu_mgr((usb_utr_t*)ptr, USB_ERROR); /* Return to MGR */

            break;

        default:

            g_usb_shhub_process[ptr->ip] = USB_NULL;
            usb_hstd_return_enu_mgr((usb_utr_t*)ptr, USB_ERROR); /* Return to MGR */

            break;
    }
} /* End of function usb_hhub_enumeration() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_init_down_port
 Description     : Down port initialized
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : usb_clsinfo_t *mess
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_init_down_port(usb_utr_t* ptr, uint16_t hubaddr, usb_clsinfo_t* mess)
{
    usb_er_t err;
    uint16_t retval;

    hubaddr = g_usb_shhub_hub_addr[ptr->ip];
    retval  = USB_OK;

    if (USB_MSG_CLS_INIT != g_usb_shhub_process[ptr->ip]) /* Check Hub Process */
    {
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)mess);
        if (USB_OK != err)
        {
            USB_PRINTF0("### HUB snd_msg error\n"); /* Send Message failure */
        }
    }
    else
    {
        switch (g_usb_shhub_init_seq[ptr->ip])
        {
            case USB_SEQ_0: /* HUB port power */

                hubaddr                       = mess->keyword;
                g_usb_shhub_hub_addr[ptr->ip] = hubaddr;

                usb_hhub_device_descrip_info(ptr);
                usb_hhub_config_descrip_info(ptr);
                USB_PRINTF0("\nHHHHHHHHHHHHHHHHHHHHHHHHH\n");
                USB_PRINTF0("         USB HOST        \n");
                USB_PRINTF0("      HUB CLASS DEMO     \n");
                USB_PRINTF0("HHHHHHHHHHHHHHHHHHHHHHHHH\n\n");
                g_usb_shhub_init_seq[ptr->ip]  = USB_SEQ_1; /* Next Sequence */
                g_usb_shhub_init_port[ptr->ip] = USB_HUB_P1;
                usb_hhub_specified_path(mess);                                        /* Next Process Selector */
                consoleTextIfAllBootedUp(l10n_get(l10n_STRING_FOR_USB_HUB_ATTACHED)); // By Rohan
                setTimeUSBInitializationEnds(44100 << 1);                             // No more popups for 2 seconds

                break;

            case USB_SEQ_1: /* Request */

                retval = usb_hhub_port_set_feature(ptr, hubaddr, g_usb_shhub_init_port[ptr->ip],
                    (uint16_t)USB_HUB_PORT_POWER, usb_hhub_class_request_complete); /* SetFeature request */
                if (USB_HUB_QOVR == retval)                                         /* Submit overlap error */
                {
                    usb_hhub_specified_path_wait(mess, (uint16_t)10);
                }
                else
                {
                    g_usb_shhub_init_port[ptr->ip]++;          /* Next Port */
                    g_usb_shhub_init_seq[ptr->ip] = USB_SEQ_2; /* Next Sequence */
                }

                break;

            case USB_SEQ_2: /* Request Result Check */

                // By Rohan: If stall, then that port number doesn't exist. The need to do this is a side-effect of my
                // other fix that just assumes the max number of ports if the hub won't tell us
                if (mess->result == USB_DATA_STALL)
                {
                    g_usb_shhub_info_data[ptr->ip][hubaddr].port_num = g_usb_shhub_init_port[ptr->ip] - 2;

                    uartPrint("new num ports: ");
                    uartPrintNumber(g_usb_shhub_info_data[ptr->ip][hubaddr].port_num);
                    mess->result = USB_CTRL_END; // Pretend everything fine
                }

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    if (g_usb_shhub_init_port[ptr->ip] > g_usb_shhub_info_data[ptr->ip][hubaddr].port_num)
                    {
                        g_usb_shhub_init_port[ptr->ip] = USB_HUB_P1; /* Port Clear */
                        g_usb_shhub_init_seq[ptr->ip]  = USB_SEQ_3;  /* Next Sequence */
                        usb_hhub_specified_path(mess);               /* Next Process Selector */
                    }
                    else
                    {
                        g_usb_shhub_init_seq[ptr->ip] = USB_SEQ_1; /* Loop Sequence */
                        usb_hhub_specified_path(mess);             /* Next Process Selector */
                    }
                }
                break;

            case USB_SEQ_3: /* HUB downport initialize */

                retval = usb_hhub_port_clr_feature(ptr, hubaddr, g_usb_shhub_init_port[ptr->ip],
                    (uint16_t)USB_HUB_C_PORT_CONNECTION, usb_hhub_class_request_complete); /* Request */
                if (USB_HUB_QOVR == retval)                                                /* Submit overlap error */
                {
                    usb_hhub_specified_path_wait(mess, (uint16_t)10);
                }
                else
                {
                    g_usb_shhub_init_port[ptr->ip]++;          /* Next Port */
                    g_usb_shhub_init_seq[ptr->ip] = USB_SEQ_4; /* Next Sequence */
                }

                break;

            case USB_SEQ_4: /* Request Result Check */

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    if (g_usb_shhub_init_port[ptr->ip] > g_usb_shhub_info_data[ptr->ip][hubaddr].port_num)
                    {
                        g_usb_shhub_init_seq[ptr->ip]  = USB_SEQ_0;  /* Sequence Clear */
                        g_usb_shhub_init_port[ptr->ip] = USB_HUB_P1; /* Port Clear */
                        g_usb_shhub_info[ptr->ip]      = USB_MSG_CLS_INIT;
                        g_usb_shhub_process[ptr->ip]   = USB_MSG_HUB_EVENT; /* Next Attach Process */
                        usb_hhub_specified_path(mess);                      /* Next Process Selector */
                    }
                    else
                    {
                        g_usb_shhub_init_seq[ptr->ip] = USB_SEQ_3; /* Loop Sequence */
                        usb_hhub_specified_path(mess);             /* Next Process Selector */
                    }
                }

                break;

            default:

                retval = USB_ERROR;

                break;
        }

        if ((USB_OK != retval) && (USB_HUB_QOVR != retval))
        {
            g_usb_shhub_init_port[ptr->ip] = USB_HUB_P1; /* Port Clear */
            g_usb_shhub_init_seq[ptr->ip]  = USB_SEQ_0;  /* Sequence Clear */
            g_usb_shhub_info[ptr->ip]      = USB_NULL;   /* Clear */
            g_usb_shhub_process[ptr->ip]   = USB_NULL;   /* Clear */
        }

        err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)mess);
        if (USB_OK != err)
        {
            USB_PRINTF0("### USB HostHubClass rel_blk error\n"); /* Release Memoryblock failure */
        }
    }
} /* End of function usb_hhub_init_down_port() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_port_attach
 Description     : port attach
 Arguments       : uint16_t     hubaddr      : Hub address
                 : uint16_t     portnum      : Down port number
                 : usb_clsinfo_t *mess
 Return value    : uint16_t                  : Manager Mode
 ***********************************************************************************************************************/
static uint16_t usb_hhub_port_attach(uint16_t hubaddr, uint16_t portnum, usb_clsinfo_t* mess)
{
    uint16_t rootport;
    uint16_t devaddr;
    uint16_t retval;
    uint16_t hpphub;
    uint16_t hubport;
    uint16_t buffer;
    usb_er_t err;
    usb_utr_t* ptr;

    ptr     = (usb_utr_t*)mess;
    hubaddr = g_usb_shhub_hub_addr[ptr->ip];
    portnum = g_usb_shhub_event_port[ptr->ip];

    if (USB_MSG_HUB_ATTACH != g_usb_shhub_process[ptr->ip])
    {
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)mess);
        if (USB_OK != err)
        {
            /* Send Message failure */
            USB_PRINTF0("### HUB snd_msg error\n");
        }
    }
    else
    {
        switch (g_usb_shhub_attach_seq[ptr->ip])
        {
            case USB_SEQ_0:

                if (USB_NULL == g_p_usb_pipe[USB_PIPE0])
                {
                    g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_1;
                    g_usb_shhub_process[ptr->ip]    = USB_MSG_HUB_RESET;
                }
                else
                {
                    g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_0;
                }
                usb_hhub_specified_path(mess); /* Next Process Selector */

                break;

            case USB_SEQ_1:

                /* Device Enumeration */
                switch ((uint16_t)(g_usb_hhub_data[ptr->ip][hubaddr][1] & (uint8_t)0x06))
                {
                    case 0x00:

                        g_usb_hstd_device_speed[ptr->ip] = USB_FSCONNECT;
                        USB_PRINTF0(" Full-Speed Device\n");

                        break;

                    case 0x02:

                        g_usb_hstd_device_speed[ptr->ip] = USB_LSCONNECT;
                        USB_PRINTF0(" Low-Speed Device\n");

                        break;

                    case 0x04:

                        g_usb_hstd_device_speed[ptr->ip] = USB_HSCONNECT;
                        USB_PRINTF0(" Hi-Speed Device\n");

                        break;

                    default:

                        g_usb_hstd_device_speed[ptr->ip] = USB_NOCONNECT;
                        USB_PRINTF0(" Detach Detached\n");

                        break;
                }
                rootport = usb_hstd_get_rootport(ptr, (uint16_t)(hubaddr << USB_DEVADDRBIT));
                devaddr  = usb_hhub_get_cnn_devaddr(ptr, hubaddr, portnum); /* Now downport device search */
                g_usb_hstd_device_addr[ptr->ip]        = devaddr;
                devaddr                                = (uint16_t)(devaddr << USB_DEVADDRBIT);
                g_usb_hstd_mgr_mode[ptr->ip][rootport] = USB_DEFAULT;
                if (0 != devaddr)
                {
                    usb_hstd_set_hub_port(ptr, devaddr, (uint16_t)(hubaddr << 11), (uint16_t)(portnum << 8));

                    /* Get DEVADDR register */
                    buffer = hw_usb_hread_devadd(ptr, devaddr);
                    do
                    {
                        hpphub  = (uint16_t)(buffer & USB_UPPHUB);
                        hubport = (uint16_t)(buffer & USB_HUBPORT);
                        devaddr = (uint16_t)(hpphub << 1);

                        /* Get DEVADDR register */
                        buffer = hw_usb_hread_devadd(ptr, devaddr);
                    } while ((USB_HSCONNECT != (buffer & USB_USBSPD)) && (USB_DEVICE_0 != devaddr));

                    usb_hstd_set_dev_addr(ptr, (uint16_t)USB_DEVICE_0, g_usb_hstd_device_speed[ptr->ip], rootport);

                    /* Set up-port hub */
                    usb_hstd_set_hub_port(ptr, (uint16_t)USB_DEVICE_0, hpphub, hubport);

                    /* Set up-port hub */
                    usb_hstd_set_hub_port(
                        ptr, (uint16_t)(g_usb_hstd_device_addr[ptr->ip] << USB_DEVADDRBIT), hpphub, hubport);

                    /* Clear Enumeration Sequence Number */
                    g_usb_hstd_enum_seq[ptr->ip] = 0;
                    if (USB_NOCONNECT != g_usb_hstd_device_speed[ptr->ip])
                    {
                        (*g_usb_hstd_enumaration_process[0])(ptr, (uint16_t)USB_DEVICE_0, (uint16_t)0);
                        g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_2;
                        usb_hhub_specified_path_wait(mess, 30u); // Modified by Rohan from 3u - see below
                    }
                    else
                    {
                        g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_3;
                        usb_hhub_specified_path(mess); /* Next Process Selector */
                    }
                }
                else
                {
                    g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_3;
                    usb_hhub_specified_path(mess); /* Next Process Selector */
                }

                break;

            case USB_SEQ_2:

                /* Get Port Number */
                rootport = usb_hstd_get_rootport(ptr, (uint16_t)(hubaddr << USB_DEVADDRBIT));
                if ((USB_CONFIGURED == g_usb_hstd_mgr_mode[ptr->ip][rootport])
                    || (USB_DEFAULT != g_usb_hstd_mgr_mode[ptr->ip][rootport]))
                {
                    /* HUB downport status */
                    g_usb_shhub_down_port[ptr->ip][hubaddr] |= USB_BITSET(portnum);
                    g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_0;
                    g_usb_shhub_process[ptr->ip]    = USB_MSG_HUB_EVENT;
                    usb_hhub_specified_path(mess);
                }
                else
                {
                    g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_2;
                    usb_hhub_specified_path_wait(mess,
                        30u); // Modified by Rohan from 3u - when too low, we get a freeze on attaching a device to a
                              // hub. I did some more accurate testing, and it seems that a delay is needed, and the
                              // minimum delay is between 100 and 330uS.
                }

                break;

            case USB_SEQ_3:

                g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_4;
                g_usb_shhub_process[ptr->ip]    = USB_MSG_HUB_RESET;
                usb_hhub_specified_path(mess); /* Next Process Selector */

                break;

            case USB_SEQ_4:

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    /* Hub Port Set Feature Request */
                    retval = usb_hhub_port_set_feature(
                        ptr, hubaddr, portnum, USB_HUB_PORT_SUSPEND, usb_hhub_class_request_complete);
                    /* Submit overlap error */
                    if (USB_HUB_QOVR == retval)
                    {
                        usb_hhub_specified_path_wait(mess, (uint16_t)10);
                    }
                    else
                    {
                        g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_5;
                    }
                }

                break;

            case USB_SEQ_5:

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    usb_hhub_port_detach(ptr, hubaddr, portnum);
                    g_usb_shhub_info_data[ptr->ip][devaddr].up_addr     = 0; /* Up-hubaddr clear */
                    g_usb_shhub_info_data[ptr->ip][devaddr].up_port_num = 0; /* Up-hubport clear */
                    g_usb_shhub_attach_seq[ptr->ip]                     = USB_SEQ_0;
                    g_usb_shhub_process[ptr->ip]                        = USB_MSG_HUB_EVENT;
                    usb_hhub_specified_path(mess);
                }

                break;

            default:

                g_usb_shhub_attach_seq[ptr->ip] = USB_SEQ_0;
                g_usb_shhub_process[ptr->ip]    = USB_NULL;
                g_usb_shhub_info[ptr->ip]       = USB_NULL;

                break;
        }

        err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)mess);
        if (USB_OK != err)
        {
            USB_PRINTF0("### USB HostHubClass rel_blk error\n");
        }
    }
    return USB_OK;
} /* End of function usb_hhub_port_attach() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_event
 Description     : USB Hub Event process.
 Arguments       : usb_clsinfo_t *mess       : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_event(usb_clsinfo_t* mess)
{
    usb_er_t err;
    uint16_t hubaddr;
    uint16_t devaddr;
    uint16_t retval;
    usb_utr_t* ptr;
    uint32_t port_status;
    uint16_t next_port_check = USB_FALSE;
    uint16_t port_clr_feature_type;

    ptr     = (usb_utr_t*)mess;
    hubaddr = g_usb_shhub_hub_addr[ptr->ip];

    if (USB_MSG_HUB_EVENT != g_usb_shhub_process[ptr->ip])
    {
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)mess);
        if (USB_OK != err)
        {
            USB_PRINTF0("### HUB snd_msg error\n");
        }
    }
    else
    {
        switch (g_usb_shhub_event_seq[ptr->ip])
        {
            case USB_SEQ_0: /* Request */

                if (USB_MSG_HUB_SUBMITRESULT == g_usb_shhub_info[ptr->ip]) /* Event Check */
                {
                    /* Hub and Port Status Change Bitmap(b0:Hub,b1:DownPort1change detected,b2:DownPort2,...) */
                    if (0 != (g_usb_hhub_data[ptr->ip][hubaddr][0] & USB_BITSET(g_usb_shhub_event_port[ptr->ip])))
                    {
                        USB_PRINTF1(" *** HUB port %d \t", g_usb_shhub_event_port[ptr->ip]);

                        /* GetStatus request */
                        retval = usb_hhub_get_port_information(
                            ptr, hubaddr, g_usb_shhub_event_port[ptr->ip], usb_hhub_class_request_complete);
                        if (USB_HUB_QOVR == retval) /* Submit overlap error */
                        {
                            usb_hhub_specified_path_wait(mess, (uint16_t)10); /* Retry */
                        }
                        else
                        {
                            g_usb_shhub_event_seq[ptr->ip] = USB_SEQ_1;
                        }
                    }
                    else
                    {
                        /* Port check end */
                        next_port_check = USB_TRUE;
                    }
                }
                else /* if (USB_MSG_CLS_INIT == g_usb_shhub_info[ptr->ip]) */ /* Event Check */
                {                                                             /* USB_MSG_HUB_INIT */
                    USB_PRINTF2(" *** address %d downport %d \t", hubaddr, g_usb_shhub_event_port[ptr->ip]);

                    /* GetStatus request */
                    retval = usb_hhub_get_port_information(
                        ptr, hubaddr, g_usb_shhub_event_port[ptr->ip], usb_hhub_class_request_complete);
                    if (USB_HUB_QOVR == retval) /* Submit overlap error */
                    {
                        usb_hhub_specified_path_wait(mess, (uint16_t)10); /* Retry */
                    }
                    else
                    {
                        g_usb_shhub_event_seq[ptr->ip] = USB_SEQ_3;
                    }
                }

                break;

            case USB_SEQ_1: /* Request Result Check & Request */

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    port_status = (uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][0];
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][1] << 8);
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][2] << 16);
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][3] << 24);
                    USB_PRINTF2(" [port/status] : %d, 0x%08x\n", g_usb_shhub_event_port[ptr->ip], port_status);

                    if (0 != (port_status & USB_BIT_C_PORT_CONNECTION)) /* C_PORT_CONNECTION */
                    {
                        retval = usb_hhub_port_clr_feature(ptr, hubaddr, g_usb_shhub_event_port[ptr->ip],
                            (uint16_t)USB_HUB_C_PORT_CONNECTION, usb_hhub_class_request_complete);
                        if (USB_HUB_QOVR == retval) /* Submit overlap error */
                        {
                            usb_hhub_specified_path_wait(mess, (uint16_t)10); /* Retry */
                        }
                        else
                        {
                            g_usb_shhub_event_seq[ptr->ip] = USB_SEQ_3; /* Attach Sequence */
                        }
                    }
                    else
                    {
                        /* Now downport device search */
                        devaddr = usb_hhub_get_cnn_devaddr(ptr, hubaddr, g_usb_shhub_event_port[ptr->ip]);
                        if (0 != (port_status & USB_BIT_PORT_ENABLE)) /* PORT_ENABLE */
                        {
                            USB_PRINTF1(" Hubport error address%d\n", devaddr);
                            port_clr_feature_type = USB_HUB_C_PORT_ENABLE; /* C_PORT_ENABLE */
                        }
                        else if (0 != (port_status & USB_BIT_PORT_SUSPEND)) /* PORT_SUSPEND */
                        {
                            USB_PRINTF1(" Hubport suspend(resume complete) address%d\n", devaddr);
                            port_clr_feature_type = USB_HUB_C_PORT_SUSPEND; /* C_PORT_SUSPEND */
                        }
                        else if (0 != (port_status & USB_BIT_C_PORT_OVER_CURRENT)) /* PORT_OVER_CURRENT */
                        {
                            USB_PRINTF1(" Hubport over current address%d\n", devaddr);
                            port_clr_feature_type = USB_HUB_C_PORT_OVER_CURRENT; /* C_PORT_OVER_CURRENT */
                        }
                        else if (0 != (port_status & USB_BIT_PORT_RESET)) /* PORT_RESET */
                        {
                            USB_PRINTF1(" Hubport reset(reset complete) address%d\n", devaddr);
                            port_clr_feature_type = USB_HUB_C_PORT_RESET; /* C_PORT_RESET */
                        }
                        else
                        {
                            next_port_check = USB_TRUE;
                        }

                        if (USB_FALSE == next_port_check)
                        {
                            /* ClearFeature request */
                            retval = usb_hhub_port_clr_feature(ptr, hubaddr, g_usb_shhub_event_port[ptr->ip],
                                port_clr_feature_type, usb_hhub_class_request_complete);
                            if (USB_HUB_QOVR == retval) /* Submit overlap error */
                            {
                                usb_hhub_specified_path_wait(mess, (uint16_t)10); /* Retry */
                            }
                            else
                            {
                                g_usb_shhub_event_seq[ptr->ip] = USB_SEQ_2; /* Next Sequence */
                            }
                        }
                    }
                }

                break;

            case USB_SEQ_2: /* Request Result Check */

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    if (0 != (port_status & USB_BIT_PORT_SUSPEND)) /* PORT_SUSPEND */
                    {                                              /* C_PORT_SUSPEND */
                        /* HUB downport status */
                        g_usb_shhub_remote[ptr->ip][hubaddr] |= USB_BITSET(g_usb_shhub_event_port[ptr->ip]);
                    }
                    next_port_check = USB_TRUE;
                }

                break;

            case USB_SEQ_3: /* Attach Sequence */

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    port_status = (uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][0];
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][1] << 8);
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][2] << 16);
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][3] << 24);
                    USB_PRINTF2(" [port/status] : %d, 0x%08x\n", g_usb_shhub_event_port[ptr->ip], port_status);

                    if (0 != (port_status & USB_BIT_PORT_CONNECTION)) /* PORT_CONNECTION */
                    {
                        if (USB_MSG_HUB_SUBMITRESULT == g_usb_shhub_info[ptr->ip])
                        {
                            if (0
                                == (g_usb_shhub_down_port[ptr->ip][hubaddr]
                                    & USB_BITSET(g_usb_shhub_event_port[ptr->ip])))
                            {
                                g_usb_shhub_event_seq[ptr->ip] = USB_SEQ_4; /* Next Attach sequence */
                                usb_hhub_new_connect(mess, (uint16_t)0, (uint16_t)0, mess);
                                if (g_usb_shhub_process[ptr->ip] != USB_MSG_HUB_ATTACH)
                                    goto noPortConnection; // Error checking added by Rohan - otherwise USB stops
                                                           // function when too many devices connected
                            }
                            else
                            {
noPortConnection:
                                next_port_check = USB_TRUE; /* Not PORT_CONNECTION */
                            }
                        }
                        else /* if (USB_MSG_CLS_INIT == g_usb_shhub_info[ptr->ip]) */
                        {
                            g_usb_shhub_event_seq[ptr->ip] = USB_SEQ_4; /* Next Attach sequence */
                            usb_hhub_new_connect(mess, (uint16_t)0, (uint16_t)0, mess);
                            if (g_usb_shhub_process[ptr->ip] != USB_MSG_HUB_ATTACH)
                                goto noPortConnection; // Error checking added by Rohan - otherwise USB stops function
                                                       // when too many devices connected
                        }
                    }
                    else
                    { /* non connect */
                        if (USB_MSG_HUB_SUBMITRESULT == g_usb_shhub_info[ptr->ip])
                        {
                            /* Now downport device search */
                            devaddr = usb_hhub_get_cnn_devaddr(ptr, hubaddr, g_usb_shhub_event_port[ptr->ip]);
                            if (0 != devaddr)
                            {
                                usb_hhub_port_detach(ptr, hubaddr, g_usb_shhub_event_port[ptr->ip]);
                                USB_PRINTF1(" Hubport disconnect address%d\n", devaddr);
                                consoleTextIfAllBootedUp(l10n_get(l10n_STRING_FOR_USB_DEVICE_DETACHED)); // By Rohan
                                g_usb_shhub_info_data[ptr->ip][devaddr].up_addr     = 0; /* Up-address clear */
                                g_usb_shhub_info_data[ptr->ip][devaddr].up_port_num = 0; /* Up-port num clear */
                                g_usb_shhub_info_data[ptr->ip][devaddr].port_num    = 0; /* Port number clear */
                                g_usb_shhub_info_data[ptr->ip][devaddr].pipe_num    = 0; /* Pipe number clear */
                            }
                        }
                        next_port_check = USB_TRUE;
                    }
                }

                break;

            case USB_SEQ_4: /* Attach */

                next_port_check = USB_TRUE;

                break;

            default: /* Error */
                break;
        }

        if (USB_TRUE == next_port_check)
        {
            if (g_usb_shhub_event_port[ptr->ip] >= g_usb_shhub_info_data[ptr->ip][hubaddr].port_num)
            {
                /* Port check end */
                usb_hhub_trans_start(ptr, hubaddr, (uint32_t)1, &g_usb_hhub_data[ptr->ip][hubaddr][0],
                    &usb_hhub_trans_complete); /* Get Hub and Port Status Change Bitmap */

                g_usb_shhub_event_port[ptr->ip] = USB_HUB_P1; /* Port Clear */
                g_usb_shhub_event_seq[ptr->ip]  = USB_SEQ_0;  /* Sequence Clear */
                g_usb_shhub_process[ptr->ip]    = USB_NULL;
                g_usb_shhub_info[ptr->ip]       = USB_NULL;
            }
            else
            {
                g_usb_shhub_event_port[ptr->ip]++;          /* Next port check */
                g_usb_shhub_event_seq[ptr->ip] = USB_SEQ_0; /* Sequence Clear */
                usb_hhub_specified_path(mess);
            }
        }

        err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)mess);
        if (USB_OK != err)
        {
            USB_PRINTF0("### USB HostHubClass rel_blk error\n");
        }
    }
} /* End of function usb_hhub_event() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_port_reset
 Description     : HUB down port USB-reset request
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     portnum      : Down port number
                 : usb_clsinfo_t *mess       : Pointer to usb_clsinfo_t structure.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_port_reset(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum, usb_clsinfo_t* mess)
{
    usb_er_t err;
    uint16_t retval;
    uint32_t port_status;

    hubaddr = g_usb_shhub_hub_addr[ptr->ip];
    portnum = g_usb_shhub_event_port[ptr->ip];

    if (USB_MSG_HUB_RESET != g_usb_shhub_process[ptr->ip])
    {
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)mess);
        if (USB_OK != err)
        {
            USB_PRINTF0("### HUB snd_msg error\n");
        }
    }
    else
    {
        switch (g_usb_shhub_reset_seq[ptr->ip])
        {
            case USB_SEQ_0:

                /* Hub port SetFeature */
                usb_cpu_delay_xms((uint16_t)100);
                retval = usb_hhub_port_set_feature(
                    ptr, hubaddr, portnum, (uint16_t)USB_HUB_PORT_RESET, usb_hhub_class_request_complete);
                /* Submit overlap error */
                if (USB_HUB_QOVR == retval)
                {
                    usb_hhub_specified_path_wait(mess, (uint16_t)10);
                }
                else
                {
                    g_usb_shhub_reset_seq[ptr->ip] = USB_SEQ_1;
                }

                break;

            case USB_SEQ_1:

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    usb_cpu_delay_xms((uint16_t)60);

                    /* Get Status */
                    retval = usb_hhub_get_port_information(ptr, hubaddr, portnum, usb_hhub_class_request_complete);

                    /* Submit overlap error */
                    if (USB_HUB_QOVR == retval)
                    {
                        usb_hhub_specified_path_wait(mess, (uint16_t)10);
                    }
                    else
                    {
                        g_usb_shhub_reset_seq[ptr->ip] = USB_SEQ_2;
                    }
                }

                break;

            case USB_SEQ_2:

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    port_status = (uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][0];
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][1] << 8);
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][2] << 16);
                    port_status |= ((uint32_t)g_usb_hhub_data[ptr->ip][hubaddr][3] << 24);

                    /* Check Reset Change(C_PORT_RESET) */
                    if (USB_BIT_C_PORT_RESET != (port_status & USB_BIT_C_PORT_RESET))
                    {
                        g_usb_shhub_reset_seq[ptr->ip] = USB_SEQ_0;
                        usb_hhub_specified_path_wait(mess, (uint16_t)10);
                    }
                    else
                    {
                        /* Hub port ClearFeature */
                        usb_cpu_delay_xms((uint16_t)20);

                        retval = usb_hhub_port_clr_feature(
                            ptr, hubaddr, portnum, (uint16_t)USB_HUB_C_PORT_RESET, usb_hhub_class_request_complete);
                        /* Submit overlap error */
                        if (USB_HUB_QOVR == retval)
                        {
                            usb_hhub_specified_path_wait(mess, (uint16_t)10);
                        }
                        else
                        {
                            g_usb_shhub_reset_seq[ptr->ip] = USB_SEQ_3;
                        }
                    }
                }

                break;

            case USB_SEQ_3:

                retval = usb_hhub_request_result(mess->result);
                if (USB_OK == retval)
                {
                    g_usb_shhub_reset_seq[ptr->ip] = USB_SEQ_0;
                    g_usb_shhub_process[ptr->ip]   = USB_MSG_HUB_ATTACH;
                    usb_hhub_specified_path(mess);
                }

                break;

            default:

                g_usb_shhub_reset_seq[ptr->ip] = USB_SEQ_0;
                g_usb_shhub_process[ptr->ip]   = USB_NULL;

                break;
        }

        err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)mess);
        if (USB_OK != err)
        {
            USB_PRINTF0("### USB HostHubClass rel_blk error\n");
        }
    }
} /* End of function usb_hhub_port_reset() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_check_class
 Description     : HUB Class driver check
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     **table      : Descriptor, etc
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_check_class(usb_utr_t* ptr, uint16_t** table)
{
    usb_clsinfo_t* p_blf;
    usb_clsinfo_t* cp;
    usb_er_t err;

    g_p_usb_shhub_device_table[ptr->ip]    = (uint8_t*)(table[0]);
    g_p_usb_shhub_config_table[ptr->ip]    = (uint8_t*)(table[1]);
    g_p_usb_shhub_interface_table[ptr->ip] = (uint8_t*)(table[2]);
    *table[3]                              = USB_OK;
    g_usb_shhub_spec[ptr->ip]              = *table[4];
    g_usb_shhub_root[ptr->ip]              = *table[5];
    g_usb_shhub_speed[ptr->ip]             = *table[6];
    g_usb_shhub_dev_addr[ptr->ip]          = *table[7];
    g_usb_shhub_index[ptr->ip]             = usb_hhub_chk_tbl_indx1(ptr, g_usb_shhub_dev_addr[ptr->ip]);

    g_usb_shhub_class_seq[ptr->ip] = 0;

    /* Get mem pool blk */
    if (USB_OK == USB_PGET_BLK(USB_HUB_MPL, &p_blf))
    {
        cp          = (usb_clsinfo_t*)p_blf;
        cp->msginfo = USB_MSG_CLS_CHECKREQUEST;

        cp->ipp = ptr->ipp;
        cp->ip  = ptr->ip;

        /* Enumeration wait TaskID change */
        usb_hstd_enu_wait(ptr, (uint8_t)USB_HUB_TSK);

        /* Class check of enumeration sequence move to class function */
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)cp);
        if (USB_OK != err)
        {
            /* Send Message failure */
            USB_PRINTF1("Host HUB snd_msg error %x\n", err);
        }
    }
    else
    {
        /* Send Message failure */
        while (1)
        {
            /* Non */
        }
    }
} /* End of function usb_hhub_check_class() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_trans_start
 Description     : HUB sys data transfer / control transfer
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint32_t     size         : Data Transfer size
                 : uint8_t      *table       : Receive Data area
                 : usb_cb_t     complete     : Callback function
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_trans_start(usb_utr_t* ptr, uint16_t hubaddr, uint32_t size, uint8_t* table, usb_cb_t complete)
{
    usb_er_t err;

    /* Transfer structure setting */
    g_usb_shhub_data_mess[ptr->ip][hubaddr].keyword   = g_usb_shhub_info_data[ptr->ip][hubaddr].pipe_num;
    g_usb_shhub_data_mess[ptr->ip][hubaddr].p_tranadr = table;
    g_usb_shhub_data_mess[ptr->ip][hubaddr].tranlen   = size;
    g_usb_shhub_data_mess[ptr->ip][hubaddr].p_setup   = 0;
    g_usb_shhub_data_mess[ptr->ip][hubaddr].status    = USB_DATA_WAIT;
    g_usb_shhub_data_mess[ptr->ip][hubaddr].complete  = complete;
    g_usb_shhub_data_mess[ptr->ip][hubaddr].segment   = USB_TRAN_END;

    g_usb_shhub_data_mess[ptr->ip][hubaddr].ipp = ptr->ipp;
    g_usb_shhub_data_mess[ptr->ip][hubaddr].ip  = ptr->ip;

    /* Transfer start */
    err = usb_hstd_transfer_start(&g_usb_shhub_data_mess[ptr->ip][hubaddr]);
    if (USB_OK != err)
    {
        /* Send Message failure */
        USB_PRINTF1("### usb_hhub_trans_start error (%ld)\n", err);
    }
} /* End of function usb_hhub_trans_start() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_port_set_feature
 Description     : SetFeature request
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     port         : Down port number
                 : uint16_t     command      : Request command
 Return value    : uint16_t                  : DONE/ERROR
 ***********************************************************************************************************************/
static uint16_t usb_hhub_port_set_feature(
    usb_utr_t* ptr, uint16_t hubaddr, uint16_t port, uint16_t command, usb_cb_t complete)
{
    usb_er_t qerr;

    /* Request */
    g_usb_shhub_class_request[ptr->ip][0] = USB_SET_FEATURE | USB_HOST_TO_DEV | USB_CLASS | USB_OTHER;
    g_usb_shhub_class_request[ptr->ip][1] = command;
    g_usb_shhub_class_request[ptr->ip][2] = port; /* Port number */
    g_usb_shhub_class_request[ptr->ip][3] = 0;
    g_usb_shhub_class_request[ptr->ip][4] = hubaddr; /* Device address */

    /* Port SetFeature */
    g_usb_shhub_ctrl_mess[ptr->ip].keyword   = USB_PIPE0;
    g_usb_shhub_ctrl_mess[ptr->ip].p_tranadr = (void*)&g_usb_hhub_data[ptr->ip][hubaddr][0];
    g_usb_shhub_ctrl_mess[ptr->ip].tranlen   = (uint32_t)g_usb_shhub_class_request[ptr->ip][3];
    g_usb_shhub_ctrl_mess[ptr->ip].p_setup   = g_usb_shhub_class_request[ptr->ip];
    g_usb_shhub_ctrl_mess[ptr->ip].segment   = USB_TRAN_END;
    g_usb_shhub_ctrl_mess[ptr->ip].complete  = complete;

    g_usb_shhub_ctrl_mess[ptr->ip].ipp = ptr->ipp;
    g_usb_shhub_ctrl_mess[ptr->ip].ip  = ptr->ip;

    /* Transfer start */
    qerr = usb_hstd_transfer_start(&g_usb_shhub_ctrl_mess[ptr->ip]);
    if (USB_QOVR == qerr)
    {
        return USB_HUB_QOVR;
    }

    return USB_OK;
} /* End of function usb_hhub_port_set_feature() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_port_clr_feature
 Description     : ClearFeature request
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     port         : Down port number
                 : uint16_t     command      : Request command
 Return value    : uint16_t                  : DONE/ERROR
 ***********************************************************************************************************************/
static uint16_t usb_hhub_port_clr_feature(
    usb_utr_t* ptr, uint16_t hubaddr, uint16_t port, uint16_t command, usb_cb_t complete)
{
    usb_er_t qerr;

    /* Request */
    g_usb_shhub_class_request[ptr->ip][0] = USB_CLEAR_FEATURE | USB_HOST_TO_DEV | USB_CLASS | USB_OTHER;
    g_usb_shhub_class_request[ptr->ip][1] = command;
    g_usb_shhub_class_request[ptr->ip][2] = port; /* Port number */
    g_usb_shhub_class_request[ptr->ip][3] = 0;
    g_usb_shhub_class_request[ptr->ip][4] = hubaddr; /* Device address */

    /* Port ClearFeature */
    g_usb_shhub_ctrl_mess[ptr->ip].keyword   = USB_PIPE0;
    g_usb_shhub_ctrl_mess[ptr->ip].p_tranadr = (void*)&g_usb_hhub_data[ptr->ip][hubaddr][0];
    g_usb_shhub_ctrl_mess[ptr->ip].tranlen   = (uint32_t)g_usb_shhub_class_request[ptr->ip][3];
    g_usb_shhub_ctrl_mess[ptr->ip].p_setup   = g_usb_shhub_class_request[ptr->ip];
    g_usb_shhub_ctrl_mess[ptr->ip].segment   = USB_TRAN_END;
    g_usb_shhub_ctrl_mess[ptr->ip].complete  = complete;

    g_usb_shhub_ctrl_mess[ptr->ip].ipp = ptr->ipp;
    g_usb_shhub_ctrl_mess[ptr->ip].ip  = ptr->ip;

    /* Transfer start */
    qerr = usb_hstd_transfer_start(&g_usb_shhub_ctrl_mess[ptr->ip]);
    if (USB_QOVR == qerr)
    {
        return USB_HUB_QOVR;
    }

    return USB_OK;
} /* End of function usb_hhub_port_clr_feature() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_request_result
 Description     : Hub Request Result Check
 Arguments       : uint16_t     errcheck     : Hub result
 Return value    : uint16_t                  : USB_OK/USB_ERROR
 ***********************************************************************************************************************/
static uint16_t usb_hhub_request_result(uint16_t errcheck)
{
    if (USB_DATA_TMO == errcheck)
    {
        USB_PRINTF0("*** HUB Request Timeout error !\n");
        return USB_ERROR;
    }
    else if (USB_DATA_STALL == errcheck)
    {
        USB_PRINTF0("*** HUB Request STALL !\n");
        return USB_ERROR;
    }
    else if (USB_CTRL_END != errcheck)
    {
        USB_PRINTF0("*** HUB Request error !\n");
        return USB_ERROR;
    }
    else
    {
        /* Non */
    }
    return USB_OK;
} /* End of function usb_hhub_request_result() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_trans_complete
 Description     : Recieve complete Hub and Port Status Change Bitmap
 Arguments       : usb_utr_t     *mess       : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_trans_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2)
{
    usb_er_t err;
    uint16_t pipenum;
    uint16_t hubaddr;
    usb_utr_t* ptr;

    ptr                           = (usb_utr_t*)mess;
    pipenum                       = mess->keyword;
    hubaddr                       = usb_hhub_get_hubaddr(ptr, pipenum);
    g_usb_shhub_hub_addr[ptr->ip] = hubaddr;

    if ((USB_MSG_HUB_SUBMITRESULT != g_usb_shhub_process[ptr->ip]) && (USB_NULL != g_usb_shhub_process[ptr->ip]))
    {
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)mess);
        if (USB_OK != err)
        {
            /* Send Message failure */
            USB_PRINTF0("### HUB snd_msg error\n");
        }
    }
    else
    {
        g_usb_shhub_process[ptr->ip] = USB_NULL;

        switch (mess->status)
        {
            /* Direction is in then data receive end */
            case USB_DATA_SHT:
            case USB_DATA_OK:

                if ((USB_DEFAULT == g_usb_hstd_mgr_mode[ptr->ip][0])
                    || (USB_DEFAULT == g_usb_hstd_mgr_mode[ptr->ip][1]))
                {
                    err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)mess);
                    if (USB_OK != err)
                    {
                        USB_PRINTF0("### HUB task snd_msg error\n");
                    }
                }
                else
                {
                    /* HUB port connection */
                    g_usb_shhub_info[ptr->ip]    = USB_MSG_HUB_SUBMITRESULT;
                    g_usb_shhub_process[ptr->ip] = USB_MSG_HUB_EVENT;
                    usb_hhub_specified_path(mess);
                }

                break;

            case USB_DATA_STALL:

                USB_PRINTF0("*** Data Read error. (STALL) !\n");
                usb_hstd_clr_stall(ptr, pipenum, (usb_cb_t)&usb_hstd_dummy_function); /* CLEAR_FEATURE */

                break;

            case USB_DATA_OVR:

                USB_PRINTF0("### receiver over. !\n");

                break;

            case USB_DATA_STOP:

                USB_PRINTF0("### receiver stop. !\n");

                break;

            default:

                USB_PRINTF0("### HUB Class Data Read error !\n");

                break;
        }
    }
} /* End of function usb_hhub_trans_complete() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_class_request_complete
 Description     : Hub class check result
 Arguments       : usb_utr_t     *mess       : Pointer to usb_utr_t structure.
                 : uint16_t     data1        : Not used.
                 : uint16_t     data2        : Not used.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_class_request_complete(usb_utr_t* ptr, uint16_t data1, uint16_t data2)
{
    usb_mh_t p_blf;
    usb_er_t err;
    usb_clsinfo_t* cp;

    /* Get mem pool blk */
    if (USB_OK == USB_PGET_BLK(USB_HUB_MPL, &p_blf))
    {
        cp          = (usb_clsinfo_t*)p_blf;
        cp->msginfo = g_usb_shhub_process[ptr->ip];
        cp->keyword = ptr->keyword;
        cp->result  = ptr->status;

        cp->ipp = ptr->ipp;
        cp->ip  = ptr->ip;

        /* Send message */
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)p_blf);
        if (USB_OK != err)
        {
            /* Send message failure */
            err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)p_blf);
            USB_PRINTF0("### CheckResult function snd_msg error\n");
        }
    }
    else
    {
        USB_PRINTF0("### CheckResult function pget_blk error\n");
        while (1)
        {
            /* Non */
        }
    }
} /* End of function usb_hhub_class_request_complete() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_initial
 Description     : Global memory initialized
 Arguments       : usb_utr_t     *mess       : Pointer to usb_utr_t structure.
                 : uint16_t     data1        : Not used.
                 : uint16_t     data2        : Not used.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_initial(usb_utr_t* ptr, uint16_t data1, uint16_t data2)
{
    uint16_t i;

    for (i = 0u; i < (USB_MAXDEVADDR + 1u); i++)
    {
        g_usb_shhub_info_data[ptr->ip][i].up_addr     = 0; /* Up-address clear */
        g_usb_shhub_info_data[ptr->ip][i].up_port_num = 0; /* Up-port num clear */
        g_usb_shhub_info_data[ptr->ip][i].port_num    = 0; /* Port number clear */
        g_usb_shhub_info_data[ptr->ip][i].pipe_num    = 0; /* Pipe number clear */
    }
    g_usb_shhub_number[ptr->ip] = 0;
} /* End of function usb_hhub_initial() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_pipe_info
 Description     : Set pipe information
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint8_t      *table       : Interface Descriptor
                 : uint16_t     offset       : DEF_EP table offset
                 : uint16_t     speed        : Device speed
                 : uint16_t     length       : Interface Descriptor length
 Return value    : uint16_t                  : DONE/ERROR
 ***********************************************************************************************************************/
static uint16_t usb_hhub_pipe_info(usb_utr_t* ptr, uint8_t* table, uint16_t offset, uint16_t speed, uint16_t length)
{
    uint16_t ofdsc;
    uint16_t retval;

    /* Check Descriptor */
    if (USB_DT_INTERFACE != table[1])
    {
        /* Configuration Descriptor */
        USB_PRINTF0("### Interface descriptor error (HUB).\n");
        return USB_ERROR;
    }

    /* Check Endpoint Descriptor */
    ofdsc = table[0];
    while (ofdsc < (length - table[0]))
    {
        /* Search within Interface */
        switch (table[ofdsc + 1])
        {
            case USB_DT_DEVICE:        /* Device Descriptor */
            case USB_DT_CONFIGURATION: /* Configuration Descriptor */
            case USB_DT_STRING:        /* String Descriptor */
            case USB_DT_INTERFACE:     /* Interface Descriptor */

                USB_PRINTF0("### Endpoint Descriptor error(HUB).\n");
                return USB_ERROR;

                break;

            case USB_DT_ENDPOINT: /* Endpoint Descriptor */

                /* Interrupt Endpoint */
                if (USB_EP_INT == (table[ofdsc + 3] & USB_EP_TRNSMASK))
                {
                    retval = usb_hstd_chk_pipe_info(speed, &g_usb_hhub_tmp_ep_tbl[ptr->ip][offset], &table[ofdsc]);
                    if (USB_DIR_H_IN == retval)
                    {
                        return USB_OK;
                    }
                    else
                    {
                        USB_PRINTF0("### Endpoint Descriptor error(HUB).\n");
                    }
                }
                ofdsc += table[ofdsc];

                break;

            case USB_DT_DEVICE_QUALIFIER: /* Device Qualifier Descriptor */
            case USB_DT_OTHER_SPEED_CONF: /* Other Speed Configuration */
            case USB_DT_INTERFACE_POWER:  /* Interface Power Descriptor */

                USB_PRINTF0("### Endpoint Descriptor error(HUB).\n");
                return USB_ERROR;

                break;
                /* Another Descriptor */

            default:

                ofdsc += table[ofdsc];

                break;
        }
    }
    return USB_ERROR;
} /* End of function usb_hhub_pipe_info() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_check_request
 Description     : Class check request
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     result       : Check result value
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_check_request(usb_utr_t* ptr, uint16_t result)
{
    usb_mh_t p_blf;
    usb_er_t err;
    usb_clsinfo_t* cp;

    /* Get mem pool blk */
    if (USB_OK == USB_PGET_BLK(USB_HUB_MPL, &p_blf))
    {
        cp          = (usb_clsinfo_t*)p_blf;
        cp->msginfo = USB_MSG_CLS_CHECKREQUEST;
        cp->result  = result;

        cp->ipp = ptr->ipp;
        cp->ip  = ptr->ip;

        /* Send message */
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)p_blf);
        if (USB_OK != err)
        {
            /* Send message failure */
            err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)p_blf);
            USB_PRINTF0("### CheckRequest function snd_msg error\n");
        }
    }
    else
    {
        /* Get memoryblock failure */
        USB_PRINTF0("### CheckRequest function pget_blk error\n");
        while (1)
        {
            /* Non */
        }
    }

} /* End of function usb_hhub_check_request() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_check_descriptor
 Description     : check descriptor
 Arguments       : uint8_t      *table       : Pointer to the descriptor.
                 : uint16_t     spec         : Descriptor valule.
 Return value    : none
 ***********************************************************************************************************************/
uint16_t usb_hhub_check_descriptor(uint8_t* table, uint16_t spec)
{
/* Condition compilation by the difference of useful function */
#ifdef USB_DEBUG_ON
    /* Check Descriptor */
    if (table[1] == spec)
    {
        switch (table[1])
        {
            /* Device Descriptor */
            case USB_DT_DEVICE:

                USB_PRINTF0("  Device Descriptor.\n");

                break;

            /* Configuration Descriptor */
            case USB_DT_CONFIGURATION:

                USB_PRINTF0("  Configuration Descriptor.\n");

                break;

            /* String Descriptor */
            case USB_DT_STRING:

                USB_PRINTF0("  String Descriptor.\n");

                break;

            /* Interface Descriptor */
            case USB_DT_INTERFACE:

                USB_PRINTF0("  Interface Descriptor.\n");

                break;

            /* Endpoint Descriptor */
            case USB_DT_ENDPOINT:

                USB_PRINTF0("  Endpoint Descriptor.\n");

                break;

            /* Device Qualifier Descriptor */
            case USB_DT_DEVICE_QUALIFIER:

                USB_PRINTF0("  Device Qualifier Descriptor.\n");

                break;

            /* Other Speed Configuration Descriptor */
            case USB_DT_OTHER_SPEED_CONF:

                USB_PRINTF0("  Other Speed Configuration Descriptor.\n");

                break;

            /* Interface Power Descriptor */
            case USB_DT_INTERFACE_POWER:

                USB_PRINTF0("  Interface Power Descriptor.\n");

                break;

            /* HUB Descriptor */
            case USB_DT_HUBDESCRIPTOR:

                USB_PRINTF0("  HUB Descriptor.\n");

                break;

            /* Not Descriptor */
            default:

                USB_PRINTF0("### Descriptor error (Not Standard Descriptor).\n");
                return USB_ERROR;

                break;
        }
        return USB_OK;
    }
    else
    {
        switch (table[1])
        {
            /* Device Descriptor */
            case USB_DT_DEVICE:

                USB_PRINTF0("### Descriptor error (Device Descriptor).\n");

                break;

            /* Configuration Descriptor */
            case USB_DT_CONFIGURATION:

                USB_PRINTF0("### Descriptor error (Configuration Descriptor).\n");

                break;

            /* String Descriptor */
            case USB_DT_STRING:

                USB_PRINTF0("### Descriptor error (String Descriptor).\n");

                break;

            /* Interface Descriptor ? */
            case USB_DT_INTERFACE:

                USB_PRINTF0("### Descriptor error (Interface Descriptor).\n");

                break;

            /* Endpoint Descriptor */
            case USB_DT_ENDPOINT:

                USB_PRINTF0("### Descriptor error (Endpoint Descriptor).\n");

                break;

            /* Device Qualifier Descriptor */
            case USB_DT_DEVICE_QUALIFIER:

                USB_PRINTF0("### Descriptor error (Device Qualifier Descriptor).\n");

                break;

            /* Other Speed Configuration Descriptor */
            case USB_DT_OTHER_SPEED_CONF:

                USB_PRINTF0("### Descriptor error (Other Speed Configuration Descriptor).\n");

                break;

            /* Interface Power Descriptor */
            case USB_DT_INTERFACE_POWER:

                USB_PRINTF0("### Descriptor error (Interface Power Descriptor).\n");

                break;

            /* Not Descriptor */
            default:

                USB_PRINTF0("### Descriptor error (Not Standard Descriptor).\n");

                break;
        }
        return USB_ERROR;
    }
#else  /* Not USB_DEBUG_ON */
    return USB_OK;
#endif /* USB_DEBUG_ON */
} /* End of function usb_hhub_check_descriptor() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_chk_config
 Description     : Configuration Descriptor check
 Arguments       : uint16_t     **table      : Pointer to Configuration Descriptor area
                 : uint16_t     spec         : HUB specification
 Return value    : uint16_t                  : DONE/ERROR
 ***********************************************************************************************************************/
static uint16_t usb_hhub_chk_config(uint16_t** table, uint16_t spec)
{
    uint8_t* descriptor_table;
    uint16_t ofset;

    descriptor_table = (uint8_t*)(table[1]);

    /* Descriptor check */
    ofset = usb_hhub_check_descriptor(descriptor_table, (uint16_t)USB_DT_CONFIGURATION);
    if (USB_ERROR == ofset)
    {
        USB_PRINTF0("### Configuration descriptor error !\n");
        return USB_ERROR;
    }

    /* Check interface number */
    switch (spec)
    {
        case USB_FSHUB: /* Full Speed Hub */

            if (USB_HUB_INTNUMFS != descriptor_table[4])
            {
                USB_PRINTF0("### HUB configuration descriptor error !\n");
                return USB_ERROR;
            }

            break;

        case USB_HSHUBS: /* Hi Speed Hub(Multi) */

            if (USB_HUB_INTNUMHSS != descriptor_table[4])
            {
                USB_PRINTF0("### HUB configuration descriptor error !\n");
                return USB_ERROR;
            }

            break;

        case USB_HSHUBM: /* Hi Speed Hub(Single) */

            if (USB_HUB_INTNUMHSM != descriptor_table[4])
            {
                USB_PRINTF0("### HUB configuration descriptor error !\n");
                return USB_ERROR;
            }

            break;

        default:

            return USB_ERROR;

            break;
    }
    return USB_OK;
} /* End of function usb_hhub_chk_config() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_chk_interface
 Description     : Interface Descriptor check
 Arguments       : uint16_t     **table      : Pointer to Interface Descriptor area
                 : uint16_t     spec         : HUB specification
 Return value    : uint16_t                  : DONE/ERROR
 ***********************************************************************************************************************/
static uint16_t usb_hhub_chk_interface(uint16_t** table, uint16_t spec)
{
    uint8_t* descriptor_Table;
    uint16_t ofset;

    descriptor_Table = (uint8_t*)(table[2]);

    /* Descriptor check */
    ofset = usb_hhub_check_descriptor(descriptor_Table, (uint16_t)USB_DT_INTERFACE);
    if (USB_ERROR == ofset)
    {
        USB_PRINTF0("### Interface descriptor error !\n");
        return USB_ERROR;
    }

    /* Check interface class */
    if (USB_IFCLS_HUB != descriptor_Table[5])
    {
        USB_PRINTF0("### HUB interface descriptor error !\n");
        return USB_ERROR;
    }

    /* Check interface number */
    switch (spec)
    {
        case USB_FSHUB: /* Full Speed Hub */

            if ((USB_HUB_INTNUMFS - 1u) != descriptor_Table[2])
            {
                USB_PRINTF0("### HUB interface descriptor error !\n");
                return USB_ERROR;
            }

            break;

        case USB_HSHUBS: /* Hi Speed Hub(Single) */

            if ((USB_HUB_INTNUMHSS - 1u) != descriptor_Table[2])
            {
                USB_PRINTF0("### HUB interface descriptor error !\n");
                return USB_ERROR;
            }

            break;

        case USB_HSHUBM: /* Hi Speed Hub(Multi) */

            if ((USB_HUB_INTNUMHSM - 1u) != descriptor_Table[2])
            {
                USB_PRINTF0("### HUB interface descriptor error !\n");
                return USB_ERROR;
            }

            break;

        default:

            return USB_ERROR;

            break;
    }
    return USB_OK;
} /* End of function usb_hhub_chk_interface() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_chk_tbl_indx1
 Description     : table index search
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
 Return value    : uint16_t                  : table index
 ***********************************************************************************************************************/
static uint16_t usb_hhub_chk_tbl_indx1(usb_utr_t* ptr, uint16_t hubaddr)
{
    uint16_t pipecheck[USB_MAX_PIPE_NO];
    uint16_t i;

    for (i = 0; i < USB_MAX_PIPE_NO; i++)
    {
        /* Check table clear */
        pipecheck[i] = 0;
    }

    for (i = 0; i < (USB_MAXDEVADDR + 1u); i++)
    {
        /* Used pipe number set */
        if (0 != g_usb_shhub_info_data[ptr->ip][i].pipe_num)
        {
            pipecheck[g_usb_shhub_info_data[ptr->ip][i].pipe_num - 1] = 1;
        }
    }

    for (i = USB_MAX_PIPE_NO; 0 != i; i--)
    {
        if (0 == pipecheck[i - 1])
        {
            return (uint16_t)((USB_MAX_PIPE_NO - i) * USB_EPL);
        }
    }
    return (USB_ERROR);
} /* End of function usb_hhub_chk_tbl_indx1() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_chk_tbl_indx2
 Description     : table index search
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
 Return value    : uint16_t                  : table index
 ***********************************************************************************************************************/
static uint16_t usb_hhub_chk_tbl_indx2(usb_utr_t* ptr, uint16_t hubaddr)
{
    /* Search table index */
    switch (g_usb_shhub_info_data[ptr->ip][hubaddr].pipe_num)
    {
        case USB_PIPE9:

            return (uint16_t)(0u * USB_EPL);

            break;

        case USB_PIPE8:

            return (uint16_t)(1u * USB_EPL);

            break;

        case USB_PIPE7:

            return (uint16_t)(2u * USB_EPL);

            break;

        case USB_PIPE6:

            return (uint16_t)(3u * USB_EPL);

            break;

        default:
            break;
    }

    return (USB_ERROR);
} /* End of function usb_hhub_chk_tbl_indx2() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_device_descrip_info
 Description     : device descriptor info
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_device_descrip_info(usb_utr_t* ptr)
{
/* Condition compilation by the difference of useful function */
#ifdef USB_DEBUG_ON
    uint8_t* p;

    p = usb_hstd_dev_descriptor(ptr);

    /* For DEMO */
    USB_PRINTF0("Device descriptor fields\n");
    USB_PRINTF2("  bcdUSB         : %02x.%02x\n", p[0x03], p[0x02]);
    USB_PRINTF1("  bDeviceClass   : 0x%02x\n", p[0x04]);
    USB_PRINTF1("  bDeviceSubClass: 0x%02x\n", p[0x05]);
    USB_PRINTF1("  bProtocolCode  : 0x%02x\n", p[0x06]);
    USB_PRINTF1("  bMaxPacletSize : 0x%02x\n", p[0x07]);
    USB_PRINTF2("  idVendor       : 0x%02x%02x\n", p[0x09], p[0x08]);
    USB_PRINTF2("  idProduct      : 0x%02x%02x\n", p[0x0b], p[0x0a]);
    USB_PRINTF2("  bcdDevice      : 0x%02x%02x\n", p[0x0d], p[0x0c]);
    USB_PRINTF1("  iSerialNumber  : 0x%02x\n", p[0x10]);
    USB_PRINTF1("  bNumConfig     : 0x%02x\n", p[0x11]);
    USB_PRINTF0("\n");
#endif /* USB_DEBUG_ON */
} /* End of function usb_hhub_device_descrip_info() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_config_descrip_info
 Description     : configuration descriptor info
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_config_descrip_info(usb_utr_t* ptr)
{
/* Condition compilation by the difference of useful function */
#ifdef USB_DEBUG_ON
    uint8_t* p;
    uint16_t len;

    p = usb_hstd_con_descriptor(ptr);

    len = 0;
    while (len < (p[2]))
    {
        /* Search within Configuration descriptor */
        switch (p[len + 1])
        {
            /* Configuration Descriptor ? */
            case 0x02:

                USB_PRINTF0("Configuration descriptor fields\n");
                USB_PRINTF1("  Configuration Value  : 0x%02x\n", p[0x05]);
                USB_PRINTF1("  Number of Interface  : 0x%02x\n", p[0x04]);

                break;

            /* Interface Descriptor ? */
            case 0x04:

                USB_PRINTF0("\nInterface descriptor fields\n");

                /* Class check */
                switch (p[len + 5])
                {
                    case 1: /* Audio Class */

                        USB_PRINTF0("  This device has Audio Class.\n");

                        break;

                    case 2: /* CDC-Control Class */

                        USB_PRINTF0("  This device has CDC-Control Class.\n");

                        break;

                    case 3: /* HID Class */

                        USB_PRINTF0("  This device has HID Class.\n");

                        break;

                    case 5: /* Physical Class */

                        USB_PRINTF0("  This device has Physical Class.\n");

                        break;

                    case 6: /* Image Class */

                        USB_PRINTF0("  This device has Image Class.\n");

                        break;

                    case 7: /* Printer Class */

                        USB_PRINTF0("  This device has Printer Class.\n");

                        break;

                    case 8: /* Mass Storage Class */
                        USB_PRINTF0("  I/F class    : Mass Storage\n");
                        switch (p[len + 6])
                        {
                            case 0x05:

                                USB_PRINTF0("  I/F subclass : SFF-8070i\n");

                                break;

                            case 0x06:

                                USB_PRINTF0("  I/F subclass : SCSI command\n");

                                break;

                            default:

                                USB_PRINTF0("### I/F subclass not support.\n");

                                break;
                        }
                        if (0x50 == p[len + 7])
                        {
                            /* Check Interface Descriptor (protocol) */
                            USB_PRINTF0("  I/F protocol : BOT\n");
                        }
                        else
                        {
                            USB_PRINTF0("### I/F protocol not support.\n");
                        }

                        break;

                    case 9: /* HUB Class */

                        USB_PRINTF0("  This device has HUB Class.\n");

                        break;

                    case 10: /* CDC-Data Class */

                        USB_PRINTF0("  This device has CDC-Data Class.\n");

                        break;

                    case 11: /* Chip/Smart Card Class */

                        USB_PRINTF0("  This device has Chip/Smart Class.\n");

                        break;

                    case 13: /* Content-Security Class */

                        USB_PRINTF0("  This device has Content-Security Class.\n");

                        break;

                    case 14: /* Video Class */

                        USB_PRINTF0("  This device has Video Class.\n");

                        break;

                    case 255: /* Vendor Specific Class */

                        USB_PRINTF0("  I/F class : Vendor Specific\n");

                        break;

                    case 0: /* Reserved */

                        USB_PRINTF0("  I/F class : class error\n");

                        break;

                    default:

                        USB_PRINTF0("  This device has not USB Device Class.\n");

                        break;
                }
                break;

            /* Endpoint Descriptor */
            case 0x05:

                usb_hhub_endp_descrip_info(&p[len]);

                break;

            default:
                break;
        }
        len += p[len];
    }
#endif /* USB_DEBUG_ON */
} /* End of function usb_hhub_config_descrip_info() */

/* Condition compilation by the difference of useful function */
#ifdef USB_DEBUG_ON
/***********************************************************************************************************************
 Function Name   : usb_hhub_endp_descrip_info
 Description     : endpoint descriptor info
 Arguments       : uint8_t      *tbl         : table (indata).
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_endp_descrip_info(uint8_t* tbl)
{
    uint16_t epnum;
    uint16_t pipe_mxp;

    switch ((uint8_t)(tbl[3] & USB_EP_TRNSMASK))
    {
        /* Isochronous Endpoint */
        case USB_EP_ISO:

            USB_PRINTF0("  ISOCHRONOUS");

            break;

        /* Bulk Endpoint */
        case USB_EP_BULK:

            USB_PRINTF0("  BULK");

            break;

        /* Interrupt Endpoint */
        case USB_EP_INT:

            USB_PRINTF0("  INTERRUPT");

            break;

        /* Control Endpoint */
        default:

            USB_PRINTF0("### Control pipe is not support.\n");

            break;
    }

    if (USB_EP_IN == (uint8_t)(tbl[2] & USB_EP_DIRMASK))
    {
        /* Endpoint address set */
        USB_PRINTF0(" IN  endpoint\n");
    }
    else
    {
        USB_PRINTF0(" OUT endpoint\n");
    }

    /* Endpoint number set */
    epnum = (uint16_t)((uint16_t)tbl[2] & USB_EP_NUMMASK);

    /* Max packet size set */
    pipe_mxp = (uint16_t)(tbl[4] | (uint16_t)((uint16_t)(tbl[5]) << 8));
    USB_PRINTF2("   Number is %d. MaxPacket is %d. \n", epnum, pipe_mxp);
    switch ((uint16_t)(tbl[3] & USB_EP_TRNSMASK))
    {
        /* Isochronous Endpoint */
        case 0x01u:
        /* Interrupt Endpoint */
        case 0x03u:

            USB_PRINTF1("    interval time is %d\n", tbl[6]);

            break;

        default:
            break;
    }
} /* End of function usb_hstd_EndpDescripInfo() */
#endif /* USB_DEBUG_ON */

/***********************************************************************************************************************
 Function Name   : usb_hhub_new_connect
 Description     : new connect
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     portnum      : Down port number
                 : usb_clsinfo_t *mess       : Pointer to usb_clsinfo_t structure
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_new_connect(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum, usb_clsinfo_t* mess)
{
    uint16_t devaddr;

    hubaddr = g_usb_shhub_hub_addr[ptr->ip];
    portnum = g_usb_shhub_event_port[ptr->ip];

    /* New downport device search */
    devaddr = usb_hhub_get_new_devaddr(ptr);
    if (0 != devaddr)
    {
        USB_PRINTF1(" Hubport connect address%d\n", devaddr);
        g_usb_shhub_info_data[ptr->ip][devaddr].up_addr     = hubaddr; /* Up-hubaddr set */
        g_usb_shhub_info_data[ptr->ip][devaddr].up_port_num = portnum; /* Up-hubport set */
        g_usb_shhub_process[ptr->ip]                        = USB_MSG_HUB_ATTACH;
        usb_hhub_specified_path(mess); /* Next Process Selector */
    }
    else
    {
        USB_PRINTF0("### device count over !\n");
        consoleTextIfAllBootedUp(l10n_get(l10n_STRING_FOR_USB_DEVICES_MAX));
    }
} /* End of function usb_hhub_new_connect() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_port_detach
 Description     : HUB down port disconnect
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     portnum      : Down port number
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_port_detach(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum)
{
    uint16_t md;
    usb_hcdreg_t* driver;
    /* Device number --> DeviceAddress */
    uint16_t devaddr;

    /* HUB downport status */
    g_usb_shhub_down_port[ptr->ip][hubaddr] &= (uint16_t)(~USB_BITSET(portnum));

    /* HUB downport RemoteWakeup */
    g_usb_shhub_remote[ptr->ip][hubaddr] &= (uint16_t)(~USB_BITSET(portnum));

    /* Now downport device search */
    devaddr = usb_hhub_get_cnn_devaddr(ptr, hubaddr, portnum);

    /* Selective detach */
    usb_hhub_selective_detach(ptr,
        devaddr); // Moved by Rohan. Was previously below the below for loop, but this function needs some of the data
                  // which gets nullified below.

    for (md = 0; md < g_usb_hstd_device_num[ptr->ip]; md++)
    {
        driver = &g_usb_hstd_device_drv[ptr->ip][md];
        if (devaddr == driver->devaddr)
        {
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

} /* End of function usb_hhub_port_detach() */

#include "RZA1/usb/userdef/r_usb_hmidi_config.h"

/***********************************************************************************************************************
 Function Name   : usb_hhub_selective_detach
 Description     : HUB down port Selective disconnect
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_selective_detach(usb_utr_t* ptr, uint16_t devaddr)
{
    uint16_t addr;
    uint16_t i;

    addr = (uint16_t)(devaddr << USB_DEVADDRBIT);
    /* Check Connection */
    if (USB_NOCONNECT != usb_hstd_get_dev_speed(ptr, addr))
    {
        for (i = USB_MIN_PIPE_NO; i <= USB_MAX_PIPE_NO; i++)
        {
            /* Not control transfer */
            if (usb_hstd_get_device_address(ptr, i) == addr)
            {

                if (i == USB_CFG_HMIDI_BULK_SEND || i == USB_CFG_HMIDI_INT_SEND)
                    continue; // Don't deconfigure the send-pipe or end its transfer, cos it's shared - Rohan

                /* Agreement device address */
                if (USB_PID_BUF == usb_cstd_get_pid(ptr, i))
                {
                    /* PID=BUF ? */
                    usb_hstd_transfer_end(ptr, i, (uint16_t)USB_DATA_STOP);
                }
                usb_cstd_clr_pipe_cnfg(ptr, i);
            }
        }
        usb_hstd_set_dev_addr(ptr, addr, USB_OK, USB_OK);
        usb_hstd_set_hub_port(ptr, addr, USB_OK, USB_OK);
        USB_PRINTF1("*** Device address %d clear.\n", devaddr);
    }

    /* Root port */
    g_usb_hstd_device_info[ptr->ip][devaddr][0] = USB_NOPORT;

    /* Device state */
    g_usb_hstd_device_info[ptr->ip][devaddr][1] = USB_DETACHED;

    /* Not configured */
    g_usb_hstd_device_info[ptr->ip][devaddr][2] = (uint16_t)0;

    /* Interface Class : NO class */
    g_usb_hstd_device_info[ptr->ip][devaddr][3] = (uint16_t)USB_IFCLS_NOT;

    /* No connect */
    g_usb_hstd_device_info[ptr->ip][devaddr][4] = (uint16_t)USB_NOCONNECT;

} /* End of function usb_hhub_selective_detach() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_string_descriptor1
 Description     : Get String descriptor
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device address
                 : uint16_t     index        : Descriptor index
                 : usb_cb_t     complete     : Callback function
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hhub_get_string_descriptor1(usb_utr_t* ptr, uint16_t devaddr, uint16_t index, usb_cb_t complete)
{
    usb_hstd_get_string_desc(ptr, devaddr, (uint16_t)0, complete);

    return USB_OK;
} /* End of function usb_hhub_get_string_descriptor1() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_string_descriptor2
 Description     : Get String descriptor
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     devaddr      : Device address
                 : uint16_t     index        : Descriptor index
                 : usb_cb_t     complete     : Callback function
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hhub_get_string_descriptor2(usb_utr_t* ptr, uint16_t devaddr, uint16_t index, usb_cb_t complete)
{
    usb_hstd_get_string_desc(ptr, devaddr, index, complete);

    return USB_OK;
} /* End of function usb_hhub_get_string_descriptor2() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_string_descriptor1check
 Description     : Get String descriptor Check
 Arguments       : uint16_t     errcheck     : Errcheck
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hhub_get_string_descriptor1check(uint16_t errcheck)
{
    if ((usb_er_t)USB_DATA_STALL == errcheck)
    {
        USB_PRINTF0("*** LanguageID  not support !\n");
        return USB_ERROR;
    }
    else if ((usb_er_t)USB_CTRL_END != errcheck)
    {
        USB_PRINTF0("*** LanguageID  not support !\n");
        return USB_ERROR;
    }
    else
    {
        /* Non */
    }

    return USB_OK;
} /* End of function usb_hhub_get_string_descriptor1check() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_string_descriptor_to_check
 Description     : Get String descriptor Check
 Arguments       : uint16_t     errcheck     : Errcheck
 Return value    : uint16_t                  : Error info
 ***********************************************************************************************************************/
uint16_t usb_hhub_get_string_descriptor_to_check(uint16_t errcheck)
{
    if ((usb_er_t)USB_DATA_STALL == errcheck)
    {
        USB_PRINTF0("*** SerialNumber not support !\n");
        return USB_ERROR;
    }
    else if ((usb_er_t)USB_CTRL_END != errcheck)
    {
        USB_PRINTF0("*** SerialNumber not support !\n");
        return USB_ERROR;
    }
    else
    {
        /* Non */
    }

    return USB_OK;
} /* End of function usb_hhub_get_string_descriptor_to_check() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_new_devaddr
 Description     : Get the new device address
                 : when connection of a device detected in the down port of HUB
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : uint16_t                  : New device address
 ***********************************************************************************************************************/
static uint16_t usb_hhub_get_new_devaddr(usb_utr_t* ptr)
{
    uint16_t i;

    /* Search new device */
    for (i = (USB_HUBDPADDR); i < (USB_MAXDEVADDR + 1u); i++)
    {
        if (0 == g_usb_shhub_info_data[ptr->ip][i].up_addr)
        {
            /* New device address */
            return i;
        }
    }
    return 0;
} /* End of function usb_hhub_get_new_devaddr() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_hubaddr
 Description     : Get the HUB address
                 : from the pipe number for HUB notification
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipenum      : Pipe number
 Return value    : uint16_t                  : HUB address
 ***********************************************************************************************************************/
static uint16_t usb_hhub_get_hubaddr(usb_utr_t* ptr, uint16_t pipenum)
{
    uint16_t i;

    for (i = 1; i < (USB_MAXDEVADDR + 1u); i++)
    {
        if (g_usb_shhub_info_data[ptr->ip][i].pipe_num == pipenum)
        {
            /* HUB address */
            return i;
        }
    }
    return 0;
} /* End of function usb_hhub_get_hubaddr() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_get_new_devaddr
 Description     : Get the new device address
                 : when connection of a device detected in the down port of HUB
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     hubaddr      : Hub address
                 : uint16_t     portnum      : Down port number
 Return value    : uint16_t                  : New device address
 ***********************************************************************************************************************/
static uint16_t usb_hhub_get_cnn_devaddr(usb_utr_t* ptr, uint16_t hubaddr, uint16_t portnum)
{
    uint16_t i;

    for (i = USB_HUBDPADDR; i < (USB_MAXDEVADDR + 1u); i++)
    {
        if ((g_usb_shhub_info_data[ptr->ip][i].up_addr == hubaddr)
            && (g_usb_shhub_info_data[ptr->ip][i].up_port_num == portnum))
        {
            /* Device address */
            return i;
        }
    }
    return 0;
} /* End of function usb_hhub_get_cnn_devaddr() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_specified_path
 Description     : Next Process Selector
 Arguments       : usb_clsinfo_t *ptr        : Pointer to usb_clsinfo_t structure.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_specified_path(usb_clsinfo_t* ptr)
{
    usb_mh_t p_blf;
    usb_er_t err;
    usb_clsinfo_t* cp;

    /* Get mem pool blk */
    err = USB_PGET_BLK(USB_HUB_MPL, &p_blf);
    if (USB_OK == err)
    {
        cp          = (usb_clsinfo_t*)p_blf;
        cp->msginfo = g_usb_shhub_process[ptr->ip];
        cp->keyword = ptr->keyword;
        cp->result  = ptr->result;

        cp->ipp = ptr->ipp;
        cp->ip  = ptr->ip;

        /* Send message */
        err = USB_SND_MSG(USB_HUB_MBX, (usb_msg_t*)p_blf);
        if (USB_OK != err)
        {
            /* Send message failure */
            err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)p_blf);
            USB_PRINTF0("### SpecifiedPass function snd_msg error\n");
        }
    }
    else
    {
        /* Get memoryblock failure */
        USB_PRINTF0("### SpecifiedPass function pget_blk error\n");
        while (1)
        {
            /* Non */
        }
    }
} /* End of function usb_hhub_specified_path() */

/***********************************************************************************************************************
 Function Name   : usb_hhub_specified_path_wait
 Description     : Next Process Selector
 Arguments       : usb_clsinfo_t *mess       : Pointer to usb_clsinfo_t structure.
                 : uint16_t     times        : Timeout value.
 Return value    : none
 ***********************************************************************************************************************/
static void usb_hhub_specified_path_wait(usb_clsinfo_t* ptr, uint16_t times)
{
    usb_mh_t p_blf;
    usb_er_t err;
    usb_clsinfo_t* hp;

    /* Get mem pool blk */
    err = USB_PGET_BLK(USB_HUB_MPL, &p_blf);
    if (USB_OK == err)
    {
        hp          = (usb_clsinfo_t*)p_blf;
        hp->msginfo = g_usb_shhub_process[ptr->ip];
        hp->keyword = ptr->keyword;
        hp->result  = ptr->result;

        hp->ipp = ptr->ipp;
        hp->ip  = ptr->ip;

        /* Wait message */
        err = USB_WAI_MSG(USB_HUB_MBX, (usb_msg_t*)p_blf, times);
        if (USB_OK != err)
        {
            /* Wait message failure */
            err = USB_REL_BLK(USB_HUB_MPL, (usb_mh_t)p_blf);
            USB_PRINTF0("### SpecifiedPassWait function snd_msg error\n");
        }
    }
    else
    {
        USB_PRINTF0("### SpecifiedPassWait function pget_blk error\n");
        while (1)
        {
            /* Non */
        }
    }
} /* End of function usb_hhub_specified_path_wait() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
