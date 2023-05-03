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
*  File Name   : sdio_direct.c
*  Contents    : io direct read and write
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
 * Summary      : direct read io register
 * Include      : 
 * Declaration  : int sdio_read_direct(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr);
 * Functions    : direct read io register from specified address (=adr) 
 *              : using CMD52 
 * Argument     : unsigned char *buff : data buffer
 *				: unsigned long func : access function number
 *              : unsigned long adr : access register address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_read_direct(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	hndl->error = SD_OK;

	/* ---- check card is mounted ---- */
	if(hndl->mount != SD_MOUNT_UNLOCKED_CARD){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* not mounted yet */
	}

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;	
	}

	/* ==== direct read and write io register ==== */
	_sdio_direct(hndl,buff,func,adr,0,0);

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : direct write io register
 * Include      : 
 * Declaration  : int sdio_write_direct(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr,unsigned long raw_flag);
 * Functions    : direct write io register from specified address (=adr) 
 *              : using CMD52 
 * Argument     : unsigned char *buff : data buffer
 *				: unsigned long func : access function number
 *              : unsigned long adr : access register address
 *              : unsigned long raw_flag : write mode
 *              : 	SD_IO_SIMPLE_WRITE : just write
 *              :	SD_IO_VERIFY_WRITE : read after write
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_write_direct(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr,
	unsigned long raw_flag)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	hndl->error = SD_OK;

	/* ---- check card is mounted ---- */
	if(hndl->mount != SD_MOUNT_UNLOCKED_CARD){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* not mounted yet */
	}

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;	
	}

	/* ==== direct read and write io register ==== */
	_sdio_direct(hndl,buff,func,adr,1,raw_flag);

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : direct read or write io register
 * Include      : 
 * Declaration  : int _sdio_direct(SDHNDL *hndl,unsigned char *buff,
 *              : 	unsigned long func,unsigned long adr,unsigned long rw_flag,
 *              : 		unsigned long raw_flag)
 * Functions    : direct read or write io register from specified address
 *              : (=adr) using CMD52 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : data buffer
 *				: unsigned long func : access function number
 *              : unsigned long adr : access register address
 *              : unsigned long rw_flag : read (= 0) or write (= 1)
 *              : unsigned long raw_flag : write mode
 *              : 	SD_IO_SIMPLE_WRITE : just write
 *              :	SD_IO_VERIFY_WRITE : read after write
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sdio_direct(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,unsigned long rw_flag,unsigned long raw_flag)
{
	unsigned long arg = 0;
	unsigned short cmd;

	/* ---- check media type ---- */
	if((hndl->media_type & SD_MEDIA_IO) == 0){
		_sd_set_err(hndl,SD_ERR_CARD_TYPE);
		return SD_ERR_CARD_TYPE;
	}

	/* check read register address and function number */
	if((func > 7) || (adr > 0x1ffff)){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;
	}

	arg = ((rw_flag << 31) | (func << 28) | (raw_flag << 27) | (adr << 9) | (unsigned long)*buff);

	if(rw_flag == 1){
		cmd = CMD52_W;		/* write */
	}
	else{
		cmd = CMD52_R;		/* read */
	}

	/* issue CMD */
	if(_sd_send_iocmd(hndl, cmd, arg) != SD_OK){
		return hndl->error;
	}

	/* Set Read data from R5 Respose */
	*buff = (unsigned char)(hndl->resp_status & 0x00ff);

	/* enable resp end and illegal access interrupts */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	return hndl->error;
}

/* End of File */
