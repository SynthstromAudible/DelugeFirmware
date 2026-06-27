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
*  File Name   : sd_cmd.c
*  Contents    : command issue, response receive and register check
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

/* ==== response errors ==== */
		/* for internal error detail	*/
static const int RespErrTbl[]={
	SD_ERR_OUT_OF_RANGE,
	SD_ERR_ADDRESS_ERROR,
	SD_ERR_BLOCK_LEN_ERROR,
	SD_ERR_CARD_ERASE,
	SD_ERR_CARD_ERASE,
	SD_ERR_WP,
	SD_ERR_CARD_LOCK,
	SD_ERR_CARD_UNLOCK,
	SD_ERR_HOST_CRC,
	SD_ERR_ILLEGAL_COMMAND,
	SD_ERR_CARD_ECC,
	SD_ERR_CARD_CC,
	SD_ERR_CARD_ERROR,
	SD_ERR_RESERVED_ERROR18,
	SD_ERR_RESERVED_ERROR17,
	SD_ERR_OVERWRITE,
};

/* ==== IO errors ==== */
static const int IOErrTbl[]={
	SD_ERR_COM_CRC_ERROR,
	SD_ERR_ILLEGAL_COMMAND,
	SD_ERR_INTERNAL,
	SD_ERR_INTERNAL,
	SD_ERR_ERROR,
	SD_ERR_INTERNAL,
	SD_ERR_FUNCTION_NUMBER,
	SD_ERR_OUT_OF_RANGE,
	SD_ERR_HOST_CRC,
	SD_ERR_INTERNAL,
	SD_ERR_INTERNAL,
	SD_ERR_INTERNAL,
	SD_ERR_CARD_ERROR,
	SD_ERR_INTERNAL,
	SD_ERR_ILL_FUNC,
	SD_ERR_INTERNAL,
};

/* ==== SD_INFO2 errors table ==== */
static const int Info2ErrTbl[]={
	SD_ERR_RES_TOE,		// SD_INFO2_MASK_ERR6	0x0040
	SD_ERR_ILL_READ,	// SD_INFO2_MASK_ERR5	0x0020
	SD_ERR_ILL_WRITE,	// SD_INFO2_MASK_ERR4	0x0010
	SD_ERR_CARD_TOE,	// SD_INFO2_MASK_ERR3	0x0008
	SD_ERR_END_BIT,		// SD_INFO2_MASK_ERR2	0x0004
	SD_ERR_CRC,			// SD_INFO2_MASK_ERR1	0x0002
	SD_ERR_CMD_ERROR,	// SD_INFO2_MASK_ERR0	0x0001
};

/* ==== transfer speed table ==== */
static const unsigned short TranSpeed[8]={
	1,		// 100kbit/s
	10,		// 1Mbit/s
	100,	// 10Mbit/s
	1000,	// 100Mbit/s
	1000,	// reserved
	1000,	// reserved
	1000,	// reserved
	1000,	// reserved
};
static const unsigned char TimeValue[16]={
	0,10,12,13,15,20,25,30,35,40,45,50,55,60,70,80
};

static void	_sd_get_info2(SDHNDL *hndl);

 /*****************************************************************************
 * ID           :
 * Summary      : issue SD command
 * Include      : 
 * Declaration  : int _sd_send_cmd(SDHNDL *hndl,unsigned short cmd)
 * Functions    : issue SD command, hearafter wait recive response
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short cmd : command code
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : not get response and check response errors
 *****************************************************************************/
int _sd_send_cmd(SDHNDL *hndl,unsigned short cmd)
{
	int time;
	int i;
	
	hndl->error = SD_OK;

	if(cmd == CMD38){	/* erase command */
		time = SD_TIMEOUT_ERASE_CMD;	/* extend timeout 1 sec */
	}
	else if(cmd == ACMD46){	/* ACMD46 */
		time = SD_TIMEOUT_MULTIPLE;	/* same as write timeout */
	}
	else if(cmd == CMD7){
		time = SD_TIMEOUT_RESP;	/* same as write timeout */
	}
	else if(cmd == CMD12){
		time = SD_TIMEOUT_RESP;	/* same as write timeout */
	}
	else if(cmd == CMD43){
		time = SD_TIMEOUT_RESP;
	}
	else if(cmd == CMD44){
		time = SD_TIMEOUT_RESP;
	}
	else if(cmd == CMD45){
		time = SD_TIMEOUT_RESP;
	}
	else{
		time = SD_TIMEOUT_CMD;
	}

	/* enable resp end and illegal access interrupts */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);

	for(i=0; i<SCLKDIVEN_LOOP_COUNT; i++){
		if(sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_SCLKDIVEN){
			break;
		}
	}
	if(i==SCLKDIVEN_LOOP_COUNT){
		_sd_set_err(hndl,SD_ERR_CBSY_ERROR);		/* treate as CBSY ERROR	*/
		return hndl->error;
	}

	/* ---- issue command ---- */

	sd_outp(hndl,SD_CMD,cmd);
	
	/* ---- wait resp end ---- */
	if(sddev_int_wait(hndl->sd_port, time) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);
		return hndl->error;
	}

	/* disable resp end and illegal access interrupts */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	_sd_get_info2(hndl);	/* get SD_INFO2 register */

	_sd_check_info2_err(hndl);	/* check SD_INFO2 error bits */

	if(!(hndl->int_info1 & SD_INFO1_MASK_RESP)){
		_sd_set_err(hndl,SD_ERR_NO_RESP_ERROR);		/* no response */
	}

	/* ---- clear previous errors ---- */
	_sd_clear_info(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ERR);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : issue application specific command
 * Include      : 
 * Declaration  : int _sd_send_acmd(SDHNDL *hndl,unsigned short cmd,
 *              : 	unsigned short h_arg,unsigned short l_arg)
 * Functions    : issue application specific command, hearafter wait recive
 *              : response
 *              : issue CMD55 preceide application specific command
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short cmd : command code
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_send_acmd(SDHNDL *hndl,unsigned short cmd,unsigned short h_arg,
	unsigned short l_arg)
{
	/* ---- issue CMD 55 ---- */
	_sd_set_arg(hndl,hndl->rca[0],0);
	if(_sd_send_cmd(hndl, CMD55) != SD_OK){
		return SD_ERR;
	}
	
	if(_sd_get_resp(hndl,SD_RESP_R1) != SD_OK){
		return SD_ERR;
	}
	
	/* ---- issue ACMD ---- */
	_sd_set_arg(hndl,h_arg,l_arg);
	if(_sd_send_cmd(hndl, cmd) != SD_OK){
		return SD_ERR;
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : issue multiple command
 * Include      : 
 * Declaration  : int _sd_send_mcmd(SDHNDL *hndl,unsigned short cmd,
 *              : 	unsigned long startaddr)
 * Functions    : issue multiple command (CMD18 or CMD25)
 *              : wait response
 *              : set read start address to startaddr
 *              : after this function finished, start data transfer
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short cmd : command code (CMD18 or CMD25)
 *              : unsigned long startaddr : data address (command argument)
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_send_mcmd(SDHNDL *hndl,unsigned short cmd, unsigned long startaddr)
{
	int i;

	_sd_set_arg(hndl,(unsigned short)(startaddr>>16),(unsigned short)startaddr);

	for(i=0; i<SCLKDIVEN_LOOP_COUNT; i++){
		if(sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_SCLKDIVEN){
			break;
		}
	}
	if(i==SCLKDIVEN_LOOP_COUNT){
		_sd_set_err(hndl,SD_ERR_CBSY_ERROR);		/* treate as CBSY ERROR	*/
		return hndl->error;
	}

	/* ---- issue command ---- */
	sd_outp(hndl,SD_CMD,cmd);

	/* ---- wait resp end ---- */
	if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_CMD) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		return hndl->error;
	}

	_sd_get_info2(hndl);	/* get SD_INFO2 register */

	_sd_check_info2_err(hndl);	/* check SD_INFO2 error bits */

	if(hndl->int_info1 & SD_INFO1_MASK_RESP){
		if(!hndl->error){
			_sd_get_resp(hndl,SD_RESP_R1);	/* check R1 resp */
		}
	}
	else{
		_sd_set_err(hndl,SD_ERR_NO_RESP_ERROR);		/* no response */
	}

	/* ---- clear previous errors ---- */
	_sd_clear_info(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ERR);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : issue io access command
 * Include      : 
 * Declaration  : int _sd_send_iocmd(SDHNDL *hndl,unsigned short cmd,
 *              : 	unsigned long arg)
 * Functions    : issue io access command (CMD52 or CMD53)
 *              : wait response
 *              : set access parameter by argument form
 *              : after this function finished, start data transfer
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short cmd : command code (CMD52 or CMD53)
 *              : unsigned long arg : access parameter (command argument)
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_send_iocmd(SDHNDL *hndl,unsigned short cmd,unsigned long arg)
{
	int i;

	hndl->error = SD_OK;
	
	_sd_set_arg(hndl,(unsigned short)(arg>>16),(unsigned short)arg);

	/* enable resp end and illegal access interrupts */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	for(i=0; i<SCLKDIVEN_LOOP_COUNT; i++){
		if(sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_SCLKDIVEN){
			break;
		}
	}
	if(i==SCLKDIVEN_LOOP_COUNT){
		_sd_set_err(hndl,SD_ERR_CBSY_ERROR);		/* treate as CBSY ERROR	*/
		return hndl->error;
	}

	/* ---- issue command ---- */
	sd_outp(hndl,SD_CMD,cmd);

	/* ---- wait resp end ---- */
	if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_CMD) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		return hndl->error;
	}

	/* disable resp end and illegal access interrupts */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	_sd_get_info2(hndl);	/* get SD_INFO2 register */

	_sd_check_info2_err(hndl);	/* check SD_INFO2 error bits */

	if(hndl->int_info1 & SD_INFO1_MASK_RESP){
		if(!hndl->error){
			_sd_get_resp(hndl,SD_RESP_R5);	/* check R5 resp */
		}
	}
	else{
		_sd_set_err(hndl,SD_ERR_INTERNAL);	/* no response */
	}

	/* ---- clear previous errors ---- */
	_sd_clear_info(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ERR);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : issue general SD command
 * Include      : 
 * Declaration  : int _sd_card_send_cmd_arg(SDHNDL *hndl,unsigned cmd, 
 *              :   int resp,unsigned short h_arg,unsigned short l_arg)
 * Functions    : issue command specified cmd code
 *              : get and check response
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short cmd : command code (CMD18 or CMD25)
 *              : int  resp : command response
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_card_send_cmd_arg(SDHNDL *hndl,unsigned short cmd, int resp,
	unsigned short h_arg,unsigned short l_arg)
{
	int ret;

	_sd_set_arg(hndl,h_arg,l_arg);
	
	/* ---- issue command ---- */
	if((ret =_sd_send_cmd(hndl,cmd)) == SD_OK){
		ret = _sd_get_resp(hndl,resp);	/* get and check response */
	}
	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : set command argument
 * Include      : 
 * Declaration  : void _sd_set_arg(SDHNDL *hndl,unsigned short h_arg,
 *              : 	unsigned short l_arg)
 * Functions    : set command argument to SDHI
 *              : h_arg means higher 16bits [31:16] and  set SD_ARG0
 *              : l_arg means lower 16bits [15:0] and set SD_ARG1
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : SD_ARG0 and SD_ARG1 are like little endian order
 *****************************************************************************/
void _sd_set_arg(SDHNDL *hndl,unsigned short h_arg,unsigned short l_arg)
{
	sd_outp(hndl,SD_ARG0,l_arg);
	sd_outp(hndl,SD_ARG1,h_arg);
}

/*****************************************************************************
 * ID           :
 * Summary      : get OCR register
 * Include      : 
 * Declaration  : int _sd_card_send_ocr(SDHNDL *hndl,int type)
 * Functions    : get OCR register and check card operation voltage
 *              : if type is SD_MEDIA_SD, issue ACMD41
 *              : if type is SD_MEDIA_MMC, issue CMD1
 *              : if type is SD_MEDIA_IO, issue CMD5
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : int type : card type
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_card_send_ocr(SDHNDL *hndl,int type)
{
	int ret,i;
	int j=0;

	/* ===== distinguish card type issuing CMD5, ACMD41 or CMD1 ==== */
	for(i=0; i < 200; i++){
		switch(type){
		case SD_MEDIA_UNKNOWN:	/* unknown media (read OCR) */
			/* ---- issue CMD5 ---- */
			_sd_set_arg(hndl,0,0);
			ret = _sd_send_cmd(hndl,CMD5);
			if(ret == SD_OK){
				return _sd_get_resp(hndl,SD_RESP_R4);	/* check R4 resp */
			}
			else{
				return ret;
			}
			break;

		case SD_MEDIA_IO:
			/* ---- issue CMD5 ---- */
			_sd_set_arg(hndl,(unsigned short)(hndl->voltage>>16),
				(unsigned short)hndl->voltage);
			ret = _sd_send_cmd(hndl,CMD5);
			break;
		
		case SD_MEDIA_SD:
		case SD_MEDIA_COMBO:
			if(hndl->sup_ver == SD_MODE_VER2X){
				/* set HCS bit */
				if( hndl->sd_spec == SD_SPEC_20 ){
					/* cmd8 have response	*/
					hndl->voltage |= 0x40000000;
				}
			}
			/* ---- issue ACMD41 ---- */
			ret = _sd_send_acmd(hndl,ACMD41,
				(unsigned short)(hndl->voltage>>16),
					(unsigned short)hndl->voltage);
			break;
		
		case SD_MEDIA_MMC:	/* MMC */
			/* ---- issue CMD1 ---- */
			_sd_set_arg(hndl,(unsigned short)(hndl->voltage>>16),
				(unsigned short)hndl->voltage);
			ret = _sd_send_cmd(hndl,CMD1);
			break;

		default:
			hndl->resp_status = 0;
		 	/* for internal error detail	*/
		 	/* but not need to change		*/
			hndl->error = SD_ERR_INTERNAL;
			return SD_ERR;
		}
		
		if(ret == SD_OK){
			if(type == SD_MEDIA_IO){	/* IO */
				_sd_get_resp(hndl,SD_RESP_R4);	/* check R4 resp */
				/* ---- polling busy bit ---- */
				if(hndl->io_ocr[0] & 0x8000){	/* busy cleared */
					break;
				}
				else{
					ret = SD_ERR;	/* busy */
					sddev_int_wait(hndl->sd_port, 5);	/* add wait function because retry interval is too short */
				}
			}
			else{	/* memory */
				_sd_get_resp(hndl,SD_RESP_R3);	/* check R3 resp */
				/* ---- polling busy bit ---- */
				if(hndl->ocr[0] & 0x8000){	/* busy cleared */
					break;
				}
				else{
					ret = SD_ERR;	/* busy */
					sddev_int_wait(hndl->sd_port, 5);	/* add wait function because retry interval is too short */
				}
			}
		}
		
		/* if more than 3 times response timeout occured, retry stop quick distinction to MMC */ 
		if(hndl->error == SD_ERR_RES_TOE){
			++j;
			if( j == 3){
				break;
			}
		}
		else{
			j = 0;
		} 
	}
	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : check R1 type response errors
 * Include      : 
 * Declaration  : int _sd_get_resp_error(SDHNDL *hndl)
 * Functions    : distinguish error bit from R1 response
 *              : set the error bit to hndl->error
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : no error detected
 *              : SD_ERR: any errors detected
 * Remark       : 
 *****************************************************************************/
int _sd_check_resp_error(SDHNDL *hndl)
{
	unsigned short status;
	int bit;
	

	if(hndl->media_type == SD_MEDIA_IO){
		/* IO */
		status = (unsigned short)(hndl->resp_status & 0xcb00u);

		/* serch R5 error bit */
		bit = _sd_bit_search(status);
		
		/* R5 resp errors bits */
		_sd_set_err(hndl,IOErrTbl[bit]);
	}
	else{
		/* SD or MMC card */
		status = (unsigned short)((hndl->resp_status >>16 ) & 0xfdffu);

		/* ---- search R1 error bit ---- */
		bit = _sd_bit_search(status);
		
		if(bit != -1){
			/* R1 resp errors bits but for AKE_SEQ_ERROR */
			_sd_set_err(hndl,RespErrTbl[bit]);
			return SD_ERR;
		}
		else if(hndl->resp_status & RES_AKE_SEQ_ERROR){
			/* authentication process sequence error */
			_sd_set_err(hndl,SD_ERR_AKE_SEQ);
			return SD_ERR;
		}
		else{
			/* NOP */
		}
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get response and check response errors
 * Include      : 
 * Declaration  : int _sd_get_resp(SDHNDL *hndl,int resp)
 * Functions    : get response value from RESP register
 *              : R1, R2, R3,(R4, R5) and R6 types are available
 *              : specify response type by the argument resp
 *              : set response value to SD handle member
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_get_resp(SDHNDL *hndl,int resp)
{
	unsigned long status;
	unsigned short *ptr;
	
	/* select RESP register depend on the response type */
	switch(resp){
	case SD_RESP_NON:	/* no response */
		/* NOP */
		break;
	case SD_RESP_R1:	/* nomal response (32bits length) */
	case SD_RESP_R1b:	/* nomal response with an optional busy signal */
		status = sd_inp(hndl,SD_RESP1);
		status <<=16;
		status |= sd_inp(hndl,SD_RESP0);
		hndl->resp_status = status;
		
		if(status & 0xfdffe008){		/* ignore card locked status	*/
			/* any status error */
			return _sd_check_resp_error(hndl);
		}
		
		break;
		
	case SD_RESP_R1_SCR:	/* nomal response with an optional busy signal */
		hndl->scr[0] = sd_inp(hndl,SD_RESP1);
		hndl->scr[1] = sd_inp(hndl,SD_RESP0);
		break;
		
	case SD_RESP_R2_CID:	/* CID register (128bits length) */
		ptr = hndl->cid;
		*ptr++ = sd_inp(hndl,SD_RESP7);
		*ptr++ = sd_inp(hndl,SD_RESP6);
		*ptr++ = sd_inp(hndl,SD_RESP5);
		*ptr++ = sd_inp(hndl,SD_RESP4);
		*ptr++ = sd_inp(hndl,SD_RESP3);
		*ptr++ = sd_inp(hndl,SD_RESP2);
		*ptr++ = sd_inp(hndl,SD_RESP1);
		*ptr++ = sd_inp(hndl,SD_RESP0);
		break;
		
	case SD_RESP_R2_CSD:	/* CSD register (128bits length) */
		ptr = hndl->csd;
		*ptr++ = sd_inp(hndl,SD_RESP7);
		*ptr++ = sd_inp(hndl,SD_RESP6);
		*ptr++ = sd_inp(hndl,SD_RESP5);
		*ptr++ = sd_inp(hndl,SD_RESP4);
		*ptr++ = sd_inp(hndl,SD_RESP3);
		*ptr++ = sd_inp(hndl,SD_RESP2);
		*ptr++ = sd_inp(hndl,SD_RESP1);
		*ptr++ = sd_inp(hndl,SD_RESP0);
		break;
		
	case SD_RESP_R3:	/* OCR register (32bits length) */
		hndl->ocr[0] = sd_inp(hndl,SD_RESP1);
		hndl->ocr[1] = sd_inp(hndl,SD_RESP0);
		break;

	case SD_RESP_R4:	/* IO OCR register (24bits length) */
		hndl->io_ocr[0] = sd_inp(hndl,SD_RESP1);
		hndl->io_ocr[1] = sd_inp(hndl,SD_RESP0);
		break;
		
	case SD_RESP_R6:		/* Published RCA response (32bits length) */
		hndl->rca[0] = sd_inp(hndl,SD_RESP1);
		hndl->rca[1] = sd_inp(hndl,SD_RESP0);
		break;
		
	case SD_RESP_R5:		/* IO RW response */
		status = sd_inp(hndl,SD_RESP1);
		status <<=16;
		status |= sd_inp(hndl,SD_RESP0);
		hndl->resp_status = status;
		
		if(status & 0xcb00){
			/* any status error */
			return _sd_check_resp_error(hndl);
		}
		break;

	 case SD_RESP_R7:		/* IF_COND response */
		hndl->if_cond[0] = sd_inp(hndl,SD_RESP1);
		hndl->if_cond[1] = sd_inp(hndl,SD_RESP0);
		break;
	
	default:
		/* unknown type */
		hndl->resp_status = 0;
		hndl->error = SD_ERR_INTERNAL;
		return SD_ERR;
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : check CSD register
 * Include      : 
 * Declaration  : int _sd_check_csd(SDHNDL *hndl)
 * Functions    : check CSD register and get following information
 *              : Transfer Speed
 *              : Command Class
 *              : Read Block Length
 *              : Copy Bit
 *              : Write Protect Bit
 *              : File Format Group
 *              : Number of Erase Sector
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_check_csd(SDHNDL *hndl)
{
	unsigned long transpeed,timevalue;
	unsigned long erase_sector_size,erase_group_size;

	/* ---- CSD Structure ---- */
	if(hndl->media_type == SD_MEDIA_MMC){
		hndl->csd_structure = 0;
	}
	else{
		hndl->csd_structure = (unsigned char)((hndl->csd[0] & 0x00c0u) >> 6u);
		if( hndl->csd_structure == 1 ){
			if( hndl->sd_spec != SD_SPEC_20 ){
				/* if csd_structure is ver2.00, sd_spec has to be phys spec ver2.00 */
				_sd_set_err(hndl, SD_ERR_CSD_VER);
				return SD_ERR;
			}
		}
	}

	/* ---- TAAC/NSAC ---- */
	/* no check, to be obsolete */

	/* ---- TRAN_SPEED  ---- */
	transpeed = (hndl->csd[2] & 0x0700u) >> 8u;
	timevalue = (hndl->csd[2] & 0x7800u) >> 11u;
	
	transpeed = (unsigned long)(TranSpeed[transpeed] * TimeValue[timevalue]);
	
	/* ---- set transfer speed (memory access) ---- */
	if(transpeed >= 5000){
		hndl->csd_tran_speed = SD_CLK_50MHz;
	}
	else if(transpeed >= 2500){
		hndl->csd_tran_speed = SD_CLK_25MHz;
	}
	else if(transpeed >= 2000){
		hndl->csd_tran_speed = SD_CLK_20MHz;
	}
	else if(transpeed >= 1000){
		hndl->csd_tran_speed = SD_CLK_10MHz;
	}
	else if(transpeed >= 500){
		hndl->csd_tran_speed = SD_CLK_5MHz;
	}
	else if(transpeed >= 100){
		hndl->csd_tran_speed = SD_CLK_1MHz;
	}
	else{
		hndl->csd_tran_speed = SD_CLK_400kHz;
	}
	
	/* ---- CCC  ---- */
	hndl->csd_ccc = (unsigned short)(((hndl->csd[2] & 0x00ffu) << 4u) | 
		((hndl->csd[3] & 0xf000u) >> 12u));
	
	
	/* ---- COPY ---- */
	hndl->csd_copy = (unsigned char)(hndl->csd[7] & 0x0040u);
	
	/* ---- PERM/TMP_WRITE_PROTECT ---- */
	hndl->write_protect |= (unsigned char)((hndl->csd[7] & 0x0030u)>>3u);
	
	/* ---- FILE_FORMAT ---- */
	hndl->csd_file_format = (unsigned char)(hndl->csd[7] & 0x008cu);
	if(hndl->csd_file_format & 0x80u){
		_sd_set_err(hndl, SD_ERR_FILE_FORMAT);
		return SD_ERR;
	}
	
	/* ---- calculate the number of erase sectors ---- */
	if(hndl->media_type & SD_MEDIA_SD){
		erase_sector_size = ((hndl->csd[5] & 0x003fu) << 1u) | 
			((hndl->csd[6] & 0x8000) >> 15);
		erase_group_size = (hndl->csd[6] & 0x7f00u) >> 8u; 
	}
	else{
		erase_sector_size = (hndl->csd[5] & 0x007cu) >> 2u;
		erase_group_size = ((hndl->csd[5] & 0x0003u) << 3u) | 
			((hndl->csd[6] & 0xe000u) >> 13u); 
	}
	hndl->erase_sect = (erase_sector_size+1) * (erase_group_size+1);
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : check SD_INFO2 register errors
 * Include      : 
 * Declaration  : int _sd_check_info2_err(SDHNDL *hndl)
 * Functions    : check error bit of SD_INFO2 register
 *              : set the error bit to hndl->error
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int _sd_check_info2_err(SDHNDL *hndl)
{
	unsigned short info2;
	int bit;
	
	info2 = (unsigned short)(hndl->int_info2 & SD_INFO2_MASK_ERR);
	
	/* ---- search error bit ---- */
	bit = _sd_bit_search(info2);
	
	if(bit == -1){	/* no error */
		_sd_set_err(hndl,SD_OK);
	}
	else if(bit == 0){	/* CMD error */
		_sd_set_err(hndl,SD_ERR_CMD_ERROR);
	}
	else{	/* other error */
		_sd_set_err(hndl,Info2ErrTbl[bit-9]);
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SD_INFO2 register
 * Include      : 
 * Declaration  : static void _sd_get_info2(SDHNDL *hndl)
 * Functions    : get SD_INFO2 register
 *              : set the register value to hndl->int_info2
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : none
 * Remark       : 
 *****************************************************************************/
static void _sd_get_info2(SDHNDL *hndl)
{
	unsigned short info2_reg;

	info2_reg = (unsigned short)((sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_ERR));
	sd_outp(hndl,SD_INFO2,(unsigned short)~info2_reg);
	hndl->int_info2 = (unsigned short)(hndl->int_info2 | info2_reg);
}

/* End of File */
