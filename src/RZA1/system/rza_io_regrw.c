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
* File Name     : rza_io_regrw.c
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Low level register read/write
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/*******************************************************************************
Includes   <System Includes> , "Project Includes"
*******************************************************************************/
/* Default  type definition header */
#include "RZA1/system/r_typedefs.h"

/*******************************************************************************
* Function Name: rza_io_reg_write_8
* Description  : IO register 8-bit write
* Arguments    : volatile uint8_t * ioreg : IO register for writing
*              :                          : Use register definition name of the
*              :                          : iodefine.h
*              : uint8_t write_value      : Write value for the IO register
*              : uint8_t shift            : The number of left shifts to the 
*              :                          : target bit
*              : uint8_t mask             : Mask value for the IO register
*              :                          : (Target bit : "1")
* Return Value : None
*******************************************************************************/
void RZA_IO_RegWrite_8 (volatile uint8_t * ioreg,
		                         uint8_t write_value,
		                         uint8_t shift,
		                         uint8_t mask)
{
    uint8_t reg_value;

    /* Read from register */
    reg_value = (*ioreg);

    /* Modify value       */
    reg_value = (uint8_t)((reg_value & (~mask)) | (write_value << shift));

    /* Write to register  */
    (*ioreg)    = reg_value;
}

/*******************************************************************************
* Function Name: rza_io_reg_write_16
* Description  : IO register 16-bit write
* Arguments    : volatile uint16_t * ioreg : IO register for writing
*              :                           : Use register definition name of the
*              :                           : iodefine.h
*              : uint16_t write_value      : Write value for the IO register
*              : uint16_t shift            : The number of left shifts to the 
*              :                           : target bit
*              : uint16_t mask             : Mask value for the IO register
*              :                           : (Target bit : "1")
* Return Value : None
*******************************************************************************/
void RZA_IO_RegWrite_16 (volatile uint16_t * ioreg,
		                          uint16_t write_value,
		                          uint16_t shift,
		                          uint16_t mask)
{
    uint16_t reg_value;

    /* Read from register */
    reg_value = (*ioreg);

    /* Modify value       */
    reg_value = (uint16_t)((reg_value & (~mask)) | (write_value << shift));

    /* Write to register  */
    (*ioreg)    = reg_value;
}

/*******************************************************************************
* Function Name: rza_io_reg_write_32
* Description  : IO register 32-bit write
* Arguments    : volatile uint32_t * ioreg : IO register for writing
*              :                           : Use register definition name of the
*              :                           : iodefine.h
*              : uint32_t write_value      : Write value for the IO register
*              : uint32_t shift            : The number of left shifts to the 
*              :                           : target bit
*              : uint32_t mask             : Mask value for the IO register
*              :                           : (Target bit : "1")
* Return Value : None
*******************************************************************************/
void RZA_IO_RegWrite_32 (volatile uint32_t * ioreg, uint32_t write_value,
                                  uint32_t shift,   uint32_t mask)
{
    uint32_t reg_value;

    /* Read from register */
    reg_value = (*ioreg);

    /* Modify value       */
    reg_value = ((reg_value & (~mask)) | (write_value << shift));

    /* Write to register  */
    (*ioreg)    = reg_value;
}

/*******************************************************************************
* Function Name: rza_io_reg_read_8
* Description  : IO register 8-bit read
* Arguments    : volatile uint8_t * ioreg : IO register for reading
*              :                          : Use register definition name of the 
*              :                          : iodefine.h
*              : uint8_t shift            : The number of right shifts to the 
*              :                          : target bit
*              : uint8_t mask             : Mask bit for the IO register
*              :                          : (Target bit: "1")
* Return Value : uint8_t : Value of the obtained target bit
*******************************************************************************/
uint8_t RZA_IO_RegRead_8 (volatile uint8_t * ioreg, uint8_t shift, uint8_t mask)
{
    uint8_t reg_value;

    /* Read from register */
    reg_value = (*ioreg);

    /* Clear other bit and Bit shift */
    reg_value = (uint8_t)((reg_value & mask) >> shift);

    return (reg_value);
}

/*******************************************************************************
* Function Name: rza_io_reg_read_16
* Description  : IO register 16-bit read
* Arguments    : volatile uint16_t * ioreg : IO register for reading
*              :                           : Use register definition name of the 
*              :                           : iodefine.h
*              : uint16_t shift            : The number of right shifts to the 
*              :                           : target bit
*              : uint16_t mask             : Mask bit for the IO register
*              :                           : (Target bit: "1")
* Return Value : uint16_t : Value of the obtained target bit
*******************************************************************************/
uint16_t RZA_IO_RegRead_16 (volatile uint16_t * ioreg,
		                                          uint16_t shift, uint16_t mask)
{
    uint16_t reg_value;

    /* Read from register */
    reg_value = (*ioreg);

    /* Clear other bit and Bit shift */
    reg_value = (uint16_t)((reg_value & mask) >> shift);

    return (reg_value);
}

/*******************************************************************************
* Function Name: rza_io_reg_read_32
* Description  : IO register 32-bit read
* Arguments    : volatile uint32_t * ioreg : IO register for reading
*              :                           : Use register definition name of the 
*              :                           : iodefine.h
*              : uint32_t shift            : The number of right shifts to the 
*              :                           : target bit
*              : uint32_t mask             : Mask bit for the IO register
*              :                           : (Target bit: "1")
* Return Value : uint32_t : Value of the obtained target bit
*******************************************************************************/
uint32_t RZA_IO_RegRead_32 (volatile uint32_t * ioreg, uint32_t shift,
                                                                 uint32_t mask)
{
    uint32_t reg_value;

    /* Read from register */
    reg_value = (*ioreg);

    /* Clear other bit and Bit shift */
    reg_value = ((reg_value & mask) >> shift);

    return (reg_value);
}


/* End of File */
