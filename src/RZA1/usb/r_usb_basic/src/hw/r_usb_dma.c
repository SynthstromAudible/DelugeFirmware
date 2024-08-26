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
 * File Name    : r_usb_dma.c
 * Description  : Setting code of DMA
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 30.09.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes>, "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/system/iodefine.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_reg_access.h"

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
#include "RZA1/cache/cache.h"
#include "RZA1/intc/devdrv_intc.h" /* INTC Driver Header   */
#include "drivers/usb/r_usb_basic/src/hw/inc/r_usb_dmac.h"

/***********************************************************************************************************************
 Exported global functions (to be accessed by other files)
 ***********************************************************************************************************************/

usb_dma_int_t g_usb_cstd_dma_int; /* DMA Interrupt Info */

uint16_t g_usb_cstd_dma_dir[USB_NUM_USBIP][USB_DMA_USE_CH_MAX];  /* DMA0 and DMA1 direction */
uint32_t g_usb_cstd_dma_size[USB_NUM_USBIP][USB_DMA_USE_CH_MAX]; /* DMA0 and DMA1 buffer size */
uint16_t g_usb_cstd_dma_fifo[USB_NUM_USBIP][USB_DMA_USE_CH_MAX]; /* DMA0 and DMA1 FIFO buffer size */
uint16_t g_usb_cstd_dma_pipe[USB_NUM_USBIP][USB_DMA_USE_CH_MAX]; /* DMA0 and DMA1 pipe number */

/* DMA channel table by usb ip and usb pipemode setting
 * default setting 0xff */
uint8_t g_usb_cstd_dma_ch[USB_NUM_USBIP][USB_FIFO_ACCESS_NUM_MAX] = {/* DMA ch no. table */
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

/* DMA channel setting define table
 * setting value by r_usb_basic_config.h file */
const uint16_t g_usb_dma_setting_ch[USB_NUM_USBIP][USB_DMA_TXRX] = {
    {USB_CFG_USB0_DMA_TX, USB_CFG_USB0_DMA_RX}, {USB_CFG_USB1_DMA_TX, USB_CFG_USB1_DMA_RX}};

/***********************************************************************************************************************
 Exported global variables
 ***********************************************************************************************************************/
extern uint16_t g_usb_usbmode; /* USB mode HOST/PERI */

/***********************************************************************************************************************
 Function Name   : usb_cstd_buf2dxfifo_start_dma
 Description     : Start transfer using DMA. If transfer size is 0, write
                 : more data to buffer.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     useport      : FIFO select
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_buf2dxfifo_start_dma(usb_utr_t* ptr, uint16_t pipe, uint16_t useport)
{
    uint8_t TransDataBlockSize;
    uint32_t DmaSize;
    uint16_t ch_no;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif
    }
    else
    {
        ip = ptr->ip;
    }

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
    /* BFRE OFF */
#if (USB_CFG_USE_USBIP == USB_CFG_IP0)
    USB200.PIPESEL = pipe;
    while (1)
    {
        if (USB200.PIPESEL == pipe)
        {
            break;
        }
    }
    USB200.PIPECFG &= (0xffff - 0x0400);
#endif
#if (USB_CFG_USE_USBIP == USB_CFG_IP1)
    USB201.PIPESEL = pipe;
    while (1)
    {
        if (USB201.PIPESEL == pipe)
        {
            break;
        }
    }
    USB201.PIPECFG &= (0xffff - 0x0400);
#endif

#endif
    ch_no   = usb_dma_def_ch_no(ip, USB_DMA_TX);
    DmaSize = g_usb_cstd_dma_size[ip][ch_no];

#if USE_DMA32
    if (0u == (DmaSize % 32u))
    {
        TransDataBlockSize = 32u;
    }
    else
    {
        TransDataBlockSize = 1u;
    }
#else
    if (4u == (DmaSize % 4u))
    {
        TransDataBlockSize = 4u;
    }
    else
    {
        TransDataBlockSize = 1u;
    }

#endif /* USE_DMA32 */

    if (0u != DmaSize)
    {
        g_usb_cstd_dma_size[ip][ch_no] = DmaSize;
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        usb_cstd_Buf2Fifo_DMAX(
            ch_no, ptr, useport, (uint32_t)(g_p_usb_hstd_data[ip][pipe]), DmaSize, pipe, TransDataBlockSize);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        usb_cstd_Buf2Fifo_DMAX(
            ch_no, ptr, useport, (uint32_t)(g_p_usb_pstd_data[pipe]), DmaSize, pipe, TransDataBlockSize);
#endif
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        usb_hstd_buf2fifo(ptr, pipe, useport);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        usb_pstd_buf2fifo(pipe, useport);
#endif
    }
}
/***********************************************************************************************************************
End of function usb_cstd_Buf2DXfifoStartDma
***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_cstd_dxfifo2buf_start_dma
 Description     : Start transfer using DMA. If transfer size is 0, clear DMA.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipe         : Pipe number.
                 : uint16_t     useport      : FIFO select
                 : uint32_t     length       : Data length
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_dxfifo2buf_start_dma(usb_utr_t* ptr, uint16_t pipe, uint16_t useport, uint32_t length)
{
    uint8_t* p_data_ptr;
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    uint16_t ip;
    ip = ptr->ip;
#endif

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    p_data_ptr = g_p_usb_hstd_data[ip][pipe];
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
    p_data_ptr = g_p_usb_pstd_data[pipe];
#endif

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
    /* BFRE ON */
#if (USB_CFG_USE_USBIP == USB_CFG_IP0)
    USB200.PIPESEL = pipe;
    while (1)
    {
        if (USB200.PIPESEL == pipe)
        {
            break;
        }
    }
    USB200.PIPECFG |= 0x0400;
#endif
#if (USB_CFG_USE_USBIP == USB_CFG_IP1)
    USB201.PIPESEL = pipe;
    while (1)
    {
        if (USB201.PIPESEL == pipe)
        {
            break;
        }
    }
    USB201.PIPECFG |= 0x0400;
#endif

#endif

    usb_cstd_DXfifo2BufStartDma(ptr, pipe, useport, length, (uint32_t)p_data_ptr);

} /* End of function usb_cstd_dxfifo2buf_start_dma() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_dxfifo_stop
 Description     : Setup external variables used for USB data transfer; to reg-
                 : ister if you want to stop the transfer of DMA.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     useport      : FIFO select
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_dxfifo_stop(usb_utr_t* ptr, uint16_t useport)
{
    uint16_t pipe;
    uint16_t ip;
    uint32_t* p_data_cnt;
    uint16_t mbw_setting;
    uint16_t channel;

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif
    }
    else
    {
        ip = ptr->ip;
    }

    channel = usb_dma_ref_ch_no(ip, useport);
    pipe    = g_usb_cstd_dma_pipe[ip][channel];

    if (USB_NULL == ptr)
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        p_data_cnt = &g_usb_pstd_data_cnt[pipe];
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
        p_data_cnt = &g_usb_hstd_data_cnt[ip][pipe];
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */
    }

    if (USB_D0DMA == useport)
    {
        if (USB_IP0 == ip)
        {
            mbw_setting = USB0_D0FIFO_MBW;
        }
        else
        {
            mbw_setting = USB1_D0FIFO_MBW;
        }
    }
    else
    {
        if (USB_IP0 == ip)
        {
            mbw_setting = USB0_D1FIFO_MBW;
        }
        else
        {
            mbw_setting = USB1_D1FIFO_MBW;
        }
    }

    hw_usb_set_mbw(ptr, useport, mbw_setting);

    /* received data size */
    *p_data_cnt = (*p_data_cnt) - g_usb_cstd_dma_size[ip][channel];
} /* End of function usb_cstd_dxfifo_stop() */

/***********************************************************************************************************************
 Function Name   : usb_dma_driver
 Description     : USB DMA transfer complete process.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_dma_driver(void)
{
    uint16_t ip;
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    usb_utr_t utr;
#endif /* ( (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST ) */

    if (g_usb_cstd_dma_int.wp != g_usb_cstd_dma_int.rp)
    {
        if (USB_HOST == g_usb_usbmode)
        {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
            utr.ip  = g_usb_cstd_dma_int.buf[g_usb_cstd_dma_int.rp].ip;
            utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip);

            usb_dma_stop_dxfifo(
                utr.ip, g_usb_cstd_dma_int.buf[g_usb_cstd_dma_int.rp].fifo_type); /* Stop DMA,FIFO access */

            usb_dma_buf2dxfifo_complete(&utr, g_usb_cstd_dma_int.buf[g_usb_cstd_dma_int.rp].fifo_type);
#endif /* ( (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST ) */
        }
        else
        {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)

#if USB_CFG_USE_USBIP == USB_CFG_IP0
            ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
            ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
            usb_dma_stop_dxfifo(ip, g_usb_cstd_dma_int.buf[g_usb_cstd_dma_int.rp].fifo_type); /* Stop DMA,FIFO access */

            usb_dma_buf2dxfifo_complete(USB_NULL, g_usb_cstd_dma_int.buf[g_usb_cstd_dma_int.rp].fifo_type);
#endif /* ( (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI ) */
        }

        /* Read countup */
        g_usb_cstd_dma_int.rp = ((g_usb_cstd_dma_int.rp + 1) % USB_INT_BUFSIZE);
    }
} /* End of function usb_dma_driver() */

/***********************************************************************************************************************
 Function Name   : usb_dma_buf2dxfifo_complete
 Description     : Set end of DMA transfer. Set to restart DMA trans-
                 : fer according to data size of remaining functions to be processed.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     useport      : FIFO select
 Return value    : none
 ***********************************************************************************************************************/
void usb_dma_buf2dxfifo_complete(usb_utr_t* ptr, uint16_t useport)
{
    usb_cstd_DmaxInt(ptr, useport);

} /* End of function usb_dma_buf2dxfifo_complete() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_DmaxInt
 Description     : Set end of DMA transfer. Set to restart DMA trans-
                 : fer according to data size of remaining functions to be pro-
                 : cessed.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_DmaxInt(usb_utr_t* ptr, uint16_t pipemode)
{
    uint16_t pipe;
    uint16_t last_trans_size;
    uint16_t ch_no;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif
    }
    else
    {
        ip = ptr->ip;
    }

    ch_no = usb_dma_ref_ch_no(ip, pipemode);
    pipe  = g_usb_cstd_dma_pipe[ip][ch_no];

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    last_trans_size = g_usb_hstd_data_cnt[ip][pipe] % g_usb_cstd_dma_fifo[ip][ch_no];
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
    last_trans_size = (uint16_t)(g_usb_pstd_data_cnt[pipe] % g_usb_cstd_dma_fifo[ip][ch_no]);
#endif
    /* trans data smaller than Buffer size */
    /*  equal all data transfer end  */
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    if (0u == g_usb_hstd_data_cnt[ip][pipe])
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
        if (0u == g_usb_pstd_data_cnt[pipe])
#endif
        {
        /* FIFO buffer empty flag clear */
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
            usb_creg_clr_sts_bemp(ip, pipe);
            /* bval control for transfer enable fifo 2 usb control */
            usb_creg_set_bval(ptr, pipemode);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
            usb_creg_clr_sts_bemp(ip, pipe);
            /* bval control for transfer enable fifo 2 usb control */
            usb_creg_set_bval(USB_NULL, pipemode);

#endif
            /* FIFO bufer empty interrupt enable */
            usb_creg_set_bempenb(ip, pipe);
        }
        else
        {
        /* update remaining transfer data size */
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
            g_usb_hstd_data_cnt[ip][pipe] -= g_usb_cstd_dma_size[ip][ch_no];
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
            g_usb_pstd_data_cnt[pipe] -= g_usb_cstd_dma_size[ip][ch_no];
#endif
        /* check transfer remaining data */
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
            if (0u == g_usb_hstd_data_cnt[ip][pipe])
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
                if (0u == g_usb_pstd_data_cnt[pipe])
#endif
                {
                    if (0 < last_trans_size)
                    {
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
                        /* FIFO buffer empty flag clear */
                        usb_creg_clr_sts_bemp(ptr->ip, pipe);
                        /* bval control for transfer enable fifo 2 usb control */
                        usb_creg_set_bval(ptr, pipemode);
                        /* FIFO bufer empty interrupt enable */
                        usb_creg_set_bempenb(ptr->ip, pipe);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)

                        /* FIFO buffer empty flag clear */
                        usb_creg_clr_sts_bemp(ip, pipe);

                        /* bval control for transfer enable fifo 2 usb control */
                        usb_creg_set_bval(USB_NULL, pipemode);

                        /* FIFO bufer empty interrupt enable */
                        usb_creg_set_bempenb(ip, pipe);
#endif
                    }
                    else
                    {
                        /* FIFO buffer empty flag clear */
                        usb_creg_clr_sts_bemp(ip, pipe);
                        /* check FIFO_EMPTY / INBUF bit */
                        if ((usb_creg_read_pipectr(ip, pipe) & USB_INBUFM) != USB_INBUFM)
                        {
                            L1_D_CacheWritebackFlushAll();
                    /* DMA transfer function end. call callback function */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
                            usb_hstd_data_end(ptr, pipe, (uint16_t)USB_DATA_NONE);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
                            usb_pstd_data_end(pipe, (uint16_t)USB_DATA_NONE);
#endif
                        }
                        else
                        {
                            /* FIFO bufer empty interrupt enable */
                            usb_creg_set_bempenb(ip, pipe);
                        }
                    }
                }
        }
} /* End of function usb_cstd_DmaxInt() */

/***********************************************************************************************************************
 Function Name   : usb_dma_get_dxfifo_ir_vect
 Description     : Get vector no. of USB DxFIFO
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     useport      : FIFO select
 Return value    : Vector no.
 ***********************************************************************************************************************/
uint16_t usb_dma_get_dxfifo_ir_vect(usb_utr_t* ptr, uint16_t use_port)
{
    uint16_t ip;
    uint16_t vect;
    uint16_t ch_no;

    if (USB_NULL != ptr)
    {
        ip = ptr->ip;
    }
    else
    {
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else
        ip = USB_IP1;
#endif
#endif /* ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI) */
    }

    /* ip+useport to ch_no */
    ch_no = usb_dma_ref_ch_no(ip, use_port);
    switch (ch_no)
    {
        case USB_CFG_CH0:

            vect = INTC_ID_DMAINT0;

            break;

        case USB_CFG_CH1:

            vect = INTC_ID_DMAINT1;

            break;

        case USB_CFG_CH2:

            vect = INTC_ID_DMAINT2;

            break;

        case USB_CFG_CH3:

            vect = INTC_ID_DMAINT3;

            break;

        default:
            break;
    }
    return vect;
} /* End of function usb_dma_get_dxfifo_ir_vect() */

/***********************************************************************************************************************
 Function Name   : usb_dma_stop_dxfifo
 Description     : DMA stop
 Arguments       : uint8_t      ip_type      : USB_IP0/USB_IP1
                 : uint16_t     use_port     : FIFO select
 Return value    : void
 ***********************************************************************************************************************/
void usb_dma_stop_dxfifo(uint8_t ip_type, uint16_t use_port)
{

#if USB_CFG_DMA == USB_CFG_ENABLE
    uint16_t ch_no;
    uint16_t ip;

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    ip = USB_IP0;
#else
    ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#else
    ip = ip_type;
#endif

    ch_no = usb_dma_ref_ch_no(ip, use_port);
    switch (ch_no)
    {
        case USB_CFG_CH0:

            usb_stop_dma0();

            break;

        case USB_CFG_CH1:

            usb_stop_dma1();

            break;

        case USB_CFG_CH2:

            usb_stop_dma2();

            break;

        case USB_CFG_CH3:

            usb_stop_dma3();

            break;

        default:
            break;
    }
#endif /* USB_CFG_DMA == USB_CFG_ENABLE */
} /* End of function usb_dma_stop_dxfifo() */

/***********************************************************************************************************************
 Function Name   : usb_dma_set_ch_no
 Description     : Set DMA channel no.
 Arguments       : uint16_t     ip_no        : USB module number (USB_IP0/USB_IP1)
                 : uint16_t     useport      : FIFO select
                 : uint8_t  dma_ch_no        : DMA channel no.
 Return value    : none
 ***********************************************************************************************************************/
void usb_dma_set_ch_no(uint16_t ip_no, uint16_t use_port, uint8_t dma_ch_no)
{
    uint16_t ip;

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    ip = USB_IP0;
#else
    ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#else
    ip = ip_no;
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

    g_usb_cstd_dma_ch[ip][use_port] = dma_ch_no; /* DMA ch no. table */
} /* End of function usb_dma_set_ch_no() */

/***********************************************************************************************************************
 Function Name   : usb_dma_ref_ch_no
 Description     : Get DMA channel no.
 Arguments       : uint16_t     ip_no        : USB module number (USB_IP0/USB_IP1)
                 : uint16_t     useport      : FIFO select
 Return value    : DMA channel no.
 ***********************************************************************************************************************/
uint8_t usb_dma_ref_ch_no(uint16_t ip_no, uint16_t use_port)
{
    uint16_t ip;

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    ip = USB_IP0;
#else
    ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#else
    ip = ip_no;
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

    return g_usb_cstd_dma_ch[ip][use_port]; /* DMA ch no. table */
} /* End of function usb_dma_ref_ch_no() */

/***********************************************************************************************************************
 Function Name   : usb_dma_ref_ch_no
 Description     : Get DMA channel no.
 Arguments       : uint16_t     ip_no        : USB module number (USB_IP0/USB_IP1)
                 : uint16_t     useport      : FIFO select
 Return value    : DMA channel no.
 ***********************************************************************************************************************/
uint8_t usb_dma_def_ch_no(uint16_t ip_no, uint16_t mode_txrx)
{
    uint16_t ip;

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    ip = USB_IP0;
#else
    ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#else
    ip = ip_no;
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

    return g_usb_dma_setting_ch[ip][mode_txrx];
} /* End of function usb_dma_def_ch_no() */

/***********************************************************************************************************************
 Function Name   : usb_dma_ip_ch_no2useport
 Description     : Get DMA channel no.
 Arguments       : uint16_t     ip_no        : USB module number (USB_IP0/USB_IP1)
                 : uint16_t     ch_no        : FIFO select
 Return value    : DMA channel no.
 ***********************************************************************************************************************/
uint16_t usb_dma_ip_ch_no2useport(uint16_t ip_no, uint16_t ch_no)
{
    uint16_t port_cnt;
    uint16_t ip;

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    ip = USB_IP0;
#else
    ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#else
    ip = ip_no;
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

    for (port_cnt = 0; port_cnt < USB_FIFO_ACCESS_NUM_MAX; port_cnt++)
    {
        if (g_usb_cstd_dma_ch[ip][port_cnt] == ch_no)
        {
            return port_cnt;
        }
    }
    return 0xffff;
} /* End of function usb_dma_ip_ch_no2useport() */

/***********************************************************************************************************************
 Function Name   : usb_dma_def_tx_ch_no2ip_no
 Description     : Get DMA channel no.
 Arguments       : uint16_t     ch_no        : FIFO select
 Return value    : DMA channel no.
 ***********************************************************************************************************************/
uint16_t usb_dma_def_tx_ch_no2ip_no(uint16_t ch_no)
{
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
    return USB_IP0;
#else
    return USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#else
    uint16_t ip_cnt;

    for (ip_cnt = 0; ip_cnt < USB_NUM_USBIP; ip_cnt++)
    {
        if (g_usb_dma_setting_ch[ip_cnt][USB_DMA_TX] == ch_no)
        {
            return ip_cnt;
        }
    }
    return 0xffff;
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
} /* End of function usb_dma_def_tx_ch_no2ip_no() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_dmaint0_handler
 Description     : DMA interrupt routine. Send message to PCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_dmaint0_handler(void)
{
    uint16_t useport;
    usb_utr_t utr;

    utr.ip = usb_dma_def_tx_ch_no2ip_no(USB_CFG_CH0);
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if (USB_CFG_USE_USBIP == USB_CFG_IP0)
    utr.ipp = (usb_regadr_t)&USB200;
#endif
#if (USB_CFG_USE_USBIP == USB_CFG_IP1)
    utr.ipp = (usb_regadr_t)&USB201;
#endif
#endif

    useport = usb_dma_ip_ch_no2useport(utr.ip, USB_CFG_CH0);

    usb_creg_clr_dreqe(utr.ip, useport); /* DMA Transfer request disable */
    usb_stop_dma0();                     /* Stop DMA,FIFO access */

    usb_cstd_DmaxInt(&utr, useport);
} /* End of function usb_cstd_dmaint0_handler() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_dmaint1_handler
 Description     : DMA interrupt routine. Send message to PCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_dmaint1_handler(void)
{
    uint16_t useport;
    usb_utr_t utr;

    utr.ip = usb_dma_def_tx_ch_no2ip_no(USB_CFG_CH1);
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if (USB_CFG_USE_USBIP == USB_CFG_IP0)
    utr.ipp = (usb_regadr_t)&USB200;
#endif
#if (USB_CFG_USE_USBIP == USB_CFG_IP1)
    utr.ipp = (usb_regadr_t)&USB201;
#endif
#endif
    useport = usb_dma_ip_ch_no2useport(utr.ip, USB_CFG_CH1);

    usb_creg_clr_dreqe(utr.ip, useport); /* DMA Transfer request disable */
    usb_stop_dma1();                     /* Stop DMA,FIFO access */

    usb_cstd_DmaxInt(&utr, useport);
} /* End of function usb_cstd_dmaint1_handler() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_dmaint2_handler
 Description     : DMA interrupt routine. Send message to PCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_dmaint2_handler(void)
{
    uint16_t useport;
    usb_utr_t utr;

    utr.ip = usb_dma_def_tx_ch_no2ip_no(USB_CFG_CH2);
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip);
#endif
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if (USB_CFG_USE_USBIP == USB_CFG_IP0)
    utr.ipp = (usb_regadr_t)&USB200;
#endif
#if (USB_CFG_USE_USBIP == USB_CFG_IP1)
    utr.ipp = (usb_regadr_t)&USB201;
#endif
#endif
    useport = usb_dma_ip_ch_no2useport(utr.ip, USB_CFG_CH2);

    usb_creg_clr_dreqe(utr.ip, useport); /* DMA Transfer request disable */
    usb_stop_dma2();                     /* Stop DMA,FIFO access */

    usb_cstd_DmaxInt(&utr, useport);
} /* End of function usb_cstd_dmaint2_handler() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_dmaint3_handler
 Description     : DMA interrupt routine. Send message to PCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_dmaint3_handler(void)
{
    uint16_t useport;
    usb_utr_t utr;

    utr.ip = usb_dma_def_tx_ch_no2ip_no(USB_CFG_CH3);
#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    utr.ipp = usb_hstd_get_usb_ip_adr(utr.ip);
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if (USB_CFG_USE_USBIP == USB_CFG_IP0)
    utr.ipp = (usb_regadr_t)&USB200;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#if (USB_CFG_USE_USBIP == USB_CFG_IP1)
    utr.ipp = (usb_regadr_t)&USB201;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP1 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

    useport = usb_dma_ip_ch_no2useport(utr.ip, USB_CFG_CH3);

    usb_creg_clr_dreqe(utr.ip, useport); /* DMA Transfer request disable */
    usb_stop_dma3();                     /* Stop DMA,FIFO access */

    usb_cstd_DmaxInt(&utr, useport);
} /* End of function usb_cstd_dmaint3_handler() */

/***********************************************************************************************************************
 Function Name   : usb_dma_get_n0tb
 Description     : Get DMA Current Transaction Byte reg B(CRTB).
 Arguments       : uint16_t     dma_ch       : DMA Channel no.
 Return value    : DMA Current Transaction Byte reg B(CRTB)
 ***********************************************************************************************************************/
uint16_t usb_dma_get_n0tb(uint16_t dma_ch)
{
    uint16_t t_crtb_val = 0u;

#if USB_CFG_DMA == USB_CFG_ENABLE

    switch (dma_ch)
    {
        case USB_CFG_CH0:

            while (DMAC0.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC0.N0TB_n;

            break;

        case USB_CFG_CH1:

            while (DMAC1.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC1.N0TB_n;

            break;

        case USB_CFG_CH2:

            while (DMAC2.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC2.N0TB_n;

            break;

        case USB_CFG_CH3:

            while (DMAC3.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC3.N0TB_n;

            break;

        default:
            break;
    }
#endif /* USB_CFG_DMA == USB_CFG_ENABLE */

    return t_crtb_val;
} /* End of function usb_dma_get_n0tb() */

/***********************************************************************************************************************
 Function Name   : usb_dma_get_crtb
 Description     : Get DMA Current Transaction Byte reg B(CRTB).
 Arguments       : uint16_t     dma_ch       : DMA Channel no.
 Return value    : DMA Current Transaction Byte reg B(CRTB)
 ***********************************************************************************************************************/
uint16_t usb_dma_get_crtb(uint16_t dma_ch)
{
    uint16_t t_crtb_val = 0u;

#if USB_CFG_DMA == USB_CFG_ENABLE

    switch (dma_ch)
    {
        case USB_CFG_CH0:

            while (DMAC0.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC0.CRTB_n;

            break;

        case USB_CFG_CH1:

            while (DMAC1.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC1.CRTB_n;

            break;

        case USB_CFG_CH2:

            while (DMAC2.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC2.CRTB_n;

            break;

        case USB_CFG_CH3:

            while (DMAC3.CHSTAT_n & 0x0001)
                ;
            t_crtb_val = DMAC3.CRTB_n;

            break;

        default:
            break;
    }

#endif /* USB_CFG_DMA == USB_CFG_ENABLE */

    return t_crtb_val;
} /* End of function usb_dma_get_crtb() */

/***********************************************************************************************************************
 Function Name   : usb_stop_dma0
 Description     : DMA stop
 Arguments       : void
 Return value    : void
 ***********************************************************************************************************************/
void usb_stop_dma0(void)
{
    usb_disable_dmaInt0();

    DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_CLREN; /* Set CLREN bit */

    /* Wait to stop DMA  */
    while ((DMAC0.CHSTAT_n & USB_DMA_CHSTAT_TACT) == USB_DMA_CHSTAT_TACT)
        ;

    DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
} /* End of function usb_stop_dma0() */

/***********************************************************************************************************************
 Function Name   : usb_stop_dma1
 Description     : DMA stop
 Arguments       : void
 Return value    : void
 ***********************************************************************************************************************/
void usb_stop_dma1(void)
{
    usb_disable_dmaInt1();

    DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_CLREN; /* Set CLREN bit */

    /* Wait to stop DMA  */
    while ((DMAC1.CHSTAT_n & USB_DMA_CHSTAT_TACT) == USB_DMA_CHSTAT_TACT)
        ;

    DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
} /* End of function usb_stop_dma1() */

/***********************************************************************************************************************
 Function Name   : usb_stop_dma2
 Description     : DMA stop
 Arguments       : void
 Return value    : void
 ***********************************************************************************************************************/
void usb_stop_dma2(void)
{
    usb_disable_dmaInt2();

    DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_CLREN; /* Set CLREN bit */

    /* Wait to stop DMA  */
    while ((DMAC2.CHSTAT_n & USB_DMA_CHSTAT_TACT) == USB_DMA_CHSTAT_TACT)
        ;

    DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
} /* End of function usb_stop_dma2() */

/***********************************************************************************************************************
 Function Name   : usb_stop_dma3
 Description     : DMA stop
 Arguments       : void
 Return value    : void
 ***********************************************************************************************************************/
void usb_stop_dma3(void)
{
    usb_disable_dmaInt3();

    DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_CLREN; /* Set CLREN bit */

    /* Wait to stop DMA  */
    while ((DMAC3.CHSTAT_n & USB_DMA_CHSTAT_TACT) == USB_DMA_CHSTAT_TACT)
        ;

    DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
} /* End of function usb_stop_dma3() */

/***********************************************************************************************************************
 Function Name   : usb_enable_dmaIntX
 Description     : DTC(D0FIFO) interrupt enable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_enable_dmaIntX(uint16_t ch_no)
{
    switch (ch_no)
    {
        case USB_CFG_CH0:

            R_INTC_Enable(INTC_ID_DMAINT0);

            break;

        case USB_CFG_CH1:

            R_INTC_Enable(INTC_ID_DMAINT1);

            break;

        case USB_CFG_CH2:

            R_INTC_Enable(INTC_ID_DMAINT2);

            break;

        case USB_CFG_CH3:

            R_INTC_Enable(INTC_ID_DMAINT3);

            break;

        default:
            break;
    }
} /* End of function usb_enable_dmaIntX() */

/***********************************************************************************************************************
 Function Name   : usb_enable_dmaInt0
 Description     : DMA interrupt enable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_enable_dmaInt0(void)
{
    R_INTC_Enable(INTC_ID_DMAINT0);
} /* End of function usb_enable_dmaInt0() */

/***********************************************************************************************************************
 Function Name   : usb_enable_dmaInt1
 Description     : DMA interrupt enable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_enable_dmaInt1(void)
{
    R_INTC_Enable(INTC_ID_DMAINT1);
} /* End of function usb_enable_dmaInt1() */

/***********************************************************************************************************************
 Function Name   : usb_enable_dmaInt2
 Description     : DMA interrupt enable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_enable_dmaInt2(void)
{
    R_INTC_Enable(INTC_ID_DMAINT2);
} /* End of function usb_enable_dmaInt2() */

/***********************************************************************************************************************
 Function Name   : usb_enable_dmaInt3
 Description     : DMA interrupt enable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_enable_dmaInt3(void)
{
    R_INTC_Enable(INTC_ID_DMAINT3);
} /* End of function usb_enable_dmaInt3() */

/***********************************************************************************************************************
 Function Name   : usb_disable_dmaInt0
 Description     : DMA interrupt disable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_disable_dmaInt0(void)
{
    R_INTC_Disable(INTC_ID_DMAINT0);
} /* End of function usb_disable_dmaInt0() */

/***********************************************************************************************************************
 Function Name   : usb_disable_dmaInt1
 Description     : DMA interrupt disable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_disable_dmaInt1(void)
{
    R_INTC_Disable(INTC_ID_DMAINT1);
} /* End of function usb_disable_dmaInt1() */

/***********************************************************************************************************************
 Function Name   : usb_disable_dmaInt2
 Description     : DMA interrupt disable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_disable_dmaInt2(void)
{
    R_INTC_Disable(INTC_ID_DMAINT2);
} /* End of function usb_disable_dmaInt2() */

/***********************************************************************************************************************
 Function Name   : usb_disable_dmaInt3
 Description     : DMA interrupt disable
 Arguments       : void
 Return value    : none
 ***********************************************************************************************************************/
void usb_disable_dmaInt3(void)
{
    R_INTC_Disable(INTC_ID_DMAINT3);
} /* End of function usb_disable_dmaInt3() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_DXfifo2BufStartDma
 Description     : Start transfer using DMA. If transfer size is 0, clear DMA.
 Arguments       : uint16_t     pipe         : Pipe number
                 : uint16_t     useport      : FIFO select
                 : uint32_t     length       : Data length
                 : uint32_t     dest_addr    : destination address
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_DXfifo2BufStartDma(usb_utr_t* ptr, uint16_t pipe, uint16_t useport, uint32_t length, uint32_t dest_addr)
{
    uint16_t mxps;
    uint32_t dma_size;
    uint16_t bcfg_setting;
    uint32_t source_adr;
    uint32_t transfer_size;
    uint16_t ch_no;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    else
    {
        ip = ptr->ip;
    }

    ch_no = usb_dma_def_ch_no(ip, USB_DMA_RX);

    dma_size      = g_usb_cstd_dma_size[ip][ch_no];
    transfer_size = length;

#if USE_DMA32
    bcfg_setting = USB_DFACC_32;
    source_adr   = usb_cstd_GetDXfifoYAdr(ip, 32u, useport);
#else
    bcfg_setting = USB_DFACC_CS;
    source_adr   = usb_cstd_GetDXfifoYAdr(ip, 1u, useport);
#endif

    /* Data size check */
    if (0u != dma_size)
    {
        /* DMA access Buffer to FIFO start */
        usb_creg_write_dxfbcfg(ip, useport, bcfg_setting);

        usb_cpu_dxfifo2buf_start_dmaX(ptr, ch_no, source_adr, useport, dest_addr, transfer_size);

        /* Changes the FIFO port by the pipe. */
        usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);

        /* Max Packet Size */
        mxps = usb_cstd_get_maxpacket_size(ptr, pipe);

        /* Set Transaction counter */
        usb_cstd_set_transaction_counter(ptr, pipe, (uint16_t)(((length - (uint32_t)1u) / mxps) + (uint32_t)1u));

        /* Set BUF */
        usb_cstd_set_buf(ptr, pipe);

        /* Enable Ready Interrupt */
        usb_creg_set_brdyenb(ip, pipe);

        /* Enable Not Ready Interrupt */
        // usb_cstd_nrdy_enable(ptr, pipe);
        //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
        //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in
        //  2019).

        /* Read (FIFO -> MEMORY) : USB register set */
        /* DMA buffer clear mode & MBW set */
        /* usb fifo set automatic clear mode  */
        usb_creg_clr_dclrm(ip, useport);
#if USE_DMA32
        usb_creg_set_mbw(ip, useport, USB_MBW_32);
#else
        usb_creg_set_mbw(ip, useport, USB_MBW_8);
#endif
        /* usb fifo set automatic clear mode  */
        usb_creg_clr_dclrm(ip, useport);

        /* Set DREQ enable */
        usb_creg_set_dreqe(ip, useport);
    }
    else
    {
        /* Changes the FIFO port by the pipe. */
        usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);

        /* DMA buffer clear mode set */
        usb_creg_set_dclrm(ip, useport);

        /* Set BUF */
        usb_cstd_set_buf(ptr, pipe);

        /* Enable Ready Interrupt */
        usb_creg_set_brdyenb(ip, pipe);

        /* Enable Not Ready Interrupt */
        // usb_cstd_nrdy_enable(ptr, pipe);
        //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was
        //  causing freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in
        //  2019).
    }
} /* End of function usb_cstd_DXfifo2BufStartDma() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dxfifo2buf_start_dma0
 Description     : FIFO to Buffer data read DMA start
 Arguments       : uint32_t     SourceAddr      : Source address
                 : uint16_t     useport         : FIFO Access mode
                 : uint32_t     destAddr        : Destination address
                 : uint32_t     transfer_size   : Transfer size
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_dxfifo2buf_start_dma0(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size)
{
    uint32_t chcfg_data;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    else
    {
        ip = ptr->ip;
    }

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC0.CHSTAT_n & 0x05) != 0u)
    {
        DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC0.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
    /* stop DMARS */
    DMAC.DMARS0 &= 0xffffff00;

#if USE_DMA32
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_0_8 | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_256;
#else
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_0_8 | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_8;
#endif

    DMAC07.DCTRL_0_7 = 0x00000000;
    DMAC0.N0SA_n     = SourceAddr;
    DMAC0.N0DA_n     = destAddr;
    DMAC0.N0TB_n     = transfer_size;
    DMAC0.CHCFG_n    = chcfg_data;
    DMAC0.CHITVL_n   = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x00000083;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x00000087;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x0000008B;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x0000008F;
        }
    }

    DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_dxfifo2buf_start_dma0() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dxfifo2buf_start_dma1
 Description     : FIFO to Buffer data read DMA start
 Arguments       : uint32_t     SourceAddr      : Source address
                 : uint16_t     useport         : FIFO Access mode
                 : uint32_t     destAddr        : Destination address
                 : uint32_t     transfer_size   : Transfer size
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_dxfifo2buf_start_dma1(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size)
{
    uint32_t chcfg_data;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    else
    {
        ip = ptr->ip;
    }

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC1.CHSTAT_n & 0x05) != 0u)
    {
        DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC1.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }

    /* stop DMARS */
    DMAC.DMARS0 &= 0xff00ffff;

#if USE_DMA32
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_1_9 | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_256;
#else
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_1_9 | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_8;
#endif

    DMAC07.DCTRL_0_7 = 0x00000000;
    DMAC1.N0SA_n     = SourceAddr;
    DMAC1.N0DA_n     = destAddr;
    DMAC1.N0TB_n     = transfer_size;
    DMAC1.CHCFG_n    = chcfg_data;
    DMAC1.CHITVL_n   = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x00830000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x00870000;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x008B0000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x008F0000;
        }
    }
    DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_dxfifo2buf_start_dma1() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dxfifo2buf_start_dma1
 Description     : FIFO to Buffer data read DMA start
 Arguments       : uint32_t     SourceAddr      : Source address
                 : uint16_t     useport         : FIFO Access mode
                 : uint32_t     destAddr        : Destination address
                 : uint32_t     transfer_size   : Transfer size
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_dxfifo2buf_start_dma2(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size)
{
    uint32_t chcfg_data;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    else
    {
        ip = ptr->ip;
    }

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC2.CHSTAT_n & 0x05) != 0u)
    {
        DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC2.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }

    /* stop DMARS */
    DMAC.DMARS1 &= 0xffffff00;

#if USE_DMA32
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_2_10 | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_256;
#else
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_2_10 | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_8;
#endif

    DMAC07.DCTRL_0_7 = 0x00000000;
    DMAC2.N0SA_n     = SourceAddr;
    DMAC2.N0DA_n     = destAddr;
    DMAC2.N0TB_n     = transfer_size;
    DMAC2.CHCFG_n    = chcfg_data;
    DMAC2.CHITVL_n   = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x00000083;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x00000087;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x0000008B;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x0000008F;
        }
    }

    DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_dxfifo2buf_start_dma2() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dxfifo2buf_start_dma1
 Description     : FIFO to Buffer data read DMA start
 Arguments       : uint32_t     SourceAddr      : Source address
                 : uint16_t     useport         : FIFO Access mode
                 : uint32_t     destAddr        : Destination address
                 : uint32_t     transfer_size   : Transfer size
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_dxfifo2buf_start_dma3(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size)
{
    uint32_t chcfg_data;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */
    }
    else
    {
        ip = ptr->ip;
    }

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC3.CHSTAT_n & 0x05) != 0u)
    {
        DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC3.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }

    /* stop DMARS */
    DMAC.DMARS1 &= 0xff00ffff;

#if USE_DMA32
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_3_11 | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_256;
#else
    chcfg_data = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_DEM | USB_DMA_CHCFG_HIEN
                 | USB_DMA_CHCFG_SEL_3_11 | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_SAD | USB_DMA_CHCFG_SDS_8;
#endif

    DMAC07.DCTRL_0_7 = 0x00000000;
    DMAC3.N0SA_n     = SourceAddr;
    DMAC3.N0DA_n     = destAddr;
    DMAC3.N0TB_n     = transfer_size;
    DMAC3.CHCFG_n    = chcfg_data;
    DMAC3.CHITVL_n   = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x00830000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x00870000;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x008B0000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x008F0000;
        }
    }
    DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_dxfifo2buf_start_dma3() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_dxfifo2buf_start_dmaX
 Description     : FIFO to Buffer data read DMA start
 Arguments       : uint32_t     SourceAddr      : Source address
                 : uint16_t     useport         : FIFO Access mode
                 : uint32_t     destAddr        : Destination address
                 : uint32_t     transfer_size   : Transfer size
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_dxfifo2buf_start_dmaX(
    usb_utr_t* ptr, uint16_t ch_no, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size)
{
    switch (ch_no)
    {
        case USB_CFG_CH0:

            usb_cpu_dxfifo2buf_start_dma0(ptr, SourceAddr, useport, destAddr, transfer_size);

            break;

        case USB_CFG_CH1:

            usb_cpu_dxfifo2buf_start_dma1(ptr, SourceAddr, useport, destAddr, transfer_size);

            break;

        case USB_CFG_CH2:

            usb_cpu_dxfifo2buf_start_dma2(ptr, SourceAddr, useport, destAddr, transfer_size);

            break;

        case USB_CFG_CH3:

            usb_cpu_dxfifo2buf_start_dma3(ptr, SourceAddr, useport, destAddr, transfer_size);

            break;

        default:
            break;
    }
} /* End of function usb_cpu_dxfifo2buf_start_dmaX() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_buf2dxfifo_start_dma0
 Description     : Buffer to FIFO data write DMA start
 Arguments       : uint32_t     DistAdr      : Destination address
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_buf2dxfifo_start_dma0(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size)
{
    useport = usb_dma_ip_ch_no2useport(ip, USB_CFG_CH0);

    /* add cache update */
    L1_D_CacheWritebackFlushAll();

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC0.CHSTAT_n & 0x05) != 0u)
    {
        DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC0.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }

    /* stop DMARS */
    DMAC.DMARS0 &= 0xffffff00;

    DMAC07.DCTRL_0_7 = 0x00000000;

    DMAC0.N0SA_n = src_adr;
    DMAC0.N0TB_n = transfer_size;
    DMAC0.N0DA_n = usb_cstd_GetDXfifoYAdr(ip, trans_block_size, useport);

    if (1u == trans_block_size)
    {
        DMAC0.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_0_8
                        | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_8 | USB_DMA_CHCFG_REQD;
    }
    else if (4u == trans_block_size)
    {
        DMAC0.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_0_8
                        | USB_DMA_CHCFG_DDS_32 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_32 | USB_DMA_CHCFG_REQD;
    }
    else if (32u == trans_block_size)
    {
        DMAC0.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_0_8
                        | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_256 | USB_DMA_CHCFG_REQD;
    }

    DMAC0.CHITVL_n = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x00000083;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x00000087;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x0000008B;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x0000008F;
        }
    }

    DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_buf2dxfifo_start_dma0() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_buf2dxfifo_start_dma1
 Description     : Buffer to FIFO data write DMA start
 Arguments       : uint32_t     DistAdr      : Destination address
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_buf2dxfifo_start_dma1(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size)
{
    useport = usb_dma_ip_ch_no2useport(ip, USB_CFG_CH1);

    /* add cache update */
    L1_D_CacheWritebackFlushAll();

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC1.CHSTAT_n & 0x05) != 0u)
    {
        DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC1.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
    /* stop DMARS */
    DMAC.DMARS0 &= 0xff00ffff;

    DMAC07.DCTRL_0_7 = 0x00000000;

    DMAC1.N0SA_n = src_adr;
    DMAC1.N0TB_n = transfer_size;
    DMAC1.N0DA_n = usb_cstd_GetDXfifoYAdr(ip, trans_block_size, useport);

    if (1u == trans_block_size)
    {
        DMAC1.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_1_9
                        | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_8 | USB_DMA_CHCFG_REQD;
    }
    else if (4u == trans_block_size)
    {
        DMAC1.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_1_9
                        | USB_DMA_CHCFG_DDS_32 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_32 | USB_DMA_CHCFG_REQD;
    }
    else if (32u == trans_block_size)
    {
        DMAC1.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_1_9
                        | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_256 | USB_DMA_CHCFG_REQD;
    }

    DMAC1.CHITVL_n = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x00830000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x00870000;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS0 |= 0x008B0000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS0 |= 0x008F0000;
        }
    }

    DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_buf2dxfifo_start_dma1() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_buf2dxfifo_start_dma2
 Description     : Buffer to FIFO data write DMA start
 Arguments       : uint32_t     DistAdr      : Destination address
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_buf2d1fifo_start_dma2(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size)
{
    useport = usb_dma_ip_ch_no2useport(ip, USB_CFG_CH2);

    /* add cache update */
    L1_D_CacheWritebackFlushAll();

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC2.CHSTAT_n & 0x05) != 0u)
    {
        DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC2.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
    /* stop DMARS */
    DMAC.DMARS1 &= 0xffffff00;

    DMAC07.DCTRL_0_7 = 0x00000000;

    DMAC2.N0SA_n = src_adr;
    DMAC2.N0TB_n = transfer_size;
    DMAC2.N0DA_n = usb_cstd_GetDXfifoYAdr(ip, trans_block_size, useport);

    if (1u == trans_block_size)
    {
        DMAC2.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_2_10
                        | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_8 | USB_DMA_CHCFG_REQD;
    }
    else if (4u == trans_block_size)
    {
        DMAC2.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_2_10
                        | USB_DMA_CHCFG_DDS_32 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_32 | USB_DMA_CHCFG_REQD;
    }
    else if (32u == trans_block_size)
    {
        DMAC2.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_2_10
                        | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_256 | USB_DMA_CHCFG_REQD;
    }

    DMAC2.CHITVL_n = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x00000083;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x00000087;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x0000008B;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x0000008F;
        }
    }

    DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_buf2d1fifo_start_dma2() */

/***********************************************************************************************************************
 Function Name   : usb_cpu_buf2dxfifo_start_dma3
 Description     : Buffer to FIFO data write DMA start
 Arguments       : uint32_t     DistAdr      : Destination address
 Return value    : void
 ***********************************************************************************************************************/
void usb_cpu_buf2d1fifo_start_dma3(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size)
{
    useport = usb_dma_ip_ch_no2useport(ip, USB_CFG_CH3);

    /* add cache update */
    L1_D_CacheWritebackFlushAll();

    /* Wait for RZA1 check stasus register Channel Status Register EN==0 and TACT==0 */
    if ((DMAC3.CHSTAT_n & 0x05) != 0u)
    {
        DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC3.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
    /* stop DMARS */
    DMAC.DMARS1 &= 0xff00ffff;

    DMAC07.DCTRL_0_7 = 0x00000000;

    DMAC3.N0SA_n = src_adr;
    DMAC3.N0TB_n = transfer_size;
    DMAC3.N0DA_n = usb_cstd_GetDXfifoYAdr(ip, trans_block_size, useport);

    if (1u == trans_block_size)
    {
        DMAC3.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_3_11
                        | USB_DMA_CHCFG_DDS_8 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_8 | USB_DMA_CHCFG_REQD;
    }
    else if (4u == trans_block_size)
    {
        DMAC3.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_3_11
                        | USB_DMA_CHCFG_DDS_32 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_32 | USB_DMA_CHCFG_REQD;
    }
    else if (32u == trans_block_size)
    {
        DMAC3.CHCFG_n = USB_DMA_CHCFG_AM_BCM | USB_DMA_CHCFG_LVL | USB_DMA_CHCFG_HIEN | USB_DMA_CHCFG_SEL_3_11
                        | USB_DMA_CHCFG_DDS_256 | USB_DMA_CHCFG_DAD | USB_DMA_CHCFG_SDS_256 | USB_DMA_CHCFG_REQD;
    }

    DMAC3.CHITVL_n = 0x00000000;

    if (USB_CFG_IP0 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x00830000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x00870000;
        }
    }
    if (USB_CFG_IP1 == ip)
    {
        if (USB_D0DMA == useport)
        {
            DMAC.DMARS1 |= 0x008B0000;
        }
        if (USB_D1DMA == useport)
        {
            DMAC.DMARS1 |= 0x008F0000;
        }
    }

    DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_SWRST;
    DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_SETEN;
} /* End of function usb_cpu_buf2d1fifo_start_dma3() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_Buf2Fifo_DMAX
 Description     : Start transfer using DMA0. accsess size 32bytes.
 Arguments       : uint32_t     src                 : transfer data pointer
                 : uint32_t     data_size           : transfer data size
                 : uint16_t     pipe                : Pipe nr.
                 : uint8_t      trans_block_size    : DMA trans block size (1, 4, 32byte)
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_Buf2Fifo_DMAX(uint16_t ch_no, usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size,
    uint8_t pipe, uint8_t trans_block_size)
{
    switch (ch_no)
    {
        case USB_CFG_CH0:

            usb_cstd_Buf2Fifo_DMA0(ptr, useport, src, data_size, pipe, trans_block_size);

            break;

        case USB_CFG_CH1:

            usb_cstd_Buf2Fifo_DMA1(ptr, useport, src, data_size, pipe, trans_block_size);

            break;

        case USB_CFG_CH2:

            usb_cstd_Buf2Fifo_DMA2(ptr, useport, src, data_size, pipe, trans_block_size);

            break;

        case USB_CFG_CH3:

            usb_cstd_Buf2Fifo_DMA3(ptr, useport, src, data_size, pipe, trans_block_size);

            break;

        default:
            break;
    }
} /* End of function usb_cstd_Buf2Fifo_DMAX() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_Buf2Fifo_DMA0
 Description     : Start transfer using DMA0. accsess size 32bytes.
 Arguments       : uint32_t     src                 : transfer data pointer
                 : uint32_t     data_size           : transfer data size
                 : uint16_t     pipe                : Pipe nr.
                 : uint8_t      trans_block_size    : DMA trans block size (1, 4, 32byte)
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_Buf2Fifo_DMA0(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size)
{
    uint16_t bcfg_setting;
    uint16_t mbw_setting;
    uint16_t ch_no;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif
    }
    else
    {
        ip = ptr->ip;
    }

    ch_no = USB_CFG_CH0;

    disable_dmax(ch_no);

    /* D0FIFO interrupt enable */
    usb_disable_dmaInt0();

    /* NotReady      Int Disable */
    hw_usb_clear_nrdyenb(ptr, pipe);

    /* Empty/SizeErr Int Disable */
    hw_usb_clear_bempenb(ptr, pipe);

    if (32u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_32;
        mbw_setting  = USB_MBW_32;
    }
    else if (4u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_32;
    }
    else /* trans_block_size == 1 */
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_8;
    }

    /* bass control setting */
    usb_creg_clr_dreqe(ip, useport);

    /* bass control setting */
    usb_creg_write_dxfbcfg(ip, useport, bcfg_setting);

    usb_creg_clr_dclrm(ip, useport);

    /* Change MBW setting */
    usb_creg_set_mbw(ip, useport, mbw_setting);

    /* SetPID */
    usb_cstd_set_buf(ptr, pipe);

    /* dma trans setting Divisible by FIFO buffer size  */
    usb_cpu_buf2dxfifo_start_dma0(useport, src, ip, data_size, trans_block_size);

    /* Changes the FIFO port by the pipe. */
    usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);

    /* CPU access Buffer to FIFO start */
    /* D0FIFO interrupt enable */
    usb_enable_dmaIntX(ch_no);

    /* Set DREQ enable */
    usb_creg_set_dreqe(ip, useport);
} /* End of function usb_cstd_Buf2Fifo_DMA0() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_Buf2Fifo_DMA1
 Description     : Start transfer using DMA0. accsess size 32bytes.
 Arguments       : uint32_t     src                 : transfer data pointer
                 : uint32_t     data_size           : transfer data size
                 : uint16_t     pipe                : Pipe nr.
                 : uint8_t      trans_block_size    : DMA trans block size (1, 32byte)
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_Buf2Fifo_DMA1(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size)
{
    uint16_t bcfg_setting;
    uint16_t mbw_setting;
    uint16_t ch_no;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif
    }
    else
    {
        ip = ptr->ip;
    }

    ch_no = USB_CFG_CH1;

    disable_dmax(ch_no);

    if (32u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_32;
        mbw_setting  = USB_MBW_32;
    }
    else if (1u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_8;
    }
    else if (4u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_32;
    }

    /* disable dreqe bit  */
    usb_creg_clr_dreqe(ip, useport);

    usb_creg_write_dxfbcfg(ip, useport, bcfg_setting);

    usb_creg_clr_dclrm(ip, useport);

    /* Change MBW setting */
    usb_creg_set_mbw(ip, useport, mbw_setting);

    /* dma trans setting Divisible by FIFO buffer size  */
    usb_cpu_buf2dxfifo_start_dma1(useport, src, ip, data_size, trans_block_size);

    /* Changes the FIFO port by the pipe. */
    usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);

    /* Enable Not Ready Interrupt */
    // usb_cstd_nrdy_enable(ptr, pipe);
    //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was causing
    //  freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in 2019).

    /* D0FIFO interrupt enable */
    usb_enable_dmaIntX(ch_no);

    /* Set DREQ enable */
    usb_creg_set_dreqe(ip, useport);
} /* End of function usb_cstd_Buf2Fifo_DMA1() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_Buf2Fifo_DMA1
 Description     : Start transfer using DMA0. accsess size 32bytes.
 Arguments       : uint32_t     src                 : transfer data pointer
                 : uint32_t     data_size           : transfer data size
                 : uint16_t     pipe                : Pipe nr.
                 : uint8_t      trans_block_size    : DMA trans block size (1, 32byte)
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_Buf2Fifo_DMA2(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size)
{
    uint16_t bcfg_setting;
    uint16_t mbw_setting;
    uint16_t ch_no;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif
    }
    else
    {
        ip = ptr->ip;
    }

    ch_no = USB_CFG_CH2;

    disable_dmax(ch_no);

    if (32u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_32;
        mbw_setting  = USB_MBW_32;
    }
    else if (1u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_8;
    }
    else if (4u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_32;
    }

    /* disable dreqe bit  */
    usb_creg_clr_dreqe(ip, useport);

    usb_creg_write_dxfbcfg(ip, useport, bcfg_setting);

    usb_creg_clr_dclrm(ip, useport);

    /* Change MBW setting */
    usb_creg_set_mbw(ip, useport, mbw_setting);

    /* dma trans setting Divisible by FIFO buffer size  */
    usb_cpu_buf2d1fifo_start_dma2(useport, src, ip, data_size, trans_block_size);

    /* Changes the FIFO port by the pipe. */
    usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);

    /* Enable Not Ready Interrupt */
    // usb_cstd_nrdy_enable(ptr, pipe);
    //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was causing
    //  freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in 2019).

    /* D0FIFO interrupt enable */
    usb_enable_dmaIntX(ch_no);

    /* Set DREQ enable */
    usb_creg_set_dreqe(ip, useport);
} /* End of function usb_cstd_Buf2Fifo_DMA2() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_Buf2Fifo_DMA3
 Description     : Start transfer using DMA0. accsess size 32bytes.
 Arguments       : uint32_t     src                 : transfer data pointer
                 : uint32_t     data_size           : transfer data size
                 : uint16_t     pipe                : Pipe nr.
                 : uint8_t      trans_block_size    : DMA trans block size (1, 32byte)
 Return value    : none
 ***********************************************************************************************************************/
void usb_cstd_Buf2Fifo_DMA3(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size)
{
    uint16_t bcfg_setting;
    uint16_t mbw_setting;
    uint16_t ch_no;
    uint16_t ip;

    if (USB_NULL == ptr)
    {
        /* if USB_PERI ip setting define data */
#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        ip = USB_IP0;
#else  /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
        ip = USB_IP1;
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */
#endif
    }
    else
    {
        ip = ptr->ip;
    }

    ch_no = USB_CFG_CH3;

    disable_dmax(ch_no);

    if (32u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_32;
        mbw_setting  = USB_MBW_32;
    }
    else if (1u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_8;
    }
    else if (4u == trans_block_size)
    {
        bcfg_setting = USB_DFACC_CS;
        mbw_setting  = USB_MBW_32;
    }

    /* disable dreqe bit  */
    usb_creg_clr_dreqe(ip, useport);

    usb_creg_write_dxfbcfg(ip, useport, bcfg_setting);

    usb_creg_clr_dclrm(ip, useport);

    /* Change MBW setting */
    usb_creg_set_mbw(ip, useport, mbw_setting);

    /* dma trans setting Divisible by FIFO buffer size  */
    usb_cpu_buf2d1fifo_start_dma3(useport, src, ip, data_size, trans_block_size);

    /* Changes the FIFO port by the pipe. */
    usb_cstd_chg_curpipe(ptr, pipe, useport, USB_FALSE);

    /* Enable Not Ready Interrupt */
    // usb_cstd_nrdy_enable(ptr, pipe);
    //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling them at all was causing
    //  freezes (or was it UART / SD lockups?), right since we first added this "new" (2016) USB driver (in 2019).

    /* D0FIFO interrupt enable */
    usb_enable_dmaIntX(ch_no);

    /* Set DREQ enable */
    usb_creg_set_dreqe(ip, useport);
} /* End of function usb_cstd_Buf2Fifo_DMA3() */

/* USB IP settings */
/***********************************************************************************************************************
 Function Name   : usb_cstd_GetDxfifoYAdr
 Description     : Get 32 bits of used channel's D0FIFO register content.
 Arguments       : uint16_t     ip           :
                 : uint16_t     trans_size   :
                 : uint16_t     pipemode     :
 Return          : Address of D0FIFO
 ***********************************************************************************************************************/
uint32_t usb_cstd_GetDXfifoYAdr(uint16_t ip, uint16_t trans_size, uint16_t pipemode)
{
    uint32_t ret_address;

    ret_address = USB_NULL;
    switch (trans_size)
    {
        case 1u:

            if (USB_IP0 == ip)
            {
                if (USB_D0DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB200.D0FIFO.UINT8[3]);
                }
                if (USB_D1DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB200.D1FIFO.UINT8[3]);
                }
            }
            if (USB_IP1 == ip)
            {
                if (USB_D0DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB201.D0FIFO.UINT8[3]);
                }
                if (USB_D1DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB201.D1FIFO.UINT8[3]);
                }
            }

            break;

        case 4u:

            if (USB_IP0 == ip)
            {
                if (USB_D0DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB200.D0FIFO);
                }
                if (USB_D1DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB200.D1FIFO);
                }
            }
            if (USB_IP1 == ip)
            {
                if (USB_D0DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB201.D0FIFO);
                }
                if (USB_D1DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB201.D1FIFO);
                }
            }

            break;

        case 32u:

            if (USB_IP0 == ip)
            {
                if (USB_D0DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB200.D0FIFOB0);
                }
                if (USB_D1DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB200.D1FIFOB0);
                }
            }
            if (USB_IP1 == ip)
            {
                if (USB_D0DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB201.D0FIFOB0);
                }
                if (USB_D1DMA == pipemode)
                {
                    ret_address = (uint32_t)&(USB201.D1FIFOB0);
                }
            }

            break;

        default:
            break;
    }
    return ret_address;
} /* End of function usb_cstd_GetDXfifoYAdr() */

/*********************/
/*  D0FBCFG, D1FBCFG */
/*********************/

/***********************************************************************************************************************
 Function Name   : usb_creg_write_dxfbcfg
 Description     : Write d0fbcfg register.
 Arguments       : uint16_t     ip           :
                 : uint16_t     pipemode     :
                 : uint16_t     data         : writing data
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_write_dxfbcfg(uint16_t ip, uint16_t pipemode, uint16_t data)
{
    if (USB_IP0 == ip)
    {
        if (USB_D0DMA == pipemode)
        {
            USB200.D0FBCFG = data;
        }
        if (USB_D1DMA == pipemode)
        {
            USB200.D1FBCFG = data;
        }
    }
    if (USB_IP1 == ip)
    {
        if (USB_D0DMA == pipemode)
        {
            USB201.D0FBCFG = data;
        }
        if (USB_D1DMA == pipemode)
        {
            USB201.D1FBCFG = data;
        }
    }
} /* End of function usb_creg_write_dxfbcfg() */

/**********************************/
/* CFIFOSEL, D0FIFOSEL, D1FIFOSEL */
/**********************************/
/* FIFO Port Select Register */

/***********************************************************************************************************************
 Function Name   : usb_creg_get_fifosel_adr
 Description     : Returns the *address* of the FIFOSEL register corresponding to
                 : specified PIPEMODE.
 Arguments       : uint16_t     ip           : ip port no (port0 = 0, ...)
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : none
 ***********************************************************************************************************************/
static void* usb_creg_get_fifosel_adr(uint16_t ip, uint16_t pipemode)
{
    void* reg_p = (void*)USB_NULL;

    switch (pipemode)
    {
        case USB_CUSE:

            if (USB_IP0 == ip)
            {
                reg_p = (void*)&(USB200.CFIFOSEL);
            }
            if (USB_IP1 == ip)
            {
                reg_p = (void*)&(USB201.CFIFOSEL);
            }

            break;

#if USB_DMA_PP == USB_DMA_USE_PP
        case USB_D0DMA:

            if (USB_IP0 == ip)
            {
                reg_p = (void*)&(USB200.D0FIFOSEL);
            }
            if (USB_IP1 == ip)
            {
                reg_p = (void*)&(USB201.D0FIFOSEL);
            }

            break;

        case USB_D1DMA:

            if (USB_IP0 == ip)
            {
                reg_p = (void*)&(USB200.D1FIFOSEL);
            }
            if (USB_IP1 == ip)
            {
                reg_p = (void*)&(USB201.D1FIFOSEL);
            }

            break;
#endif /* USB_DMA_PP */

        default:

            USB_DEBUG_HOOK(USB_DEBUG_HOOK_STD | USB_DEBUG_HOOK_CODE12);

            break;
    }
    return reg_p;
} /* End of function usb_creg_get_fifosel_adr() */

/***********************************************************************************************************************
 Function Name   : usb_creg_set_dclrm
 Description     : Set DCLRM-bits (FIFO buffer auto clear) of the FIFOSEL cor-
                 : responding to specified PIPEMODE.
 Arguments       : uint16_t     ip           : ip Portno (Port0 = 0...)
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_set_dclrm(uint16_t ip, uint16_t pipemode)
{
    volatile uint16_t* reg_p;

    reg_p = (uint16_t*)usb_creg_get_fifosel_adr(ip, pipemode);

    *reg_p |= USB_DCLRM;
} /* End of function usb_creg_set_dclrm() */

/***********************************************************************************************************************
 Function Name   : usb_creg_clr_dclrm
 Description     : Reset DCLRM-bits (FIFO buffer not auto-cleared) of the FIFOSEL
                 : corresponding to the specified PIPEMODE.
 Arguments       : uint16_t     ip           : ip Portno (port0 = 0...)
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_clr_dclrm(uint16_t ip, uint16_t pipemode)
{
    volatile uint16_t* reg_p;

    reg_p = usb_creg_get_fifosel_adr(ip, pipemode);

    *reg_p &= ~USB_DCLRM;
} /* End of function usb_creg_clr_dclrm() */

/***********************************************************************************************************************
 Function Name   : usb_creg_set_dreqe
 Description     : Set DREQE-bits (to output signal DxREQ_Na) of the FIFOSEL cor-
                 : responding to specified PIPEMODE.
 Arguments       : uint16_t     ip           : ip Port no (port0 = 0...)
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_set_dreqe(uint16_t ip, uint16_t pipemode)
{
    volatile uint16_t* reg_p;

    reg_p = usb_creg_get_fifosel_adr(ip, pipemode);

    *reg_p |= USB_DREQE;
} /* End of function usb_creg_set_dreqe() */

/***********************************************************************************************************************
 Function Name   : usb_creg_clr_dreqe
 Description     : Clear DREQE-bits (To prohibit the output of the signal DxREQ_N)
                 : of the FIFOSEL corresponding to the specified PIPEMODE.
 Arguments       : uint16_t     ip           : ip Port no (port0 = 0...)
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_clr_dreqe(uint16_t ip, uint16_t pipemode)
{
    volatile uint16_t* reg_p;

    reg_p = usb_creg_get_fifosel_adr(ip, pipemode);

    *reg_p &= ~USB_DREQE;
} /* End of function usb_creg_clr_dreqe() */

/***********************************************************************************************************************
 Function Name   : usb_creg_set_mbw
 Description     : Set MBW-bits (CFIFO Port Access Bit Width) of the FIFOSEL cor-
                 : responding to the specified PIPEMODE, to select 8 or 16-bit
                 : wide FIFO port access.
 Arguments       : uint16_t      ip           : ip Port no(port0 = 0...)
                 : uint16_t      pipemode     : CUSE/D0DMA/D1DMA.
                 : uint16_t      data         : Defined value of 8 (data = 0x0000) or 16 bit
                 :                              (data = 0x0400), 32 bit (data = 0x0800) access mode.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_set_mbw(uint16_t ip, uint16_t pipemode, uint16_t data)
{
    volatile uint16_t* reg_p;
    volatile uint32_t dummy_rd_w;
    uint16_t pipe_no;
    uint16_t reg_data;

    reg_p    = usb_creg_get_fifosel_adr(ip, pipemode);
    reg_data = *reg_p;
    pipe_no  = reg_data & 0x000f;
    reg_data &= 0xfff0;

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
    *reg_p = reg_data;
    while (1)
    {
        if ((*reg_p & 0x000f) == 0u)
        {
            break;
        }
    }
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

    reg_data &= ~USB_MBW;
    reg_data |= data;
    *reg_p = reg_data;

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
    while (1)
    {
        if ((*reg_p & 0x000f) == 0u)
        {
            break;
        }
    }
#else
    while (1)
    {
        if (*reg_p == reg_data)
        {
            break;
        }
    }
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

    /* dummy read */
    switch (pipemode)
    {

        case USB_CUSE:

            if (USB_IP0 == ip)
            {
                dummy_rd_w = USB200.CFIFO.UINT32;
            }
            if (USB_IP1 == ip)
            {
                dummy_rd_w = USB201.CFIFO.UINT32;
            }

            break;

        case USB_D0DMA:

            if (USB_IP0 == ip)
            {
                dummy_rd_w = USB200.D0FIFO.UINT32;
            }
            if (USB_IP1 == ip)
            {
                dummy_rd_w = USB201.D0FIFO.UINT32;
            }

            break;

        case USB_D1DMA:

            if (USB_IP0 == ip)
            {
                dummy_rd_w = USB200.D1FIFO.UINT32;
            }
            if (USB_IP1 == ip)
            {
                dummy_rd_w = USB201.D1FIFO.UINT32;
            }

            break;

        default:
            break;
    }

    reg_data |= pipe_no;
    *reg_p = reg_data;

    while (1)
    {
        if ((*reg_p & 0x000f) == pipe_no)
        {
            break;
        }
    }
} /* End of function usb_creg_set_mbw() */

/***********************************************************************************************************************
 Function Name   : usb_creg_set_bval
 Description     : Set BVAL (Buffer Memory Valid Flag) to the number given; in the FIFOCTR corresponding
                 : to the specified PIPEMODE.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
                 : uint16_t     pipemode     : CUSE/D0DMA/D1DMA.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_set_bval(usb_utr_t* ptr, uint16_t pipemode)
{
    hw_usb_set_bval(ptr, pipemode);
} /* End of function usb_creg_set_bval() */

/*************/
/*  BRDYENB  */
/*************/
/* BRDY Interrupt Enable Register */

/***********************************************************************************************************************
 Function Name   : usb_creg_set_brdyenb
 Description     : A bit is set in the specified pipe's BRDYENB, enabling the
                 : respective pipe BRDY interrupt(s).
 Arguments       : uint16_t     ip           : ip Port no(port0 = 0...)
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_set_brdyenb(uint16_t ip, uint16_t pipeno)
{
    if (USB_IP0 == ip)
    {
        USB200.BRDYENB |= (1 << pipeno);
    }
    if (USB_IP1 == ip)
    {
        USB201.BRDYENB |= (1 << pipeno);
    }
} /* End of function usb_creg_set_brdyenb() */

/*************/
/*  BEMPENB  */
/*************/
/* BEMP (buffer empty) Interrupt Enable Register */
/***********************************************************************************************************************
 Function Name   : usb_creg_set_bempenb
 Description     : A bit is set in the specified pipe's BEMPENB enabling the
                 : respective pipe's BEMP interrupt(s).
 Arguments       : uint16_t     ip           : ip Port no(port0 = 0...)
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_set_bempenb(uint16_t ip, uint16_t pipeno)
{
    if (USB_IP0 == ip)
    {
        USB200.BEMPENB |= (1 << pipeno);
    }
    if (USB_IP1 == ip)
    {
        USB201.BEMPENB |= (1 << pipeno);
    }
} /* End of function usb_creg_set_bempenb() */

/***********************************************************************************************************************
 Function Name   : usb_creg_clr_sts_bemp
 Description     : Clear the PIPExBEMP status bit of the specified pipe to clear
                 : its BEMP interrupt status.
 Arguments       : uint16_t     ip           : ip Port no(port0 = 0...)
                 : uint16_t     pipeno       : Pipe number.
 Return value    : none
 ***********************************************************************************************************************/
void usb_creg_clr_sts_bemp(uint16_t ip, uint16_t pipeno)
{
    if (USB_IP0 == ip)
    {
        USB200.BEMPSTS = (uint16_t)~(1 << pipeno);
    }
    if (USB_IP1 == ip)
    {
        USB201.BEMPSTS = (uint16_t)~(1 << pipeno);
    }
} /* End of function usb_creg_clr_sts_bemp() */

/********************/
/* DCPCTR, PIPEnCTR */
/********************/
/* PIPEn Control Registers */

/***********************************************************************************************************************
 Function Name   : usb_creg_read_pipectr
 Description     : Returns DCPCTR or the specified pipe's PIPECTR register content.
                 : The Pipe Control Register returned is determined by the speci-
                 : fied pipe number.
 Arguments       : uint16_t     ip           : ip Port no(port0 = 0...)
                 : uint16_t     pipeno       : Pipe number.
 Return value    : PIPExCTR content
 ***********************************************************************************************************************/
uint16_t usb_creg_read_pipectr(uint16_t ip, uint16_t pipeno)
{
    volatile uint16_t* reg_p = (uint16_t*)USB_NULL;

    if (USB_PIPE0 == pipeno)
    {
        if (USB_IP0 == ip)
        {
            reg_p = (uint16_t*)&(USB200.DCPCTR);
        }
        if (USB_IP1 == ip)
        {
            reg_p = (uint16_t*)&(USB201.DCPCTR);
        }
    }
    if (USB_PIPE1 == pipeno)
    {
        if (USB_IP0 == ip)
        {
            reg_p = (uint16_t*)&(USB200.PIPE1CTR);
        }
        if (USB_IP1 == ip)
        {
            reg_p = (uint16_t*)&(USB201.PIPE1CTR);
        }
    }
    if (USB_PIPE2 == pipeno)
    {
        if (USB_IP0 == ip)
        {
            reg_p = (uint16_t*)&(USB200.PIPE2CTR);
        }
        if (USB_IP1 == ip)
        {
            reg_p = (uint16_t*)&(USB201.PIPE2CTR);
        }
    }
    if (USB_PIPE3 == pipeno)
    {
        if (USB_IP0 == ip)
        {
            reg_p = (uint16_t*)&(USB200.PIPE3CTR);
        }
        if (USB_IP1 == ip)
        {
            reg_p = (uint16_t*)&(USB201.PIPE3CTR);
        }
    }

    return *reg_p;
} /* End of function usb_creg_read_pipectr() */

/***********************************************************************************************************************
 Function Name   : disable_dmax
 Description     : disable dma any channel
 Arguments       : uint16_t     ch_no        : channel no
 Return value    : none
 ***********************************************************************************************************************/
void disable_dmax(uint16_t ch_no)
{
    switch (ch_no)
    {
        case USB_CFG_CH0:

            disable_dma0();

            break;

        case USB_CFG_CH1:

            disable_dma1();

            break;

        case USB_CFG_CH2:

            disable_dma2();

            break;

        case USB_CFG_CH3:

            disable_dma3();

            break;

        default:
            break;
    }
} /* End of function disable_dmax() */

/***********************************************************************************************************************
 Function Name   : disable_dma0
 Description     : disable dma channel 0
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void disable_dma0(void)
{
    /* dma clear */
    if ((DMAC0.CHSTAT_n & 0x05) != 0u)
    {
        DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC0.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC0.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
} /* End of function disable_dma0() */

/***********************************************************************************************************************
 Function Name   : disable_dma1
 Description     : disable dma channel 1
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void disable_dma1(void)
{
    if ((DMAC1.CHSTAT_n & 0x05) != 0u)
    {
        DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC1.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC1.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
} /* End of function disable_dma1() */

/***********************************************************************************************************************
 Function Name   : disable_dma2
 Description     : disable dma channel 2
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void disable_dma2(void)
{
    /* dma clear */
    if ((DMAC2.CHSTAT_n & 0x05) != 0u)
    {
        DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC2.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC2.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
} /* End of function disable_dma2() */

/***********************************************************************************************************************
 Function Name   : disable_dma3
 Description     : disable dma channel 3
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void disable_dma3(void)
{
    /* dma clear */
    if ((DMAC3.CHSTAT_n & 0x05) != 0u)
    {
        DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_CLREN;
        DMAC3.CHCTRL_n |= USB_DMA_CHCTRL_CLRRQ;
        while (1)
        {
            if ((DMAC3.CHSTAT_n & 0x05) == 0u)
            {
                break;
            }
        }
    }
} /* End of function disable_dma3() */
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
