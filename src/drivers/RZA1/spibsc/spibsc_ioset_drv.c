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
* Copyright (C) 2014 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
* File Name     : spibsc_ioset_drv.c
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : 
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include <drivers/RZA1/system/rza_io_regrw.h>
#include "r_typedefs.h"
#include "iodefine.h"
#include "spibsc.h"
#include "r_spibsc_ioset_api.h"
#include "spibsc_iobitmask.h"
#include "gpio_iobitmask.h"

#include "Deluge.h"

/******************************************************************************
Typedef definitions
******************************************************************************/
volatile struct st_spibsc* SPIBSC[SPIBSC_COUNT] = SPIBSC_ADDRESS_LIST;

/******************************************************************************
Macro definitions
******************************************************************************/

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/

/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/

/******************************************************************************
Private global variables and functions
******************************************************************************/
static int32_t io_spibsc_port_setting(uint32_t ch_no, int_t data_bus_width, uint32_t bsz);

/******************************************************************************
* Function Name: spibsc_bsz_set
* Description  : Set a data bus width of a SPI multi-I/O bus controller.
* Arguments    : uint32_t ch_no : use channel No
*              : uint32_t bsz : BSZ bit
*              : uint8_t data_witdh
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t spibsc_bsz_set(uint32_t ch_no, uint32_t bsz, uint8_t data_width)
{
    if (ch_no > 1)
    {
        return -1;
    }

    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNSR, SPIBSC_CMNSR_TEND_SHIFT, SPIBSC_CMNSR_TEND) != SPIBSC_TRANS_END)
    {
        return -1;
    }

    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_BSZ_SHIFT, SPIBSC_CMNCR_BSZ) != bsz)
    {
        if (bsz == SPIBSC_CMNCR_BSZ_DUAL)
        {
            /* s-flash x 2 (4bit x 2) */
            if (io_spibsc_port_setting(ch_no, data_width, bsz) < 0)
            {
                return -1;
            }
        }

        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, bsz, SPIBSC_CMNCR_BSZ_SHIFT, SPIBSC_CMNCR_BSZ);
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRCR, SPIBSC_DRCR_RCF_EXE, SPIBSC_DRCR_RCF_SHIFT, SPIBSC_DRCR_RCF);
    }

    return 0;

} /* End of function  spibsc_bsz_set() */

/******************************************************************************
* Function Name: spibsc_common_init
* Description  : Initialize the operation mode independent part of a SPI 
*                multi-I/O bus controller.
* Arguments    : uint32_t ch_no : use channel No
*                uint32_t bsz : BSZ bit
*                uint8_t spbr
*                uint8_t brdv
*                uint8_t data_width
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t spibsc_common_init(uint32_t ch_no, uint32_t bsz, uint8_t spbr, uint8_t brdv, uint8_t data_width)
{
    if (ch_no > 1)
    {
        return -1;
    }

    /* PORT setting of SPIBSC */
    if (io_spibsc_port_setting(ch_no, data_width, bsz) < 0)
    {
        return -1;
    }

    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNSR, SPIBSC_CMNSR_TEND_SHIFT, SPIBSC_CMNSR_TEND) != SPIBSC_TRANS_END)
    {
        return -1;
    }

#if 0 /* Pin status at the time of an idol�FHIGH */
    RZA_IO_RegWrite_32(&SPIBSC[ ch_no ]->CMNCR, SPIBSC_OUTPUT_HIGH, SPIBSC_CMNCR_MOIIO3_SHIFT, SPIBSC_CMNCR_MOIIO3);
    RZA_IO_RegWrite_32(&SPIBSC[ ch_no ]->CMNCR, SPIBSC_OUTPUT_HIGH, SPIBSC_CMNCR_MOIIO2_SHIFT, SPIBSC_CMNCR_MOIIO2);
    RZA_IO_RegWrite_32(&SPIBSC[ ch_no ]->CMNCR, SPIBSC_OUTPUT_HIGH, SPIBSC_CMNCR_MOIIO1_SHIFT, SPIBSC_CMNCR_MOIIO1);
    RZA_IO_RegWrite_32(&SPIBSC[ ch_no ]->CMNCR, SPIBSC_OUTPUT_HIGH, SPIBSC_CMNCR_MOIIO0_SHIFT, SPIBSC_CMNCR_MOIIO0);

    RZA_IO_RegWrite_32(&SPIBSC[ ch_no ]->CMNCR, SPIBSC_OUTPUT_HIGH, SPIBSC_CMNCR_IO3FV_SHIFT, SPIBSC_CMNCR_IO3FV);
    RZA_IO_RegWrite_32(&SPIBSC[ ch_no ]->CMNCR, SPIBSC_OUTPUT_HIGH, SPIBSC_CMNCR_IO2FV_SHIFT, SPIBSC_CMNCR_IO2FV);
    RZA_IO_RegWrite_32(&SPIBSC[ ch_no ]->CMNCR, SPIBSC_OUTPUT_HIGH, SPIBSC_CMNCR_IO0FV_SHIFT, SPIBSC_CMNCR_IO0FV);
#else /* Pin status at the time of an idol�FHiZ */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_OUTPUT_HiZ, SPIBSC_CMNCR_MOIIO3_SHIFT, SPIBSC_CMNCR_MOIIO3);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_OUTPUT_HiZ, SPIBSC_CMNCR_MOIIO2_SHIFT, SPIBSC_CMNCR_MOIIO2);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_OUTPUT_HiZ, SPIBSC_CMNCR_MOIIO1_SHIFT, SPIBSC_CMNCR_MOIIO1);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_OUTPUT_HiZ, SPIBSC_CMNCR_MOIIO0_SHIFT, SPIBSC_CMNCR_MOIIO0);

    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_OUTPUT_HiZ, SPIBSC_CMNCR_IO3FV_SHIFT, SPIBSC_CMNCR_IO3FV);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_OUTPUT_HiZ, SPIBSC_CMNCR_IO2FV_SHIFT, SPIBSC_CMNCR_IO2FV);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_OUTPUT_HiZ, SPIBSC_CMNCR_IO0FV_SHIFT, SPIBSC_CMNCR_IO0FV);
#endif

    /* swap by 8-bit unit(Defaults) */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, 1, SPIBSC_CMNCR_SFDE_SHIFT, SPIBSC_CMNCR_SFDE);

    /* S-flash mode 0 */
    /* even edge : write */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_CPHAT_EVEN, SPIBSC_CMNCR_CPHAT_SHIFT, SPIBSC_CMNCR_CPHAT);
    /* even edge : read */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_CPHAR_EVEN, SPIBSC_CMNCR_CPHAR_SHIFT, SPIBSC_CMNCR_CPHAR);
    /* SPBSSL   : low active */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_SSLP_LOW, SPIBSC_CMNCR_SSLP_SHIFT, SPIBSC_CMNCR_SSLP);
    /* SPBCLK   : low at negate */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_CPOL_LOW, SPIBSC_CMNCR_CPOL_SHIFT, SPIBSC_CMNCR_CPOL);

    spibsc_bsz_set(ch_no, bsz, data_width);

    /* next access delay */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SSLDR, SPIBSC_DELAY_1SPBCLK, SPIBSC_SSLDR_SPNDL_SHIFT, SPIBSC_SSLDR_SPNDL);
    /* SPBSSL negate delay */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SSLDR, SPIBSC_DELAY_1SPBCLK, SPIBSC_SSLDR_SLNDL_SHIFT, SPIBSC_SSLDR_SLNDL);
    /* clock delay */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SSLDR, SPIBSC_DELAY_1SPBCLK, SPIBSC_SSLDR_SCKDL_SHIFT, SPIBSC_SSLDR_SCKDL);

    /* ---- Bit rate Setting ---- */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SPBCR, spbr, SPIBSC_SPBCR_SPBR_SHIFT, SPIBSC_SPBCR_SPBR);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SPBCR, brdv, SPIBSC_SPBCR_BRDV_SHIFT, SPIBSC_SPBCR_BRDV);

    return 0;

} /* End of function spibsc_common_init() */

/******************************************************************************
* Function Name: spibsc_wait_tend
* Description  : Wait TEND
* Arguments    : uint32_t ch_no : use channel No
* Return Value : none
******************************************************************************/
void spibsc_wait_tend(uint32_t ch_no)
{
    while (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNSR, SPIBSC_CMNSR_TEND_SHIFT, SPIBSC_CMNSR_TEND) != SPIBSC_TRANS_END)
    {
        routineForSD();
    }

} /* End of function spibsc_wait_tend() */

/******************************************************************************
* Function Name: spibsc_dr_init
* Description  : The setting which makes a SPI multi-I/O bus controller activate
*                an outside address space read mode.
* Arguments    : uint32_t ch_no : use channel No
*              : st_spibsc_cfg_t *spibsccfg
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t spibsc_dr_init(uint32_t ch_no, st_spibsc_cfg_t* spibsccfg)
{
    if (ch_no > 1)
    {
        return -1;
    }

    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNSR, SPIBSC_CMNSR_TEND_SHIFT, SPIBSC_CMNSR_TEND) != SPIBSC_TRANS_END)
    {
        return -1;
    }

    /* SPI I/O mode */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_MD_EXTRD, SPIBSC_CMNCR_MD_SHIFT, SPIBSC_CMNCR_MD);

    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRCR, SPIBSC_BURST_2, SPIBSC_DRCR_RBURST_SHIFT, SPIBSC_DRCR_RBURST);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRCR, SPIBSC_BURST_ENABLE, SPIBSC_DRCR_RBE_SHIFT, SPIBSC_DRCR_RBE);
    /* Keep SSL after read  */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRCR, SPIBSC_SPISSL_KEEP, SPIBSC_DRCR_SSLE_SHIFT, SPIBSC_DRCR_SSLE);
    /* if not continuous address it negeted */

    /* ---- Command ---- */
    /* Command */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRCMR, spibsccfg->udef_cmd, SPIBSC_DRCMR_CMD_SHIFT, SPIBSC_DRCMR_CMD);
    /* Single */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, spibsccfg->udef_cmd_width, SPIBSC_DRENR_CDB_SHIFT, SPIBSC_DRENR_CDB);
    /* Enable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, SPIBSC_OUTPUT_ENABLE, SPIBSC_DRENR_CDE_SHIFT, SPIBSC_DRENR_CDE);
    /* ---- Option Command ---- */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRCMR, 0x00, SPIBSC_DRCMR_OCMD_SHIFT, SPIBSC_DRCMR_OCMD);
    /* Single */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, SPIBSC_1BIT, SPIBSC_DRENR_OCDB_SHIFT, SPIBSC_DRENR_OCDB);
    /* Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, SPIBSC_OUTPUT_DISABLE, SPIBSC_DRENR_OCDE_SHIFT, SPIBSC_DRENR_OCDE);

    /* ---- Address ---- */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, spibsccfg->udef_addr_width, SPIBSC_DRENR_ADB_SHIFT, SPIBSC_DRENR_ADB);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, spibsccfg->udef_addr_mode, SPIBSC_DRENR_ADE_SHIFT, SPIBSC_DRENR_ADE);

    /* EAV */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DREAR, 0, SPIBSC_DREAR_EAV_SHIFT, SPIBSC_DREAR_EAV);

    /* ---- Option Data ---- */
    /* Option Data */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DROPR, spibsccfg->udef_opd3, SPIBSC_DROPR_OPD3_SHIFT, SPIBSC_DROPR_OPD3);
    /* Option Data */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DROPR, spibsccfg->udef_opd2, SPIBSC_DROPR_OPD2_SHIFT, SPIBSC_DROPR_OPD2);
    /* Option Data */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DROPR, spibsccfg->udef_opd1, SPIBSC_DROPR_OPD1_SHIFT, SPIBSC_DROPR_OPD1);
    /* Option Data */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DROPR, spibsccfg->udef_opd0, SPIBSC_DROPR_OPD0_SHIFT, SPIBSC_DROPR_OPD0);
    /* Single */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, spibsccfg->udef_opd_width, SPIBSC_DRENR_OPDB_SHIFT, SPIBSC_DRENR_OPDB);
    /* Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, spibsccfg->udef_opd_enable, SPIBSC_DRENR_OPDE_SHIFT, SPIBSC_DRENR_OPDE);

    /* ---- Data ---- */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, spibsccfg->udef_data_width, SPIBSC_DRENR_DRDB_SHIFT, SPIBSC_DRENR_DRDB);
    /* dummycycle enable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRENR, spibsccfg->udef_dmycyc_enable, SPIBSC_DRENR_DME_SHIFT, SPIBSC_DRENR_DME);

    /* Set data read dummycycle */
    RZA_IO_RegWrite_32(
        &SPIBSC[ch_no]->DRDMCR, spibsccfg->udef_dmycyc_width, SPIBSC_DRDMCR_DMDB_SHIFT, SPIBSC_SMDMCR_DMDB);
    RZA_IO_RegWrite_32(
        &SPIBSC[ch_no]->DRDMCR, spibsccfg->udef_dmycyc_num, SPIBSC_DRDMCR_DMCYC_SHIFT, SPIBSC_DRDMCR_DMCYC);
    /* address:SDR */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRDRENR, SPIBSC_SDR_TRANS, SPIBSC_DRDRENR_ADDRE_SHIFT, SPIBSC_DRDRENR_ADDRE);
    /* option data:SDR */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRDRENR, SPIBSC_SDR_TRANS, SPIBSC_DRDRENR_OPDRE_SHIFT, SPIBSC_DRDRENR_OPDRE);
    /* data read: SDR */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRDRENR, SPIBSC_SDR_TRANS, SPIBSC_DRDRENR_DRDRE_SHIFT, SPIBSC_DRDRENR_DRDRE);

    return 0;

} /* End of function spibsc_dr_init() */

/*******************************************************************************
* Function Name: int32_t spibsc_exmode(void);
* Description  :
* Arguments    : uint32_t ch_no
* Return Value :  0 :
*              : -1 : error
*******************************************************************************/
int32_t spibsc_exmode(uint32_t ch_no)
{
    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_MD_SHIFT, SPIBSC_CMNCR_MD) != SPIBSC_CMNCR_MD_EXTRD)
    {
        if (spibsc_stop(ch_no) < 0)
        {
            return -1;
        }

        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_MD_EXTRD, SPIBSC_CMNCR_MD_SHIFT, SPIBSC_CMNCR_MD);
    }

    return 0;

} /* End of function spibsc_exmode() */

/*******************************************************************************
* Function Name: int32_t spibsc_spimode(void);
* Description  :
* Arguments    : uint32_t ch_no
* Return Value :  0 :
*              : -1 : error
*******************************************************************************/
int32_t spibsc_spimode(uint32_t ch_no)
{
    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_MD_SHIFT, SPIBSC_CMNCR_MD) != SPIBSC_CMNCR_MD_SPI)
    {
        if (spibsc_stop(ch_no) < 0)
        {
            return -1;
        }

        /* SPI Mode */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_MD_SPI, SPIBSC_CMNCR_MD_SHIFT, SPIBSC_CMNCR_MD);
    }

    return 0;

} /* End of function spibsc_spimode() */

/*******************************************************************************
* Function Name: int32_t spibsc_stop(void);
* Description  :
* Arguments    : uint32_t ch_no
* Return Value :  0 :
*              : -1 : error
*******************************************************************************/
int32_t spibsc_stop(uint32_t ch_no)
{
    if (ch_no > 1)
    {
        return -1;
    }

    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->DRCR, 1, SPIBSC_DRCR_SSLN_SHIFT, SPIBSC_DRCR_SSLN);
    while (1)
    {
        if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNSR, SPIBSC_CMNSR_SSLF_SHIFT, SPIBSC_CMNSR_SSLF) == SPIBSC_SSL_NEGATE)
        {
            break;
        }
    }
    return 0;
} /* End of function spibsc_stop() */

/******************************************************************************
* Function Name: spibsc_transfer
* Description  : Transmission setting of a SPI multi-I/O bus controller.
* Arguments    : uint32_t ch_no : use channel No
*                 st_spibsc_spimd_reg_t *regset :
*                    The pointer to a structure for the transfer
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t spibsc_transfer(uint32_t ch_no, st_spibsc_spimd_reg_t* regset)
{
    if (ch_no > 1)
    {
        return -1;
    }

    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_MD_SHIFT, SPIBSC_CMNCR_MD) != SPIBSC_CMNCR_MD_SPI)
    {
        if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNSR, SPIBSC_CMNSR_SSLF_SHIFT, SPIBSC_CMNSR_SSLF) != SPIBSC_SSL_NEGATE)
        {
            return -1;
        }

        /* SPI Mode */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->CMNCR, SPIBSC_CMNCR_MD_SPI, SPIBSC_CMNCR_MD_SHIFT, SPIBSC_CMNCR_MD);
    }

    if (RZA_IO_RegRead_32(&SPIBSC[ch_no]->CMNSR, SPIBSC_CMNSR_TEND_SHIFT, SPIBSC_CMNSR_TEND) != SPIBSC_TRANS_END)
    {
        return -1;
    }

    /* ---- Command ---- */
    /* Enable/Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->cde, SPIBSC_SMENR_CDE_SHIFT, SPIBSC_SMENR_CDE);
    if (regset->cde != SPIBSC_OUTPUT_DISABLE)
    {
        /* Command */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMCMR, regset->cmd, SPIBSC_SMCMR_CMD_SHIFT, SPIBSC_SMCMR_CMD);
        /* Single/Dual/Quad */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->cdb, SPIBSC_SMENR_CDB_SHIFT, SPIBSC_SMENR_CDB);
    }

    /* ---- Option Command ---- */
    /* Enable/Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->ocde, SPIBSC_SMENR_OCDE_SHIFT, SPIBSC_SMENR_OCDE);

    if (regset->ocde != SPIBSC_OUTPUT_DISABLE)
    {
        /* Option Command */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMCMR, regset->ocmd, SPIBSC_SMCMR_OCMD_SHIFT, SPIBSC_SMCMR_OCMD);
        /* Single/Dual/Quad */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->ocdb, SPIBSC_SMENR_OCDB_SHIFT, SPIBSC_SMENR_OCDB);
    }

    /* ---- Address ---- */
    /* Enable/Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->ade, SPIBSC_SMENR_ADE_SHIFT, SPIBSC_SMENR_ADE);

    if (regset->ade != SPIBSC_OUTPUT_DISABLE)
    {
        /* Address */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMADR, regset->addr, SPIBSC_SMADR_ADR_SHIFT, SPIBSC_SMADR_ADR);
        /* Single/Dual/Quad */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->adb, SPIBSC_SMENR_ADB_SHIFT, SPIBSC_SMENR_ADB);
    }

    /* ---- Option Data ---- */
    /* Enable/Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->opde, SPIBSC_SMENR_OPDE_SHIFT, SPIBSC_SMENR_OPDE);

    if (regset->opde != SPIBSC_OUTPUT_DISABLE)
    {
        /* Option Data */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMOPR, regset->opd[0], SPIBSC_SMOPR_OPD3_SHIFT, SPIBSC_SMOPR_OPD3);
        /* Option Data */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMOPR, regset->opd[1], SPIBSC_SMOPR_OPD2_SHIFT, SPIBSC_SMOPR_OPD2);
        /* Option Data */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMOPR, regset->opd[2], SPIBSC_SMOPR_OPD1_SHIFT, SPIBSC_SMOPR_OPD1);
        /* Option Data */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMOPR, regset->opd[3], SPIBSC_SMOPR_OPD0_SHIFT, SPIBSC_SMOPR_OPD0);
        /* Single/Dual/Quad */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->opdb, SPIBSC_SMENR_OPDB_SHIFT, SPIBSC_SMENR_OPDB);
    }

    /* ---- Dummy ---- */
    /* Enable/Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->dme, SPIBSC_SMENR_DME_SHIFT, SPIBSC_SMENR_DME);
    if (regset->dme != SPIBSC_DUMMY_CYC_DISABLE)
    {
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMDMCR, regset->dmdb, SPIBSC_SMDMCR_DMDB_SHIFT, SPIBSC_SMDMCR_DMDB);
        /* Dummy Cycle */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMDMCR, regset->dmcyc, SPIBSC_SMDMCR_DMCYC_SHIFT, SPIBSC_SMDMCR_DMCYC);
    }

    /* ---- Data ---- */
    /* Enable/Disable */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->spide, SPIBSC_SMENR_SPIDE_SHIFT, SPIBSC_SMENR_SPIDE);
    if (regset->spide != SPIBSC_OUTPUT_DISABLE)
    {
        SPIBSC[ch_no]->SMWDR0.UINT32 = regset->smwdr[0];
        SPIBSC[ch_no]->SMWDR1.UINT32 = regset->smwdr[1]; /* valid in two serial-flash */
        /* Single/Dual/Quad */
        RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMENR, regset->spidb, SPIBSC_SMENR_SPIDB_SHIFT, SPIBSC_SMENR_SPIDB);
    }

    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMCR, regset->sslkp, SPIBSC_SMCR_SSLKP_SHIFT, SPIBSC_SMCR_SSLKP);

    if ((regset->spidb != SPIBSC_1BIT) && (regset->spide != SPIBSC_OUTPUT_DISABLE))
    {
        if ((regset->spire == SPIBSC_SPIDATA_ENABLE) && (regset->spiwe == SPIBSC_SPIDATA_ENABLE))
        {
            /* not set in same time */
            return (-1);
        }
    }

    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMCR, regset->spire, SPIBSC_SMCR_SPIRE_SHIFT, SPIBSC_SMCR_SPIRE);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMCR, regset->spiwe, SPIBSC_SMCR_SPIWE_SHIFT, SPIBSC_SMCR_SPIWE);

    /* SDR Transmission/DDR Transmission Setting */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMDRENR, regset->addre, SPIBSC_SMDRENR_ADDRE_SHIFT, SPIBSC_SMDRENR_ADDRE);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMDRENR, regset->opdre, SPIBSC_SMDRENR_OPDRE_SHIFT, SPIBSC_SMDRENR_OPDRE);
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMDRENR, regset->spidre, SPIBSC_SMDRENR_SPIDRE_SHIFT, SPIBSC_SMDRENR_SPIDRE);

    /* execute after setting SPNDL bit */
    RZA_IO_RegWrite_32(&SPIBSC[ch_no]->SMCR, SPIBSC_SPI_ENABLE, SPIBSC_SMCR_SPIE_SHIFT, SPIBSC_SMCR_SPIE);

    /* wait for transfer-start */
    R_SFLASH_WaitTend(ch_no);

    regset->smrdr[0] = SPIBSC[ch_no]->SMRDR0.UINT32;
    regset->smrdr[1] = SPIBSC[ch_no]->SMRDR1.UINT32; /* valid in two serial-flash  */

    return (0);

} /* End of function spibsc_transfer() */

/******************************************************************************
* Function Name: io_spibsc_port_setting
* Description  : Port Setting of SPIBSC
* Arguments    : uint32_t ch_no : use channel No
*                int_t data_bus_width
*                uint32_t bsz : BSZ bit
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
static int32_t io_spibsc_port_setting(uint32_t ch_no, int_t data_bus_width, uint32_t bsz)
{
    if (ch_no > 1)
    {
        return (-1);
    }

    if (0 == ch_no)
    {
        /* ==== P9_2 : SPBCLK_0 ==== */
        /* port function : SPBCLK_0     */
        RZA_IO_RegWrite_16(&GPIO.PFCAE9, 0, GPIO_PFCAE9_PFCAE92_SHIFT, GPIO_PFCAE9_PFCAE92);
        RZA_IO_RegWrite_16(&GPIO.PFCE9, 0, GPIO_PFCE9_PFCE92_SHIFT, GPIO_PFCE9_PFCE92);
        RZA_IO_RegWrite_16(&GPIO.PFC9, 1, GPIO_PFC9_PFC92_SHIFT, GPIO_PFC9_PFC92);

        /* port mode : Both use mode(The 2nd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC9, 1, GPIO_PMC9_PMC92_SHIFT, GPIO_PMC9_PMC92);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC9, 1, GPIO_PIPC9_PIPC92_SHIFT, GPIO_PIPC9_PIPC92);

        /* ==== P9_3 : SPBSSL_0 ==== */
        /* port function : SPBSSL_0     */
        RZA_IO_RegWrite_16(&GPIO.PFCAE9, 0, GPIO_PFCAE9_PFCAE93_SHIFT, GPIO_PFCAE9_PFCAE93);
        RZA_IO_RegWrite_16(&GPIO.PFCE9, 0, GPIO_PFCE9_PFCE93_SHIFT, GPIO_PFCE9_PFCE93);
        RZA_IO_RegWrite_16(&GPIO.PFC9, 1, GPIO_PFC9_PFC93_SHIFT, GPIO_PFC9_PFC93);

        /* port mode : Both use mode(The 2nd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC9, 1, GPIO_PMC9_PMC93_SHIFT, GPIO_PMC9_PMC93);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC9, 1, GPIO_PIPC9_PIPC93_SHIFT, GPIO_PIPC9_PIPC93);

        /* ==== P9_4 : SPBIO00_0 ==== */
        /* port function : SPBIO00_0    */
        RZA_IO_RegWrite_16(&GPIO.PFCAE9, 0, GPIO_PFCAE9_PFCAE94_SHIFT, GPIO_PFCAE9_PFCAE94);
        RZA_IO_RegWrite_16(&GPIO.PFCE9, 0, GPIO_PFCE9_PFCE94_SHIFT, GPIO_PFCE9_PFCE94);
        RZA_IO_RegWrite_16(&GPIO.PFC9, 1, GPIO_PFC9_PFC94_SHIFT, GPIO_PFC9_PFC94);

        /* port mode : Both use mode(The 2nd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC9, 1, GPIO_PMC9_PMC94_SHIFT, GPIO_PMC9_PMC94);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC9, 1, GPIO_PIPC9_PIPC94_SHIFT, GPIO_PIPC9_PIPC94);

        /* ==== P9_5 : SPBIO10_0 ==== */
        /* port function : SPBIO10_0    */
        RZA_IO_RegWrite_16(&GPIO.PFCAE9, 0, GPIO_PFCAE9_PFCAE95_SHIFT, GPIO_PFCAE9_PFCAE95);
        RZA_IO_RegWrite_16(&GPIO.PFCE9, 0, GPIO_PFCE9_PFCE95_SHIFT, GPIO_PFCE9_PFCE95);
        RZA_IO_RegWrite_16(&GPIO.PFC9, 1, GPIO_PFC9_PFC95_SHIFT, GPIO_PFC9_PFC95);

        /* port mode : Both use mode(The 2nd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC9, 1, GPIO_PMC9_PMC95_SHIFT, GPIO_PMC9_PMC95);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC9, 1, GPIO_PIPC9_PIPC95_SHIFT, GPIO_PIPC9_PIPC95);

        if (data_bus_width == SPIBSC_4BIT)
        {
            /* ==== P9_6 : SPBIO20_0 ==== */
            /* port function : SPBIO20_0    */
            RZA_IO_RegWrite_16(&GPIO.PFCAE9, 0, GPIO_PFCAE9_PFCAE96_SHIFT, GPIO_PFCAE9_PFCAE96);
            RZA_IO_RegWrite_16(&GPIO.PFCE9, 0, GPIO_PFCE9_PFCE96_SHIFT, GPIO_PFCE9_PFCE96);
            RZA_IO_RegWrite_16(&GPIO.PFC9, 1, GPIO_PFC9_PFC96_SHIFT, GPIO_PFC9_PFC96);

            /* port mode : Both use mode(The 2nd both use) */
            RZA_IO_RegWrite_16(&GPIO.PMC9, 1, GPIO_PMC9_PMC96_SHIFT, GPIO_PMC9_PMC96);

            /* Input/output control mode : peripheral function */
            RZA_IO_RegWrite_16(&GPIO.PIPC9, 1, GPIO_PIPC9_PIPC96_SHIFT, GPIO_PIPC9_PIPC96);

            /* ==== P9_7 : SPBIO30_0 ==== */
            /* port function : SPBIO30_0    */
            RZA_IO_RegWrite_16(&GPIO.PFCAE9, 0, GPIO_PFCAE9_PFCAE97_SHIFT, GPIO_PFCAE9_PFCAE97);
            RZA_IO_RegWrite_16(&GPIO.PFCE9, 0, GPIO_PFCE9_PFCE97_SHIFT, GPIO_PFCE9_PFCE97);
            RZA_IO_RegWrite_16(&GPIO.PFC9, 1, GPIO_PFC9_PFC97_SHIFT, GPIO_PFC9_PFC97);

            /* port mode : Both use mode(The 2nd both use) */
            RZA_IO_RegWrite_16(&GPIO.PMC9, 1, GPIO_PMC9_PMC97_SHIFT, GPIO_PMC9_PMC97);

            /* Input/output control mode : peripheral function */
            RZA_IO_RegWrite_16(&GPIO.PIPC9, 1, GPIO_PIPC9_PIPC97_SHIFT, GPIO_PIPC9_PIPC97);

            if (SPIBSC_CMNCR_BSZ_DUAL == bsz)
            {
                /* ==== P2_12 : SPBIO01_0 ==== */
                /* port function : SPBIO01_0    */
                RZA_IO_RegWrite_16(&GPIO.PFCAE2, 0, GPIO_PFCAE2_PFCAE212_SHIFT, GPIO_PFCAE2_PFCAE212);
                RZA_IO_RegWrite_16(&GPIO.PFCE2, 1, GPIO_PFCE2_PFCE212_SHIFT, GPIO_PFCE2_PFCE212);
                RZA_IO_RegWrite_16(&GPIO.PFC2, 1, GPIO_PFC2_PFC212_SHIFT, GPIO_PFC2_PFC212);

                /* port mode : Both use mode(The 4th both use) */
                RZA_IO_RegWrite_16(&GPIO.PMC2, 1, GPIO_PMC2_PMC212_SHIFT, GPIO_PMC2_PMC212);

                /* Input/output control mode : peripheral function */
                RZA_IO_RegWrite_16(&GPIO.PIPC2, 1, GPIO_PIPC2_PIPC212_SHIFT, GPIO_PIPC2_PIPC212);

                /* ==== P2_13 : SPBIO11_0 ==== */
                /* port function : SPBIO11_0    */
                RZA_IO_RegWrite_16(&GPIO.PFCAE2, 0, GPIO_PFCAE2_PFCAE213_SHIFT, GPIO_PFCAE2_PFCAE213);
                RZA_IO_RegWrite_16(&GPIO.PFCE2, 1, GPIO_PFCE2_PFCE213_SHIFT, GPIO_PFCE2_PFCE213);
                RZA_IO_RegWrite_16(&GPIO.PFC2, 1, GPIO_PFC2_PFC213_SHIFT, GPIO_PFC2_PFC213);

                /* port mode : Both use mode(The 4th both use) */
                RZA_IO_RegWrite_16(&GPIO.PMC2, 1, GPIO_PMC2_PMC213_SHIFT, GPIO_PMC2_PMC213);

                /* Input/output control mode : peripheral function */
                RZA_IO_RegWrite_16(&GPIO.PIPC2, 1, GPIO_PIPC2_PIPC213_SHIFT, GPIO_PIPC2_PIPC213);

                /* ==== P2_14 : SPBIO21_0 ==== */
                /* port function : SPBIO21_0    */
                RZA_IO_RegWrite_16(&GPIO.PFCAE2, 0, GPIO_PFCAE2_PFCAE214_SHIFT, GPIO_PFCAE2_PFCAE214);
                RZA_IO_RegWrite_16(&GPIO.PFCE2, 1, GPIO_PFCE2_PFCE214_SHIFT, GPIO_PFCE2_PFCE214);
                RZA_IO_RegWrite_16(&GPIO.PFC2, 1, GPIO_PFC2_PFC214_SHIFT, GPIO_PFC2_PFC214);

                /* port mode : Both use mode(The 4th both use) */
                RZA_IO_RegWrite_16(&GPIO.PMC2, 1, GPIO_PMC2_PMC214_SHIFT, GPIO_PMC2_PMC214);

                /* Input/output control mode : peripheral function */
                RZA_IO_RegWrite_16(&GPIO.PIPC2, 1, GPIO_PIPC2_PIPC214_SHIFT, GPIO_PIPC2_PIPC214);

                /* ==== P2_15 : SPBIO31_0 ==== */
                /* port function : SPBIO31_0    */
                RZA_IO_RegWrite_16(&GPIO.PFCAE2, 0, GPIO_PFCAE2_PFCAE215_SHIFT, GPIO_PFCAE2_PFCAE215);
                RZA_IO_RegWrite_16(&GPIO.PFCE2, 1, GPIO_PFCE2_PFCE215_SHIFT, GPIO_PFCE2_PFCE215);
                RZA_IO_RegWrite_16(&GPIO.PFCE2, 1, GPIO_PFCE2_PFCE215_SHIFT, GPIO_PFCE2_PFCE215);
                RZA_IO_RegWrite_16(&GPIO.PFC2, 1, GPIO_PFC2_PFC215_SHIFT, GPIO_PFC2_PFC215);

                /* port mode : Both use mode(The 4th both use) */
                RZA_IO_RegWrite_16(&GPIO.PMC2, 1, GPIO_PMC2_PMC215_SHIFT, GPIO_PMC2_PMC215);

                /* Input/output control mode : peripheral function */
                RZA_IO_RegWrite_16(&GPIO.PIPC2, 1, GPIO_PIPC2_PIPC215_SHIFT, GPIO_PIPC2_PIPC215);
            }
        }
    }
    else
    {
        /* ==== P8_2 : SPBCLK_0 ==== */
        /* port function : SPBCLK_1     */
        RZA_IO_RegWrite_16(&GPIO.PFCAE8, 0, GPIO_PFCAE8_PFCAE812_SHIFT, GPIO_PFCAE8_PFCAE812);
        RZA_IO_RegWrite_16(&GPIO.PFCE8, 1, GPIO_PFCE8_PFCE812_SHIFT, GPIO_PFCE8_PFCE812);
        RZA_IO_RegWrite_16(&GPIO.PFC8, 0, GPIO_PFC8_PFC812_SHIFT, GPIO_PFC8_PFC812);

        /* port mode : Both use mode(The 3rd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC8, 1, GPIO_PMC8_PMC812_SHIFT, GPIO_PMC8_PMC812);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC8, 1, GPIO_PIPC8_PIPC812_SHIFT, GPIO_PIPC8_PIPC812);

        /* ==== P8_3 : SPBSSL_0 ==== */
        /* port function : SPBSSL_1     */
        RZA_IO_RegWrite_16(&GPIO.PFCAE8, 0, GPIO_PFCAE8_PFCAE813_SHIFT, GPIO_PFCAE8_PFCAE813);
        RZA_IO_RegWrite_16(&GPIO.PFCE8, 1, GPIO_PFCE8_PFCE813_SHIFT, GPIO_PFCE8_PFCE813);
        RZA_IO_RegWrite_16(&GPIO.PFC8, 0, GPIO_PFC8_PFC813_SHIFT, GPIO_PFC8_PFC813);

        /* port mode : Both use mode(The 3rd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC8, 1, GPIO_PMC8_PMC813_SHIFT, GPIO_PMC8_PMC813);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC8, 1, GPIO_PIPC8_PIPC813_SHIFT, GPIO_PIPC8_PIPC813);

        /* ==== P8_4 : SPBIO00_0 ==== */
        /* port function : SPBIO0_1    */
        RZA_IO_RegWrite_16(&GPIO.PFCAE8, 0, GPIO_PFCAE8_PFCAE814_SHIFT, GPIO_PFCAE8_PFCAE814);
        RZA_IO_RegWrite_16(&GPIO.PFCE8, 1, GPIO_PFCE8_PFCE814_SHIFT, GPIO_PFCE8_PFCE814);
        RZA_IO_RegWrite_16(&GPIO.PFC8, 0, GPIO_PFC8_PFC814_SHIFT, GPIO_PFC8_PFC814);

        /* port mode : Both use mode(The 3rd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC8, 1, GPIO_PMC8_PMC814_SHIFT, GPIO_PMC8_PMC814);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC8, 1, GPIO_PIPC8_PIPC814_SHIFT, GPIO_PIPC8_PIPC814);

        /* ==== P8_5 : SPBIO10_0 ==== */
        /* port function : SPBIO1_1    */
        RZA_IO_RegWrite_16(&GPIO.PFCAE8, 0, GPIO_PFCAE8_PFCAE815_SHIFT, GPIO_PFCAE8_PFCAE815);
        RZA_IO_RegWrite_16(&GPIO.PFCE8, 1, GPIO_PFCE8_PFCE815_SHIFT, GPIO_PFCE8_PFCE815);
        RZA_IO_RegWrite_16(&GPIO.PFC8, 0, GPIO_PFC8_PFC815_SHIFT, GPIO_PFC8_PFC815);

        /* port mode : Both use mode(The 3rd both use) */
        RZA_IO_RegWrite_16(&GPIO.PMC8, 1, GPIO_PMC8_PMC815_SHIFT, GPIO_PMC8_PMC815);

        /* Input/output control mode : peripheral function */
        RZA_IO_RegWrite_16(&GPIO.PIPC8, 1, GPIO_PIPC8_PIPC815_SHIFT, GPIO_PIPC8_PIPC815);

        if (SPIBSC_4BIT == data_bus_width)
        {
            /* ==== P8_10 : SPBIO20_0 ==== */
            /* port function : SPBIO2_1    */
            RZA_IO_RegWrite_16(&GPIO.PFCAE8, 0, GPIO_PFCAE8_PFCAE810_SHIFT, GPIO_PFCAE8_PFCAE810);
            RZA_IO_RegWrite_16(&GPIO.PFCE8, 1, GPIO_PFCE8_PFCE810_SHIFT, GPIO_PFCE8_PFCE810);
            RZA_IO_RegWrite_16(&GPIO.PFC8, 0, GPIO_PFC8_PFC810_SHIFT, GPIO_PFC8_PFC810);

            /* port mode : Both use mode(The 3rd both use) */
            RZA_IO_RegWrite_16(&GPIO.PMC8, 1, GPIO_PMC8_PMC810_SHIFT, GPIO_PMC8_PMC810);

            /* Input/output control mode : peripheral function */
            RZA_IO_RegWrite_16(&GPIO.PIPC8, 1, GPIO_PIPC8_PIPC810_SHIFT, GPIO_PIPC8_PIPC810);

            /* ==== P8_11 : SPBIO3_1 ==== */
            /* port function : SPBIO3_1    */
            RZA_IO_RegWrite_16(&GPIO.PFCAE8, 0, GPIO_PFCAE8_PFCAE811_SHIFT, GPIO_PFCAE8_PFCAE811);
            RZA_IO_RegWrite_16(&GPIO.PFCE8, 1, GPIO_PFCE8_PFCE811_SHIFT, GPIO_PFCE8_PFCE811);
            RZA_IO_RegWrite_16(&GPIO.PFC8, 0, GPIO_PFC8_PFC811_SHIFT, GPIO_PFC8_PFC811);

            /* port mode : Both use mode(The 3rd both use) */
            RZA_IO_RegWrite_16(&GPIO.PMC8, 1, GPIO_PMC8_PMC811_SHIFT, GPIO_PMC8_PMC811);

            /* Input/output control mode : peripheral function */
            RZA_IO_RegWrite_16(&GPIO.PIPC8, 1, GPIO_PIPC8_PIPC811_SHIFT, GPIO_PIPC8_PIPC811);
        }
    }

    return (0);

} /* End of function io_spibsc_port_setting() */

/* End of File */
