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
*  File Name   : sdio_util.c
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
 * Summary      : Reset SDIO Function
 * Include      : 
 * Declaration  : int sdio_reset(int sd_port);
 * Functions    : 
 * Argument     : 
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sdio_reset(int sd_port)
{
	SDHNDL *hndl;
	unsigned char io_buff;
	unsigned short sd_option,sd_clk_ctrl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	if((sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_CBSY) == SD_INFO2_MASK_CBSY){
		sddev_loc_cpu(sd_port);
		sd_option = sd_inp(hndl,SD_OPTION);
		sd_clk_ctrl = sd_inp(hndl,SD_CLK_CTRL);
		#if		(TARGET_RZ_A1 == 1)
		sd_outp(hndl,SOFT_RST,0x0006);
		sd_outp(hndl,SOFT_RST,0x0007);
		#else
		sd_outp(hndl,SOFT_RST,0);
		sd_outp(hndl,SOFT_RST,1);
		#endif
		sd_outp(hndl,SD_OPTION,sd_option);
		sd_outp(hndl,SD_CLK_CTRL,sd_clk_ctrl);
		sddev_unl_cpu(sd_port);
	}

	io_buff = 0x08;
	if(sdio_write_direct(sd_port,&io_buff,0,0x06,SD_IO_VERIFY_WRITE) != SD_OK){
		return hndl->error;
	}

	/* ---- clear mount flag ---- */
	hndl->mount = SD_UNMOUNT_CARD;

	/* ---- initilaize SD handle ---- */
	_sd_init_hndl(hndl,0,hndl->voltage);
	hndl->error = SD_OK;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get io ocr register
 * Include      : 
 * Declaration  : int sdio_get_ioocr(int sd_port, unsigned long *ioocr);
 * Functions    : get io ocr register value
 * Argument     : unsigned long *ioocr : IO OCR register address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sdio_get_ioocr(int sd_port, unsigned long *ioocr)
{
	SDHNDL *hndl;
	unsigned long temp;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	/* ---- check media type ---- */
	if((hndl->media_type & SD_MEDIA_IO) == 0){
		_sd_set_err(hndl,SD_ERR_CARD_TYPE);
		return SD_ERR_CARD_TYPE;
	}

	if(ioocr){
		temp   = (unsigned long)(hndl->io_ocr[0] & 0xffff);
		temp <<= 16;
		temp  |= (unsigned long)(hndl->io_ocr[1] & 0xffff);
		*ioocr = temp;
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get io information
 * Include      : 
 * Declaration  : int sdio_get_ioinfo(int sd_port, unsigned char *ioinfo);
 * Functions    : get io information(IO OCR[31:24])
 * Argument     : unsigned char *ioinfo : io information(IO OCR[31:24])
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sdio_get_ioinfo(int sd_port, unsigned char *ioinfo)
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

	/* ---- check media type ---- */
	if((hndl->media_type & SD_MEDIA_IO) == 0){
		_sd_set_err(hndl,SD_ERR_CARD_TYPE);
		return SD_ERR_CARD_TYPE;
	}

	if(ioinfo){
		*ioinfo = hndl->io_info;
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get cia register
 * Include      : 
 * Declaration  : int sdio_get_cia(int sd_port, unsigned char *reg, unsigned char *cis, unsigned long func_num, long cnt);
 * Functions    : get cia value
 * Argument     : unsigned char *reg : CCCR or FBR register address
 *              : unsigned char *cis : CIS register address
 *              : unsigned long func_num : function number (0 to 7, 0 means Common)
 *              : long cnt : size of CIS to read
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : if pointer has NULL ,the register value isn't returned
 *****************************************************************************/
int sdio_get_cia(int sd_port, unsigned char *reg, unsigned char *cis, unsigned long func_num, long cnt)
{
	SDHNDL *hndl;
	unsigned int i;
	unsigned long adr = 0;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	/* check read register address and function number */
	if(func_num > 7){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	if( sdio_read(sd_port, hndl->io_reg[func_num], 0, (unsigned long)(0x100 * func_num),
		SDIO_INTERNAL_REG_SIZE, (SD_IO_INCREMENT_ADDR | SD_IO_FORCE_BYTE)) != SD_OK ){
		return hndl->error;
	}

	if(reg){	/* CCCR or FBR */
		for(i = 0; i < SDIO_INTERNAL_REG_SIZE; ++i){
			*reg++ = hndl->io_reg[func_num][i];
		}
	}

	adr = (unsigned long)( ((unsigned long)hndl->io_reg[func_num][0x0b] << 16) |
		  				   ((unsigned long)hndl->io_reg[func_num][0x0a] <<  8) |
		  				   hndl->io_reg[func_num][0x09] );
	if( adr != 0 ){
		if(cis){
			if(sdio_read(sd_port, cis, 0, adr, cnt, (SD_IO_INCREMENT_ADDR | SD_IO_FORCE_BYTE)) != SD_OK){
				return hndl->error;
			}

			for(i = 0; i < cnt; ++i){
				hndl->cis[func_num][i] = *cis++;
			}
		}
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : set io functions ready
 * Include      : 
 * Declaration  : int sdio_set_enable(int sd_port, unsigned char func_bit);
 * Functions    : set io functions access raedy state
 * Argument     : unsigned char func_bit : specify function by bit map
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_set_enable(int sd_port, unsigned char func_bit)
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

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	func_bit &= 0xfe;

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* ==== set io functions (I/O enable) ==== */
	if(_sdio_direct(hndl,&func_bit,0,0x02,1,SD_IO_VERIFY_WRITE) != SD_OK){
		goto ErrExit;
	}

	/* save I/O enable register */
	hndl->io_reg[0][0x02] = func_bit;

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
 * Summary      : get ready io functions
 * Include      : 
 * Declaration  : int sdio_get_ready(int sd_port, unsigned char *func_bit);
 * Functions    : inquire io function's access ready status
 * Argument     : unsigned char *func_bit : access enable function by bit map
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_get_ready(int sd_port, unsigned char *func_bit)
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

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* ==== get io functions (I/O ready) ==== */
	if(_sdio_direct(hndl,func_bit,0,0x03,0,0) != SD_OK){
		goto ErrExit;
	}

	/* save I/O ready register */
	hndl->io_reg[0][0x03] = *func_bit;

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
 * Summary      : set interrupt from io functions
 * Include      : 
 * Declaration  : int sdio_set_int(int sd_port, unsigned char func_bit, int enab);
 * Functions    : enable or disable interrupt from io functions
 * Argument     : unsigned char func_bit : specify enable function by bit map
 *              : unsigned char enab : control enable or disable 
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_set_int(int sd_port, unsigned char func_bit, int enab)
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

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	if(enab){
		func_bit |= 0x01;	/* set enable */
	}
	else{
		func_bit &= 0xfe;	/* set disable */
	}

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* ==== set io functions (Int Enable) ==== */
	if(_sdio_direct(hndl,&func_bit,0,0x04,1,SD_IO_VERIFY_WRITE) != SD_OK){
		goto ErrExit;
	}

	/* save Int enable register */
	hndl->io_reg[0][0x04] = func_bit;

	sdio_enable_int(sd_port);

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
 * Summary      : get interrupt form io functions
 * Include      : 
 * Declaration  : int sdio_get_int(int sd_port, unsigned char *func_bit,int *enab);
 * Functions    : inquire io functions's interrupt status
 * Argument     : unsigned char *func_bit : ready function by bit map
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_get_int(int sd_port, unsigned char *func_bit, int *enab)
{
	SDHNDL *hndl;
	unsigned char int_enab;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* ==== get io functions (Int Enable) ==== */
	if(_sdio_direct(hndl,func_bit,0,0x04,0,0) != SD_OK){
		goto ErrExit;
	}

	/* save I/O Int Enable register */
	hndl->io_reg[0][0x04] = *func_bit;

	int_enab = hndl->io_reg[0][0x04];
	if(int_enab & 0x1){
		*enab = 1;
	}
	else{
		*enab = 0;
	}

	return SD_OK;

ErrExit:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : set io functions block length
 * Include      : 
 * Declaration  : int sdio_set_blocklen(int sd_port, unsigned short len, unsigned long func_num);
 * Functions    : set io function's block length
 * Argument     : unsigned short len : block length
 *              : unsigned long func_num : specify function by number (from 0 to 7)
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_set_blocklen(int sd_port, unsigned short len, unsigned long func_num)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	/* check read register address and function number */
	if(func_num > 7){
		return SD_ERR;
	}

	switch(len){
	case 32:
	case 64:
	case 128:
	case 256:
	case 512:
		/* len is OK */
		break;
	default:
		return SD_ERR;
		break;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

#if		(TARGET_RZ_A1 == 1)
	if(hndl->trans_mode & SD_MODE_DMA){
		if(hndl->trans_mode & SD_MODE_DMA_64){
			if( len == 32 ){
				return SD_ERR;
			}
		}
	}
#endif

	hndl->error = SD_OK;

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	/* ---- set io functions block length ---- */
	if(_sdio_set_blocklen(hndl, len, func_num) != SD_OK){
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
 * Summary      : set io functions block length
 * Include      : 
 * Declaration  : int _sdio_set_blocklen(SDHNDL *hndl,unsigned short len,
 *              : 	unsigned long func_num)
 * Functions    : set io function's block length
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short len : block length
 *              : unsigned long func_num : specify function by number (from 0 to 7)
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sdio_set_blocklen(SDHNDL *hndl, unsigned short len, unsigned long func_num)
{
	unsigned long adr;
	unsigned long base_adr;
	unsigned char reg_val;
	unsigned short len_t;

	if(func_num > 7){
		_sd_set_err(hndl,SD_ERR_ILL_FUNC);
		return hndl->error;		/* illegal function */
	}

	if(len > 512){
		_sd_set_err(hndl,SD_ERR_ILL_FUNC);
		return hndl->error;		/* illegal function */
	}

	/* ---- check is support multiple block transfer ---- */
	if((hndl->io_reg[0][0x08] & 0x02) == 0){
		_sd_set_err(hndl,SD_ERR_ILL_FUNC);
		return hndl->error;		/* illegal function */
	}

	len_t = len;

	base_adr = (unsigned long)(0x100 * func_num);
	for(adr = 0x10; adr <= 0x11; adr++){
		/* set write value by little endian form */
		reg_val = (unsigned char)len;
		len   >>= 8;
		/* ==== set io functions (FN Block Size) ==== */
		if(_sdio_direct(hndl,&reg_val, 0, (base_adr + adr), 1, SD_IO_VERIFY_WRITE) != SD_OK){
			return hndl->error;
		}

		/* save FN Block Size */
		hndl->io_reg[func_num][adr] = reg_val;
	}

	/* save io block length */
	hndl->io_len[func_num] = len_t;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get io functions block length
 * Include      : 
 * Declaration  : int sdio_get_blocklen(int sd_port, unsigned short *len,unsigned long func_num);
 * Functions    : inquire io function's block length
 * Argument     : unsigned short *len : block length
 *              : unsigned long func_num : specify function by number (from 0 to 7)
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Input        : 
 * Output       : 
 * Call functions : 
 * Remark       : 
 *****************************************************************************/
int sdio_get_blocklen(int sd_port, unsigned short *len, unsigned long func_num)
{
	SDHNDL *hndl;
	unsigned short len_t;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	/* check read register address and function number */
	if(func_num > 7){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	hndl->error = SD_OK;

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	/* check to support block transfer */
	if((hndl->io_reg[0][0x08] & 0x02) == 0){
		_sd_set_err(hndl,SD_ERR_ILL_FUNC);
		return SD_ERR;
	}

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		goto ErrExit;
	}

	if( _sdio_read_byte(hndl, hndl->io_reg[func_num], 0, (unsigned long)(0x100 * func_num),
		SDIO_INTERNAL_REG_SIZE, SD_IO_INCREMENT_ADDR) != SD_OK ){
		goto ErrExit;
	}
	len_t  = hndl->io_reg[func_num][0x11];
	len_t <<= 8;
	len_t |= (hndl->io_reg[func_num][0x10] & 0x00ff);

	switch(len_t){
	case 32:
	case 64:
	case 128:
	case 256:
	case 512:
		/* len is OK */
		hndl->io_len[func_num] = len_t;				/* already and supported */
		break;
	default:
		hndl->io_len[func_num] = 0xffff;			/* already but not supported */
		break;
	}

#if		(TARGET_RZ_A1 == 1)
	if(hndl->trans_mode & SD_MODE_DMA){
		if(hndl->trans_mode & SD_MODE_DMA_64){
			if( len_t == 32 ){
				hndl->io_len[func_num] = 0xffff;	/* already but not supported */
			}
		}
	}
#endif

	*len = len_t;

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
 * Summary      : stop operations compulsory
 * Include      : 
 * Declaration  : void sdio_abort(int sd_port, unsigned long func_num);
 * Functions    : set flag (=SD handle member stop) stop operations compulsory
 *              : if this flag is set, read, write, format operations is
 *              : stopped
 *              : this flag is used for card detect/removal interrupt detection
 *              : 
 * Argument     : none
 * Return       : none
 * Remark       : 
 *****************************************************************************/
void sdio_abort(int sd_port, unsigned long func_num)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return;
	}

	/* check read register address and function number */
	if(func_num > 7){
		return;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return;	/* not initilized */
	}

	hndl->io_abort[func_num] = 1;
}

/*****************************************************************************
 * ID           :
 * Summary      : set block count
 * Include      : 
 * Declaration  : int sdio_set_blkcnt(int sd_port, short blocks);
 * Functions    : set maximum block count per multiple command
 * Argument     : short sectors : block count
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : maximam block count is constrained from 3 to 32767(0x7fff)
 *****************************************************************************/
int sdio_set_blkcnt(int sd_port, short blocks)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	if(blocks < 1){
		/* need more than 1 continuous transfer */
		return SD_ERR;	/* undefined value */
	}

	if(!(hndl->media_type & SD_MEDIA_IO)){
		return SD_ERR_IO_CAPAB;
	}

	hndl->trans_blocks = blocks;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get block count
 * Include      : 
 * Declaration  : int sdio_get_blkcnt(int sd_port);
 * Functions    : get maximum block count per multiple command
 * Argument     : none
 * Return       : not less than 0 : block count
 *              : SD_ERR: end of error
 * Remark       : maximam block count are constrained from 1 to 65535
 *****************************************************************************/
int sdio_get_blkcnt(int sd_port)
{
	SDHNDL *hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	if(!(hndl->media_type & SD_MEDIA_IO)){
		return SD_ERR_IO_CAPAB;
	}

	return (int)hndl->trans_blocks;
}

/* End of File */
