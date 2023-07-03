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
* Copyright (C) 2013,2014 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
*  System Name : SD Driver
*  File Name   : sdio_int.c
*  Contents    : SDIO interrupt operations
*  Version     : 4.01.00
*  Device      : RZ/A1 Group
*  Tool-Chain  : 
*  OS          : None
*
*  Note        : 
*              
*
*  History     : May.30.2013 ver.4.00.00 Initial release
*              : Jun.30.2014 ver.4.01.00 Added compliler swtich (#ifdef __CC_ARM - #endif)
*******************************************************************************/
#include "../../../inc/sdif.h"
#include "../inc/access/sd.h"

#ifdef __CC_ARM
#pragma arm section code = "CODE_SDHI"
#pragma arm section rodata = "CONST_SDHI"
#pragma arm section rwdata = "DATA_SDHI"
#pragma arm section zidata = "BSS_SDHI"
#endif

/*****************************************************************************
 * ID           :
 * Summary      : set SDIO_INFO1 interrupt mask
 * Include      : 
 * Declaration  : int _sdio_set_int_mask(SDHNDL *hndl,unsigned short mask)
 * Functions    : set int_io_info_mask depend on the mask bits value
 *              : if mask bit is one, it is enabled
 *              : if mask bit is zero, it is disabled
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short mask : SDIO_INFO1_MASK bits value
 * Return       : SD_OK : end of succeed
 * Remark       : execute sdio_enable_int preceeded, to enable interrupt
 *****************************************************************************/
int	_sdio_set_int_mask(SDHNDL *hndl,unsigned short mask)
{
	sddev_loc_cpu(hndl->sd_port);
	
	/* ---- set int_io_info_mask ---- */
	hndl->int_io_info_mask |= mask;

	/* ---- set hardware mask ---- */
	sd_outp(hndl,SDIO_INFO1_MASK,(unsigned short)~(hndl->int_io_info_mask));

	sddev_unl_cpu(hndl->sd_port);

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : clear SDIO_INFO1 interrupt mask
 * Include      : 
 * Declaration  : int _sdio_clear_int_mask(SDHNDL *hndl,unsigned short mask)
 * Functions    : clear int_io_info_mask depend on the mask bits value
 *              : if mask bit is one, it is disabled
 *              : if mask bit is zero, it is enabled
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short mask : SDIO_INFO1_MASK bits value
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int	_sdio_clear_int_mask(SDHNDL *hndl,unsigned short mask)
{
	sddev_loc_cpu(hndl->sd_port);
	
	/* ---- clear int_io_info_mask ---- */
	hndl->int_io_info_mask &= (unsigned short)~mask;
	
	/* ---- clear hardware mask ---- */
	sd_outp(hndl,SDIO_INFO1_MASK,(unsigned short)~(hndl->int_io_info_mask));
	
	sddev_unl_cpu(hndl->sd_port);
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : clear int_sdio_info bits
 * Include      : 
 * Declaration  : int _sdio_clear_info(SDHNDL hndl,unsigned short clear)
 * Functions    : clear int_io_info depend on the clear value
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short clear : int_io_info clear bits value
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int	_sdio_clear_info(SDHNDL *hndl,unsigned short clear)
{
	sddev_loc_cpu(hndl->sd_port);
	
	/* ---- clear int_io_info ---- */
	hndl->int_io_info &= (unsigned short)~clear;

	sddev_unl_cpu(hndl->sd_port);
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SDIO_INFO1 interrupt elements
 * Include      : 
 * Declaration  : int _sdio_get_int(SDHNDL *hndl)
 * Functions    : get SDIO_INFO1 bits
 *              : examine enabled elements
 *              : hearafter, clear SDIO_INFO1 bits
 *              : save those bits to int_io_info
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sdio_get_int(SDHNDL *hndl)
{
	unsigned short info;

	/* get SDIO_INFO1 bits */
	info = (unsigned short)(sd_inp(hndl,SDIO_INFO1) & hndl->int_io_info_mask);
	
	/* save enabled elements */
	hndl->int_io_info = info;
	
	if((info & SDIO_MODE_IOMOD) == 0){
		/* clear SDIO_INFO1 bits */
		sd_outp(hndl,SDIO_INFO1,(unsigned short)~(info));
	}
	else{
		/* Set Mask of SDIO_MODE_IOMODE to mask SDIO Interrupts */
		hndl->int_io_info_mask &= ~SDIO_MODE_IOMOD;
		sd_outp(hndl,SDIO_INFO1_MASK, (unsigned short)~(hndl->int_io_info_mask) );
	}
	
	if(info){
		return SD_OK;	/* any interrupt occured */
	}
	
	return SD_ERR;	/* no interrupt occured */
}

/*****************************************************************************
 * ID           :
 * Summary      : check SDIO_INFO1 interrupt elements
 * Include      : 
 * Declaration  : int sdio_check_int(int sd_port);
 * Functions    : check SDIO_INFO1 interrupt elements
 *              : if any interrupt is detected, return SD_OK
 *              : if no interrupt is detected, return SD_ERR
 * Argument     : none
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_check_int(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(hndl->int_mode){
		/* ---- hardware interrupt mode ---- */
		if(hndl->int_io_info){
			return SD_OK;
		}
		else{
			return SD_ERR;
		}
	}

	/* ---- polling mode ---- */
	return _sdio_get_int(hndl);
}

/*****************************************************************************
 * ID           :
 * Summary      : SDIO_INFO1 interrupt handler
 * Include      : 
 * Declaration  : void sdio_int_handler(int sd_port);
 * Functions    : SD_INFO1 and SD_INFO2 interrupt handler
 *              : examine the relevant elements (without masked)
 *              : save those elements to int_info1 or int_info2
 *              : if a callback function is registered, call it
 * Argument     : none
 * Return       : none
 * Remark       : 
 *****************************************************************************/
void sdio_int_handler(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return ;
	}

	if(_sdio_get_int(hndl) == SD_OK){
		if(hndl->int_io_callback){
			(*hndl->int_io_callback)(sd_port);
		}
	}
	else{
		/* any interrupt should be occured! */
	}
}

/*****************************************************************************
 * ID           :
 * Summary      : register SDIO_INFO1 interrupt callback function
 * Include      : 
 * Declaration  : int sdio_set_intcallback(int sd_port, int (*callback)(int));
 * Functions    : register SDIO_INFO1 callback function
 *              : if SDIO_INFO1 interrupt are occured, call callback function
 * Argument     : int (*callback)(int) : callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_set_intcallback(int sd_port, int (*callback)(int))
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	hndl->int_io_callback = callback;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : enable SDHI detect interrupt from SDIO
 * Include      : 
 * Declaration  : int sdio_enable_int(int sd_port);
 * Functions    : enable SDHI detect interrupt from SDIO (=IRQ)
 * Argument     : none
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int sdio_enable_int(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	sddev_loc_cpu(hndl->sd_port);
	
	hndl->int_io_info &= ~SDIO_MODE_IOMOD;

	/* ---- clear IOIRQ ---- */
	sd_outp(hndl,SDIO_INFO1,(unsigned short)~(SDIO_MODE_IOMOD));

	/* Clear Mask of SDIO_MODE_IOMODE to Set SDIO Interrupts Enable */
	sd_outp(hndl,SDIO_INFO1_MASK,((unsigned short)(sd_inp(hndl,SDIO_INFO1_MASK) & ~SDIO_MODE_IOMOD)) | 0x06);

	hndl->int_io_info_mask |= SDIO_MODE_IOMOD;

	sddev_unl_cpu(hndl->sd_port);

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : disable SDHI detect interrupt from SDIO
 * Include      : 
 * Declaration  : int sdio_disable_int(int sd_port);
 * Functions    : disable SDHI detect interrupt from SDIO (=IRQ)
 * Argument     : none
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int sdio_disable_int(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	sddev_loc_cpu(hndl->sd_port);

	/* Set Mask of SDIO_MODE_IOMODE to mask SDIO Interrupts */
	sd_outp(hndl,SDIO_INFO1_MASK,  sd_inp(hndl,SDIO_INFO1_MASK) | SDIO_MODE_IOMOD);

	hndl->int_io_info_mask &= ~SDIO_MODE_IOMOD;

	sddev_unl_cpu(hndl->sd_port);

	return SD_OK;
}

/* End of File */
