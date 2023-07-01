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
 * File Name    : r_usb_dmac.h
 * Description  : DMA Difinition for USB
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 30.09.2016 1.00    First Release
 ***********************************************************************************************************************/

#ifndef R_USB_DMAC_H
#define R_USB_DMAC_H

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))

/***********************************************************************************************************************
 Macro definitions
 ***********************************************************************************************************************/
#define USB_DMA_USE_CH_MAX (4u) /* MAX USE DMAC CH for USB */
#define USB_DMA_CH_PRI     (3u) /* DMACmI interrupt priority level for USB Pipe : DxFIFO->buff */
#define USB_DMA_CH2_PRI    (3u) /* DMACmI interrupt priority level for USB Pipe : buff->DxFIFO */

#define USB_FIFO_TYPE_D0DMA   (0u) /* D0FIFO Select */
#define USB_FIFO_TYPE_D1DMA   (1u) /* D1FIFO Select */
#define USB_DMA_FIFO_TYPE_NUM (USB_FIFO_TYPE_D1DMA + 1)

#define USB_FIFO_ACCESS_TYPE_32BIT (0u) /* FIFO port 32bit access */
#define USB_FIFO_ACCESS_TYPE_16BIT (1u) /* FIFO port 16bit access */
#define USB_FIFO_ACCESS_TYPE_8BIT  (2u) /* FIFO port 8bit access */

#define USB_FIFO_ACCSESS_TYPE_NUM (USB_FIFO_ACCESS_TYPE_8BIT + 1)

/* CHSTAT Register */
#define USB_DMA_CHSTAT_DNUM   0xFF000000 /* b31-24 : DNUM bit   */
#define USB_DMA_CHSTAT_SWPRQ  0x00040000 /* b18    : SWPRQ bit  */
#define USB_DMA_CHSTAT_DMARQM 0x00020000 /* b17    : DMARQM bit */
#define USB_DMA_CHSTAT_INTM   0x00010000 /* b16    : INTM bit   */
#define USB_DMA_CHSTAT_MODE   0x00000800 /* b11    : MODE bit   */
#define USB_DMA_CHSTAT_DER    0x00000400 /* b10    : DER bit    */
#define USB_DMA_CHSTAT_DW     0x00000200 /* b9     : DW bit     */
#define USB_DMA_CHSTAT_DL     0x00000100 /* b8     : DL bit     */
#define USB_DMA_CHSTAT_SR     0x00000080 /* b7     : SR bit     */
#define USB_DMA_CHSTAT_END    0x00000020 /* b5     : END bit    */
#define USB_DMA_CHSTAT_ER     0x00000010 /* b4     : ER bit     */
#define USB_DMA_CHSTAT_SUS    0x00000008 /* b3     : SUS bit    */
#define USB_DMA_CHSTAT_TACT   0x00000004 /* b2     : TACT bit   */
#define USB_DMA_CHSTAT_RQST   0x00000002 /* b1     : RQST bit   */
#define USB_DMA_CHSTAT_EN     0x00000001 /* b0     : EN bit     */

/* CHCTRL Register */
#define USB_DMA_CHCTRL_CLRDMARQM 0x00080000 /* b19    : CLRDMARQM bit  */
#define USB_DMA_CHCTRL_SETDMARQM 0x00040000 /* b18    : SETDMARQM bit  */
#define USB_DMA_CHCTRL_CLRINTM   0x00020000 /* b17    : CLRINTM bit    */
#define USB_DMA_CHCTRL_SETINTM   0x00010000 /* b16    : SETINTM bit    */
#define USB_DMA_CHCTRL_SETSSWPRQ 0x00004000 /* b14    : SETSSWPRQ bit  */
#define USB_DMA_CHCTRL_SETREN    0x00001000 /* b12    : SETREN bit     */
#define USB_DMA_CHCTRL_CLRSUS    0x00000200 /* b9     : CLRSUS bit     */
#define USB_DMA_CHCTRL_SETSUS    0x00000100 /* b8     : SETSUS bit     */
#define USB_DMA_CHCTRL_CLRDE     0x00000080 /* b7     : CLRDE bit      */
#define USB_DMA_CHCTRL_CLRTC     0x00000040 /* b6     : CLRTC bit      */
#define USB_DMA_CHCTRL_CLREND    0x00000020 /* b5     : CLREND bit     */
#define USB_DMA_CHCTRL_CLRRQ     0x00000010 /* b4     : CLRRQ bit      */
#define USB_DMA_CHCTRL_SWRST     0x00000008 /* b3     : SWRST bit      */
#define USB_DMA_CHCTRL_CLREN     0x00000002 /* b1     : CLREN bit      */
#define USB_DMA_CHCTRL_SETEN     0x00000001 /* b0     : SETEN bit      */

/* CHCFG Register */
#define USB_DMA_CHCFG_DMS        0x80000000 /* b31    : DMS bit        */
#define USB_DMA_CHCFG_REN        0x40000000 /* b30    : REN bit        */
#define USB_DMA_CHCFG_RSW        0x20000000 /* b29    : RSW bit        */
#define USB_DMA_CHCFG_RSEL       0x10000000 /* b28    : RSEL bit       */
#define USB_DMA_CHCFG_SBE        0x08000000 /* b27    : SBE bit        */
#define USB_DMA_CHCFG_DIM        0x04000000 /* b26    : DIM bit        */
#define USB_DMA_CHCFG_DEM        0x01000000 /* b24    : DEM bit        */
#define USB_DMA_CHCFG_WONLY      0x00800000 /* b23    : WONLY bit      */
#define USB_DMA_CHCFG_TM         0x00400000 /* b22    : TM bit         */
#define USB_DMA_CHCFG_DAD        0x00200000 /* b21    : DAD bit        */
#define USB_DMA_CHCFG_SAD        0x00100000 /* b20    : SAD bit        */
#define USB_DMA_CHCFG_DDS        0x000F0000 /* b19-16 : DDS bit        */
#define USB_DMA_CHCFG_DDS_8      0x00000000 /*          8   bit access */
#define USB_DMA_CHCFG_DDS_16     0x00010000 /*          16  bit access */
#define USB_DMA_CHCFG_DDS_32     0x00020000 /*          32  bit access */
#define USB_DMA_CHCFG_DDS_128    0x00040000 /*          128 bit access */
#define USB_DMA_CHCFG_DDS_256    0x00050000 /*          256 bit access */
#define USB_DMA_CHCFG_DDS_512    0x00060000 /*          512 bit access */
#define USB_DMA_CHCFG_DDS_NORMAL 0x00000000 /*          Normal mode    */
#define USB_DMA_CHCFG_DDS_SKIP   0x00080000 /*          Skip mode      */
#define USB_DMA_CHCFG_SDS        0x0000F000 /* b15-12 : SDS bit        */
#define USB_DMA_CHCFG_SDS_8      0x00000000 /*          8   bit access */
#define USB_DMA_CHCFG_SDS_16     0x00001000 /*          16  bit access */
#define USB_DMA_CHCFG_SDS_32     0x00002000 /*          32  bit access */
#define USB_DMA_CHCFG_SDS_128    0x00004000 /*          128 bit access */
#define USB_DMA_CHCFG_SDS_256    0x00005000 /*          256 bit access */
#define USB_DMA_CHCFG_SDS_512    0x00006000 /*          512 bit access */
#define USB_DMA_CHCFG_SDS_NORMAL 0x00000000 /*          Normal mode    */
#define USB_DMA_CHCFG_SDS_SKIP   0x00008000 /*          Skip mode      */
#define USB_DMA_CHCFG_DRRP       0x00000800 /* b11    : DRRP bit       */
#define USB_DMA_CHCFG_AM         0x00000700 /* b10-8  : AM bit         */
#define USB_DMA_CHCFG_AM_LM      0x00000100 /*          level mode     */
#define USB_DMA_CHCFG_AM_BCM     0x00000200 /*          bus cycle mode */
#define USB_DMA_CHCFG_AM_OMM     0x00000400 /*          DACK/TEND output mask mode */
#define USB_DMA_CHCFG_LVL        0x00000040 /* b6     : LVL bit        */
#define USB_DMA_CHCFG_HIEN       0x00000020 /* b5     : HIEN bit       */
#define USB_DMA_CHCFG_LOEN       0x00000010 /* b4     : LOEN bit       */
#define USB_DMA_CHCFG_REQD       0x00000008 /* b3     : REQD bit       */
#define USB_DMA_CHCFG_SEL        0x00000007 /* b2-0   : SEL bit        */
#define USB_DMA_CHCFG_SEL_0_8    0x00000000 /*          channel 0/8    */
#define USB_DMA_CHCFG_SEL_1_9    0x00000001 /*          channel 1/9    */
#define USB_DMA_CHCFG_SEL_2_10   0x00000002 /*          channel 2/10   */
#define USB_DMA_CHCFG_SEL_3_11   0x00000003 /*          channel 3/11   */
#define USB_DMA_CHCFG_SEL_4_12   0x00000004 /*          channel 4/12   */
#define USB_DMA_CHCFG_SEL_5_13   0x00000005 /*          channel 5/13   */
#define USB_DMA_CHCFG_SEL_6_14   0x00000006 /*          channel 6/14   */
#define USB_DMA_CHCFG_SEL_7_15   0x00000007 /*          channel 7/15   */

#define USB_TYPE_NUM_SHIFT (14u)
#define USB_MXPS_NUM_SHIFT (0u)

#define USB_SHTNAKON  0x0080
#define USB_SHTNAKOFF 0x0000

#define USB_DMA_TX   (0u)
#define USB_DMA_RX   (1u)
#define USB_DMA_TXRX (2u)
/* DMA32 USE Setting */
#define USE_DMA32 (1u)

/******************************************************************************
 Exported global functions (to be accessed by other files)
 ******************************************************************************/
extern uint16_t g_usb_cstd_dma_dir[USB_NUM_USBIP][USB_DMA_USE_CH_MAX];  /* DMA0 and DMA1 direction */
extern uint32_t g_usb_cstd_dma_size[USB_NUM_USBIP][USB_DMA_USE_CH_MAX]; /* DMA0 and DMA1 buffer size */
extern uint16_t g_usb_cstd_dma_fifo[USB_NUM_USBIP][USB_DMA_USE_CH_MAX]; /* DMA0 and DMA1 FIFO buffer size */
extern uint16_t g_usb_cstd_dma_pipe[USB_NUM_USBIP][USB_DMA_USE_CH_MAX]; /* DMA0 and DMA1 pipe number */

void usb_dma_driver(void);
uint16_t usb_dma_get_crtb(uint16_t use_port);
uint16_t usb_dma_get_dxfifo_ir_vect(usb_utr_t* ptr, uint16_t useport);
void usb_dma_stop_dxfifo(uint8_t ip_type, uint16_t fifo_mode);
void usb_cstd_dxfifo_stop(usb_utr_t* ptr, uint16_t useport);
uint16_t usb_dma_get_n0tb(uint16_t dma_ch);

uint8_t usb_dma_ref_ch_no(uint16_t ip_no, uint16_t use_port);
void usb_dma_set_ch_no(uint16_t ip_no, uint16_t use_port, uint8_t dma_ch_no);
uint16_t usb_dma_ip_ch_no2useport(uint16_t ip_no, uint16_t ch_no);
void usb_dma_buf2dxfifo_complete(usb_utr_t* ptr, uint16_t useport);

void usb_cstd_dxfifo2buf_start_dma(usb_utr_t* ptr, uint16_t pipe, uint16_t useport, uint32_t length);
void usb_cstd_buf2dxfifo_start_dma(usb_utr_t* ptr, uint16_t pipe, uint16_t useport);
void usb_dma_buf2dxfifo_complete_event_set(uint16_t ip_no, uint16_t use_port);
void usb_cstd_DmaxInt(usb_utr_t* ptr, uint16_t pipemode);

void usb_cstd_dmaint0_handler(void);
void usb_cstd_dmaint1_handler(void);
void usb_cstd_dmaint2_handler(void);
void usb_cstd_dmaint3_handler(void);

void usb_stop_dma0(void);
void usb_stop_dma1(void);
void usb_stop_dma2(void);
void usb_stop_dma3(void);

void usb_enable_dmaInt0(void);
void usb_enable_dmaInt1(void);
void usb_enable_dmaInt2(void);
void usb_enable_dmaInt3(void);

void usb_disable_dmaInt0(void);
void usb_disable_dmaInt1(void);
void usb_disable_dmaInt2(void);
void usb_disable_dmaInt3(void);

void usb_cstd_DXfifo2BufStartDma(usb_utr_t* ptr, uint16_t pipe, uint16_t useport, uint32_t length, uint32_t dest_addr);
void usb_cpu_dxfifo2buf_start_dma0(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size);
void usb_cpu_dxfifo2buf_start_dma1(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size);
void usb_cpu_dxfifo2buf_start_dma2(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size);
void usb_cpu_dxfifo2buf_start_dma3(
    usb_utr_t* ptr, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size);

void usb_cpu_dxfifo2buf_start_dmaX(
    usb_utr_t* ptr, uint16_t ch_no, uint32_t SourceAddr, uint16_t useport, uint32_t destAddr, uint32_t transfer_size);

void usb_cpu_buf2dxfifo_start_dma0(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size);
void usb_cpu_buf2dxfifo_start_dma1(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size);
void usb_cpu_buf2d1fifo_start_dma2(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size);
void usb_cpu_buf2d1fifo_start_dma3(
    uint16_t useport, uint32_t src_adr, uint16_t ip, uint32_t transfer_size, uint8_t trans_block_size);

void usb_cstd_Buf2Fifo_DMAX(uint16_t ch_no, usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size,
    uint8_t pipe, uint8_t trans_block_size);

void usb_cstd_Buf2Fifo_DMA0(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size);
void usb_cstd_Buf2Fifo_DMA1(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size);
void usb_cstd_Buf2Fifo_DMA2(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size);
void usb_cstd_Buf2Fifo_DMA3(
    usb_utr_t* ptr, uint16_t useport, uint32_t src, uint32_t data_size, uint8_t pipe, uint8_t trans_block_size);

uint32_t usb_cstd_GetDXfifoYAdr(uint16_t ip, uint16_t trans_size, uint16_t pipemode);

uint16_t usb_dma_def_tx_ch_no2ip_no(uint16_t ch_no);
uint8_t usb_dma_def_ch_no(uint16_t ip_no, uint16_t mode_txrx);

void usb_creg_write_dxfbcfg(uint16_t ip, uint16_t pipemode, uint16_t data);

void usb_creg_clr_dclrm(uint16_t ip, uint16_t pipemode);
void usb_creg_clr_dreqe(uint16_t ip, uint16_t pipemode);
void usb_creg_clr_sts_bemp(uint16_t ip, uint16_t pipeno);

uint16_t usb_creg_read_pipectr(uint16_t ip, uint16_t pipeno);
void usb_creg_set_bempenb(uint16_t ip, uint16_t pipeno);

void usb_creg_set_brdyenb(uint16_t ip, uint16_t pipeno);
void usb_creg_set_bval(usb_utr_t* ptr, uint16_t pipemode);
void usb_creg_set_dclrm(uint16_t ip, uint16_t pipemode);
void usb_creg_set_dreqe(uint16_t ip, uint16_t pipemode);
void usb_creg_set_mbw(uint16_t ip, uint16_t pipemode, uint16_t data);
void usb_cstd_chg_curpipe(usb_utr_t* ptr, uint16_t pipe, uint16_t fifosel, uint16_t isel);

void disable_dmax(uint16_t ch_no);
void disable_dma0(void);
void disable_dma1(void);
void disable_dma2(void);
void disable_dma3(void);
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

#endif /* R_USB_DMAC_H */
/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
