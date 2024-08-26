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
 * File Name    : r_usb_reg_access.h
 * Description  : USB IP Register control code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/
#ifndef HW_USB_REG_ACCESS_H
#define HW_USB_REG_ACCESS_H

#include "RZA1/system/iodefine.h"
#include "RZA1/system/iodefines/usb20_iodefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"

// "I'm gonna inline a bunch of these functions." - Rohan
void* hw_usb_get_fifosel_adr(usb_utr_t* ptr,
    uint16_t pipemode); // Formerly static function, now accessed by inline functions in this header file.

/***********************************************************************************************************************
 Macro definitions
 ***********************************************************************************************************************/
#define USB_BUFSIZE_BIT  (10u)
#define USB_SUSPEND_MODE (1u)
#define USB_NORMAL_MODE  (0)

/****************/
/*  INITIARIZE  */
/****************/
void hw_usb_hmodule_init(usb_ctrl_t* p_ctrl);
void hw_usb_pmodule_init(void);

/************/
/*  SYSCFG  */
/************/
uint16_t hw_usb_read_syscfg(usb_utr_t* ptr, uint16_t port);
void hw_usb_write_syscfg(usb_utr_t* ptr, uint16_t port, uint16_t data);
void hw_usb_clear_cnen(usb_utr_t* ptr);
void hw_usb_set_hse(usb_utr_t* ptr, uint16_t port);
void hw_usb_clear_hse(usb_utr_t* ptr, uint16_t port);
void hw_usb_set_dcfm(void);
void hw_usb_clear_dcfm(usb_utr_t* ptr);
void hw_usb_clear_drpd(usb_utr_t* ptr, uint16_t port);
void hw_usb_set_usbe(usb_utr_t* ptr);
void hw_usb_clear_usbe(usb_utr_t* ptr);
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
void hw_usb_set_cnen(void);
void hw_usb_pset_dprpu(void);
void hw_usb_pclear_dprpu(void);
#endif /*(USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/************/
/*  BUSWAIT */
/************/
void hw_usb_set_buswait(usb_utr_t* ptr);

/************/
/*  SYSSTS0 */
/************/
uint16_t hw_usb_read_syssts(usb_utr_t* ptr, uint16_t port);

/**************/
/*  DVSTCTR0  */
/**************/
uint16_t hw_usb_read_dvstctr(usb_utr_t* ptr, uint16_t port);
void hw_usb_write_dvstctr(usb_utr_t* ptr, uint16_t port, uint16_t data);
void hw_usb_rmw_dvstctr(usb_utr_t* ptr, uint16_t port, uint16_t data, uint16_t width);
void hw_usb_clear_dvstctr(usb_utr_t* ptr, uint16_t port, uint16_t data);
void hw_usb_set_vbout(usb_utr_t* ptr, uint16_t port);
void hw_usb_clear_vbout(usb_utr_t* ptr, uint16_t port);
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void hw_usb_hset_rwupe(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_rwupe(usb_utr_t* ptr, uint16_t port);
void hw_usb_hset_resume(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_resume(usb_utr_t* ptr, uint16_t port);
void hw_usb_hset_uact(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_uact(usb_utr_t* ptr, uint16_t port);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
void hw_usb_pset_wkup(void);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/**************/
/*  TESTMODE  */
/**************/
void hw_usb_set_utst(usb_utr_t* ptr, uint16_t data);

/***************************/
/*  CFIFO, D0FIFO, D1FIFO  */
/***************************/
uint32_t hw_usb_read_fifo32(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_write_fifo32(usb_utr_t* ptr, uint16_t pipemode, uint32_t data);
uint16_t hw_usb_read_fifo16(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_write_fifo16(usb_utr_t* ptr, uint16_t pipemode, uint16_t data);
void hw_usb_write_fifo8(usb_utr_t* ptr, uint16_t pipemode, uint8_t data);

/************************************/
/*  CFIFOSEL, D0FIFOSEL, D1FIFOSEL  */
/************************************/
// uint16_t    hw_usb_read_fifosel(usb_utr_t *ptr, uint16_t pipemode);
// void        hw_usb_rmw_fifosel(usb_utr_t *ptr, uint16_t pipemode, uint16_t data, uint16_t width);
void hw_usb_set_dclrm(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_clear_dclrm(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_set_dreqe(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_clear_dreqe(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_set_mbw(usb_utr_t* ptr, uint16_t pipemode, uint16_t data);
void hw_usb_set_curpipe(usb_utr_t* ptr, uint16_t pipemode, uint16_t pipeno);

extern uint16_t fifoSels[];

/***********************************************************************************************************************
 Function Name   : hw_usb_rmw_fifosel
 Description     : Data is written to the specified pipemode's FIFOSEL register
                 : (the FIFOSEL corresponding to the specified PIPEMODE), using
                 : read-modify-write.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
                 : uint16_t     data         : Setting value.
                 : uint16_t     bitptn       : bitptn: Bit pattern to read-modify-write.
 Return value    : none
 ***********************************************************************************************************************/
static inline void hw_usb_rmw_fifosel(usb_utr_t* ptr, uint16_t pipemode, uint16_t data, uint16_t bitptn)
{
    uint16_t buf;
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);

    buf = *p_reg;
    buf &= (~bitptn);
    buf |= (data & bitptn);
    *p_reg             = buf;
    fifoSels[pipemode] = buf; // This function only changed some of the bits, according to bitptn
} /* End of function hw_usb_rmw_fifosel() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_fifosel
 Description     : Returns the value of the specified pipemode's FIFOSEL register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : FIFOSEL content
 ***********************************************************************************************************************/
static inline uint16_t hw_usb_read_fifosel(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);

    return (*p_reg);
} /* End of function hw_usb_read_fifosel() */

/**********************************/
/* CFIFOCTR, D0FIFOCTR, D1FIFOCTR */
/**********************************/
uint16_t hw_usb_read_fifoctr(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_set_bval(usb_utr_t* ptr, uint16_t pipemode);
void hw_usb_set_bclr(usb_utr_t* ptr, uint16_t pipemode);

/*************/
/*  INTENB0  */
/*************/
uint16_t hw_usb_ReadIntenb(usb_utr_t* ptr);
void hw_usb_write_intenb(usb_utr_t* ptr, uint16_t data);
void hw_usb_set_intenb(usb_utr_t* ptr, uint16_t data);
void hw_usb_clear_enb_vbse(usb_utr_t* ptr);
void hw_usb_clear_enb_sofe(usb_utr_t* ptr);
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
void hw_usb_pset_enb_rsme(void);
void hw_usb_pclear_enb_rsme(void);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/*************/
/*  BRDYENB  */
/*************/
void hw_usb_write_brdyenb(usb_utr_t* ptr, uint16_t data);
// void        hw_usb_set_brdyenb(usb_utr_t *ptr, uint16_t pipeno);
// void        hw_usb_clear_brdyenb(usb_utr_t *ptr, uint16_t pipeno);

/***********************************************************************************************************************
 Function Name   : hw_usb_set_brdyenb
 Description     : A bit is set in the specified pipe's BRDYENB, enabling the
                 : respective pipe BRDY interrupt(s).
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_set_brdyenb(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.BRDYENB |= (1 << pipeno);
#else
        USB201.BRDYENB |= (1 << pipeno);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->BRDYENB |= (1 << pipeno);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_brdyenb() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_brdyenb
 Description     : Clear the PIPExBRDYE-bit of the specified pipe to prohibit
                 : BRDY interrupts of that pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_clear_brdyenb(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.BRDYENB &= (~(1 << pipeno));
#else
        USB201.BRDYENB &= (~(1 << pipeno));
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->BRDYENB &= (~(1 << pipeno));
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_brdyenb() */

/*************/
/*  NRDYENB  */
/*************/
void hw_usb_write_nrdyenb(usb_utr_t* ptr, uint16_t data);
// void        hw_usb_set_nrdyenb(usb_utr_t *ptr, uint16_t pipeno);
// void        hw_usb_clear_nrdyenb(usb_utr_t *ptr, uint16_t pipeno);

/***********************************************************************************************************************
 Function Name   : hw_usb_set_nrdyenb
 Description     : A bit is set in the specified pipe's NRDYENB, enabling the
                 : respective pipe NRDY interrupt(s).
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_set_nrdyenb(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.NRDYENB |= (1 << pipeno);
#else
        USB201.NRDYENB |= (1 << pipeno);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->NRDYENB |= (1 << pipeno);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_nrdyenb() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_nrdyenb
 Description     : Clear the PIPExNRDYE-bit of the specified pipe to prohibit
                 : NRDY interrupts of that pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_clear_nrdyenb(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.NRDYENB &= (~(1 << pipeno));
#else
        USB201.NRDYENB &= (~(1 << pipeno));
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->NRDYENB &= (~(1 << pipeno));
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_nrdyenb() */

/*************/
/*  BEMPENB  */
/*************/
void hw_usb_write_bempenb(usb_utr_t* ptr, uint16_t data);
// void        hw_usb_set_bempenb(usb_utr_t *ptr, uint16_t pipeno);
// void        hw_usb_clear_bempenb(usb_utr_t *ptr, uint16_t pipeno);

/***********************************************************************************************************************
 Function Name   : hw_usb_set_bempenb
 Description     : A bit is set in the specified pipe's BEMPENB enabling the
                 : respective pipe's BEMP interrupt(s).
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_set_bempenb(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.BEMPENB |= (1 << pipeno);
#else
        USB201.BEMPENB |= (1 << pipeno);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->BEMPENB |= (1 << pipeno);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_bempenb() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_bempenb
 Description     : Clear the PIPExBEMPE-bit of the specified pipe to prohibit
                 : BEMP interrupts of that pipe.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_clear_bempenb(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.BEMPENB &= (~(1 << pipeno));
#else
        USB201.BEMPENB &= (~(1 << pipeno));
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->BEMPENB &= (~(1 << pipeno));
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_bempenb() */

/*************/
/*  SOFCFG   */
/*************/
void hw_usb_set_sofcfg(usb_utr_t* ptr, uint16_t data);
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void hw_usb_hset_trnensel(usb_utr_t* ptr);
#endif /*(USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/*************/
/*  INTSTS0  */
/*************/
void hw_usb_write_intsts(usb_utr_t* ptr, uint16_t data);
void hw_usb_clear_sts_sofr(usb_utr_t* ptr);
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
uint16_t hw_usb_read_intsts(void);
void hw_usb_pclear_sts_resm(void);
void hw_usb_pclear_sts_valid(void);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/************/
/* BRDYSTS  */
/************/
uint16_t hw_usb_read_brdysts(usb_utr_t* ptr);
void hw_usb_write_brdysts(usb_utr_t* pt, uint16_t data);
// void        hw_usb_clear_sts_brdy(usb_utr_t *ptr, uint16_t pipeno);

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_sts_brdy
 Description     : Clear the PIPExBRDY status bit of the specified pipe to clear
                 : its BRDY interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_clear_sts_brdy(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.BRDYSTS = (uint16_t)~(1 << pipeno);
#else
        USB201.BRDYSTS = (uint16_t)~(1 << pipeno);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->BRDYSTS = (uint16_t)~(1 << pipeno);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_sts_brdy() */

/************/
/* NRDYSTS  */
/************/
void hw_usb_write_nrdy_sts(usb_utr_t* ptr, uint16_t data);
void hw_usb_clear_status_nrdy(usb_utr_t* ptr, uint16_t pipeno);

/************/
/* BEMPSTS  */
/************/
void hw_usb_write_bempsts(usb_utr_t* ptr, uint16_t data);
// void        hw_usb_clear_status_bemp(usb_utr_t *ptr, uint16_t pipeno);

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_status_bemp
 Description     : Clear the PIPExBEMP status bit of the specified pipe to clear
                 : its BEMP interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_clear_status_bemp(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.BEMPSTS = (uint16_t)~(1 << pipeno);
#else
        USB201.BEMPSTS = (uint16_t)~(1 << pipeno);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->BEMPSTS = (uint16_t)~(1 << pipeno);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_status_bemp() */

/************/
/* FRMNUM   */
/************/
uint16_t hw_usb_read_frmnum(usb_utr_t* ptr);

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/************/
/* USBREQ   */
/************/
void hw_usb_hwrite_usbreq(usb_utr_t* ptr, uint16_t data);

/************/
/* USBVAL   */
/************/
void hw_usb_hset_usbval(usb_utr_t* ptr, uint16_t data);

/************/
/* USBINDX  */
/************/
void hw_usb_hset_usbindx(usb_utr_t* ptr, uint16_t data);

/************/
/* USBLENG  */
/************/
void hw_usb_hset_usbleng(usb_utr_t* ptr, uint16_t data);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/************/
/* USBREQ   */
/************/
uint16_t hw_usb_read_usbreq(void);

/************/
/* USBVAL   */
/************/
uint16_t hw_usb_read_usbval(void);

/************/
/* USBINDX  */
/************/
uint16_t hw_usb_read_usbindx(void);

/************/
/* USBLENG  */
/************/
uint16_t hw_usb_read_usbleng(void);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/************/
/* DCPCFG   */
/************/
uint16_t hw_usb_read_dcpcfg(usb_utr_t* ptr);
void hw_usb_write_dcpcfg(usb_utr_t* ptr, uint16_t data);

/************/
/* DCPMAXP  */
/************/
uint16_t hw_usb_read_dcpmaxp(usb_utr_t* ptr);
void hw_usb_write_dcpmxps(usb_utr_t* ptr, uint16_t data);

/************/
/* DCPCTR   */
/************/
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void hw_usb_hwrite_dcpctr(usb_utr_t* ptr, uint16_t data);
void hw_usb_hset_sureq(usb_utr_t* ptr);

#endif /*(USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
uint16_t hw_usb_read_dcpctr(void);
void hw_usb_pset_ccpl(void);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/************/
/* PIPESEL  */
/************/
// void        hw_usb_write_pipesel(usb_utr_t *ptr, uint16_t data);

/***********************************************************************************************************************
 Function Name   : hw_usb_write_pipesel
 Description     : Specified data is written to PIPESEL register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_write_pipesel(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.PIPESEL = data;
#else
        USB201.PIPESEL = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->PIPESEL = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_pipesel() */

/************/
/* PIPECFG  */
/************/
uint16_t hw_usb_read_pipecfg(usb_utr_t* ptr);
void hw_usb_write_pipecfg(usb_utr_t* ptr, uint16_t data, uint16_t pipe);

/************/
/* PIPEBUF  */
/************/
void hw_usb_write_pipebuf(usb_utr_t* ptr, uint16_t data, uint16_t pipe);
uint16_t hw_usb_read_pipebuf(usb_utr_t* ptr);

/************/
/* PIPEMAXP */
/************/
uint16_t hw_usb_read_pipemaxp(usb_utr_t* ptr);
void hw_usb_write_pipemaxp(usb_utr_t* ptr, uint16_t data, uint16_t pipe);

/************/
/* PIPEPERI */
/************/
void hw_usb_write_pipeperi(usb_utr_t* ptr, uint16_t data);

/********************/
/* DCPCTR, PIPEnCTR */
/********************/
uint16_t hw_usb_read_pipectr(usb_utr_t* ptr, uint16_t pipeno);
void hw_usb_write_pipectr(usb_utr_t* ptr, uint16_t pipeno, uint16_t data);
void hw_usb_set_csclr(usb_utr_t* ptr, uint16_t pipeno);
void hw_usb_set_aclrm(usb_utr_t* ptr, uint16_t pipeno);
void hw_usb_clear_aclrm(usb_utr_t* ptr, uint16_t pipeno);
void hw_usb_set_sqclr(usb_utr_t* ptr, uint16_t pipeno);
void hw_usb_set_sqset(usb_utr_t* ptr, uint16_t pipeno);
void hw_usb_set_pid(usb_utr_t* ptr, uint16_t pipeno, uint16_t data);
void hw_usb_clear_pid(usb_utr_t* ptr, uint16_t pipeno, uint16_t data);

inline static void hw_usb_set_pid_nonzero_pipe_rohan(uint16_t pipeno, uint16_t data)
{
    volatile uint16_t* p_reg;

#if USB_CFG_USE_USBIP == USB_CFG_IP0
    p_reg = ((uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1));
#else
    p_reg = ((uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1));
#endif
    uint16_t value = *p_reg;
    value &= (~USB_PID);
    value |= data;
    *p_reg = value;
}
/************/
/* PIPEnTRE */
/************/
// void        hw_usb_set_trenb(usb_utr_t *ptr, uint16_t pipeno);
// void        hw_usb_clear_trenb(usb_utr_t *ptr, uint16_t pipeno);
// void        hw_usb_set_trclr(usb_utr_t *ptr, uint16_t pipeno);

/***********************************************************************************************************************
 Function Name   : hw_usb_set_trenb
 Description     : The TRENB-bit (Transaction Counter Enable) is set in the speci-
                 : fied pipe's control register, to enable the counter.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_set_trenb(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (uint16_t*)&(USB200.PIPE1TRE) + ((pipeno - 1) * 2);
#else
        p_reg = (uint16_t*)&(USB201.PIPE1TRE) + ((pipeno - 1) * 2);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1TRE) + ((pipeno - 1) * 2);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    (*p_reg) |= USB_TRENB;
} /* End of function hw_usb_set_trenb() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_trenb
 Description     : The TRENB-bit (Transaction Counter Enable) is cleared in the
                 : specified pipe's control register, to disable the counter.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_clear_trenb(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (uint16_t*)&(USB200.PIPE1TRE) + ((pipeno - 1) * 2);
#else
        p_reg = (uint16_t*)&(USB201.PIPE1TRE) + ((pipeno - 1) * 2);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1TRE) + ((pipeno - 1) * 2);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    (*p_reg) &= (~USB_TRENB);
} /* End of function hw_usb_clear_trenb() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_trclr
 Description     : The TRENB-bit (Transaction Counter Clear) is set in the speci-
                 : fied pipe's control register to clear the current counter
                 : value.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_set_trclr(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (uint16_t*)&(USB200.PIPE1TRE) + ((pipeno - 1) * 2);
#else
        p_reg = (uint16_t*)&(USB201.PIPE1TRE) + ((pipeno - 1) * 2);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1TRE) + ((pipeno - 1) * 2);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    (*p_reg) |= USB_TRCLR;
} /* End of function hw_usb_set_trclr() */

/************/
/* PIPEnTRN */
/************/
// void        hw_usb_write_pipetrn(usb_utr_t *ptr, uint16_t pipeno, uint16_t data);
/***********************************************************************************************************************
 Function Name   : hw_usb_write_pipetrn
 Description     : Specified data is written to the specified pipe's PIPETRN register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
                 : uint16_t     data         : The value to write.
 Return value    : none
 ***********************************************************************************************************************/
inline static void hw_usb_write_pipetrn(usb_utr_t* ptr, uint16_t pipeno, uint16_t data)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (uint16_t*)&(USB200.PIPE1TRN) + ((pipeno - 1) * 2);
#else
        p_reg = (uint16_t*)&(USB201.PIPE1TRN) + ((pipeno - 1) * 2);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1TRN) + ((pipeno - 1) * 2);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    *p_reg = data;
} /* End of function hw_usb_write_pipetrn */

/************/
/* BCCTRL   */
/************/
void hw_usb_set_bcctrl(usb_utr_t* ptr, uint16_t data);
void hw_usb_clear_bcctrl(usb_utr_t* ptr, uint16_t data);
uint16_t hw_usb_read_bcctrl(usb_utr_t* ptr);
void hw_usb_set_vdmsrce(usb_utr_t* ptr);
void hw_usb_clear_vdmsrce(usb_utr_t* ptr);
void hw_usb_set_idpsinke(usb_utr_t* ptr);
void hw_usb_set_suspendm(void);
void hw_usb_clear_suspm(void);
void hw_usb_clear_idpsinke(usb_utr_t* ptr);
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void hw_usb_hset_dcpmode(usb_utr_t* ptr);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/**********************************/
/*  DMA0CFG, DMA1CFG  for 597ASSP */
/**********************************/
void hw_usb_write_dmacfg(usb_utr_t* ptr, uint16_t pipemode, uint16_t data);

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/*************/
/*  INTENB1  */
/*************/
void hw_usb_hwrite_intenb(usb_utr_t* ptr, uint16_t port, uint16_t data);
void hw_usb_hset_enb_ovrcre(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_enb_ovrcre(usb_utr_t* ptr, uint16_t port);
void hw_usb_hset_enb_bchge(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_enb_bchge(usb_utr_t* ptr, uint16_t port);
void hw_usb_hset_enb_dtche(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_enb_dtche(usb_utr_t* ptr, uint16_t port);
void hw_usb_hset_enb_attche(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_enb_attche(usb_utr_t* ptr, uint16_t port);
void hw_usb_hset_enb_signe(usb_utr_t* ptr);
void hw_usb_hset_enb_sacke(usb_utr_t* ptr);
void hw_usb_hset_enb_pddetinte(usb_utr_t* ptr);

/*************/
/*  INTSTS1  */
/*************/
void hw_usb_hwrite_intsts(usb_utr_t* ptr, uint16_t port, uint16_t data);
void hw_usb_hclear_sts_ovrcr(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_sts_bchg(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_sts_dtch(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_sts_attch(usb_utr_t* ptr, uint16_t port);
void hw_usb_hclear_sts_sign(usb_utr_t* ptr);
void hw_usb_hclear_sts_sack(usb_utr_t* ptr);
void hw_usb_hclear_sts_pddetint(usb_utr_t* ptr);

/************/
/* DEVADDn  */
/************/
uint16_t hw_usb_hread_devadd(usb_utr_t* ptr, uint16_t devadr);
void hw_usb_hrmw_devadd(usb_utr_t* ptr, uint16_t devsel, uint16_t data, uint16_t width);
void hw_usb_hset_usbspd(usb_utr_t* ptr, uint16_t devadr, uint8_t data);

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

void usb_std_clr_uclksel_flg(void);

#endif /* HW_USB_REG_ACCESS_H */
/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
