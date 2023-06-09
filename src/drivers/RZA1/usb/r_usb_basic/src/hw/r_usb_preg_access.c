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
 * File Name    : r_usb_preg_access.c
 * Description  : USB IP Peripheral control register access code
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
#include "r_usb_reg_access.h"
#include "r_usb_bitdefine.h"
#include "r_usb_extern.h"
#include "iodefine.h"

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 External variables and functions
 ***********************************************************************************************************************/
extern uint8_t g_usb_std_uclksel;

/***********************************************************************************************************************
 Function Name   : hw_usb_pset_dprpu
 Description     : Set DPRPU-bit SYSCFG0 register.
                 : (Enable D+Line pullup when PeripheralController function is selected)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pset_dprpu(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.SYSCFG0 |= USB_DPRPU;
#else
    USB201.SYSCFG0 |= USB_DPRPU;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pset_dprpu() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pclear_dprpu
 Description     : Clear DPRPU-bit of the SYSCFG0 register.
                 : (Disable D+Line pullup when PeripheralController function is
                 : selected.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pclear_dprpu(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.SYSCFG0 &= (~USB_DPRPU);
#else
    USB201.SYSCFG0 &= (~USB_DPRPU);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pclear_dprpu() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pset_wkup
 Description     : Set WKUP-bit DVSTCTR register.
                 : (Output Remote wakeup signal when PeripheralController function is selected)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pset_wkup(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.DVSTCTR0 |= USB_WKUP;
#else
    USB201.DVSTCTR0 |= USB_WKUP;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pset_wkup() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pset_enb_rsme
 Description     : Enable interrupt from RESUME
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pset_enb_rsme(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.INTENB0 |= USB_RSME;
#else
    USB201.INTENB0 |= USB_RSME;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pset_enb_rsme() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pclear_enb_rsme
 Description     : Disable interrupt from RESUME
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pclear_enb_rsme(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.INTENB0 &= (~USB_RSME);
#else
    USB201.INTENB0 &= (~USB_RSME);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pclear_enb_rsme() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pclear_sts_resm
 Description     : Clear interrupt status of RESUME.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pclear_sts_resm(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.INTSTS0 = (uint16_t)~USB_RESM;
#else
    USB201.INTSTS0 = (uint16_t)~USB_RESM;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pclear_sts_resm() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pclear_sts_valid
 Description     : Clear the Setup Packet Reception interrupt flag.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pclear_sts_valid(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.INTSTS0 = (uint16_t)~USB_VALID;
#else
    USB201.INTSTS0 = (uint16_t)~USB_VALID;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pclear_sts_valid() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pset_ccpl
 Description     : Enable termination of control transfer status stage.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pset_ccpl(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.DCPCTR |= USB_CCPL;
#else
    USB201.DCPCTR |= USB_CCPL;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pset_ccpl() */

/***********************************************************************************************************************
 Function Name   : hw_usb_pmodule_init
 Description     : USB module initialization for USB Peripheral mode
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_pmodule_init(void)
{
    if (USB_FALSE == g_usb_std_uclksel)
    {
        USB200.SUSPMODE &= ~(USB_SUSPM);
        USB201.SUSPMODE &= ~(USB_SUSPM);
#if USB_CFG_CLKSEL == USB_CFG_12MHZ
        USB200.SYSCFG0 |= USB_UCKSEL;
#elif USB_CFG_CLKSEL == USB_CFG_48MHZ
        USB200.SYSCFG0 &= ~(USB_UCKSEL);
#endif /* USB_CFG_CLKSEL == USB_CFG_12MHZ */
        USB200.SYSCFG0 |= USB_UPLLE;
        usb_cpu_delay_xms(1); /* 1ms wait */

        g_usb_std_uclksel = USB_TRUE;
    }

#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.SUSPMODE |= USB_SUSPM;
    USB200.BUSWAIT = USB_CFG_BUSWAIT;
    USB200.SYSCFG0 &= ~USB_DCFM;
    USB200.SYSCFG0 &= ~USB_DPRPU;
    USB200.SYSCFG0 &= ~USB_DRPD;
    USB200.SYSCFG0 |= USB_USBE;

    usb_cpu_delay_xms(1); /* 1ms wait */

    USB200.CFIFOSEL  = USB0_CFIFO_MBW;
    USB200.D0FIFOSEL = USB0_CFIFO_MBW;
    USB200.D1FIFOSEL = USB0_CFIFO_MBW;

    USB200.INTENB0 = (USB_BEMPE | USB_NRDYE | USB_BRDYE | USB_VBSE | USB_DVSE | USB_CTRE);
#else
    USB201.SUSPMODE |= USB_SUSPM;
    USB201.BUSWAIT = USB_CFG_BUSWAIT;
    USB201.SYSCFG0 &= ~USB_DCFM;
    USB201.SYSCFG0 &= ~USB_DPRPU;
    USB201.SYSCFG0 &= ~USB_DRPD;
    USB201.SYSCFG0 |= USB_USBE;

    usb_cpu_delay_xms(1); /* 1ms wait */

    USB201.CFIFOSEL  = USB1_CFIFO_MBW;
    USB201.D0FIFOSEL = USB1_CFIFO_MBW;
    USB201.D1FIFOSEL = USB1_CFIFO_MBW;

    USB201.INTENB0 = (USB_BEMPE | USB_NRDYE | USB_BRDYE | USB_VBSE | USB_DVSE | USB_CTRE);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_pmodule_init() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
