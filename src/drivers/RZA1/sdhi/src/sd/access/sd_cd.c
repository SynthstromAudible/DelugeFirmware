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
*  File Name   : sd_cd.c
*  Contents    : card detect
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
 * Summary      : set card detect interrupt
 * Include      : 
 * Declaration  : int sd_cd_int(int sd_port, int enable,int (*callback)(int, int));
 * Functions    : set card detect interrupt
 *              : if select SD_CD_INT_ENABLE, detect interrupt is enbled and
 *              : it is possible register callback function
 *              : if select SD_CD_INT_DISABLE, detect interrupt is disabled
 *              : 
 * Argument     : int enable : is enable or disable card detect interrupt?
 *              : (*callback)(int) : interrupt callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_cd_int(int sd_port, int enable,int (*callback)(int, int))
{
	unsigned short info1;
	SDHNDL	*hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	if((enable != SD_CD_INT_ENABLE) && (enable != SD_CD_INT_DISABLE)){
		return SD_ERR;	/* parameter error */
	}
	/* is change interrupt disable to enable? */
	if((hndl->int_info1_mask & (SD_INFO1_MASK_DET_DAT3 
		| SD_INFO1_MASK_DET_CD)) == 0){
		sddev_loc_cpu(sd_port);
		info1 = sd_inp(hndl,SD_INFO1) ;
		info1 &= (unsigned short)~SD_INFO1_MASK_DET_DAT3_CD;
		sd_outp(hndl,SD_INFO1,info1);	/* clear insert and remove bits */
		sddev_unl_cpu(sd_port);
	}

	if(enable == SD_CD_INT_ENABLE){
		/* enable insert and remove interrupts */
		if(hndl->cd_port == SD_CD_SOCKET){	/* CD */
			_sd_set_int_mask(hndl,SD_INFO1_MASK_DET_CD,0);
		}
		else{	/* DAT3 */
			_sd_set_int_mask(hndl,SD_INFO1_MASK_DET_DAT3,0);
		}
	}
	else{	/* case SD_CD_INT_DISABLE */
		/* disable insert and remove interrupts */
		if(hndl->cd_port == SD_CD_SOCKET){	/* CD */
			_sd_clear_int_mask(hndl,SD_INFO1_MASK_DET_CD,0);
		}
		else{	/* DAT3 */
			_sd_clear_int_mask(hndl,SD_INFO1_MASK_DET_DAT3,0);
		}
	}

	/* ---- register callback function ---- */
	hndl->int_cd_callback = callback;
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : check card insertion
 * Include      : 
 * Declaration  : int sd_check_media(int sd_port);
 * Functions    : check card insertion
 *              : if card is inserted, return SD_OK
 *              : if card is not inserted, return SD_ERR
 *              : if SD handle is not initialized, return SD_ERR
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : card is inserted
 *              : SD_ERR: card is not inserted
 * Remark       : 
 *****************************************************************************/
int sd_check_media(int sd_port)
{
	SDHNDL	*hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	return _sd_check_media(hndl);
}

/*****************************************************************************
 * ID           :
 * Summary      : check card insertion
 * Include      : 
 * Declaration  : int _sd_check_media(SDHNDL *hndl)
 * Functions    : check card insertion
 *              : if card is inserted, return SD_OK
 *              : if card is not inserted, return SD_ERR
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : card is inserted
 *              : SD_ERR: card is not inserted
 * Remark       : 
 *****************************************************************************/
int _sd_check_media(SDHNDL *hndl)
{
	unsigned short reg;

	reg = sd_inp(hndl,SD_INFO1);
	if(hndl->cd_port == SD_CD_SOCKET){
		reg &= 0x0020u;	/* check CD level */
	}
	else{
		reg &= 0x0400u;	/* check DAT3 level */
	}

	if(reg){
		return SD_OK;	/* inserted */
	}
	
	return SD_ERR;	/* no card */
}


/* End of File */
