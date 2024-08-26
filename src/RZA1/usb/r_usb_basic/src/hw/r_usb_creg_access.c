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
 * File Name    : r_usb_reg_access.c
 * Description  : USB IP register access code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_reg_access.h"
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_dmac.h"
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */
#include "RZA1/system/iodefine.h"

/***********************************************************************************************************************
Private global variables and functions
***********************************************************************************************************************/
uint8_t g_usb_std_uclksel = USB_FALSE;

/***********************************************************************************************************************
 Function Name   : hw_usb_read_syscfg
 Description     : Returns the specified port's SYSCFG register value.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number (not used $REA)
 Return value    : SYSCFG content.
 ***********************************************************************************************************************/
uint16_t hw_usb_read_syscfg(usb_utr_t* ptr, uint16_t port)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return USB200.SYSCFG0;
#else
        return USB201.SYSCFG0;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return ptr->ipp->SYSCFG0;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_syscfg() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_write_syscfg
 Description     : Write specified value to the SYSCFG register of the given port.
 Arguments       : usb_utr_t *ptr  : USB internal structure. Selects USB channel.
                 : uint16_t     port         : Port number (only port 0 used $REA)
                 : uint16_t     data         : Value to write.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_syscfg(usb_utr_t* ptr, uint16_t port, uint16_t data)
{
    if (USB_PORT0 == port)
    {
        ptr->ipp->SYSCFG0 = data;
    }
} /* End of function hw_usb_write_syscfg */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Function Name   : hw_usb_set_cnen
 Description     : Enable single end receiver.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_cnen(void)
{
} /* End of function hw_usb_set_cnen() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_cnen
 Description     : Disable single end receiver.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_cnen(usb_utr_t* ptr)
{
} /* End of function hw_usb_clear_cnen() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_hse
 Description     : Not processed as the functionality is provided by R8A66597(ASSP).
 Arguments       : usb_utr_t    *ptr         : Not used.
                 : uint16_t     port         : Not used.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_hse(usb_utr_t* ptr, uint16_t port)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.SYSCFG0 |= USB_HSE;
#else
        USB201.SYSCFG0 |= USB_HSE;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->SYSCFG0 |= USB_HSE;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_hse() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_hse
 Description     : Clears HSE bit of the specified port's SYSCFG register
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_hse(usb_utr_t* ptr, uint16_t port)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.SYSCFG0 &= (~USB_HSE);
#else
        USB201.SYSCFG0 &= (~USB_HSE);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->SYSCFG0 &= (~USB_HSE);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_hse() */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Function Name   : hw_usb_set_dcfm
 Description     : DCFM-bit set of register SYSCFG
                 : (USB Host mode is selected.)
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_dcfm(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.SYSCFG0 |= USB_DCFM;
#else
    USB201.SYSCFG0 |= USB_DCFM;
#endif
} /* End of function hw_usb_set_dcfm() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_dcfm
 Description     : DCFM-bit clear of register SYSCFG.
                 : (USB Peripheral mode is selected.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_dcfm(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.SYSCFG0 &= (~USB_DCFM);
#else
        USB201.SYSCFG0 &= (~USB_DCFM);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->SYSCFG0 &= (~USB_DCFM);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_dcfm() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_clear_drpd
 Description     : Clear bit of the specified port's SYSCFG DRPD register.
                 : (for USB Host mode; Enable D + / D-line PullDown.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_drpd(usb_utr_t* ptr, uint16_t port)
{
    if (USB_PORT0 == port)
    {
        ptr->ipp->SYSCFG0 &= (~USB_DRPD);
    }
} /* End of function hw_usb_clear_drpd() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_usbe
 Description     : Enable USB operation.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_usbe(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.SYSCFG0 |= USB_USBE;
#else
        USB201.SYSCFG0 |= USB_USBE;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->SYSCFG0 |= USB_USBE;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_usbe() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_usbe
 Description     : Enable USB operation.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_usbe(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.SYSCFG0 &= (~USB_USBE);
#else
        USB201.SYSCFG0 &= (~USB_USBE);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->SYSCFG0 &= (~USB_USBE);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_usbe() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_set_buswait
 Description     : Set BUSWAIT register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_buswait(usb_utr_t* ptr)
{
    ptr->ipp->BUSWAIT = USB_CFG_BUSWAIT;
} /* End of function hw_usb_set_buswait() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_bcctrl
 Description     : Set BCCTRL's bits.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_bcctrl(usb_utr_t* ptr, uint16_t data)
{
} /* End of function hw_usb_set_bcctrl() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_bcctrl
 Description     : Clear BCCTRL's bits.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value
 Return value    : none
***********************************************************************************************************************/
void hw_usb_clear_bcctrl(usb_utr_t* ptr, uint16_t data)
{
} /* End of function hw_usb_clear_bcctrl() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_syssts
 Description     : Returns the value of the specified port's SYSSTS register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : USB port number. ($REA not used.)
 Return value    : SYSSTS0 content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_syssts(usb_utr_t* ptr, uint16_t port)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)(USB200.SYSSTS0);
#else
        return (uint16_t)(USB201.SYSSTS0);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return (uint16_t)(ptr->ipp->SYSSTS0);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_syssts() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_dvstctr
 Description     : Returns the specified port's DVSTCTR register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : USB port number. ($REA not used.)
 Return value    : DVSTCTR0 content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_dvstctr(usb_utr_t* ptr, uint16_t port)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)(USB200.DVSTCTR0);
#else
        return (uint16_t)(USB201.DVSTCTR0);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return (uint16_t)(ptr->ipp->DVSTCTR0);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_dvstctr() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_write_dvstctr
 Description     : Write data to the specified port's DVSTCTR register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : USB port number.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_dvstctr(usb_utr_t* ptr, uint16_t port, uint16_t data)
{
    if (USB_PORT0 == port)
    {
        ptr->ipp->DVSTCTR0 = data;
    }
} /* End of function hw_usb_write_dvstctr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_rmw_dvstctr
 Description     : Read-modify-write the specified port's DVSTCTR.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number
                 : uint16_t     data         : The value to write.
                 : uint16_t     bitptn       : Bit pattern to read-modify-write.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_rmw_dvstctr(usb_utr_t* ptr, uint16_t port, uint16_t data, uint16_t bitptn)
{
    uint16_t buf;

    if (USB_PORT0 == port)
    {
        buf = ptr->ipp->DVSTCTR0;
        buf &= (~bitptn);
        buf |= (data & bitptn);
        ptr->ipp->DVSTCTR0 = buf;
    }
} /* End of function hw_usb_rmw_dvstctr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_dvstctr
 Description     : Clear the bit pattern specified in argument, of the specified
                 : port's DVSTCTR register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number
                 : uint16_t     bitptn       : Bit pattern to read-modify-write.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_dvstctr(usb_utr_t* ptr, uint16_t port, uint16_t bitptn)
{
    if (USB_PORT0 == port)
    {
        ptr->ipp->DVSTCTR0 &= (~bitptn);
    }
} /* End of function hw_usb_clear_dvstctr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_vbout
 Description     : Set specified port's VBOUT-bit in the DVSTCTR register.
                 : (To output a "High" to pin VBOUT.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_vbout(usb_utr_t* ptr, uint16_t port)
{
} /* End of function hw_usb_set_vbout() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_vbout
 Description     : Clear specified port's VBOUT-bit in the DVSTCTR register.
                 : (To output a "Low" to pin VBOUT.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     port         : Port number
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_vbout(usb_utr_t* ptr, uint16_t port)
{
} /* End of function hw_usb_clear_vbout() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_utst
 Description     : Not processed as the functionality is provided by R8A66597(ASSP).
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_utst(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.TESTMODE = data;
#else
        USB201.TESTMODE = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->TESTMODE = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_utst() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_fifo32
 Description     : Data is read from the specified pipemode's FIFO register, 32-bits
                 : wide, corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : CFIFO/D0FIFO/D1FIFO content (32-bit)
 ***********************************************************************************************************************/
uint32_t hw_usb_read_fifo32(usb_utr_t* ptr, uint16_t pipemode)
{
    uint32_t data;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        switch (pipemode)
        {
            case USB_CUSE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                data = USB200.CFIFO.UINT32;
#else
                data = USB201.CFIFO.UINT32;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D0USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                data = USB200.D0FIFO.UINT32;
#else
                data = USB201.D0FIFO.UINT32;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D1USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                data = USB200.D1FIFO.UINT32;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
                data = USB201.D1FIFO.UINT32;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE2);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        switch (pipemode)
        {
            case USB_CUSE:

                data = ptr->ipp->CFIFO.UINT32;

                break;

            case USB_D0USE:

                data = ptr->ipp->D0FIFO.UINT32;

                break;

            case USB_D1USE:

                data = ptr->ipp->D1FIFO.UINT32;

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE2);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return (data);
} /* End of function hw_usb_read_fifo32() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_fifo32
 Description     : Data is written to the specified pipemode's FIFO register, 32-bits
                 : wide, corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
                 : uint32_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_fifo32(usb_utr_t* ptr, uint16_t pipemode, uint32_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        switch (pipemode)
        {
            case USB_CUSE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.CFIFO.UINT32 = data;
#else
                USB201.CFIFO.UINT32 = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D0USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.D0FIFO.UINT32 = data;
#else
                USB201.D0FIFO.UINT32 = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D1USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.D1FIFO.UINT32 = data;
#else
                USB201.D1FIFO.UINT32 = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE3);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        switch (pipemode)
        {
            case USB_CUSE:

                ptr->ipp->CFIFO.UINT32 = data;

                break;

            case USB_D0USE:

                ptr->ipp->D0FIFO.UINT32 = data;

                break;

            case USB_D1USE:

                ptr->ipp->D1FIFO.UINT32 = data;

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE3);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_fifo32() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_fifo16
 Description     : Data is read from the specified pipemode's FIFO register, 16-bits
                 : wide, corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : CFIFO/D0FIFO/D1FIFO content (16-bit)
 ***********************************************************************************************************************/
uint16_t hw_usb_read_fifo16(usb_utr_t* ptr, uint16_t pipemode)
{
    uint16_t data;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        switch (pipemode)
        {
            case USB_CUSE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                data = USB200.CFIFO.UINT16[H];
#else
                data = USB201.CFIFO.UINT16[H];
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D0USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                data = USB200.D0FIFO.UINT16[H];
#else
                data = USB201.D0FIFO.UINT16[H];
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D1USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                data = USB200.D1FIFO.UINT16[H];
#else
                data = USB201.D1FIFO.UINT16[H];
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE5);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        switch (pipemode)
        {
            case USB_CUSE:

                data = ptr->ipp->CFIFO.UINT16[H];

                break;

            case USB_D0USE:

                data = ptr->ipp->D0FIFO.UINT16[H];

                break;

            case USB_D1USE:

                data = ptr->ipp->D1FIFO.UINT16[H];

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE5);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    return (data);
} /* End of function hw_usb_read_fifo16() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_fifo16
 Description     : Data is written to the specified pipemode's FIFO register, 16-bits
                 : wide, corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_fifo16(usb_utr_t* ptr, uint16_t pipemode, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        switch (pipemode)
        {
            case USB_CUSE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.CFIFO.UINT16[H] = data;
#else
                USB201.CFIFO.UINT16[H] = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D0USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.D0FIFO.UINT16[H] = data;
#else
                USB201.D0FIFO.UINT16[H] = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D1USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.D1FIFO.UINT16[H] = data;
#else
                USB201.D1FIFO.UINT16[H] = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE7);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        switch (pipemode)
        {
            case USB_CUSE:

                ptr->ipp->CFIFO.UINT16[H] = data;

                break;

            case USB_D0USE:

                ptr->ipp->D0FIFO.UINT16[H] = data;

                break;

            case USB_D1USE:

                ptr->ipp->D1FIFO.UINT16[H] = data;

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE6);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_fifo16() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_fifo8
 Description     : Data is written to the specified pipemode's FIFO register, 8-bits
                 : wide, corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipdemode    : CUSE/D0DMA/D1DMA
                 : uint8_t      data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_fifo8(usb_utr_t* ptr, uint16_t pipemode, uint8_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        switch (pipemode)
        {
            case USB_CUSE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.CFIFO.UINT8[HH] = data;
#else
                USB201.CFIFO.UINT8[HH] = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D0USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.D0FIFO.UINT8[HH] = data;
#else
                USB201.D0FIFO.UINT8[HH] = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D1USE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                USB200.D1FIFO.UINT8[HH] = data;
#else
                USB201.D1FIFO.UINT8[HH] = data;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE7);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        switch (pipemode)
        {
            case USB_CUSE:

                ptr->ipp->CFIFO.UINT8[HH] = data;

                break;

            case USB_D0USE:

                ptr->ipp->D0FIFO.UINT8[HH] = data;

                break;

            case USB_D1USE:

                ptr->ipp->D1FIFO.UINT8[HH] = data;

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE6);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_fifo8() */

/***********************************************************************************************************************
 Function Name   : hw_usb_get_fifosel_adr
 Description     : Returns the *address* of the FIFOSEL register corresponding to
                 : specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : none
 ***********************************************************************************************************************/
void* hw_usb_get_fifosel_adr(usb_utr_t* ptr, uint16_t pipemode)
{
    void* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        switch (pipemode)
        {
            case USB_CUSE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                p_reg = (void*)&(USB200.CFIFOSEL);
#else
                p_reg = (void*)&(USB201.CFIFOSEL);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D0USE:
            case USB_D0DMA:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                p_reg = (void*)&(USB200.D0FIFOSEL);
#else
                p_reg = (void*)&(USB201.D0FIFOSEL);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D1USE:
            case USB_D1DMA:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                p_reg = (void*)&(USB200.D1FIFOSEL);
#else
                p_reg = (void*)&(USB201.D1FIFOSEL);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE12);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        switch (pipemode)
        {
            case USB_CUSE:

                p_reg = (void*)&(ptr->ipp->CFIFOSEL);

                break;

            case USB_D0USE:
            case USB_D0DMA:

                p_reg = (void*)&(ptr->ipp->D0FIFOSEL);

                break;

            case USB_D1USE:
            case USB_D1DMA:

                p_reg = (void*)&(ptr->ipp->D1FIFOSEL);

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE12);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return (p_reg);
} /* End of function hw_usb_get_fifosel_adr() */

uint16_t fifoSels[USB_FIFO_ACCESS_NUM_MAX]; // By Rohan

/***********************************************************************************************************************
 Function Name   : hw_usb_set_dclrm
 Description     : Set DCLRM-bits (FIFO buffer auto clear) of the FIFOSEL cor-
                 : responding to specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_dclrm(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);

    (*p_reg) |= USB_DCLRM;
} /* End of function hw_usb_clear_dclrm() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_dclrm
 Description     : Reset DCLRM-bits (FIFO buffer not auto-cleared) of the FIFOSEL
                 : corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_dclrm(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);

    (*p_reg) &= (~USB_DCLRM);
} /* End of function hw_usb_set_dclrm() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_dreqe
 Description     : Set DREQE-bits (to output signal DxREQ_Na) of the FIFOSEL cor-
                 : responding to specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_dreqe(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);

    (*p_reg) &= (~USB_DREQE);
    (*p_reg) |= USB_DREQE;
} /* End of function hw_usb_set_dreqe() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_dreqe
 Description     : Clear DREQE-bits (To prohibit the output of the signal DxREQ_N)
                 : of the FIFOSEL corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_dreqe(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);

    (*p_reg) &= (~USB_DREQE);
} /* End of function hw_usb_clear_dreqe() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_mbw
 Description     : Set MBW-bits (CFIFO Port Access Bit Width) of the FIFOSEL cor-
                 : responding to the specified PIPEMODE, to select 8 or 16-bit
                 : wide FIFO port access.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_mbw(usb_utr_t* ptr, uint16_t pipemode, uint16_t data)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        (*p_reg) &= (~USB_MBW);

        if (data != 0)
        {
            (*p_reg) |= data;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        (*p_reg) &= (~USB_MBW);

        if (data != 0)
        {
            (*p_reg) |= data;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_mbw() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_curpipe
 Description     : Set pipe to the number given; in the FIFOSEL corresponding
                 : to specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_curpipe(usb_utr_t* ptr, uint16_t pipemode, uint16_t pipeno)
{
    volatile uint16_t* p_reg;
    volatile uint16_t reg;

    p_reg = (uint16_t*)hw_usb_get_fifosel_adr(ptr, pipemode);
    reg   = *p_reg;

    reg &= (~USB_DREQE);
    reg &= (~USB_CURPIPE);
    reg |= pipeno;

    *p_reg = reg;
} /* End of function hw_usb_set_curpipe() */

/***********************************************************************************************************************
 Function Name   : hw_usb_get_fifoctr_adr
 Description     : Returns the *address* of the FIFOCTR register corresponding to
                 : specified PIPEMODE.
                 : (FIFO Port Control Register.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
static void* hw_usb_get_fifoctr_adr(usb_utr_t* ptr, uint16_t pipemode)
{
    void* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        switch (pipemode)
        {
            case USB_CUSE:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                p_reg = (void*)&(USB200.CFIFOCTR);
#else
                p_reg = (void*)&(USB201.CFIFOCTR);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D0USE:
            case USB_D0DMA:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                p_reg = (void*)&(USB200.D0FIFOCTR);
#else
                p_reg = (void*)&(USB201.D0FIFOCTR);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            case USB_D1USE:
            case USB_D1DMA:

#if USB_CFG_USE_USBIP == USB_CFG_IP0
                p_reg = (void*)&(USB200.D1FIFOCTR);
#else
                p_reg = (void*)&(USB201.D1FIFOCTR);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE13);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        switch (pipemode)
        {
            case USB_CUSE:

                p_reg = (void*)&(ptr->ipp->CFIFOCTR);

                break;

            case USB_D0USE:
            case USB_D0DMA:

                p_reg = (void*)&(ptr->ipp->D0FIFOCTR);

                break;

            case USB_D1USE:
            case USB_D1DMA:

                p_reg = (void*)&(ptr->ipp->D1FIFOCTR);

                break;

            default:

                USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE13);

                break;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return p_reg;
} /* End of function hw_usb_get_fifoctr_adr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_fifoctr
 Description     : Returns the value of the FIFOCTR register corresponding to
                 : specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : FIFOCTR content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_fifoctr(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifoctr_adr(ptr, pipemode);

    return *p_reg;
} /* End of function hw_usb_read_fifoctr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_bval
 Description     : Set BVAL (Buffer Memory Valid Flag) to the number given; in
                 : the FIFOCTR corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_bval(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifoctr_adr(ptr, pipemode);

    (*p_reg) |= USB_BVAL;
} /* End of function hw_usb_set_bval() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_bclr
 Description     : Set BCLR (CPU Buffer Clear) to the number given; in the
                 : FIFOCTR corresponding to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_bclr(usb_utr_t* ptr, uint16_t pipemode)
{
    volatile uint16_t* p_reg;

    p_reg = (uint16_t*)hw_usb_get_fifoctr_adr(ptr, pipemode);

    *p_reg = USB_BCLR;
} /* End of function hw_usb_set_bclr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_intenb
 Description     : Data is written to INTENB register,
                 : enabling/disabling the various USB interrupts.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_intenb(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTENB0 = data;
#else
        USB201.INTENB0 = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->INTENB0 = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_intenb() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_intenb
 Description     : Bit(s) to be set in INTENB register,
                 : enabling the respective USB interrupt(s).
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Bit pattern: Respective interrupts with '1'
                 : will be enabled.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_intenb(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTENB0 |= data;
#else
        USB201.INTENB0 |= data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->INTENB0 |= data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_intenb() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_clear_enb_vbse
 Description     : Clear the VBE-bit of INTENB register,
                 : to prohibit VBUS interrupts.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_enb_vbse(usb_utr_t* ptr)
{
    ptr->ipp->INTENB0 &= (~USB_VBSE);
} /* End of function hw_usb_clear_enb_vbse() */

/***********************************************************************************************************************
 Description     : Clear the SOFE-bit of INTENB register,
                 : to prohibit Frame Number Update interrupts.
 Function Name   : hw_usb_clear_enb_sofe
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_enb_sofe(usb_utr_t* ptr)
{
    ptr->ipp->INTENB0 &= (~USB_SOFE);
} /* End of function hw_usb_clear_enb_sofe() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_brdyenb
 Description     : Data is written to BRDYENB register,
                 : enabling/disabling each respective pipe's BRDY interrupt.
                 : (The BRDY interrupt indicates that a FIFO port is accessible.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_brdyenb(usb_utr_t* ptr, uint16_t data)
{
    ptr->ipp->BRDYENB = data;
} /* End of function hw_usb_write_brdyenb() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_write_nrdyenb
 Description     : Data is written to NRDYENB register,
                 : enabling/disabling each respective pipe's NRDY interrupt
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_nrdyenb(usb_utr_t* ptr, uint16_t data)
{
    ptr->ipp->NRDYENB = data;
} /* End of function hw_usb_write_nrdyenb() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_write_bempenb
 Description     : Data is written to BEMPENB register,
                 : enabling/disabling each respective pipe's BEMP interrupt.
                 : (The BEMP interrupt indicates that the USB buffer is empty,
                 : and so the FIFO can now be written to.)
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_bempenb(usb_utr_t* ptr, uint16_t data)
{
    ptr->ipp->BEMPENB = data;
} /* End of function hw_usb_write_bempenb() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_sofcfg
 Description     : Set Bit pattern for SOFCFG
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_sofcfg(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        if (USB_USBIP_1 == ptr->ip)
        {
            ptr->ipp->SOFCFG |= data;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_sofcfg() */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Function Name   : hw_usb_read_intsts
 Description     : Returns INTSTS0 register content.
 Arguments       : none
 Return value    : INTSTS0 content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_intsts(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    return USB200.INTSTS0;
#else
    return USB201.INTSTS0;
#endif
} /* End of function hw_usb_read_intsts() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_intsts
 Description     : Data is written to INTSTS0 register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_intsts(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.INTSTS0 = data;
#else
        USB201.INTSTS0 = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->INTSTS0 = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_intsts() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_clear_sts_sofr
 Description     : Clear the SOFR-bit (Frame Number Refresh Interrupt Status) of
                 : the clear SOF interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_sts_sofr(usb_utr_t* ptr)
{
    ptr->ipp->INTSTS0 = (uint16_t)~USB_SOFR;
} /* End of function hw_usb_clear_sts_sofr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_brdysts
 Description     : Returns BRDYSTS register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : BRDYSTS content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_brdysts(usb_utr_t* ptr)
{
    return ptr->ipp->BRDYSTS;
} /* End of function hw_usb_read_brdysts() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_brdysts
 Description     : Data is written to BRDYSTS register, to set the BRDY interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_brdysts(usb_utr_t* ptr, uint16_t data)
{
    ptr->ipp->BRDYSTS = data;
} /* End of function hw_usb_write_brdysts() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_write_nrdy_sts
 Description     : Data is written to NRDYSTS register, to
                 : set the NRDY interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_nrdy_sts(usb_utr_t* ptr, uint16_t data)
{
    ptr->ipp->NRDYSTS = data;
} /* End of function hw_usb_write_nrdy_sts() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_status_nrdy
 Description     : Clear the PIPExNRDY status bit of the specified pipe to clear
                 : its NRDY interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_status_nrdy(usb_utr_t* ptr, uint16_t pipeno)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.NRDYSTS = (uint16_t)~(1 << pipeno);
#else
        USB201.NRDYSTS = (uint16_t)~(1 << pipeno);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->NRDYSTS = (uint16_t)~(1 << pipeno);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_status_nrdy() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_write_bempsts
 Description     : Data is written to BEMPSTS register, to set the BEMP interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_bempsts(usb_utr_t* ptr, uint16_t data)
{
    ptr->ipp->BEMPSTS = data;
} /* End of function hw_usb_write_bempsts() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_frmnum
 Description     : Returns FRMNUM register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : FRMNUM content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_frmnum(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)USB200.FRMNUM;
#else
        return (uint16_t)USB201.FRMNUM;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return (uint16_t)ptr->ipp->FRMNUM;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_frmnum() */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Function Name   : hw_usb_read_usbreq
 Description     : Returns USBREQ register content.
 Arguments       : none
 Return value    : USBREQ content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_usbreq(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    return (uint16_t)USB200.USBREQ;
#else
    return (uint16_t)USB201.USBREQ;
#endif
} /* End of function hw_usb_read_usbreq() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_usbval
 Description     : Returns USBVAL register content.
 Arguments       : none
 Return value    : USBVAL content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_usbval(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    return (uint16_t)USB200.USBVAL;
#else
    return (uint16_t)USB201.USBVAL;
#endif
} /* End of function hw_usb_read_usbval() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_usbindx
 Description     : Returns USBINDX register content.
 Arguments       : none
 Return value    : USBINDX content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_usbindx(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    return (uint16_t)USB200.USBINDX;
#else
    return (uint16_t)USB201.USBINDX;
#endif
} /* End of function hw_usb_read_usbindx() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_usbleng
 Description     : Returns USBLENG register content.
 Arguments       : none
 Return value    : USBLENG content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_usbleng(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    return (uint16_t)USB200.USBLENG;
#else
    return (uint16_t)USB201.USBLENG;
#endif
} /* End of function hw_usb_read_usbleng() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_dcpcfg
 Description     : Returns DCPCFG register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : DCPCFG content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_dcpcfg(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)USB200.DCPCFG;
#else
        return (uint16_t)USB201.DCPCFG;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return (uint16_t)ptr->ipp->DCPCFG;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_dcpcfg() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_dcpcfg
 Description     : Specified data is written to DCPCFG register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_dcpcfg(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.DCPCFG = data;
#else
        USB201.DCPCFG = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->DCPCFG = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_dcpcfg()*/

/***********************************************************************************************************************
 Function Name   : hw_usb_read_dcpmaxp
 Description     : Returns DCPMAXP register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : DCPMAXP content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_dcpmaxp(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)USB200.DCPMAXP;
#else
        return (uint16_t)USB201.DCPMAXP;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return (uint16_t)ptr->ipp->DCPMAXP;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_dcpmaxp() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_dcpmxps
 Description     : Specified data is written to DCPMAXP register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_dcpmxps(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.DCPMAXP = data;
#else
        USB201.DCPMAXP = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->DCPMAXP = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_dcpmxps() */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Function Name   : hw_usb_read_dcpctr
 Description     : Returns DCPCTR register content.
 Arguments       : none
 Return value    : DCPCTR content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_dcpctr(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    return (uint16_t)USB200.DCPCTR;
#else
    return (uint16_t)USB201.DCPCTR;
#endif
} /* End of function hw_usb_read_dcpctr() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_pipecfg
 Description     : Returns PIPECFG register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : PIPECFG content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_pipecfg(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)USB200.PIPECFG;
#else
        return (uint16_t)USB201.PIPECFG;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return (uint16_t)ptr->ipp->PIPECFG;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_pipecfg() */

uint16_t pipeCfgs[USB_MAX_PIPE_NO + 1]; // By Rohan. These would need expanding if two USB ports in use
uint16_t pipeBufs[USB_MAX_PIPE_NO + 1];
uint16_t pipeMaxPs[USB_MAX_PIPE_NO + 1];

/***********************************************************************************************************************
 Function Name   : hw_usb_write_pipecfg
 Description     : Specified data is written to PIPECFG register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_pipecfg(usb_utr_t* ptr, uint16_t data,
    uint16_t pipe) // pipe number added by Rohan so the set pipe type can be stored in an array and accessed without
                   // changing the selected pipe
{
    pipeCfgs[pipe] = data;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.PIPECFG = data;
#else
        USB201.PIPECFG = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->PIPECFG = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_pipecfg() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_pipebuf
 Description     : Specified the value by 2nd argument is set to PIPEBUF register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_pipebuf(usb_utr_t* ptr, uint16_t data,
    uint16_t pipe) // pipe number added by Rohan so the set pipe type can be stored in an array and accessed without
                   // changing the selected pipe
{
    pipeBufs[pipe] = data;
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.PIPEBUF = data;
#else
        USB201.PIPEBUF = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->PIPEBUF = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_pipebuf() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_pipebuf
 Description     : Returns PIPECFG register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : PIPEBUF content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_pipebuf(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)USB200.PIPEBUF;
#else
        return (uint16_t)USB201.PIPEBUF;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)

        return (uint16_t)ptr->ipp->PIPEBUF;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_pipebuf() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_pipemaxp
 Description     : Returns PIPEMAXP register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : PIPEMAXP content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_pipemaxp(usb_utr_t* ptr)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        return (uint16_t)USB200.PIPEMAXP;
#else
        return (uint16_t)USB201.PIPEMAXP;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        return (uint16_t)ptr->ipp->PIPEMAXP;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
    return 0;
} /* End of function hw_usb_read_pipemaxp() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_pipemaxp
 Description     : Specified data is written to PIPEMAXP register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_pipemaxp(usb_utr_t* ptr, uint16_t data, uint16_t pipe)
{
    pipeMaxPs[pipe] = data;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.PIPEMAXP = data;
#else
        USB201.PIPEMAXP = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->PIPEMAXP = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_pipemaxp() */

/***********************************************************************************************************************
 Function Name   : hw_usb_write_pipeperi
 Description     : Specified data is written to PIPEPERI register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_pipeperi(usb_utr_t* ptr, uint16_t data)
{
    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        USB200.PIPEPERI = data;
#else
        USB201.PIPEPERI = data;
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        ptr->ipp->PIPEPERI = data;
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_write_pipeperi() */

/***********************************************************************************************************************
 Function Name   : hw_usb_read_pipectr
 Description     : Returns DCPCTR or the specified pipe's PIPECTR register content.
                 : The Pipe Control Register returned is determined by the speci-
                 : fied pipe number.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : PIPExCTR content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_pipectr(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        if (USB_PIPE0 == pipeno)
        {
            p_reg = (uint16_t*)&(USB200.DCPCTR);
        }
        else
        {
            p_reg = (uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1);
        }
#else
        if (USB_PIPE0 == pipeno)
        {
            p_reg = (uint16_t*)&(USB201.DCPCTR);
        }
        else
        {
            p_reg = (uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1);
        }
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        if (USB_PIPE0 == pipeno)
        {
            p_reg = (uint16_t*)&(ptr->ipp->DCPCTR);
        }
        else
        {
            p_reg = (uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1);
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    return *p_reg;
} /* End of function hw_usb_read_pipectr() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_write_pipectr
 Description     : Specified data is written to the specified pipe's PIPEPERI register.
 Arguments       : usb_utr_t *ptr            : USB internal structure. Selects USB channel.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     data         : Setting value.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_write_pipectr(usb_utr_t* ptr, uint16_t pipeno, uint16_t data)
{
    volatile uint16_t* p_reg;

    if (USB_PIPE0 == pipeno)
    {
        p_reg = (uint16_t*)&(ptr->ipp->DCPCTR);
    }
    else
    {
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1);
    }
    *p_reg = data;
} /* End of function hw_usb_write_pipectr() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_csclr
 Description     : Set CSCLR bit in the specified pipe's PIPECTR register
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_csclr(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1);
#else
        p_reg = (uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1);
#endif
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    (*p_reg) |= USB_CSCLR;
} /* End of function hw_usb_set_csclr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_aclrm
 Description     : The ACLRM-bit (Auto Buffer Clear Mode) is set in the specified
                 : pipe's control register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_aclrm(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1);
#else
        p_reg = (uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    (*p_reg) |= USB_ACLRM;
} /* End of function hw_usb_set_aclrm() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_aclrm
 Description     : Clear the ACLRM bit in the specified pipe's control register
                 : to disable Auto Buffer Clear Mode.
                 : its BEMP interrupt status.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_aclrm(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1);
#else
        p_reg = (uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_reg = (uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    (*p_reg) &= (~USB_ACLRM);
} /* End of function hw_usb_clear_aclrm() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_sqclr
 Description     : The SQCLR-bit (Toggle Bit Clear) is set in the specified pipe's
                 : control register. Setting SQSET to 1 makes DATA0 the expected
                 : data in the pipe's next transfer.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_sqclr(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        if (USB_PIPE0 == pipeno)
        {
            USB200.DCPCTR |= USB_SQCLR;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1));
            (*p_reg) |= USB_SQCLR;
        }
        else
        {
        }
#else
        if (USB_PIPE0 == pipeno)
        {
            USB201.DCPCTR |= USB_SQCLR;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1));
            (*p_reg) |= USB_SQCLR;
        }
        else
        {
        }
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        if (pipeno == USB_PIPE0)
        {
            ptr->ipp->DCPCTR |= USB_SQCLR;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1));
            (*p_reg) |= USB_SQCLR;
        }
        else
        {
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_sqclr() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_sqset
 Description     : The SQSET-bit (Toggle Bit Set) is set in the specified pipe's
                 : control register. Setting SQSET to 1 makes DATA1 the expected
                 : data in the next transfer.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_sqset(usb_utr_t* ptr, uint16_t pipeno)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        if (USB_PIPE0 == pipeno)
        {
            USB200.DCPCTR |= USB_SQSET;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1));
            (*p_reg) |= USB_SQSET;
        }
#else
        if (USB_PIPE0 == pipeno)
        {
            USB201.DCPCTR |= USB_SQSET;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1));
            (*p_reg) |= USB_SQSET;
        }
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        if (USB_PIPE0 == pipeno)
        {
            ptr->ipp->DCPCTR |= USB_SQSET;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1));
            (*p_reg) |= USB_SQSET;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_sqset() */

// See cut-down version of this by Rohan in header file
/***********************************************************************************************************************
 Function Name   : hw_usb_set_pid
 Description     : Set the specified PID of the specified pipe's DCPCTR/PIPECTR
                 : register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
                 : uint16_t     data         : NAK/BUF/STALL.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_pid(usb_utr_t* ptr, uint16_t pipeno, uint16_t data)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        if (USB_PIPE0 == pipeno)
        {
            p_reg = ((uint16_t*)&(USB200.DCPCTR));
            (*p_reg) &= (~USB_PID);
            (*p_reg) |= data;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1));
            (*p_reg) &= (~USB_PID);
            (*p_reg) |= data;
        }
#else
        if (USB_PIPE0 == pipeno)
        {
            p_reg = ((uint16_t*)&(USB201.DCPCTR));
            (*p_reg) &= (~USB_PID);
            (*p_reg) |= data;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1));
            (*p_reg) &= (~USB_PID);
            (*p_reg) |= data;
        }
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        if (USB_PIPE0 == pipeno)
        {
            p_reg = ((uint16_t*)&(ptr->ipp->DCPCTR));
            (*p_reg) &= (~USB_PID);
            (*p_reg) |= data;
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1));
            (*p_reg) &= (~USB_PID);
            (*p_reg) |= data;
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_set_pid() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_pid
 Description     : Clear the specified PID-bits of the specified pipe's DCPCTR/
                 : PIPECTR register.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipeno       : Pipe number.
                 : uint16_t     data         : NAK/BUF/STALL - to be cleared.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_pid(usb_utr_t* ptr, uint16_t pipeno, uint16_t data)
{
    volatile uint16_t* p_reg;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        if (pipeno == USB_PIPE0)
        {
            p_reg = ((uint16_t*)&(USB200.DCPCTR));
            (*p_reg) &= (~data);
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB200.PIPE1CTR) + (pipeno - 1));
            (*p_reg) &= (~data);
        }
#else
        if (pipeno == USB_PIPE0)
        {
            p_reg = ((uint16_t*)&(USB201.DCPCTR));
            (*p_reg) &= (~data);
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(USB201.PIPE1CTR) + (pipeno - 1));
            (*p_reg) &= (~data);
        }
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        if (pipeno == USB_PIPE0)
        {
            p_reg = ((uint16_t*)&(ptr->ipp->DCPCTR));
            (*p_reg) &= (~data);
        }
        else if ((USB_MIN_PIPE_NO <= pipeno) && (pipeno <= USB_MAX_PIPE_NO))
        {
            p_reg = ((uint16_t*)&(ptr->ipp->PIPE1CTR) + (pipeno - 1));
            (*p_reg) &= (~data);
        }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }
} /* End of function hw_usb_clear_pid() */

#if USB_CFG_BC == USB_CFG_ENABLE
/***********************************************************************************************************************
 Function Name   : hw_usb_read_bcctrl
 Description     : Returns BCCTRL register content.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : BCCTRL content
 ***********************************************************************************************************************/
uint16_t hw_usb_read_bcctrl(usb_utr_t* ptr)
{
    return 0;
} /* End of function hw_usb_read_bcctrl() */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Function Name   : hw_usb_set_vdmsrce
 Description     : Set VDMSRCE bit.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_vdmsrce(usb_utr_t* ptr)
{
} /* End of function hw_usb_set_vdmsrce() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_vdmsrce
 Description     : Clear VDMSRCE bits.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_vdmsrce(usb_utr_t* ptr)
{
} /* End of function hw_usb_clear_vdmsrce() */

/***********************************************************************************************************************
 Function Name   : hw_usb_set_idpsinke
 Description     : Set IDPSINKE bit.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_idpsinke(usb_utr_t* ptr)
{
} /* End of function hw_usb_set_idpsinke() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_idpsinke
 Description     : Clear IDPSINKE bits.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_idpsinke(usb_utr_t* ptr)
{
} /* End of function hw_usb_clear_idpsinke() */
#endif /* USB_CFG_BC == USB_CFG_ENABLE */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Function Name   : hw_usb_set_suspendm
 Description     : Set SUSPM bit.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_set_suspendm(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.SUSPMODE |= USB_SUSPM;
#else
    USB201.SUSPMODE |= USB_SUSPM;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_set_suspendm() */

/***********************************************************************************************************************
 Function Name   : hw_usb_clear_suspm
 Description     : Clear SUSPM bit.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void hw_usb_clear_suspm(void)
{
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    USB200.SUSPMODE &= (~USB_SUSPM);
#else
    USB201.SUSPMODE &= (~USB_SUSPM);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
} /* End of function hw_usb_clear_suspm() */
#endif /* ( (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI ) */

/***********************************************************************************************************************
 Function Name   : usb_std_clr_uclksel_flg
 Description     :
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_std_clr_uclksel_flg(void)
{
    g_usb_std_uclksel = USB_FALSE;
} /* End of function usb_std_clr_uclksel_flg() */

/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
