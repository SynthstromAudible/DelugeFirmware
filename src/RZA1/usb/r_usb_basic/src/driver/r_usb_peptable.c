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
 * File Name    : r_usb_peri_eptable.c
 * Description  : USB Peripheral Driver Endpoint table.
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

#if defined(USB_CFG_PHID_USE)
#include "r_usb_phid_if.h"
#endif /* defined(USB_CFG_PHID_USE) */

#if defined(USB_CFG_PCDC_USE)
#include "r_usb_pcdc_if.h"
#endif /* defined(USB_CFG_PCDC_USE) */

#if defined(USB_CFG_PMSC_USE)
#include "r_usb_pmsc_if.h"
#endif /* defined(USB_CFG_PMSC_USE) */

#if defined(USB_CFG_PMIDI_USE)
#include "r_usb_pmidi_config.h"
#endif

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/
uint16_t g_usb_pstd_eptbl[] = {
/************/
/* for PCDC */
/************/
#if defined(USB_CFG_PCDC_USE)
    USB_CFG_PCDC_INT_IN, /* Pipe No. */
    /* TYPE     / DIR      / EPNUM */
    USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
    USB_NULL,                       /* PIPEBUF(for USBA USE) */
    USB_NULL,                       /* PIPEMAXP */
    USB_NULL,                       /* PIPEPERI */
    USB_NULL,                       /* eserve */

    USB_CFG_PCDC_BULK_IN, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(2048u) | USB_BUF_NUMB(8u),                                             /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */

    USB_CFG_PCDC_BULK_OUT, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(2048u) | USB_BUF_NUMB(72u),                                            /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */
#endif /* defined(USB_CFG_PCDC_USE) */

/************/
/* for PMSC */
/************/
#if defined(USB_CFG_PMSC_USE)
    USB_CFG_PMSC_BULK_IN, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(8u),                                              /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */

    USB_CFG_PMSC_BULK_OUT, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(72u),                                             /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */
#endif /* defined(USB_CFG_PCDC_USE) */

/************/
/* for PMIDI */
/************/
#if defined(USB_CFG_PMIDI_USE)
    USB_CFG_PMIDI_BULK_IN, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(8u),                                              /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */

    USB_CFG_PMIDI_BULK_OUT, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(72u),                                             /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */
#endif /* defined(USB_CFG_PCDC_USE) */

/************/
/* for PHID */
/************/
#if defined(USB_CFG_PHID_USE)
    USB_CFG_PHID_INT_IN, /* Pipe No. */
    /* TYPE   / DIR     / EPNUM */
    USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
    USB_NULL,                       /* PIPEBUF */
    USB_NULL,                       /* PIPEMAXP */
    USB_NULL,                       /* PIPEPERI */
    USB_NULL,                       /* reserve */
#if USB_CFG_PHID_INT_OUT != USB_NULL
    USB_CFG_PHID_INT_OUT, /* Pipe No. */
    /* TYPE   / DIR     / EPNUM */
    USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
    USB_NULL,                       /* PIPEBUF */
    USB_NULL,                       /* PIPEMAXP */
    USB_NULL,                       /* PIPEPERI */
    USB_NULL,                       /* reserve */
#endif                              /* USB_CFG_PHID_INT_OUT != USB_NULL */
#endif                              /* defined(USB_CFG_PHID_USE) */

/************/
/* for PVND */
/************/
#if defined(USB_CFG_PVND_USE)
    /* PIPE1 Definition */
    USB_PIPE1, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(8u),                                              /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */

    /* PIPE2 Definition */
    USB_PIPE2, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(24u), /* PIPEBUF */
    USB_NULL,                                         /* PIPEMAXP */
    USB_NULL,                                         /* PIPEPERI */
    USB_NULL,                                         /* reserve */

    /* PIPE3 Definition */
    USB_PIPE3, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(40u),                                             /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */

    /* PIPE4 Definition */
    USB_PIPE4, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(56u),                                             /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */

    /* PIPE5 Definition */
    USB_PIPE5, /* Pipe No. */
    /* TYPE    / BFRE        / DBLB         / CNTMD         / SHTNAK         / DIR      / EPNUM */
    USB_NULL | USB_BFREOFF | USB_CFG_DBLB | USB_CFG_CNTMD | USB_CFG_SHTNAK | USB_NULL | USB_NULL, /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(512u) | USB_BUF_NUMB(72u),                                             /* PIPEBUF */
    USB_NULL,                                                                                     /* PIPEMAXP */
    USB_NULL,                                                                                     /* PIPEPERI */
    USB_NULL,                                                                                     /* reserve */

    /* PIPE6 Definition */
    USB_PIPE6, /* Pipe No. */
    /* TYPE     / DIR      / EPNUM */
    USB_NULL | USB_NULL | USB_NULL,                 /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(4u), /* PIPECFG */
    USB_NULL,                                       /* PIPEMAXP */
    USB_NULL,                                       /* PIPEPERI */
    USB_NULL,                                       /* reserve */

    /* PIPE7 Definition */
    USB_PIPE7, /* Pipe No. */
    /* TYPE     / DIR      / EPNUM */
    USB_NULL | USB_NULL | USB_NULL,                 /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(5u), /* PIPECFG */
    USB_NULL,                                       /* PIPEMAXP */
    USB_NULL,                                       /* PIPEPERI */
    USB_NULL,                                       /* reserve */

    /* PIPE8 Definition */
    USB_PIPE8, /* Pipe No. */
    /* TYPE     / DIR      / EPNUM */
    USB_NULL | USB_NULL | USB_NULL,                 /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(6u), /* PIPECFG */
    USB_NULL,                                       /* PIPEMAXP */
    USB_NULL,                                       /* PIPEPERI */
    USB_NULL,                                       /* reserve */

    /* PIPE9 Definition */
    USB_PIPE9, /* Pipe No. */
    /* TYPE     / DIR      / EPNUM */
    USB_NULL | USB_NULL | USB_NULL,                 /* PIPECFG */
    (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(7u), /* PIPECFG */
    USB_NULL,                                       /* PIPEMAXP */
    USB_NULL,                                       /* PIPEPERI */
    USB_NULL,                                       /* reserve */
#endif                                              /* defined(USB_CFG_PVND_USE) */

    /* Pipe end */
    USB_PDTBLEND,
};
#endif /* ( (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI ) */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
