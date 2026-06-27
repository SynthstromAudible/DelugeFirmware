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
*  File Name   : sd_util.c
*  Contents    : Function setting
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
******************************************************************************/
#include "../../../inc/sdif.h"
#include "../inc/access/sd.h"

#ifdef __CC_ARM
#pragma arm section code = "CODE_SDHI"
#pragma arm section rodata = "CONST_SDHI"
#pragma arm section rwdata = "DATA_SDHI"
#pragma arm section zidata = "BSS_SDHI"
#endif

static unsigned long next;		/* Volume ID Number */

static unsigned char _sd_calc_crc(unsigned char* data,int len);

/*****************************************************************************
 * ID           :
 * Summary      : control SD clock
 * Include      : 
 * Declaration  : int _sd_set_clock(SDHNDL *hndl,int clock,int enable)
 * Functions    : supply or halt SD clock
 *              : if enable is SD_CLOCK_ENABLE, supply SD clock
 *              : if enable is SD_CLOCK_DISKABLE, halt SD clock
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : int clock : SD clock frequency
 *              : int enable : supply or halt SD clock
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_set_clock(SDHNDL *hndl,int clock, int enable)
{
	unsigned int div;
	int i;
	
	if(enable == SD_CLOCK_ENABLE){
		/* convert clock frequency to clock divide ratio */
		div = sddev_get_clockdiv(hndl->sd_port, clock);
#ifdef SDIP_SUPPORT_DIV1
		if((div > SD_DIV_512) && (div != SD_DIV_1)){
			_sd_set_err(hndl,SD_ERR_CPU_IF);
			return SD_ERR;
		}

#else
		if(div > SD_DIV_512){
			_sd_set_err(hndl,SD_ERR_CPU_IF);
			return SD_ERR;
		}
#endif

		sd_outp(hndl,SD_CLK_CTRL,(unsigned short)(div|0x0100u));
	}
	else{
		for(i=0; i<SCLKDIVEN_LOOP_COUNT; i++){
		#ifdef USE_INFO2_CBSY
			if( (sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_CBSY) == 0 ){
				break;
			}
		#else
			if(sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_SCLKDIVEN){
				break;
			}
		#endif
		}
		if(i==SCLKDIVEN_LOOP_COUNT){
			hndl->error = SD_ERR_CBSY_ERROR;
		}
		sd_outp(hndl,SD_CLK_CTRL,0);		/* halt */
	}
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : control data bus width
 * Include      : 
 * Declaration  : int _sd_set_port(SDHNDL *hndl,int port)
 * Functions    : change data bus width
 *              : if port is SD_PORT_SERIAL, set data bus width 1bit
 *              : if port is SD_PORT_PARALEL, set data bus width 4bits
 *              : change between 1bit and 4bits by ACMD6
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : int port : setting bus with
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : before execute this function, check card supporting bus 
 *              : width
 *              : SD memory card is 4bits support mandatory
 *****************************************************************************/
int _sd_set_port(SDHNDL *hndl,int port)
{
	unsigned short reg;
	unsigned short arg;
	unsigned char io_buff;
	
	if(hndl->media_type == SD_MEDIA_IO){	/* IO */
		/* ==== change io bus width and clear pull-up DAT3 (issue CMD52)==== */
		if(port == SD_PORT_SERIAL){
			/* data:00'h */
			io_buff = 0x00;
		}
		else{
			/* data:02'h */
			io_buff = 0x02;
		}

		/* data:00'h or 02'h func:0 address:07'h verify write */
		if(_sdio_direct(hndl,&io_buff,0,0x07,1,SD_IO_VERIFY_WRITE) != SD_OK){
			return SD_ERR;
		}
	}
	else if(hndl->media_type & SD_MEDIA_SD){	/* SD or COMBO */
		/* ---- check card state ---- */
		if((hndl->resp_status & RES_STATE) == STATE_TRAN){	/* transfer state */
			if(port == SD_PORT_SERIAL){
				arg = ARG_ACMD6_1bit;
			}
			else{
				arg = ARG_ACMD6_4bit;
			}
			/* ==== change card bus width (issue ACMD6) ==== */
			if(_sd_send_acmd(hndl,ACMD6,0,arg) != SD_OK){
				return SD_ERR;
			}
			if(_sd_get_resp(hndl,SD_RESP_R1) != SD_OK){
				return SD_ERR;
			}
		}
	}
	
	/* ==== change SDHI bus width ==== */
	if(port == SD_PORT_SERIAL){	/* 1bit */
		sddev_set_port(hndl->sd_port, port);
		reg = sd_inp(hndl,SD_OPTION);
		reg |= 0x8000u;
		sd_outp(hndl,SD_OPTION,reg);
	}
	else{	/* 4bits */
		reg = sd_inp(hndl,SD_OPTION);
		reg &= 0x7fffu;
		sd_outp(hndl,SD_OPTION,reg);
		sddev_set_port(hndl->sd_port, port);
	}
	
	hndl->if_mode = (unsigned char)port;
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : chech hardware write protect
 * Include      : 
 * Declaration  : int sd_iswp(int sd_port);
 * Functions    : check hardware write protect refer to SDHI register
 *              : if WP pin is disconnected to SDHI, return value has no
 *              : meaning
 *              : 
 *              : 
 * Argument     : none
 * Return       : hndl->write_protect : write protected state
 * Remark       : 
 *****************************************************************************/
int sd_iswp(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	return (int)hndl->write_protect;
}

/*****************************************************************************
 * ID           :
 * Summary      : chech hardware write protect
 * Include      : 
 * Declaration  : int _sd_iswp(SDHNDL *hndl)
 * Functions    : check hardware write protect refer to SDHI register
 *              : if WP pin is disconnected to SDHI, return value has no
 *              : meaning
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_WP_OFF (0): write protected
 *              : SD_WP_HW (1): not write protected
 * Remark       : don't check CSD write protect bits and ROM card
 *****************************************************************************/
int _sd_iswp(SDHNDL *hndl)
{
	int wp;

	/* ===== check SD_INFO1 WP bit ==== */
	wp = (~sd_inp(hndl,SD_INFO1) & 0x0080u) >> 7;

	return wp;
}

/*****************************************************************************
 * ID           :
 * Summary      : get bit information
 * Include      : 
 * Declaration  : int _sd_bit_search(unsigned short data)
 * Functions    : check every bits of argument (data) from LSB
 *              : return first bit whose value is 1'b
 *              : bit number is big endian (MSB is 0)
 * Argument     : unsigned short data : checked data
 * Return       : not less than 0 : bit number has 1'b
 *              : -1 : no bit has 1'b
 * Remark       : just 16bits value can be applied
 *****************************************************************************/
int _sd_bit_search(unsigned short data)
{
	int i;
	
	for(i=15;i >= 0 ; i--){
		if(data & 1u){
			return i;
		}
		data >>=1;
	}
	
	return -1;
}

/*****************************************************************************
 * ID           :
 * Summary      : set errors information
 * Include      : 
 * Declaration  : int _sd_set_err(SDHNDL *hndl,int error)
 * Functions    : set error information (=error) to SD Handle member
 *              : (=hndl->error)
 * Argument     : SDHNDL *hndl : SD handle
 *              : int error : setting error information
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if hndl->error was already set, no overwrite it
 *****************************************************************************/
int _sd_set_err(SDHNDL *hndl,int error)
{
	if(hndl->error == SD_OK){
		hndl->error = error;
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : stop operations compulsory
 * Include      : 
 * Declaration  : void sd_stop(int sd_port);
 * Functions    : set flag (=SD handle member stop) stop operations compulsory
 *              : if this flag is set, read, write, format operations is
 *              : stopped
 *              : this flag is used for card detect/removal interrupt detection
 *              : 
 * Argument     : none
 * Return       : none
 * Remark       : 
 *****************************************************************************/
void sd_stop(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return;	/* not initilized */
	}
	
	hndl->stop = 1;
}

/*****************************************************************************
 * ID           :
 * Summary      : get card type
 * Include      : 
 * Declaration  : int sd_get_type(int sd_port, unsigned char *type,unsigned char *speed,unsigned char *capa);
 * Functions    : get mounting card type, current and supported speed mode
 *              : and capacity type
 *              : (if SD memory card)
 *              : following card types are defined
 *              : SD_MEDIA_UNKNOWN : unknown media
 *              : SD_MEDIA_MMC : MMC card
 *              : SD_MEDIA_SD : SD Memory card
 *              : SD_MEDIA_IO : SD IO card (IO spec ver1.10)
 *              : SD_MEDIA_COMBO : SD COMBO card (IO spec ver1.10)
 * Argument     : unsigned char *type : mounting card type
 *              : unsigned char *speed : speed mode
 *              :   supported speed is bit4, current speed is bit0
 *              : unsigned char *speed : speed mode
 *              :   Standard capacity:0, High capacity:1
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the value isn't returned
 *              : only SD memory card, speed mode has meaning
 *****************************************************************************/
int sd_get_type(int sd_port, unsigned char *type,unsigned char *speed,unsigned char *capa)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(type){
		*type = hndl->media_type;
	}
	if(hndl->partition_id > 0){
		*type |= SD_MEDIA_EMBEDDED;
	}
	if(speed){
		*speed = hndl->speed_mode;
	}
	if(capa){
		*capa = hndl->csd_structure;
	}
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get card size
 * Include      : 
 * Declaration  : int sd_get_size(int sd_port, unsigned long *user,unsigned long *protect);
 * Functions    : get total sectors of user area calculated from CSD 
 *              : get total sectors of protect area calculated from CSD and 
 *              : SD STAUS
 * Argument     : unsigned long *user : user area size
 *              : unsigned long *protect : protect area size
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the value isn't returned
 *              : return just the number of all sectors
 *              : only SD memory card, protect area size has meaning
 *****************************************************************************/
int sd_get_size(int sd_port, unsigned long *user,unsigned long *protect)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(user){
		*user = hndl->card_sector_size;
	}
	if(protect){
		*protect = hndl->prot_sector_size;
	}
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get card size
 * Include      : 
 * Declaration  : int _sd_get_size(SDHNDL *hndl,int area)
 * Functions    : get memory card size
 * Argument     : SDHNDL *hndl : SD handle
 *              : int area : memory area (bit0:user area, bit1:protect area)
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : protect area is just the number of all sectors
 *****************************************************************************/
int _sd_get_size(SDHNDL *hndl,unsigned int area)
{
	unsigned long c_mult,c_size;
	unsigned int read_bl_len;

	/* ---- READ BL LEN ---- */
	read_bl_len = (hndl->csd[3] & 0x0f00u) >> 8;
	
	/* ---- C_SIZE_MULT ---- */
	c_mult = ((hndl->csd[5] & 0x0380u) >> 7);
	
	if(area & SD_PROT_AREA){
		/* calculate the number of all sectors */
		if((hndl->sup_ver == SD_MODE_VER2X) && (hndl->csd_structure == 0x01)){
			hndl->prot_sector_size = (((unsigned long)hndl->sdstatus[2] << 16u) 
				| ((unsigned long)hndl->sdstatus[3]))/512;
		}
		else{
			hndl->prot_sector_size = hndl->sdstatus[3] * 
				((unsigned long)1 << (c_mult + 2)) * 
					((unsigned long)1 << read_bl_len)/512;
		}
	}
	
	if(area & SD_USER_AREA){
		if((hndl->sup_ver == SD_MODE_VER2X) && (hndl->csd_structure == 0x01)){
			c_size = ((((unsigned long)hndl->csd[4] & 0x3fffu) << 8u) |
			(((unsigned long)hndl->csd[5] & 0xff00u) >> 8u));
			/* memory capacity = C_SIZE*512K byte */
			/* sector_size = memory capacity/512 */
			hndl->card_sector_size = ((c_size +1) << 10u);
		}
		else{
			/* ---- C_SIZE ---- */
			c_size = ((hndl->csd[3] & 0x0003u) << 10) 
				| ((hndl->csd[4] & 0xffc0u) >> 6);

			/* calculate the number of all sectors */
			hndl->card_sector_size = ((unsigned long)(c_size +1) * 
				((unsigned long)1 << (c_mult+2))*((unsigned long)1 
					<< read_bl_len)) / 512;
		}
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SD Driver errors
 * Include      : 
 * Declaration  : int sd_get_error(int sd_port);
 * Functions    : get SD Driver errors (=hndl->error) and return it
 * Argument     : none
 * Return       : hndl->error
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_get_error(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : get card register
 * Include      : 
 * Declaration  : int sd_get_reg(int sd_port, unsigned char *ocr,unsigned char *cid,unsigned char *csd, unsigned char *dsr,unsigned char *scr);
 * Functions    : get card register value
 * Argument     : unsigned char *ocr : OCR register address
 *              : unsigned char *cid : CID register address
 *              : unsigned char *csd : CSD register address
 *              : unsigned char *dsr : DSR register address
 *              : unsigned char *scr : SCR register address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sd_get_reg(int sd_port, unsigned char *ocr,unsigned char *cid,unsigned char *csd,
	unsigned char *dsr,unsigned char *scr)
{
	SDHNDL *hndl;
	unsigned int i;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(ocr){
		for(i = 0; i < 2; ++i){
			*ocr++ = (unsigned char)(hndl->ocr[i] >> 8);
			*ocr++ = (unsigned char)hndl->ocr[i];
		}
	}
	if(cid){
		for(i = 0; i < 8; ++i){
			*cid++ = (unsigned char)(hndl->cid[i] >> 8);
			*cid++ = (unsigned char)hndl->cid[i];
		}
	}
	if(csd){
		for(i = 0; i < 8; ++i){
			*csd++ = (unsigned char)(hndl->csd[i] >> 8);
			*csd++ = (unsigned char)hndl->csd[i];
		}
	}
	if(dsr){
		*dsr++ = (unsigned char)(hndl->dsr[0] >> 8);
		*dsr++ = (unsigned char)hndl->dsr[0];
	}
	if(scr){
		for(i = 0; i < 4; ++i){
			*scr++ = (unsigned char)(hndl->scr[i] >> 8);
			*scr++ = (unsigned char)hndl->scr[i];
		}
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get rca register
 * Include      : 
 * Declaration  : int sd_get_rca(int sd_port, unsigned char *rca);
 * Functions    : get card register value
 * Argument     : unsigned char *rca : RCA register address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sd_get_rca(int sd_port, unsigned char *rca)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(rca){	/* return high 16bits */
		*rca++ = (unsigned char)(hndl->rca[0] >> 8);
		*rca++ = (unsigned char)hndl->rca[0];
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SD Status
 * Include      : 
 * Declaration  : int sd_get_sdstatus(int sd_port, unsigned char *sdstatus);
 * Functions    : get SD Status register value
 * Argument     : unsigned char *sdstatus : SD STATUS address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sd_get_sdstatus(int sd_port, unsigned char *sdstatus)
{
	SDHNDL *hndl;
	unsigned int i;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(sdstatus){
		for(i = 0; i < 7; ++i){
			*sdstatus++ = (unsigned char)(hndl->sdstatus[i] >> 8);
			*sdstatus++ = (unsigned char)hndl->sdstatus[i];
		}
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get card speed
 * Include      : 
 * Declaration  : int sd_get_speed(int sd_port, unsigned char *clss,unsigned char *move);
 * Functions    : get card speed class and performance move
 * Argument     : unsigned char *class : speed class
 *              : unsigned char *move : performance move
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sd_get_speed(int sd_port, unsigned char *clss,unsigned char *move)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(clss){
		*clss = hndl->speed_class;
	}

	if(move){
		*move = hndl->perform_move;
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : set block count
 * Include      : 
 * Declaration  : int sd_set_seccnt(int sd_port, short sectors);
 * Functions    : set maximum block count per multiple command
 * Argument     : short sectors : block count
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : maximam block count is constrained from 3 to 32767(0x7fff)
 *****************************************************************************/
int sd_set_seccnt(int sd_port, short sectors)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	if(sectors <= 2){
		/* need more than 3 continuous transfer */
		return SD_ERR;	/* undefined value */
	}

	hndl->trans_sectors = sectors;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get block count
 * Include      : 
 * Declaration  : int sd_get_seccnt(int sd_port);
 * Functions    : get maximum block count per multiple command
 * Argument     : none
 * Return       : not less than 0 : block count
 *              : SD_ERR: end of error
 * Remark       : maximam block count are constrained from 1 to 65535
 *****************************************************************************/
int sd_get_seccnt(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	return (int)hndl->trans_sectors;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SDHI and SD Driver versions
 * Include      : 
 * Declaration  : int sd_get_ver(int sd_port, unsigned short *sdhi_ver,char *sddrv_ver);
 * Functions    : get SDHI version from VERSION register
 *              : get SD Driver version from constant DRIVER NAME
 * Argument     : unsigned short *sdhi_ver : SDHI version
 *              : char *sddrv_ver : SD Driver version
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the value isn't returned
 *****************************************************************************/
int sd_get_ver(int sd_port, unsigned short *sdhi_ver,char *sddrv_ver)
{
	SDHNDL *hndl;
	int i;
	char *name = (char*)DRIVER_NAME;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(sdhi_ver){
		*sdhi_ver = sd_inp(hndl,VERSION);
	}
	
	if(sddrv_ver){
		for(i = 0;i < 32; ++i){
			*sddrv_ver++ = *name++;
		}
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : set card detect time
 * Include      : 
 * Declaration  : int sd_set_cdtime(int sd_port, unsigned short cdtime);
 * Functions    : set card detect time equal to IMCLK*2^(10+cdtime)
 * Argument     : unsigned short cdtime : card detect time interval
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_set_cdtime(int sd_port, unsigned short cdtime)
{
	SDHNDL *hndl;
	unsigned short reg;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	if(cdtime >= 0x000f){
		return SD_ERR;	/* undefined value */
	}
	
	reg = sd_inp(hndl,SD_OPTION);
	reg &= 0xfff0u;
	reg |= (unsigned short)(cdtime & 0x000fu);
	sd_outp(hndl,SD_OPTION,reg);
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : set response timeout
 * Include      : 
 * Declaration  : int sd_set_responsetime(int sd_port, unsigned short responsetime);
 * Functions    : set response timeout equal to IMCLK*2^(13+cdtime)
 * Argument     : unsigned short responsetime : response timeout interval
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_set_responsetime(int sd_port, unsigned short responsetime)
{
	SDHNDL *hndl;
	unsigned short reg;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	if(responsetime >= 0x000f){
		return SD_ERR;	/* undefined value */
	}
	
	reg = sd_inp(hndl,SD_OPTION);
	reg &= 0xff0f;
	reg |= (unsigned short)((responsetime & 0x000fu) << 4);
	sd_outp(hndl,SD_OPTION,reg);
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : initialize SD driver work buffer
 * Include      : 
 * Declaration  : int sd_set_buffer(int sd_port, void *buff,unsigned long size);
 * Functions    : initialize SD driver work buffer
 *              : this buffer is used for mainly MKB process
 * Argument     : void *buff : work buffer address
 *              : int size : work buffer size
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if applied to CPRM, allocating more than 8K bytes
 *****************************************************************************/
int sd_set_buffer(int sd_port, void *buff,unsigned long size)
{
	SDHNDL	*hndl;

	/* check buffer boundary (quadlet unit) */
	if( ((unsigned long)buff & 0x00000003u) != 0 ){
		return SD_ERR;
	}

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	/* initialize buffer area */
	hndl->rw_buff = (unsigned char*)buff;

	/* initialize buffer size */
	hndl->buff_size = size;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer card to stand-by state
 * Include      : 
 * Declaration  : int sd_standby(int sd_port);
 * Functions    : transfer card from transfer state to stand-by state
 * Argument     : none
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_standby(int sd_port)
{
	SDHNDL *hndl;
	int ret;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	ret = _sd_standby(hndl);

	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer card to stand-by state
 * Include      : 
 * Declaration  : int _sd_standby(SDHNDL *hndl)
 * Functions    : transfer card from transfer state to stand-by state
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_standby(SDHNDL *hndl)
{
	int ret;
	unsigned short de_rca;
	
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* set deselect RCA */
	de_rca = 0;

	/* ==== state transfer (transfer to stand-by) ==== */
	ret = _sd_card_send_cmd_arg(hndl,CMD7,SD_RESP_R1b,de_rca,0x0000);
	/* timeout error occured due to no response or response busy */
	if((ret != SD_OK) && (hndl->error != SD_ERR_RES_TOE)){
		goto ErrExit;
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return SD_OK;

ErrExit:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer card to transfer state
 * Include      : 
 * Declaration  : int sd_active(int sd_port);
 * Functions    : transfer card from stand-by state to transfer state
 * Argument     : none
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_active(int sd_port)
{
	SDHNDL *hndl;
	int ret;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	ret = _sd_active(hndl);

	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer card to transfer state
 * Include      : 
 * Declaration  : int _sd_active(SDHNDL *hndl)
 * Functions    : transfer card from stand-by state to transfer state
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_active(SDHNDL *hndl)
{
	unsigned short reg;

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}
	
	if(hndl->if_mode == SD_PORT_SERIAL){	/* 1bit */
		sddev_set_port(hndl->sd_port, SD_PORT_SERIAL);
		reg = sd_inp(hndl,SD_OPTION);
		reg |= 0x8000u;
		sd_outp(hndl,SD_OPTION,reg);
	}
	else{	/* 4bits */
		reg = sd_inp(hndl,SD_OPTION);
		reg &= 0x7fffu;
		sd_outp(hndl,SD_OPTION,reg);
		sddev_set_port(hndl->sd_port, SD_PORT_PARALLEL);
	}

	/* ==== state transfer (stand-by to transfer) ==== */
	if(_sd_card_send_cmd_arg(hndl,CMD7,SD_RESP_R1b,hndl->rca[0],0x0000) 
		!= SD_OK){
		goto ErrExit;
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return SD_OK;

ErrExit:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer card to inactive state
 * Include      : 
 * Declaration  : int sd_inactive(int sd_port);
 * Functions    : transfer card from any state to inactive state
 * Argument     : none
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_inactive(int sd_port)
{
	SDHNDL *hndl;
	int ret;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	ret = _sd_inactive(hndl);

	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer card to inactive state
 * Include      : 
 * Declaration  : int _sd_inactive(SDHNDL *hndl)
 * Functions    : transfer card from any state to inactive state
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_inactive(SDHNDL *hndl)
{
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* ==== state transfer (transfer to stand-by) ==== */
	if(_sd_card_send_cmd_arg(hndl,CMD15,SD_RESP_NON,hndl->rca[0],0x0000) 
		!= SD_OK){
		goto ErrExit;
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return SD_OK;

ErrExit:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : reget register
 * Include      : 
 * Declaration  : int sd_reget_reg(int sd_port, unsigned char *reg,int is_csd);
 * Functions    : reget CID or CSD
 * Argument     : unsigned char *cid : reget CID or CSD register address
 *              : int : CID(=0) or CSD(=1)
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_reget_reg(int sd_port, unsigned char *reg,int is_csd)
{
	SDHNDL *hndl;
	int i;
	unsigned short* ptr;
	unsigned short cmd;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	hndl->error = SD_OK;
	
	/* ---- transfer stand-by state ---- */
	if(_sd_standby(hndl) != SD_OK){
		goto ErrExit;
	}

	/* verify CID or CSD */
	if(is_csd == 0){
		ptr = hndl->cid;
		cmd = CMD10;
	}
	else{
		ptr = hndl->csd;
		cmd = CMD9;
	}
	
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* ---- reget CID or CSD (issue CMD10 or CMD9) ---- */
	if(_sd_card_send_cmd_arg(hndl,cmd,SD_RESP_R2_CID,hndl->rca[0],0x0000) 
		!= SD_OK){
		goto ErrExit;
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	if(_sd_active(hndl) != SD_OK){
		goto ErrExit;
	}

	for(i = 0; i < 8; ++i){
		*reg++ = (unsigned char)(*ptr >> 8);
		*reg++ = (unsigned char)(*ptr++);
	}

	return SD_OK;

ErrExit:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : set software write protect
 * Include      : 
 * Declaration  : int sd_set_softwp(int sd_port, int is_set,unsigned long data);
 * Functions    : set software write protect issuing write protect commands
 * Argument     : int is_set : set or clear
 *              : unsigned long data : write protect data address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_set_softwp(int sd_port, int is_set,unsigned long data)
{
	SDHNDL *hndl;
	int ret;
	unsigned char is_wp[4];	/* write protect info */
	unsigned short cmd;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	hndl->error = SD_OK;
	
	/* check suppoted command class */
	if(!(hndl->csd_ccc & 0x0040)){	/* don't support WP */
		_sd_set_err(hndl,SD_ERR_NOTSUP_CMD);
		return SD_ERR;
	}
	
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	if(is_set == 1){	/* set write protect */
		cmd = CMD28;
	}
	else{	/* clear write protect */
		cmd = CMD29;
	}
	ret = _sd_card_send_cmd_arg(hndl,cmd,SD_RESP_R1b,
		(unsigned short)(data>>16),(unsigned short)data);
	if(ret != SD_OK){
		/* timeout error possibly occur during programing */
		if(hndl->error == SD_ERR_CARD_TOE){
			if(_sd_wait_rbusy(hndl,100000) != SD_OK){
				goto ErrExit;
			}
		}
		else{
			goto ErrExit;
		}
	}
	
	/* verify preciding write protect operation */
	if(_sd_read_byte(hndl,CMD30,(unsigned short)(data>>16),
		(unsigned short)data,is_wp,4) != SD_OK){
		goto ErrExit;
	}
	if(is_wp[3] != (unsigned char)is_set){
		_sd_set_err(hndl,SD_ERR);
		goto ErrExit;
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return SD_OK;

ErrExit:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : lock/unlock
 * Include      : 
 * Declaration  : int sd_lock_unlock(int sd_port, unsigned char code,unsigned char *pwd,unsigned char len);
 * Functions    : lock/unlock operation
 *              : passward length is up to 16 bytes
 *              : case of cahnge passward, total length is 32 bytes,that is 
 *              : old and new passward maximum length
 * Argument     : unsigned char code : operation code
 *              : unsigned char *pwd : passward
 *              : unsigned char len : passward length
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_lock_unlock(int sd_port, unsigned char code,unsigned char *pwd,unsigned char len)
{
	SDHNDL *hndl;
	unsigned short cmd_len;	/* lock/unlock data length */
	char data[32];
	int	temp_error;
	int	loop;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	hndl->error = SD_OK;
	
	/* ---- check mount ---- */
	if( (hndl->mount && (SD_MOUNT_LOCKED_CARD | SD_MOUNT_UNLOCKED_CARD)) == 0 ){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* not mounted yet */
	}

	/* check suppoted command class */
	if(!(hndl->csd_ccc & 0x0080)){	/* don't support lock/unlock */
		_sd_set_err(hndl,SD_ERR_NOTSUP_CMD);
		return SD_ERR_NOTSUP_CMD;
	}

	data[0] = (char)code;

	if(code & 0x08){	/* forcing erase */
		cmd_len = 1;
	}
	else{
        if(code & 0x01){    /* set passward */
		    if(len > 16){
			    /* total passward length is not more than 32 bytes 		*/
			    /* but the library prohibit change password operation	*/
			    return SD_ERR;
			}
		    if(hndl->resp_status & 0x02000000){
			     /* prohibit set passward to lock card */ 
				_sd_set_err(hndl,SD_ERR_CARD_LOCK);
			     return SD_ERR;
            }
		}
		else if(len > 16){	/* only lock or unlock */
			/* one passward length is not more than 16 bytes */
			return SD_ERR;
		}

		/* include code and total data length */
		cmd_len = (unsigned short)(len + 2);

		/* set lock/unlock command data */
		data[1] = (char)len;
		while(len){
			data[cmd_len-len] = *pwd++;
			len--;
		}
	}
	
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}
	
	/* ---- set block length (issue CMD16) ---- */
	if(_sd_card_send_cmd_arg(hndl,CMD16,SD_RESP_R1,0x0000,cmd_len) != SD_OK){
		if(hndl->error == SD_ERR_CARD_LOCK){
			hndl->error = SD_OK;
		}
		else{
			goto ErrExit;
		}
	}

	if(_sd_write_byte(hndl,CMD42,0x0000,0x0000,(unsigned char*)data,cmd_len) 
		!= SD_OK){
		goto ErrExit;
	}

	if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) 
		== SD_OK){
		if((hndl->resp_status & RES_STATE) != STATE_TRAN){	/* not transfer state */
			 hndl->error = SD_ERR;
			goto ErrExit;
		}
	}
	else{	/* SDHI error */
		goto ErrExit;
	}

	if( (code & SD_LOCK_CARD) == SD_UNLOCK_CARD ){
		/* ---- clear locked status	---- */
		hndl->mount &= ~SD_CARD_LOCKED;

		if( hndl->mount == SD_MOUNT_UNLOCKED_CARD ){
			/* the card is already mounted as unlock card	*/

			/* ---- set block length (issue CMD16) ---- */
			if(_sd_card_send_cmd_arg(hndl,CMD16,SD_RESP_R1,0x0000,0x0200) != SD_OK){
				/* ---- set locked status ---- */
				hndl->mount |=  SD_CARD_LOCKED;
				goto ErrExit;
			}
		}
	}
	else{
		/* ---- set locked status ---- */
		hndl->mount |=  SD_CARD_LOCKED;
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return SD_OK;

ErrExit:
	/* keep error	*/
	temp_error = hndl->error;

	for( loop = 0; loop < 3; loop++ ){
		/* ---- retrive block length ---- */
		if(_sd_card_send_cmd_arg(hndl,CMD16,SD_RESP_R1,0x0000,0x0200) == SD_OK){
			break;
		}
	}

	_sd_set_err(hndl,temp_error);

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : set tempolary write protect
 * Include      : 
 * Declaration  : int sd_set_tmpwp(int sd_port, int is_set);
 * Functions    : set tempolary write protect programing csd
 * Argument     : int is_set : set or clear
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_set_tmpwp(int sd_port, int is_set)
{
	SDHNDL *hndl;
    int i;
	unsigned short *ptr;	/* got csd */
	unsigned char w_csd[16];	/* work csd */
	unsigned char crc7;	/* calculated crc7 value */

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	
	hndl->error = SD_OK;
	
	/* check suppoted command class */
	if(!(hndl->csd_ccc & 0x0010)){	/* don't support block write */
		_sd_set_err(hndl,SD_ERR_NOTSUP_CMD);
		return SD_ERR;
	}

	/* ---- make programing csd value ---- */
	/* set unprogramable fields */
	ptr = hndl->csd;
	for(i = 0;i < 14;i += 2){
		w_csd[i] = (unsigned char)(*ptr++);
		w_csd[i+1] = (unsigned char)((*ptr >> 8u));
	}
	
	/* set programing fields */
	w_csd[14] = (unsigned char)(*ptr);
	if(is_set == 1){	/* set write protect */
		w_csd[14] |= 0x10;
	}
	else{	/* clear write protect */
		w_csd[14] &= ~0x10;
	}

	/* calculate crc7 for CSD */
	crc7 = _sd_calc_crc(w_csd,15);

	/* set crc7 filelds */
	w_csd[15] = (unsigned char)((crc7<<1u) | 0x01);
	
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	if(_sd_write_byte(hndl,CMD27,0x0000,0x0000,w_csd,sizeof(w_csd)) != SD_OK){
		goto ErrExit;
	}

	if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) 
		== SD_OK){
		if((hndl->resp_status & RES_STATE) != STATE_TRAN){	/* not transfer state */
			 hndl->error = SD_ERR;
			goto ErrExit;
		}
	}
	else{	/* SDHI error */
		goto ErrExit;
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	if(is_set == 1){	/* set write protect */
		hndl->write_protect |= (unsigned char)SD_WP_TEMP;
	}
	else{	/* clear write protect */
		hndl->write_protect &= (unsigned char)~SD_WP_TEMP;
	}

	return SD_OK;

ErrExit:

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : calculate crc7
 * Include      : 
 * Declaration  : unsigned char _sd_calc_crc(unsigned char* data,int len)
 * Functions    : calculate crc7 value
 * Argument     : unsigned char* data : input data
 *              : int len : input data length (byte unit)
 * Return       : calculated crc7 value 
 * Remark       : 
 *****************************************************************************/
unsigned char _sd_calc_crc(unsigned char* data,int len)
{
	int i,j,k;
	unsigned char p_crc[7];	/* previous crc value */
	unsigned char t_crc[7];	/* tentative crc value */
	unsigned char m_data;	/* input bit mask data */
	unsigned char crc7 = 0;	/* calculated crc7 */

	for(i = 0;i < sizeof(p_crc);i++){
		p_crc[i] = 0;
		t_crc[i] = 0;
	}

	for(i = len;i > 0;i--,data++){	/* byte loop */
		for(j = 8;j > 0;j--){	/* bit loop */
			m_data = (unsigned char)(((*data >> (j-1)) & 0x01));
			t_crc[6] = (p_crc[0] != m_data);
			t_crc[5] = p_crc[6];
			t_crc[4] = p_crc[5];
			t_crc[3] = (p_crc[4] != (p_crc[0] != m_data));
			t_crc[2] = p_crc[3];
			t_crc[1] = p_crc[2];
			t_crc[0] = p_crc[1];

			/* save tentative crc value */
			for(k = 0;k < sizeof(p_crc);k++){
				p_crc[k] = t_crc[k];
			}
		}
	}
	
	/* convert bit to byte form */
	for(i = 0;i < sizeof(p_crc);i++){
		crc7 |= (unsigned char)((p_crc[i] << (6-i)));
	}

	return crc7;
}

/*****************************************************************************
 * ID           :
 * Summary      : set memory
 * Include      : 
 * Declaration  : int _sd_memset(unsigned char *p,int data,
 *              : 	unsigned long cnt)
 * Functions    : fill memory filling data(=data) from start address(=p)
 *              : by filling size(=cnt)
 * Argument     : unsigned char *p : start address of memory
 *              : int data : filling data
 *              : unsigned long cnt : filling size
 * Return       : 0 : end of succeed
 * Remark       : 
 *****************************************************************************/
int _sd_memset(unsigned char *p,unsigned char data, unsigned long cnt)
{
	while(cnt--){
		*p++ = data;
	}
	
	return 0;
}

/*****************************************************************************
 * ID           :
 * Summary      : copy memory
 * Include      : 
 * Declaration  : static int _sd_memcpy(unsigned char *dst,unsigned char *src,
 *              :	unsigned long cnt)
 * Functions    : copy data from source address(=src) to destination address
 *              : (=dst) by copy size(=cnt)
 * Argument     : unsigned char *dst : destination address
 *              : unsigned char *src : source address
 *              : unsigned long cnt : copy size
 * Return       : 0 : end of succeed
 * Remark       : 
 *****************************************************************************/
int _sd_memcpy(unsigned char *dst,unsigned char *src,unsigned long cnt)
{
	while(cnt--){
		*dst++ = *src++;
	}
	
	return 0;
}

/*****************************************************************************
 * ID           :
 * Summary      : create Volume ID Number
 * Include      : 
 * Declaration  : unsigned short _sd_rand(void)
 * Functions    : get Volume ID Number
 *              : Volume ID Number is created by pseudo random number
 * Argument     : none
 * Return       : created Volume ID Number
 * Remark       : 
 *****************************************************************************/
unsigned short _sd_rand(void)
{

	next = next * 1103515245L + 12345;
	return (unsigned short)next;
	
}

/*****************************************************************************
 * ID           :
 * Summary      : set initial value of Volume ID Number
 * Include      : 
 * Declaration  : void _sd_srand(unsigned long seed)
 * Functions    : set initial value of Volume ID Number
 * Argument     : unsigned long seed : initial seting value
 * Return       : created Volume ID Number
 * Remark       : 
 *****************************************************************************/
void _sd_srand(unsigned long seed)
{
	if(next == 0){
		next = seed;
	}
}

/*****************************************************************************
 * ID           :
 * Summary      : wait response busy
 * Include      : 
 * Declaration  : int _sd_wait_rbusy(SDHNDL *hndl,int time)
 * Functions    : wait response busy finished
 * Argument     : SDHNDL *hndl : SD handle
 *              : int time : response busy wait interval
 * Return       : SD_OK : response busy finished
 *              : SD_ERR: response busy not finished
 * Remark       :
 *****************************************************************************/
int _sd_wait_rbusy(SDHNDL *hndl,int time)
{
	int i;


	for(i = 0;i < time;++i){
		if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) == SD_OK){
			if((hndl->resp_status & RES_STATE) == STATE_TRAN){	/* transfer state */
				return SD_OK;
			}
		}
		else{	/* SDHI error */
			break;
		}

		if(_sd_check_media(hndl) != SD_OK){
			break;
		}

		sddev_int_wait(hndl->sd_port, 1);
	}

	_sd_set_err(hndl,SD_ERR_HOST_TOE);

	return SD_ERR;

}

/* End of File */
