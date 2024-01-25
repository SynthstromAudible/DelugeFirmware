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
 * File Name     : bsc_userdef.c
 * Device(s)     : RZ/A1H (R7S721001)
 * Tool-Chain    : GNUARM-NONEv14.02-EABI
 * H/W Platform  : RSK+RZA1H CPU Board
 * Description   : Common driver (User define function)
 *******************************************************************************/
/*******************************************************************************
 * History       : DD.MM.YYYY Version Description
 *               : 21.10.2014 1.00
 *******************************************************************************/

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
/* Default  type definition header */
#include "RZA1/system/r_typedefs.h"
/* Device driver header */
#include "RZA1/system/dev_drv.h"
/* Common Driver header */
#include "RZA1/bsc/bsc_userdef.h"
/* I/O Register root header */
#include "RZA1/system/iodefine.h"
/* Bus State Controller User header */
#include "RZA1/bsc/bsc_userdef.h"

/******************************************************************************
Typedef definitions
******************************************************************************/

/******************************************************************************
Macro definitions
******************************************************************************/
/* The address when writing in a SDRAM mode register */
#define SDRAM_MODE_CS2 (*(volatile uint16_t*)(0x3FFFD040))
#define SDRAM_MODE_CS3 (*(volatile uint16_t*)(0x3FFFE040))

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/

/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/

/******************************************************************************
Private global variables and functions
******************************************************************************/

/******************************************************************************
 * Function Name: userdef_bsc_cs0Init
 * Description  : This is the user-defined function called by the BSC_Init
 *              : function. The setting for initialisation of the BSC in the CS0
 *              : space is required. In this sample code, the setting to use
 *              : the NOR flash memory in the CS0 space is executed. Sets the BSC
 *              : to connect the Spansion NOR flash memory S29GL512S10T to the
 *              : CS0 space with 16-bit bus width.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void userdef_bsc_cs0_init(void)
{
    /* ---- CS0BCR settings ---- */
    /* Idle Cycles between Write-read Cycles    */
    /*  and Write-write Cycles : 1 idle cycle   */
    /* Data Bus Size: 16-bit                    */
    BSC.CS0BCR = 0x10000C00uL;

    /* ---- CS0WCR settings ----  */
    /* Number of Delay Cycles from Address,     */
    /*  CS0# Assertion to RD#,WEn Assertion     */
    /*  : 1.5 cycles                            */
    /* Number of Access Wait Cycles: 6 cycles   */
    /* Delay Cycles from RD,WEn# negation to    */
    /*  Address,CSn# negation: 0.5 cycles       */
    BSC.CS0WCR = 0x00000B40uL;
}

/******************************************************************************
 * Function Name: userdef_bsc_cs1_init
 * Description  : This is the user-defined function called by the BSC_Init
 *              : function. The setting for initialisation of the BSC in the CS1
 *              : space is required. In this sample code, the setting to use
 *              : the NOR flash memory in the CS1 space is executed. Sets the BSC
 *              : to connect the Spansion NOR flash memory S29GL512S10T to the
 *              : CS1 space with 16-bit bus width.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void userdef_bsc_cs1_init(void)
{
    /* ---- CS1BCR settings ---- */
    /* Idle Cycles between Write-read Cycles    */
    /*  and Write-write Cycles : 1 idle cycle   */
    /* Data Bus Size: 16-bit                    */
    BSC.CS1BCR = 0x10000C00uL;

    /* ---- CS1WCR settings ----  */
    /* Number of Delay Cycles from Address,     */
    /*  CS1# Assertion to RD#,WEn Assertion     */
    /*  : 1.5 cycles                            */
    /* Number of Access Wait Cycles: 6 cycles   */
    /* Delay Cycles from RD,WEn# negation to    */
    /*  Address,CSn# negation: 0.5 cycles       */
    BSC.CS1WCR = 0x00000B40uL;
}

/******************************************************************************
 * Function Name: userdef_bsc_cs2Init
 * Description  : This is the user-defined function called by the BSC_Init
 *              : function. The setting for initialisation of the BSC in CS2
 *              : area is required. In this sample code, the setting to use
 *              : the SDRAM in the CS2 and CS3 space is executed as per the
 *              : note below. The function sets the BSC to connect the Micron
 *              : MT48LC16M16A2P-75 to the CS2 space with 16-bit bus width.
 *              : It assumes a second (not fitted) SDRAM device in CS3 area.
 *
 *         Note : This configuration is invalid for a single SDRAM and is a
 *              : known limitation of the RSK+ board. The port pin used by
 *              : CS3 is configured for LED0. To allow SDRAM operation CS2
 *              : and CS3 must be configured to SDRAM. Option link R164 must
 *              : NOT be fitted to avoid a Data Bus conflict on the SDRAM
 *              : and expansion buffers. In a new application with one SDRAM
 *              : always connect the SDRAM to CS3. On this RSK+ CS3 can not
 *              : be used in another configuration including the expansion
 *              : headers unless the SDRAM is completely disabled. For other
 *              : external memory mapped devices CS1 is available for use
 *              : with the expansion headers.
 *              : See the hardware manual Bus State Controller
 *              : section 8.4.3 CS2WCR(SDRAM)
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void userdef_bsc_cs2_init(uint8_t ramSize)
{
    /* ==== CS2BCR settings ==== */
    /* Idle Cycles between Write-read Cycles  */
    /* and Write-write Cycles : 0 idle cycles */
    /* Memory type :SDRAM                     */
    /* Data Bus Size : 16-bit                 */
    BSC.CS2BCR = 0x00004C00ul;

    /* ==== CS3BCR settings ==== */
    /* SDRAM WORKAROUND - see Note */
    /* Idle Cycles between Write-read Cycles  */
    /* and Write-write Cycles : 0 idle cycles */
    /* Memory type :SDRAM                     */
    /* Data Bus Size : 16-bit                 */
    BSC.CS3BCR = 0x00004C00ul;

    /* ==== CS2/3WCR settings ==== */
    /* Precharge completion wait cycles: 1 cycle     */
    /* Wait cycles between ACTV command              */
    /* and READ(A)/WRITE(A) command : 1 cycles       */
    /* CAS latency for Area 3 : 2 cycles             */
    /* Auto-precharge startup wait cycles : 2 cycles */
    /* Idle cycles from REF command/self-refresh     */
    /* Release to ACTV/REF/MRS command : 5 cycles    */

    // WTRCD (bits 10 and 11) made quite a big difference
    // AC3L (bits 7 and 8) can't be reduced

    BSC.CS3WCR = 0x00000088ul;

    /* SDRAM WORKAROUND - see Note */
    // BSC.CS2WCR = 0x00000480ul;

    /* ==== SDCR settings ==== */
    /* SDRAM WORKAROUND - see Note*/
    /* Row address for Area 2 : 13-bit    */
    /* Column Address for Area 2 : 9-bit  */
    /* Refresh Control :Refresh           */
    /* RMODE :Auto-refresh is performed   */
    /* BACTV :Auto-precharge mode         */
    /* Row address for Area 3 : 13-bit    */
    /* Column Address for Area 3 : 9-bit  */
    if (ramSize != 0)
        BSC.SDCR = 0x00110911ul; // CS3 cols 9-bit. For 32MB chip
    else
        BSC.SDCR = 0x00110912ul; // CS3 cols 10-bit. For 64MB chip

    /* ==== RTCOR settings ==== */
    /* 7.64usec / 240nsec              */
    /*   = 128(0x80)cycles per refresh */
    BSC.RTCOR = 0xA55A0080ul;

    /* ==== RTCSR settings ==== */
    /* initialisation sequence start */
    /* Clock select B-phy/4          */
    /* Refresh count :Once           */
    BSC.RTCSR = 0xA55A0008ul;

    /* ==== SDRAM Mode Register ==== */
    /* Burst read (burst length 1)./Burst write */
    SDRAM_MODE_CS2 = 0;

    /* SDRAM WORKAROUND - see Note */
    SDRAM_MODE_CS3 = 0;
}

/******************************************************************************
 * Function Name: userdef_bsc_cs3_init
 * Description  : This is the user-defined function called by the BSC_Init
 *              : function. The setting for initialisation of the BSC in the CS3
 *              : space is required. In this sample code, the setting to use
 *              : the SDRAM in the CS3 space is executed. Sets the BSC to
 *              : connect the ISSI IS42S16320B-75 to the CS3 space with 16-bit
 *              : bus width.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void userdef_bsc_cs3_init(void)
{
    /* SDRAM WORKAROUND - see Note inside function userdef_bsc_cs2_init */
}

/******************************************************************************
 * Function Name: userdef_bsc_cs4_init
 * Description  : This is the user-defined function called by the BSC_Init
 *              : function. The setting for initialisation of the CS4 space is
 *              : required.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void userdef_bsc_cs4_init(void)
{
    /* Add processing when using CS4 space */
}

/******************************************************************************
 * Function Name: userdef_bsc_cs5_init
 * Description  : This is the user-defined function called by the BSC_Init
 *              : function The setting for initialisation of the CS5 space is
 *              : required.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void userdef_bsc_cs5_init(void)
{
    /* Add processing when using CS5 space */
}

/* End of File */
