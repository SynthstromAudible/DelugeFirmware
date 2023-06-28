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
*  File Name   : sd_int.c
*  Contents    : SD_INFO1 and SD_INFO2 interrupt
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
 * Summary      : set SD_INFO1 and SD_INFO2 interrupt mask
 * Include      : 
 * Declaration  : int _sd_set_int_mask(SDHNDL *hndl,unsigned short mask1,
 *              :     unsigned short mask2)
 * Functions    : set int_info1_mask and int_info2_mask depend on the mask bits
 *              : value
 *              : if mask bit is one, it is enabled
 *              : if mask bit is zero, it is disabled
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short mask1 : SD_INFO1_MASK1 bits value
 *              : unsigned short mask2 : SD_INFO1_MASK2 bits value
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int	_sd_set_int_mask(SDHNDL *hndl,unsigned short mask1,unsigned short mask2)
{
	sddev_loc_cpu(hndl->sd_port);

	/* ---- set int_info1_mask and int_info2_mask ---- */
	hndl->int_info1_mask |= mask1;
	hndl->int_info2_mask |= mask2;

	/* ---- set hardware mask ---- */
	sd_outp(hndl,SD_INFO1_MASK,(unsigned short)~(hndl->int_info1_mask));
	sd_outp(hndl,SD_INFO2_MASK,(unsigned short)~(hndl->int_info2_mask));

	sddev_unl_cpu(hndl->sd_port);

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : clear SD_INFO1 and SD_INFO2 interrupt mask
 * Include      : 
 * Declaration  : int _sd_clear_int_mask(SDHNDL *hndl,unsigned short mask1,
 *              : 	unsigned short mask2)
 * Functions    : clear int_cc_status_mask depend on the mask bits value
 *              : if mask bit is one, it is disabled
 *              : if mask bit is zero, it is enabled
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short mask1 : SD_INFO1_MASK1 bits value
 *              : unsigned short mask2 : SD_INFO1_MASK2 bits value
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int	_sd_clear_int_mask(SDHNDL *hndl,unsigned short mask1,unsigned short mask2)
{
	sddev_loc_cpu(hndl->sd_port);
	
	/* ---- clear int_info1_mask and int_info2_mask ---- */
	hndl->int_info1_mask &= (unsigned short)~mask1;
	hndl->int_info2_mask &= (unsigned short)~mask2;
	
	/* ---- clear hardware mask ---- */
	sd_outp(hndl,SD_INFO1_MASK,(unsigned short)~(hndl->int_info1_mask));
	sd_outp(hndl,SD_INFO2_MASK,(unsigned short)~(hndl->int_info2_mask));
	
	sddev_unl_cpu(hndl->sd_port);
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : clear int_info bits
 * Include      : 
 * Declaration  : int _sd_clear_info(SDHNDL hndl,unsigned short clear_info1,
 *              : 	unsigned short clear_info2)
 * Functions    : clear int_info1 and int_info2 depend on the clear value
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short clear_info1 : int_info1 clear bits value
 *              : unsigned short clear_info2 : int_info2 clear bits value
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : SD_INFO1 and SD_INFO2 bits are not cleared
 *****************************************************************************/
int	_sd_clear_info(SDHNDL *hndl,unsigned short clear_info1,	unsigned short clear_info2)
{
	sddev_loc_cpu(hndl->sd_port);
	
	/* ---- clear int_info1 and int_info2 ---- */
	hndl->int_info1 &= (unsigned short)~clear_info1;
	hndl->int_info2 &= (unsigned short)~clear_info2;

	sddev_unl_cpu(hndl->sd_port);
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SD_INFO1 and SD_INFO2 interrupt elements
 * Include      : 
 * Declaration  : int _sd_get_int(SDHNDL *hndl)
 * Functions    : get SD_INFO1 and SD_INFO2 bits
 *              : examine enabled elements
 *              : hearafter, clear SD_INFO1 and SD_INFO2 bits
 *              : save those bits to int_info1 or int_info2
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_get_int(SDHNDL *hndl)
{
	unsigned short info1,info2;
	unsigned short ret = 0;

	/* get SD_INFO1 and SD_INFO2 bits */
	info1 = (unsigned short)(sd_inp(hndl,SD_INFO1) & hndl->int_info1_mask);
	info2 = (unsigned short)(sd_inp(hndl,SD_INFO2) & hndl->int_info2_mask);

	/* clear SD_INFO1 and SD_INFO2 bits */
	sd_outp(hndl,SD_INFO1,(unsigned short)~info1);
	sd_outp(hndl,SD_INFO2,(unsigned short)~info2);

	/* save enabled elements */
	hndl->int_info1 |= info1;
	hndl->int_info2 |= info2;
	if(info1 || info2 || ret){
		return SD_OK;	/* any interrupt occured */
	}
	
	return SD_ERR;	/* no interrupt occured */
}

/*****************************************************************************
 * ID           :
 * Summary      : check SD_INFO1 and SD_INFO2 interrupt elements
 * Include      : 
 * Declaration  : int sd_check_int(int sd_port);
 * Functions    : check SD_INFO1 and SD_INFO2 interrupt elements
 *              : if any interrupt is detected, return SD_OK
 *              : if no interrupt is detected, return SD_ERR
 *              : 
 * Argument     : none
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_check_int(int sd_port)
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
		if(hndl->int_info1 || hndl->int_info2){
			return SD_OK;
		}
		else{
			return SD_ERR;
		}
	}

	/* ---- polling mode ---- */
	return _sd_get_int(hndl);
}

/*****************************************************************************
 * ID           :
 * Summary      : SD_INFO1 and SD_INFO2 interrupt handler
 * Include      : 
 * Declaration  : void sd_int_handler(int sd_port);
 * Functions    : SD_INFO1 and SD_INFO2 interrupt handler
 *              : examine the relevant elements (without masked)
 *              : save those elements to int_info1 or int_info2
 *              : if a callback function is registered, call it
 * Argument     : none
 * Return       : none
 * Remark       : 
 *****************************************************************************/
void sd_int_handler(int sd_port)
{
	//uartPrintln("si");
	SDHNDL *hndl;
	int cd;

	if( (sd_port != 0) && (sd_port != 1) ){
		return;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return;
	}

	if(_sd_get_int(hndl) == SD_OK){
		/* is card detect interrupt? */
		if(hndl->int_info1 & (SD_INFO1_MASK_DET_DAT3 | SD_INFO1_MASK_DET_CD)){
			if(hndl->int_cd_callback){
				if(hndl->int_info1 & (SD_INFO1_MASK_INS_DAT3 | SD_INFO1_MASK_INS_CD)){
					cd = 1;	/* insert */
				}
				else{
					cd = 0;	/* remove */
				}
				(*hndl->int_cd_callback)(sd_port, cd);
			}
			hndl->int_info1 &= (unsigned short)~(SD_INFO1_MASK_DET_DAT3 | SD_INFO1_MASK_DET_CD);
		}
		else{
			if(hndl->int_callback){
				(*hndl->int_callback)(sd_port, 0);	/* arguments to be defined */
			}
		}
	}
}

/*****************************************************************************
 * ID           :
 * Summary      : register SD_INFO1 or SD_INFO2 interrupt callback function
 * Include      : 
 * Declaration  : int sd_set_intcallback(int sd_port, int (*callback)(int,int));
 * Functions    : register callback function
 *              : if SD_INFO1 or SD_INFO2 interrupt are occured, call callback
 *              : function
 *              : 
 * Argument     : int (*callback)(int) : callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_set_intcallback(int sd_port, int (*callback)(int, int))
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	hndl->int_callback = callback;

	return SD_OK;
}

/* End of File */
