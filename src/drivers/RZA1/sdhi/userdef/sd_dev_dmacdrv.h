/*******************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only
* intended for use with Renesas products. No other uses are authorized. This
* software is owned by Renesas Electronics Corporation and is protected under
* all applicable laws, including copyright laws.
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
* LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
* AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
* TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
* ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
* FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
* ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
* BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software
* and to discontinue the availability of this software. By using this software,
* you agree to the additional terms and conditions found by accessing the
* following link:
* http://www.renesas.com/disclaimer
*
* Copyright (C) 2013 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/******************************************************************************
* File Name    : sd_dev_dmacdrv.h
* $Rev: $
* $Date::                           $
* Description  : RZ/A1H Sample Program - DMAC sample program
******************************************************************************/
#ifndef _SD_DEV_DMACDRV_H_
#define _SD_DEV_DMACDRV_H_

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/


/******************************************************************************
Typedef definitions
******************************************************************************/
typedef struct dmac_transinfo
{
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t count;
    uint32_t src_size;
    uint32_t dst_size;
    uint32_t saddr_dir;
    uint32_t daddr_dir;
} dmac_transinfo_t;

/******************************************************************************
Macro definitions
******************************************************************************/
#define DMAC_SAMPLE_SINGLE          (0)
#define DMAC_SAMPLE_CONTINUATION    (1)

#define DMAC_MODE_REGISTER          (0)
#define DMAC_MODE_LINK              (1)

#define DMAC_REQ_MODE_EXT           (0)
#define DMAC_REQ_MODE_PERI          (1)
#define DMAC_REQ_MODE_SOFT          (2)

#define DMAC_TRANS_SIZE_8           (0)
#define DMAC_TRANS_SIZE_16          (1)
#define DMAC_TRANS_SIZE_32          (2)
#define DMAC_TRANS_SIZE_64          (3)
#define DMAC_TRANS_SIZE_128         (4)
#define DMAC_TRANS_SIZE_256         (5)
#define DMAC_TRANS_SIZE_512         (6)
#define DMAC_TRANS_SIZE_1024        (7)

#define DMAC_TRANS_ADR_NO_INC       (1)
#define DMAC_TRANS_ADR_INC          (0)

#define DMAC_REQ_DET_FALL           (0)
#define DMAC_REQ_DET_RISE           (1)
#define DMAC_REQ_DET_LOW            (2)
#define DMAC_REQ_DET_HIGH           (3)

#define DMAC_REQ_DIR_SRC            (0)
#define DMAC_REQ_DIR_DST            (1)

#define DMAC_DESC_HEADER            (0)     /* Header              */
#define DMAC_DESC_SRC_ADDR          (1)     /* Source Address      */
#define DMAC_DESC_DST_ADDR          (2)     /* Destination Address */
#define DMAC_DESC_COUNT             (3)     /* Transaction Byte    */
#define DMAC_DESC_CHCFG             (4)     /* Channel Confg       */
#define DMAC_DESC_CHITVL            (5)     /* Channel Interval    */
#define DMAC_DESC_CHEXT             (6)     /* Channel Extension   */
#define DMAC_DESC_LINK_ADDR         (7)     /* Link Address        */

typedef enum dmac_request_factor
{
    DMAC_REQ_SDHI_0_TX,
    DMAC_REQ_SDHI_0_RX,
    DMAC_REQ_SDHI_1_TX,
    DMAC_REQ_SDHI_1_RX,
} dmac_request_factor_t;

/******************************************************************************
Variable Externs
******************************************************************************/


/******************************************************************************
Functions Prototypes
******************************************************************************/
void sd_DMAC_PeriReqInit(const dmac_transinfo_t * trans_info, uint32_t dmamode, uint32_t continuation,
                                 uint32_t request_factor, uint32_t req_direction, int dma_channel);
int32_t sd_DMAC_Open(uint32_t req, int dma_channel);
void sd_DMAC_Close(uint32_t * remain, int dma_channel);
int32_t sd_DMAC_Get_Endflag(int dma_channel);

#endif  /* _SD_DEV_DMACDRV_H_ */

/* End of File */
