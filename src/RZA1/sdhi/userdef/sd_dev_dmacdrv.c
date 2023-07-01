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
* Copyright (C) 2012 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
* File Name    : sd_dev_dmacdrv.c
* $Rev: $
* $Date::                           $
* Device(s)    : RZ/A1H
* Tool-Chain   : 
*              : 
* OS           : 
* H/W Platform : 
* Description  : RZ/A1H Sample Program - DMAC sample program (Main)
* Operation    : 
* Limitations  : 
*******************************************************************************/


/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "RZA1/system/rza_io_regrw.h"
#include <stdio.h>
#include "RZA1/system/r_typedefs.h"
#include "RZA1/system/iodefine.h"
#include "RZA1/system/dev_drv.h"                /* Device Driver common header */
#include "RZA1/intc/devdrv_intc.h"            /* INTC Driver Header */
#include "RZA1/sdhi/userdef/sd_dev_dmacdrv.h"
#include "RZA1/system/iobitmasks/dmac_iobitmask.h"

/******************************************************************************
Typedef definitions
******************************************************************************/


/******************************************************************************
Macro definitions
******************************************************************************/
#define DMAC_INDEFINE   (255)       /* �s��l */

typedef enum dmac_peri_req_reg_type
{
    DMAC_REQ_MID,
    DMAC_REQ_RID,
    DMAC_REQ_AM,
    DMAC_REQ_LVL,
    DMAC_REQ_REQD
} dmac_peri_req_reg_type_t;

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/


/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/


/******************************************************************************
Private global variables and functions
******************************************************************************/
/* ==== Prototype declaration ==== */

/* ==== Global variable ==== */
static const uint8_t sd_dmac_peri_req_init_table[4][5] =
{
/*   MID,RID,AM,LVL,REQD                                                            */
    {  48, 1, 2, 1, 1             },    /* SDHI_0 Tx                                */
    {  48, 2, 2, 1, 0             },    /* SDHI_0 Rx                                */
    {  49, 1, 2, 1, 1             },    /* SDHI_1 Tx                                */
    {  49, 2, 2, 1, 0             },    /* SDHI_1 Rx                                */
};


/******************************************************************************
* Function Name: sd_DMAC1_PeriReqInit
* Description  : 
* Arguments    : 
* Return Value : 
******************************************************************************/
void sd_DMAC_PeriReqInit(const dmac_transinfo_t * trans_info, uint32_t dmamode, uint32_t continuation,
                              uint32_t request_factor, uint32_t req_direction, int dma_channel)
{
    if (DMAC_MODE_REGISTER == dmamode)
    {
        /* ==== Next0 register set ==== */
    	DMACn(dma_channel).N0SA_n = trans_info->src_addr;
    	DMACn(dma_channel).N0DA_n = trans_info->dst_addr;
    	DMACn(dma_channel).N0TB_n = trans_info->count;

        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            trans_info->daddr_dir,
                            DMAC1_CHCFG_n_DAD_SHIFT,
                            DMAC1_CHCFG_n_DAD);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            trans_info->saddr_dir,
                            DMAC1_CHCFG_n_SAD_SHIFT,
                            DMAC1_CHCFG_n_SAD);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            trans_info->dst_size,
                            DMAC1_CHCFG_n_DDS_SHIFT,
                            DMAC1_CHCFG_n_DDS);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            trans_info->src_size,
                            DMAC1_CHCFG_n_SDS_SHIFT,
                            DMAC1_CHCFG_n_SDS);

        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            0,
                            DMAC1_CHCFG_n_DMS_SHIFT,
                            DMAC1_CHCFG_n_DMS);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            0,
                            DMAC1_CHCFG_n_RSEL_SHIFT,
                            DMAC1_CHCFG_n_RSEL);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            0,
                            DMAC1_CHCFG_n_SBE_SHIFT,
                            DMAC1_CHCFG_n_SBE);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            0,
                            DMAC1_CHCFG_n_DEM_SHIFT,
                            DMAC1_CHCFG_n_DEM);

        if (DMAC_SAMPLE_CONTINUATION == continuation)
        {
            RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                                1,
                                DMAC1_CHCFG_n_REN_SHIFT,
                                DMAC1_CHCFG_n_REN);
            RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                                1,
                                DMAC1_CHCFG_n_RSW_SHIFT,
                                DMAC1_CHCFG_n_RSW);
        }
        else
        {
            RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                                0,
                                DMAC1_CHCFG_n_REN_SHIFT,
                                DMAC1_CHCFG_n_REN);
            RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                                0,
                                DMAC1_CHCFG_n_RSW_SHIFT,
                                DMAC1_CHCFG_n_RSW);
        }

        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            0,
                            DMAC1_CHCFG_n_TM_SHIFT,
                            DMAC1_CHCFG_n_TM);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
        					dma_channel & 0b111,
                            DMAC1_CHCFG_n_SEL_SHIFT,
                            DMAC1_CHCFG_n_SEL);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            1,
                            DMAC1_CHCFG_n_HIEN_SHIFT,
                            DMAC1_CHCFG_n_HIEN);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            0,
                            DMAC1_CHCFG_n_LOEN_SHIFT,
                            DMAC1_CHCFG_n_LOEN);

        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            sd_dmac_peri_req_init_table[request_factor][DMAC_REQ_AM],
                            DMAC1_CHCFG_n_AM_SHIFT,
                            DMAC1_CHCFG_n_AM);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                            sd_dmac_peri_req_init_table[request_factor][DMAC_REQ_LVL],
                            DMAC1_CHCFG_n_LVL_SHIFT,
                            DMAC1_CHCFG_n_LVL);
        if (sd_dmac_peri_req_init_table[request_factor][DMAC_REQ_REQD] != DMAC_INDEFINE)
        {
            RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                                sd_dmac_peri_req_init_table[request_factor][DMAC_REQ_REQD],
                                DMAC1_CHCFG_n_REQD_SHIFT,
                                DMAC1_CHCFG_n_REQD);
        }
        else
        {
            RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCFG_n,
                                req_direction,
                                DMAC1_CHCFG_n_REQD_SHIFT,
                                DMAC1_CHCFG_n_REQD);
        }
        int shift = (dma_channel & 1) ? 16 : 0;
        RZA_IO_RegWrite_32(DMARSnAddress(dma_channel),
                            sd_dmac_peri_req_init_table[request_factor][DMAC_REQ_RID],
                            DMAC01_DMARS_CH0_RID_SHIFT + shift,
                            DMAC01_DMARS_CH0_RID << shift);
        RZA_IO_RegWrite_32(DMARSnAddress(dma_channel),
                            sd_dmac_peri_req_init_table[request_factor][DMAC_REQ_MID],
                            DMAC01_DMARS_CH0_MID_SHIFT + shift,
                            DMAC01_DMARS_CH0_MID << shift);

        // I have no idea why this was originally here... (Rohan)
        /*
        rza_io_reg_write_32(&DMAC.DMARS7,
                            1,
                            DMAC07_DCTRL_0_7_PR_SHIFT,
                            DMAC07_DCTRL_0_7_PR);
                            */
    }
}

/******************************************************************************
* Function Name: sd_DMAC1_Open
* Description  : 
* Arguments    : 
* Return Value : 
******************************************************************************/
int32_t sd_DMAC_Open(uint32_t req, int dma_channel)
{
    int32_t ret;
    volatile uint8_t  dummy;

    if ((0 == RZA_IO_RegRead_32(&DMACn(dma_channel).CHSTAT_n,
                                DMAC1_CHSTAT_n_EN_SHIFT,
                                DMAC1_CHSTAT_n_EN)) && 
        (0 == RZA_IO_RegRead_32(&DMACn(dma_channel).CHSTAT_n,
                                DMAC1_CHSTAT_n_TACT_SHIFT,
                                DMAC1_CHSTAT_n_TACT)))
    {
        doAnyway:
    	RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCTRL_n,
                            1,
                            DMAC1_CHCTRL_n_SWRST_SHIFT,
                            DMAC1_CHCTRL_n_SWRST);
        dummy = RZA_IO_RegRead_32(&DMACn(dma_channel).CHCTRL_n,
                                DMAC1_CHCTRL_n_SWRST_SHIFT,
                                DMAC1_CHCTRL_n_SWRST);
        RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCTRL_n,
                            1,
                            DMAC1_CHCTRL_n_SETEN_SHIFT,
                            DMAC1_CHCTRL_n_SETEN);

        if (DMAC_REQ_MODE_SOFT == req)
        {
            RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCTRL_n,
                                1,
                                DMAC1_CHCTRL_n_STG_SHIFT,
                                DMAC1_CHCTRL_n_STG);
        }
        ret = 0;
    }
    else
    {
    	// Hack - just go and do it anyway. This helps the case where the SD has been re-inserted after being ripped out during a transfer
    	goto doAnyway;
        ret = -1;
    }

    return ret;
}


/******************************************************************************
* Function Name: sd_DMAC2_Close
* Description  : 
* Arguments    : 
* Return Value : 
******************************************************************************/
void sd_DMAC_Close(uint32_t * remain, int dma_channel)
{

    RZA_IO_RegWrite_32(&DMACn(dma_channel).CHCTRL_n,
                        1,
                        DMAC2_CHCTRL_n_CLREN_SHIFT,
                        DMAC2_CHCTRL_n_CLREN);

    while (1 == RZA_IO_RegRead_32(&DMACn(dma_channel).CHSTAT_n,
                                DMAC2_CHSTAT_n_TACT_SHIFT,
                                DMAC2_CHSTAT_n_TACT))
    {
        /* Wait stop */
    }

    while (1 == RZA_IO_RegRead_32(&DMACn(dma_channel).CHSTAT_n,
                                DMAC2_CHSTAT_n_EN_SHIFT,
                                DMAC2_CHSTAT_n_EN))
    {
        /* Wait stop */
    }
    *remain = DMACn(dma_channel).CRTB_n;
}


/******************************************************************************
* Function Name: sd_DMAC2_Get_Endflag
* Description  : 
* Arguments    : 
* Return Value : 
******************************************************************************/
int32_t sd_DMAC_Get_Endflag(int dma_channel)
{
    if( RZA_IO_RegRead_32(&DMACn(dma_channel).CHSTAT_n,
                                DMAC2_CHSTAT_n_TC_SHIFT,
                                DMAC2_CHSTAT_n_TC) == 0 ){
        /* not yet */
        return 0;
    }

    /* finish */
    return 1;
}

/* End of File */

