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
*  File Name   : sd_mount.c
*  Contents    : Card mount
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
*              :                         Modified _sd_card_switch_func_access_mode1()
*******************************************************************************/
#include "../../../inc/sdif.h"
#include "../inc/access/sd.h"

#ifdef __CC_ARM
#pragma arm section code = "CODE_SDHI"
#pragma arm section rodata = "CONST_SDHI"
#pragma arm section rwdata = "DATA_SDHI"
#pragma arm section zidata = "BSS_SDHI"
#endif

static unsigned short stat_buff[NUM_PORT][64/sizeof(unsigned short)];

int _sd_calc_erase_sector(SDHNDL *hndl);

/*****************************************************************************
 * ID           :
 * Summary      : mount SD card
 * Include      : 
 * Declaration  : int sd_mount(int sd_port, unsigned long mode,unsigned long voltage);
 * Functions    : mount SD memory card user area
 *              : can be access user area after this function is finished
 *              : without errors
 *              : turn on power
 *              : 
 *              : following is available SD Driver mode
 *              : SD_MODE_POLL: software polling
 *              : SD_MODE_HWINT: hardware interrupt
 *              : SD_MODE_SW: software data transfer (SD_BUF)
 *              : SD_MODE_DMA: DMA data transfer (SD_BUF)
 *              : SD_MODE_MEM: only memory cards
 *              : SD_MODE_IO: memory and io cards
 *              : SD_MODE_COMBO: memory ,io and combo cards
 *              : SD_MODE_DS: only default speed
 *              : SD_MODE_HS: default and high speed
 *              : SD_MODE_VER1X: ver1.1 host
 *              : SD_MODE_VER2X: ver2.x host
 * Argument     : unsigned long mode : SD Driver operation mode
 *              : unsigned long voltage : operation voltage
 * Return       : hndl->error : SD handle error value
 *              : SD_OK : end of succeed
 *              : other: end of error
 * Remark       : user area should be mounted
 *****************************************************************************/
int sd_mount(int sd_port, unsigned long mode,unsigned long voltage)
{
	SDHNDL	*hndl;
	int ret;
	unsigned char spec;
	unsigned short info1_back;
	unsigned char io_buff;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}
	ret = SD_ERR;

	// This buffer is actually never used! Perhaps I removed whatever used it. - Rohan
	/* ==== check work buffer is allocated ==== */
	//if(hndl->rw_buff == 0){
		//return SD_ERR;	/* not allocated yet */
	//}

	if(mode & SD_MODE_IO){
		if( (hndl->sup_card & 0x30u) == (mode & 0x30u) ){
			/* support SDIO card */
			if( (hndl->media_type == SD_MEDIA_IO) || (hndl->media_type == SD_MEDIA_COMBO) ){
				/* media has SDIO */
				if(hndl->io_flag & SD_IO_POWER_INIT){	/* already supplied power */
					/* ==== transfer idle state (issue CMD52) ==== */
					/* data:08'h func:0 address:06'h verify write */
					io_buff = 0x08;
					if(sdio_write_direct(sd_port,&io_buff,0,0x06,SD_IO_VERIFY_WRITE) != SD_OK){
						return SD_ERR;
					}
				}
			}
		}
	}

	/* ==== initialize parameter ==== */
	_sd_init_hndl(hndl,mode,voltage);
	hndl->error = SD_OK;
	
	/* ==== is card inserted? ==== */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);
		return hndl->error;		/* not inserted */
	}
	
	/* ==== power on sequence ==== */
	/* ---- turn on voltage ---- */
	if(sddev_power_on(sd_port) != SD_OK){
		_sd_set_err(hndl,SD_ERR_CPU_IF);
		goto ERR_EXIT;
	}

	/* ---- set single port ---- */
	_sd_set_port(hndl,SD_PORT_SERIAL);

	/* ---- supply clock (card-identification ratio) ---- */
	if(_sd_set_clock(hndl,SD_CLK_400kHz,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;		/* not inserted */
	}
	
	sddev_int_wait(sd_port, 2);	/* add wait function  */

	sddev_loc_cpu(sd_port);
	info1_back = sd_inp(hndl,SD_INFO1);
	info1_back &= 0xfff8;
	sd_outp(hndl,SD_INFO1,info1_back);
	sd_outp(hndl,SD_INFO2,0);
			/* Clear DMA Enable because of CPU Transfer */
	sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) & ~CC_EXT_MODE_DMASDRW));
	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	sddev_unl_cpu(sd_port);

	/* ==== initialize card and distinguish card type ==== */
	if(_sd_card_init(hndl) != SD_OK){
		goto ERR_EXIT;	/* failed card initialize */
	}

	if(hndl->media_type & SD_MEDIA_MEM){	/* with memory part */
		/* ==== check card registers ==== */
		/* ---- check CSD register ---- */
		if(_sd_check_csd(hndl) != SD_OK){
			goto ERR_EXIT;
		}
		
		/* ---- no check other registers (to be create) ---- */
		
		/* get user area size */
		if(_sd_get_size(hndl,SD_USER_AREA) != SD_OK){
			goto ERR_EXIT;
		}
			
		/* check write protect */
		hndl->write_protect |= (unsigned char)_sd_iswp(hndl);
	}
	
	if(hndl->media_type & SD_MEDIA_IO){	/* with IO part */
		if(_sd_io_mount(hndl) != SD_OK){
			goto ERR_EXIT;
		}
	}

	if(hndl->media_type & SD_MEDIA_MEM){	/* with memory part */
		if(_sd_mem_mount(hndl) != SD_OK){
			goto ERR_EXIT;
		}
		if(hndl->error == SD_ERR_CARD_LOCK){
			hndl->mount = (SD_CARD_LOCKED | SD_MOUNT_LOCKED_CARD);
			/* ---- halt clock ---- */
			_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
			return SD_OK_LOCKED_CARD;
		}
	}

	/* if SD memory card, get SCR register */
	if(hndl->media_type & SD_MEDIA_SD){
		if(_sd_card_get_scr(hndl) != SD_OK){
			goto ERR_EXIT;
		}
		spec = (unsigned char)((hndl->scr[0] & 0x0F00u) >> 8u);
		if(spec){	/* ---- more than phys spec ver1.10 ---- */
			hndl->sd_spec = spec;
			if(hndl->sup_speed == SD_MODE_HS){
				/* set memory part speed */
				if(_sd_set_mem_speed(hndl) != SD_OK){
					goto ERR_EXIT;
				}
			}
			_sd_calc_erase_sector(hndl);
		}
		else{	/* ---- phys spec ver1.00 or ver1.01 ---- */
			hndl->sd_spec = SD_SPEC_10;
		}
	}

	/* if io or combo, set io part speed */
	if(hndl->media_type & SD_MEDIA_IO){
		if(hndl->sup_speed == SD_MODE_HS){
			if(_sd_set_io_speed(hndl) != SD_OK){
				goto ERR_EXIT;
			}
		}

		/* Enable SDIO interrupt */
		sd_outp(hndl,SDIO_MODE,(unsigned short)sd_inp(hndl,SDIO_MODE) | SDIO_MODE_IOMOD);
	}

	/* ---- set mount flag ---- */
	hndl->mount = SD_MOUNT_UNLOCKED_CARD;

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	return hndl->error;

ERR_EXIT:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : initialize card
 * Include      : 
 * Declaration  : int _sd_card_init(SDHNDL *hndl)
 * Functions    : initialize card from idle state to stand-by
 *              : distinguish card type (SD, MMC, IO or COMBO)
 *              : get CID, RCA, CSD from the card
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_card_init(SDHNDL *hndl)
{
	int ret;
	int i;
	int just_sdio_flag;
//	unsigned char io_buff;
	unsigned short if_cond_0;
	unsigned short if_cond_1;

	hndl->media_type = SD_MEDIA_UNKNOWN;
	if_cond_0 = hndl->if_cond[0];
	if_cond_1 = hndl->if_cond[1];

	if(hndl->sup_card & SD_MODE_IO){
		just_sdio_flag = 0;								/* basically treate as Combo */
		if(sddev_cmd0_sdio_mount(hndl->sd_port) == SD_OK ){
			ret = _sd_send_cmd(hndl,CMD0);
			if( ret != SD_OK ){
				hndl->error = SD_OK;
				just_sdio_flag = 1;						/* treate as just I/O */
			}
		}
		else{
			just_sdio_flag = 1;							/* treate as just I/O */
		}

		if(sddev_cmd8_sdio_mount(hndl->sd_port) == SD_OK ){
			if(hndl->sup_ver == SD_MODE_VER2X){
				ret = _sd_card_send_cmd_arg(hndl,CMD8,SD_RESP_R7,if_cond_0,if_cond_1);
				if(ret == SD_OK){
					/* check R7 response */
					if(hndl->if_cond[0] & 0xf000){
						hndl->error = SD_ERR_IFCOND_VER;
						return SD_ERR;
					}
					if((hndl->if_cond[1] & 0x00ff) != 0x00aa){
						hndl->error = SD_ERR_IFCOND_ECHO;
						return SD_ERR;
					}
					hndl->sd_spec = SD_SPEC_20;			/* cmd8 have response.				*/
														/* because of (phys spec ver2.00)	*/
				}
				else{
					/* ==== clear illegal command error for CMD8 ==== */
					if(sddev_cmd0_sdio_mount(hndl->sd_port) == SD_OK ){
						for(i=0; i < 3; i++){
							ret = _sd_send_cmd(hndl,CMD0);
							if(ret == SD_OK){
								break;
							}
						}
					}
					hndl->error = SD_OK;
					hndl->sd_spec = SD_SPEC_10;			/* cmd8 have no response.					*/
														/* because of (phys spec ver1.01 or 1.10)	*/
				}
			}
			else{
				hndl->sd_spec = SD_SPEC_10;				/* cmd8 have response.						*/
														/* because of (phys spec ver1.01 or 1.10)	*/
			}
		}
		else{
			just_sdio_flag = 1;							/* treate as just I/O */
		}

		/* ==== distinguish card and read OCR (issue CMD5) ==== */
		ret = _sd_card_send_ocr(hndl,SD_MEDIA_UNKNOWN);
		if(ret == SD_OK){
			/* set OCR (issue CMD5) */
			if(_sd_card_send_ocr(hndl,SD_MEDIA_IO) != SD_OK){
				return SD_ERR;
			}
			
			hndl->io_flag |= SD_IO_FUNC_INIT;

			hndl->io_info = (unsigned char)(hndl->io_ocr[0] >> 8);

			/* is memory present */
			hndl->media_type = SD_MEDIA_IO;

			if( just_sdio_flag == 0 ){
				/* is not memory present */
				if((hndl->io_info & 0x08) == 0){ 	/* just IO */
					goto GET_RCA;
				}
			}
			else{
				goto GET_RCA;						/* just IO */
			}
		}
		else{
			/* clear error due to card distinction */
			hndl->error = SD_OK;
		}
	}
	
	/* ==== transfer idle state (issue CMD0) ==== */
	if( hndl->media_type == SD_MEDIA_UNKNOWN ){
		for(i=0; i < 3; i++){
			ret = _sd_send_cmd(hndl,CMD0);
			if(ret == SD_OK){
				break;
			}
		}

		if(ret != SD_OK){
			return SD_ERR;	/* error for CMD0 */
		}

		/* clear error by reissuing CMD0 */
		hndl->error = SD_OK;

		hndl->media_type |= SD_MEDIA_SD;

		hndl->partition_id = 0;

		if(hndl->sup_ver == SD_MODE_VER2X){
			ret = _sd_card_send_cmd_arg(hndl,CMD8,SD_RESP_R7,if_cond_0,if_cond_1);
			if(ret == SD_OK){
				/* check R7 response */
				if(hndl->if_cond[0] & 0xf000){
					hndl->error = SD_ERR_IFCOND_VER;
					return SD_ERR;
				}
				if((hndl->if_cond[1] & 0x00ff) != 0x00aa){
					hndl->error = SD_ERR_IFCOND_ECHO;
					return SD_ERR;
				}
				hndl->sd_spec = SD_SPEC_20;			/* cmd8 have response.				*/
													/* because of (phys spec ver2.00)	*/
			}
			else{
				/* ==== clear illegal command error for CMD8 ==== */
				for(i=0; i < 3; i++){
					ret = _sd_send_cmd(hndl,CMD0);
					if(ret == SD_OK){
						break;
					}
				}
				hndl->error = SD_OK;
				hndl->sd_spec = SD_SPEC_10;			/* cmd8 have no response.					*/
													/* because of (phys spec ver1.01 or 1.10)	*/
			}
		}
		else{
			hndl->sd_spec = SD_SPEC_10;				/* cmd8 have response.						*/
													/* because of (phys spec ver1.01 or 1.10)	*/
		}
	}

	/* set OCR (issue ACMD41) */
	ret = _sd_card_send_ocr(hndl,(int)hndl->media_type);

	/* clear error due to card distinction */
	hndl->error = SD_OK;
	
	if(ret != SD_OK){
		/* softreset for error clear (issue CMD0) */
		for(i=0; i < 3; i++){
			ret = _sd_send_cmd(hndl,CMD0);
			if( ret == SD_OK){
				break;
			}
		}
		if(ret != SD_OK){
			return SD_ERR;	/* error for CMD0 */
		}
		/* clear error by reissuing CMD0 */
		hndl->error = SD_OK;
		/* ---- get OCR (issue CMD1) ---- */
		if((ret = _sd_card_send_ocr(hndl,SD_MEDIA_MMC)) == SD_OK){
			/* MMC */
			hndl->media_type = SD_MEDIA_MMC;
			hndl->error = SD_OK;
		}
		else{
			/* unknown card */
			hndl->media_type = SD_MEDIA_UNKNOWN;
			_sd_set_err(hndl,SD_ERR_CARD_TYPE);
			return SD_ERR;
		}
	}

	/* ---- get CID (issue CMD2) ---- */
	if(_sd_card_send_cmd_arg(hndl,CMD2,SD_RESP_R2_CID,0,0) != SD_OK){
		return SD_ERR;
	}
	
GET_RCA:
	/* ---- get RCA (issue CMD3) ---- */
	if(hndl->media_type & SD_MEDIA_COMBO){	/* IO or SD */
		for(i=0; i < 3; i++){
			if(_sd_card_send_cmd_arg(hndl,CMD3,SD_RESP_R6,0,0) != SD_OK){
				return SD_ERR;
			}
			if(hndl->rca[0] != 0x00){
				if(hndl->media_type & SD_MEDIA_IO){
					hndl->io_flag |= SD_IO_POWER_INIT;
				}
				break;
			}
		}
		/* illegal RCA */
		if(i == 3){
			_sd_set_err(hndl,SD_ERR_CARD_CC);
			return SD_ERR;
		}
	}
	else{
		hndl->rca[0] = 1;   /* fixed 1 */
		if(_sd_card_send_cmd_arg(hndl,CMD3,SD_RESP_R1,hndl->rca[0],0x0000) 
			!= SD_OK){
			return SD_ERR;
		}
	}

	/* ==== stand-by state  ==== */
	
	if(hndl->media_type == SD_MEDIA_IO){
		return SD_OK;
	}

	/* ---- get CSD (issue CMD9) ---- */
	if(_sd_card_send_cmd_arg(hndl,CMD9,SD_RESP_R2_CSD,hndl->rca[0],0x0000) 
		!= SD_OK){
		return SD_ERR;
	}

	hndl->dsr[0] = 0x0000;

	if(hndl->media_type & SD_MEDIA_MEM){
		/* is DSR implimented? */
		if(hndl->csd[3] & 0x0010u){	/* implimented */
			/* set DSR (issue CMD4) */
			hndl->dsr[0] = 0x0404;
            if(_sd_card_send_cmd_arg(hndl,CMD4,SD_RESP_NON,hndl->dsr[0],0x0000)
				!= SD_OK){
				return SD_ERR;
			}
		}
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : mount io card
 * Include      : 
 * Declaration  : int _sd_io_mount(SDHNDL *hndl)
 * Functions    : mount io part from stand-by to command or transfer state
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_io_mount(SDHNDL *hndl)
{
	int i;
	unsigned char io_buff;
	unsigned short len;

	/* ==== data-transfer mode ==== */
	if(_sd_card_send_cmd_arg(hndl,CMD7,SD_RESP_R1b,hndl->rca[0],0x0000) 
		!= SD_OK){
		goto ERR_EXIT;
	}

	/* ---- get card capability (include LSC and 4BLS) ---- */
	/* func:0 address:08'h read */
	if(_sdio_direct(hndl,&io_buff,0,0x08,0,0) != SD_OK){
		goto ERR_EXIT;
	}

	if(io_buff & 0x40u){	/* low speed card */
		hndl->csd_tran_speed = SD_CLK_400kHz;
	}
	else{	/* high speed card */
		hndl->csd_tran_speed = SD_CLK_25MHz;
	}
	
	/* ---- supply clock (data-transfer ratio) ---- */
	_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE);

	/* set bus width and clear pull-up DAT3 */
	if((io_buff & 0x40u) && ((io_buff & 0x80u) == 0)){	/* not support 4bits */
		_sd_set_port(hndl,SD_PORT_SERIAL);
	}
	else{
		_sd_set_port(hndl,hndl->sup_if_mode);
	}

	/* ---- get CCCR value ---- */
	if(_sdio_read_byte(hndl,hndl->io_reg[0],0,0,SDIO_INTERNAL_REG_SIZE,
		SD_IO_INCREMENT_ADDR) != SD_OK){
		goto ERR_EXIT;
	}

	/* save io function 0 block length */
	if((hndl->io_reg[0][0x08] & 0x02) != 0){
		len  = hndl->io_reg[0][0x11];
		len <<= 8;
		len |= (hndl->io_reg[0][0x10] & 0x00ff);

		switch(len){
		case 32:
		case 64:
		case 128:
		case 256:
		case 512:
			/* len is OK */
			hndl->io_len[0] = len;				/* already and supported */
			break;
		default:
			hndl->io_len[0] = 0xffff;			/* already but not supported */
			break;
		}

	#if		(TARGET_RZ_A1 == 1)
		if(hndl->trans_mode & SD_MODE_DMA){
			if(hndl->trans_mode & SD_MODE_DMA_64){
				if( len == 32 ){
					hndl->io_len[0] = 0xffff;	/* already but not supported */
				}
			}
		}
	#endif

		for( i = 1; i < 8; i++ ){
			hndl->io_len[i] = 0;				/* not yet */
		}
	}
	else{
		for( i = 0; i < 8; i++ ){
			hndl->io_len[i] = 0xffff;			/* already but not supported */
		}
	}

	return SD_OK;

ERR_EXIT:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : mount memory card
 * Include      : 
 * Declaration  : int _sd_mem_mount(SDHNDL *hndl)
 * Functions    : mount memory part from stand-by to transfer state
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_mem_mount(SDHNDL *hndl)
{
	/* case of combo, already supplied data transfer clock */
	if((hndl->media_type & SD_MEDIA_IO) == 0){
		/* ---- supply clock (data-transfer ratio) ---- */
		if( hndl->csd_tran_speed > SD_CLK_25MHz ){
			hndl->csd_tran_speed = SD_CLK_25MHz;

			/* Herein after, if switch-function(cmd6) is pass, 		*/
			/* hndl->csd_tran_speed is set to SD_CLK_50MHz			*/
		}

		if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
			goto ERR_EXIT;
		}
	}
	
	/* ==== data-transfer mode(Transfer State) ==== */
	if(_sd_card_send_cmd_arg(hndl,CMD7,SD_RESP_R1b,hndl->rca[0],0x0000) 
		!= SD_OK){
		goto ERR_EXIT;
	}
	
	if((hndl->resp_status & 0x02000000)){
		_sd_set_err(hndl,SD_ERR_CARD_LOCK);
		return SD_OK;
	}
	
	/* ---- set block length (issue CMD16) ---- */
	if(_sd_card_send_cmd_arg(hndl,CMD16,SD_RESP_R1,0x0000,0x0200) != SD_OK){
		goto ERR_EXIT;
	}

	/* if 4bits transfer supported (SD memory card mandatory), change bus width 4bits */
	if(hndl->media_type & SD_MEDIA_SD){
		_sd_set_port(hndl,hndl->sup_if_mode);
	}
		
	/* clear pull-up DAT3 */
	if(hndl->media_type & SD_MEDIA_SD){
		if(_sd_send_acmd(hndl,ACMD42,0,0) != SD_OK){
			goto ERR_EXIT;
		}
		/* check R1 resp */
		if(_sd_get_resp(hndl,SD_RESP_R1) != SD_OK){
			goto ERR_EXIT;
		}
	}

	/* if SD memory card, get SD Status */
	if(hndl->media_type & SD_MEDIA_SD){
		if(_sd_card_get_status(hndl) != SD_OK){
			goto ERR_EXIT;
		}
		/* get protect area size */
		if(_sd_get_size(hndl,SD_PROT_AREA) != SD_OK){
			goto ERR_EXIT;
		}
	}

	return SD_OK;

ERR_EXIT:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : set io part speed
 * Include      : 
 * Declaration  : int _sd_set_io_speed(SDHNDL *hndl)
 * Functions    : query high speed supported
 *              : transfer card high speed mode
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_set_io_speed(SDHNDL *hndl)
{
	unsigned char io_buff;

	/* is CCCR/FBR version 1.20? */
	if((hndl->io_reg[0][0] & 0x0F) == 0x02){
		/* is high speed supported? */
		if(hndl->io_reg[0][0x13] & 0x01){
			hndl->speed_mode |= SD_SUP_SPEED;

			io_buff = 0x02;
			if(_sdio_direct(hndl,&io_buff,0,0x13,1,SD_IO_VERIFY_WRITE) 
				!= SD_OK){
				return SD_ERR;
			}
			hndl->io_reg[0][0x13] = io_buff;
			if(io_buff & 0x02){	/* high speed mode */
				/* force set high-speed mode */
				hndl->csd_tran_speed = SD_CLK_50MHz;
				hndl->speed_mode |= SD_CUR_SPEED;
			}
		}
		else{
			hndl->speed_mode &= (unsigned char)~SD_SUP_SPEED;
		}
	}

	return SD_OK;

ERR_EXIT:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : set memory part speed
 * Include      : 
 * Declaration  : int _sd_set_mem_speed(SDHNDL *hndl)
 * Functions    : query high speed supported
 *              : transfer card high speed mode
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_set_mem_speed(SDHNDL *hndl)
{
	/* query func */
	if(_sd_card_switch_func(hndl,0x00FF,0xFF00) != SD_OK){
		goto ERR_EXIT;
	}
	if(hndl->speed_mode & SD_SUP_SPEED){ /* high speed supported */

		/* make transfer card high speed mode */
		if(_sd_card_switch_func_access_mode1(hndl,0x80FF,0xFF01) != SD_OK){
			goto ERR_EXIT;
		}

		/* case of combo card, set clock frequency high speed after transfering  io part high speed */
		if(hndl->media_type == SD_MEDIA_SD){
			hndl->csd_tran_speed = SD_CLK_50MHz;
			hndl->speed_mode |= SD_CUR_SPEED;
		}
		hndl->csd[2] &= 0x00ff;
		hndl->csd[2] |= 0x5a00;		/* Change High-Speed mode value(50MHz)in CSD.TRAN_SPEED	*/
	}

	return SD_OK;

ERR_EXIT:
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : unmount card
 * Include      : 
 * Declaration  : int sd_unmount(int sd_port);
 * Functions    : unmount card
 *              : turn off power
 * Argument     : none
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int sd_unmount(int sd_port)
{
	SDHNDL *hndl;
	unsigned char io_buff;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initialized */
	}

	if( (hndl->media_type == SD_MEDIA_IO) || (hndl->media_type == SD_MEDIA_COMBO) ){
		/* media has SDIO */
		if(hndl->io_flag & SD_IO_POWER_INIT){	/* already supplied power */
			/* ==== transfer idle state (issue CMD52) ==== */
			/* data:08'h func:0 address:06'h verify write */
			io_buff = 0x08;
			sdio_write_direct(sd_port,&io_buff,0,0x06,SD_IO_VERIFY_WRITE);
			/* dont care error */
		}
	}

	/* ---- clear mount flag ---- */
	hndl->mount = SD_UNMOUNT_CARD;

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	/* ---- set single port ---- */
	sddev_set_port(sd_port, SD_PORT_SERIAL);
	
	/* ---- turn off power ---- */
	if(sddev_power_off(sd_port) != SD_OK){
		_sd_set_err(hndl,SD_ERR_CPU_IF);
		return hndl->error;
	}
	
	/* ---- initilaize SD handle ---- */
	_sd_init_hndl(hndl,0,hndl->voltage);
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : execute SWITCH Function operation
 * Include      : 
 * Declaration  : int _sd_card_switch_func(SDHNDL *hndl,unsigned short h_arg
 *              : 	,unsigned short l_arg)
 * Functions    : issue SWITCH FUNC command
 *              : query card is high speed supported
 *              : make transfer card high speed mode
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : SWITCH FUNC command is supported from SD spec version 1.10
 *****************************************************************************/
int _sd_card_switch_func(SDHNDL *hndl,unsigned short h_arg,	unsigned short l_arg)
{
	int i;
	unsigned char *rw_buff;

	rw_buff = (unsigned char *)&stat_buff[hndl->sd_port][0];

	if(_sd_read_byte(hndl,CMD6,h_arg,l_arg,rw_buff,STATUS_DATA_BYTE) != SD_OK){
		return SD_ERR;
	}

	/* ---- save STATUS DATA ---- */
	for(i=0; i<9 ;i++){
		hndl->status_data[i] = (stat_buff[hndl->sd_port][i] << 8) | (stat_buff[hndl->sd_port][i] >> 8);
	}
	
	if(!(h_arg & 0x8000)){	/* case of query */
		/* ---- save high speed support ---- */
		if(hndl->status_data[6] & 0x0002u){
			hndl->speed_mode |= SD_SUP_SPEED;
		}
		else{
			hndl->speed_mode &= (unsigned char)~SD_SUP_SPEED;
		}
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : execute SWITCH Function operation
 * Include      : 
 * Declaration  : int _sd_card_switch_func_access_mode0(SDHNDL *hndl,
 *              :                   unsigned short h_arg, unsigned short l_arg)
 * Functions    : issue SWITCH FUNC command
 *              : query card is high speed supported
 *              : make transfer card high speed mode
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : SWITCH FUNC command is supported from SD spec version 1.10
 *****************************************************************************/
int _sd_card_switch_func_access_mode0(SDHNDL *hndl,unsigned short h_arg,unsigned short l_arg)
{
	int i;
	int	loop;
	unsigned char *rw_buff;

	rw_buff = (unsigned char *)&stat_buff[hndl->sd_port][0];

	if( (h_arg != 0x00ff) || (l_arg != 0xff01) ){
		/* This function can be used for switching access mode 0	*/
		return SD_ERR;
	}

	for( loop = 0; loop < 3; loop++ ){
		if(_sd_read_byte(hndl,CMD6,0x00ff,0xff01,rw_buff,STATUS_DATA_BYTE) != SD_OK){
			return SD_ERR;
		}

		/* ---- save STATUS DATA ---- */
		for(i = 0; i < 9 ;i++){
			hndl->status_data[i] = (stat_buff[hndl->sd_port][i] << 8) | (stat_buff[hndl->sd_port][i] >> 8);
		}

		if( !(stat_buff[hndl->sd_port][8] & 0x00ffu) ){
			/* data structure not defined status	*/
			break;
		}

		if( !(stat_buff[hndl->sd_port][14] & 0x0002u) ){
			/* status is ready */
			break;
		}
	}

	if( loop == 3 ){
		/* retry over	*/
		return SD_ERR;
	}

	if( !(hndl->status_data[6] & 0x0002u) ){
		/* high-speed disable	*/
		return SD_ERR;
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : execute SWITCH Function operation
 * Include      : 
 * Declaration  : int _sd_card_switch_func_access_mode1(SDHNDL *hndl,
 *              :                   unsigned short h_arg, unsigned short l_arg)
 * Functions    : issue SWITCH FUNC command
 *              : query card is high speed supported
 *              : make transfer card high speed mode
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : SWITCH FUNC command is supported from SD spec version 1.10
 *****************************************************************************/
int _sd_card_switch_func_access_mode1(SDHNDL *hndl,unsigned short h_arg,unsigned short l_arg)
{
	int i;
	int	loop;
	unsigned char *rw_buff;

	rw_buff = (unsigned char *)&stat_buff[hndl->sd_port][0];

	if( (h_arg != 0x80ff) || (l_arg != 0xff01) ){
		/* This function can be used for switching access mode 1	*/
		return SD_ERR;
	}

	for( loop = 0; loop < 3; loop++ ){
		if(_sd_card_switch_func_access_mode0(hndl,0x00ff,0xff01) != SD_OK){
			/* wait for function is ready	*/
			return SD_ERR;
		}

		if(_sd_read_byte(hndl,CMD6,0x80ff,0xff01,rw_buff,STATUS_DATA_BYTE) != SD_OK){
			return SD_ERR;
		}

		/* ---- save STATUS DATA ---- */
		for(i = 0; i < 9 ;i++){
			hndl->status_data[i] = (stat_buff[hndl->sd_port][i] << 8) | (stat_buff[hndl->sd_port][i] >> 8);
		}

		if( !(stat_buff[hndl->sd_port][8] & 0x00ffu) ){
			/* data structure not defined status	*/
			break;
		}

		if( !(stat_buff[hndl->sd_port][14] & 0x0002u) ){
			/* status is ready */
			break;
		}
	}

	if( loop == 3 ){
		/* retry over	*/
		return SD_ERR;
	}

	if( !(hndl->status_data[6] & 0x0002u) ){
		/* high-speed disable	*/
		return SD_ERR;
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SD Status
 * Include      : 
 * Declaration  : int _sd_card_get_status(SDHNDL *hndl)
 * Functions    : get SD Status (issue ACMD13)
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_card_get_status(SDHNDL *hndl)
{
	int ret;
	int i;
//	unsigned int au,erase_size;
	unsigned char *rw_buff;

	rw_buff = (unsigned char *)&stat_buff[hndl->sd_port][0];

	/* ---- get SD Status (issue ACMD13) ---- */
	if(_sd_read_byte(hndl,ACMD13,0,0,rw_buff,SD_STATUS_BYTE) != SD_OK){
		return SD_ERR;
	}
	
	/* ---- distinguish SD ROM card ---- */
	if((rw_buff[2] & 0xffu) == 0x00){ /* [495:488] = 0x00 */
		ret = SD_OK;
		if((rw_buff[3] & 0xffu) == 0x01){
			hndl->write_protect |= SD_WP_ROM;
		}
	}
	else{
		ret = SD_ERR;
		_sd_set_err(hndl,SD_ERR_CARD_ERROR);
	}

	hndl->speed_class = rw_buff[8];
	hndl->perform_move = rw_buff[9];
	
	/* ---- save SD STATUS ---- */
	for(i = 0;i < 14/sizeof(unsigned short);i++){
		hndl->sdstatus[i] = (stat_buff[hndl->sd_port][i] << 8) | (stat_buff[hndl->sd_port][i] >> 8);
	}

	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : get SCR register
 * Include      : 
 * Declaration  : int _sd_card_get_scr(SDHNDL *hndl);
 * Functions    : get SCR register (issue ACMD51).
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int _sd_card_get_scr(SDHNDL *hndl)
{
//	unsigned short au,erase_size;
	unsigned char *rw_buff;

	rw_buff = (unsigned char *)&stat_buff[hndl->sd_port][0];

	/* ---- get SCR register (issue ACMD51) ---- */
	if(_sd_read_byte(hndl,ACMD51,0,0,rw_buff,SD_SCR_REGISTER_BYTE) != SD_OK){
		return SD_ERR;
	}

	/* ---- save SCR register ---- */
	hndl->scr[0] = (stat_buff[hndl->sd_port][0] << 8) | (stat_buff[hndl->sd_port][0] >> 8);
	hndl->scr[1] = (stat_buff[hndl->sd_port][1] << 8) | (stat_buff[hndl->sd_port][1] >> 8);
	hndl->scr[2] = (stat_buff[hndl->sd_port][2] << 8) | (stat_buff[hndl->sd_port][2] >> 8);
	hndl->scr[3] = (stat_buff[hndl->sd_port][3] << 8) | (stat_buff[hndl->sd_port][3] >> 8);

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : read byte data from card
 * Include      : 
 * Declaration  : int _sd_read_byte(SDHNDL *hndl,unsigned short cmd,
 *              : 	unsigned short h_arg,unsigned short l_arg,
 *              : 		unsigned char *readbuff,unsigned int byte)
 * Functions    : read byte data from card
 *              : issue byte data read command and read data from SD_BUF
 *              : using following commands
 *              : SD STATUS(ACMD13),SCR(ACMD51),NUM_WRITE_BLOCK(ACMD22),
 *              : SWITCH FUNC(CMD6)
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short acmd : command code
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 *              : unsigned char *readbuff : read data buffer
 *              : unsigned short byte : the number of read bytes
 * Return       : SD_OK : end of succeed
 * Remark       : transfer type is PIO
 *****************************************************************************/
int _sd_read_byte(SDHNDL *hndl,unsigned short cmd,unsigned short h_arg,
	unsigned short l_arg,unsigned char *readbuff,unsigned short byte)
{
	/* ---- disable SD_SECCNT ---- */
	sd_outp(hndl,SD_STOP,0x0000);

	/* ---- set transfer bytes ---- */
	sd_outp(hndl,SD_SIZE,byte);

	/* ---- issue command ---- */
	if(cmd & 0x0040u){	/* ACMD13, ACMD22 and ACMD51 */
		if(_sd_send_acmd(hndl,cmd,h_arg,l_arg) != SD_OK){
			if((hndl->error == SD_ERR_END_BIT) ||
				(hndl->error == SD_ERR_CRC)){
				/* continue */
			}
			else{
				goto ErrExit;
			}
		}
	}
	else{	/* CMD6 and CMD30 */
		_sd_set_arg(hndl,h_arg,l_arg);
		if(_sd_send_cmd(hndl,cmd) != SD_OK){
			return SD_ERR;
		}
	}
	/* ---- check R1 response ---- */
	if(_sd_get_resp(hndl,SD_RESP_R1) != SD_OK){
		goto ErrExit;
	}

	/* enable All end, BRE and errors */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BRE);

	/* ---- wait BRE interrupt ---- */
	if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_MULTIPLE) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		goto ErrExit;
	}	

	/* ---- check errors ---- */
	if(hndl->int_info2&SD_INFO2_MASK_ERR){
		_sd_check_info2_err(hndl);
		goto ErrExit;
	}

	_sd_clear_info(hndl,0x0000,SD_INFO2_MASK_RE);	/* clear BRE bit */

	/* transfer data */
	if(sddev_read_data(hndl->sd_port, readbuff,(unsigned long)(hndl->reg_base+SD_BUF0),
		(long)byte) != SD_OK){
		_sd_set_err(hndl,SD_ERR_CPU_IF);
		goto ErrExit;
	}

	/* wait All end interrupt */
	if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		goto ErrExit;
	}

	/* ---- check errors ---- */
	if(hndl->int_info2&SD_INFO2_MASK_ERR){
		_sd_check_info2_err(hndl);
		goto ErrExit;
	}

	_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_ERR);	/* clear All end bit */
	/* disable all interrupts */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BRE);

	return SD_OK;

ErrExit:
	sd_outp(hndl,SD_STOP,0x0001);	/* stop data transfer */
	_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_ERR);	/* clear All end bit */
	/* disable all interrupts */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BRE);

	return SD_ERR;
}

/*****************************************************************************
 * ID           :
 * Summary      : write byte data to card
 * Include      : 
 * Declaration  : int _sd_write_byte(SDHNDL *hndl,unsigned short cmd,
 *              : 	unsigned short h_arg,unsigned short l_arg,
 *              : 		unsigned char *writebuff,unsigned int byte)
 * Functions    : write byte data to card
 *              : issue byte data write command and write data to SD_BUF
 *              : using following commands
 *              : (CMD27 and CMD42)
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned short acmd : command code
 *              : unsigned short h_arg : command argument high [31:16]
 *              : unsigned short l_arg : command argument low [15:0]
 *              : unsigned char *writebuff : write data buffer
 *              : unsigned short byte : the number of write bytes
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : transfer type is PIO
 *****************************************************************************/
int _sd_write_byte(SDHNDL *hndl,unsigned short cmd,unsigned short h_arg,
	unsigned short l_arg,unsigned char *writebuff,unsigned short byte)
{
	int time_out;

	/* ---- disable SD_SECCNT ---- */
	sd_outp(hndl,SD_STOP,0x0000);

	/* ---- set transfer bytes ---- */
	sd_outp(hndl,SD_SIZE,byte);

	/* ---- issue command ---- */
	_sd_set_arg(hndl,h_arg,l_arg);
	if(_sd_send_cmd(hndl,cmd) != SD_OK){
		return SD_ERR;
	}
	
	/* ---- check R1 response ---- */
	if(_sd_get_resp(hndl,SD_RESP_R1) != SD_OK){
		if(hndl->error == SD_ERR_CARD_LOCK){
			hndl->error = SD_OK;
		}
		else{
			goto ErrExit;
		}
	}

	/* enable All end, BWE and errors */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);

	/* ---- wait BWE interrupt ---- */
	if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_MULTIPLE) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		goto ErrExit;
	}

	/* ---- check errors ---- */
	if(hndl->int_info2&SD_INFO2_MASK_ERR){
		_sd_check_info2_err(hndl);
		goto ErrExit;
	}

	_sd_clear_info(hndl,0x0000,SD_INFO2_MASK_WE);	/* clear BWE bit */

	/* transfer data */
	if(sddev_write_data(hndl->sd_port, writebuff,(unsigned long)(hndl->reg_base+SD_BUF0),
		(long)byte) != SD_OK){
		_sd_set_err(hndl,SD_ERR_CPU_IF);
		goto ErrExit;
	}

	/* wait All end interrupt */
	if( (cmd == CMD42) && (byte == 1) ){
		/* force erase timeout	*/
		time_out = SD_TIMEOUT_ERASE_CMD;
	}
	else{
		time_out = SD_TIMEOUT_RESP;
	}

	if(sddev_int_wait(hndl->sd_port, time_out) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		goto ErrExit;
	}

	/* ---- check errors but for timeout ---- */
	if(hndl->int_info2&SD_INFO2_MASK_ERR){
		_sd_check_info2_err(hndl);
		if( time_out == SD_TIMEOUT_ERASE_CMD ){
			/* force erase	*/
			if(hndl->error == SD_ERR_CARD_TOE){
				/* force erase timeout	*/
				_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,SD_INFO2_MASK_ERR);
				if(_sd_wait_rbusy(hndl,10000000) != SD_OK){
					goto ErrExit;
				}
			}
			else{
				goto ErrExit;
			}
		}
		else{
			goto ErrExit;
		}
	}

	_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,0x0000);	/* clear All end bit */

	/* disable all interrupts */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);

	return SD_OK;

ErrExit:
	sd_outp(hndl,SD_STOP,0x0001);	/* stop data transfer */
	_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,0x0000);	/* clear All end bit */
	/* disable all interrupts */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);

	return SD_ERR;
}


/*****************************************************************************
 * ID           :
 * Summary      : calculate erase sector
 * Include      : 
 * Declaration  : int _sd_calc_erase_sector(SDHNDL *hndl);
 * Functions    : This function calculate erase sector for SD Phy Ver2.0.
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : transfer type is PIO
 *****************************************************************************/
int _sd_calc_erase_sector(SDHNDL *hndl)
{
	unsigned short au,erase_size;
	
	if((hndl->scr[0] & 0x0f00) == 0x0200){
		/* AU is not defined,set to fixed value */
		hndl->erase_sect = SD_ERASE_SECTOR;

		/* get AU size */
		au = hndl->sdstatus[5] >> 12;

		if((au > 0) && (au < 0x0a)){
			/* get AU_SIZE(sectors) */
			hndl->erase_sect = (8*1024/512) << au;

			/* get ERASE_SIZE */ 
			erase_size = (hndl->sdstatus[5] << 8) | (hndl->sdstatus[6] >> 8);
			if(erase_size != 0){
				hndl->erase_sect *= erase_size;
			}
		}
		
	}
	else{
		/* If card is not Ver2.0,it use ERASE_BLK_LEN in CSD */
	}
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : Get QUERY_PARTITIONS informations
 * Include      : 
 * Declaration  : static int _sd_card_query_partitions(SDHNDL *hndl, int sub, 
 *              :  unsigned char *rw_buff)
 * Functions    : 
 * Argument     : SDHNDL *hndl             : SD handle
 *              : int sub                  : Sub Command Code(0xA1 is available)
 *              : unsigned char *rw_buff   : 512byte area
 * Return       : SD_OK : end of succeed
 * Remark       : rw_buff is need 512Byte area
 *****************************************************************************/
static int _sd_card_query_partitions(SDHNDL *hndl, int sub, unsigned char *rw_buff)
{
	/* ---- get QUERY PARTITIONS (issue CMD45) ---- */
	if(_sd_read_byte(hndl,CMD45,(unsigned short)sub,     0,rw_buff,SD_QUERY_PARTITION_LIST_BYTE) == SD_OK){
		_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000);
	}
	else{
		int tmp = hndl->error;
		_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000);
		hndl->error = tmp;
	}

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : SELECT PARTITIONS information
 * Include      : 
 * Declaration  : int _sd_card_select_partition(SDHNDL *hndl, int id);
 * Functions    : SELECT PARTITIONS information [issue CMD43]
 * Argument     : SDHNDL *hndl : SD handle
 *              : int id       : Partition ID
 * Return       : SD_OK               : success
 *              : SD_ERR_RES_TOE      : 
 *              : SD_ERR_OUT_OF_RANGE : 
 * Remark       : 
 *              : 
 *****************************************************************************/
int _sd_card_select_partition(SDHNDL *hndl, int id)
{
	/* ==== SELECT PARTITIONS(Physical partition #id) ==== */
	if(_sd_card_send_cmd_arg(hndl,CMD43,SD_RESP_R1b,(unsigned short)(id<<8),0x0000) == SD_OK){
		_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000);
	}else{
		int tmp = hndl->error;
		_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000);
		hndl->error = tmp;
	}

	return hndl->error;
}

/*"***************************************************************************
 * ID           :
 * Summary      : select physical partition
 * Include      : 
 * Declaration  : int esd_select_partition(int sd_port, int id);
 * Functions    : 
 * Argument     : int id       : Partition ID
 * Return       : SD_OK  : success
 *              : SD_ERR : error
 * Remark       : 
 *****************************************************************************/
int esd_select_partition(int sd_port, int id)
{
	SDHNDL	*hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initialized */
	}

	/* ---- check card is mounted ---- */
	if(!hndl->mount){
		return SD_ERR;	/* not mounted yet */
	}
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;	
	}
	
	if(_sd_card_select_partition(hndl, id) == SD_OK){
		int tmp_id;
		if(_esd_get_partition_id(hndl, &tmp_id) == SD_OK){
			hndl->partition_id = tmp_id;
			hndl->card_sector_size = hndl->partition_sector_size[hndl->partition_id];
		}
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : get partition ID
 * Include      : 
 * Declaration  : int _esd_get_partition_id(int *id);
 * Functions    : 
 * Argument     : int *id      : Partition ID
 * Return       : SD_OK  : succeed
 *              : SD_ERR : error
 * Remark       : 
 *****************************************************************************/
int _esd_get_partition_id(SDHNDL *hndl, int *id)
{
//	int tmp;
	unsigned long cnt, id_cnt;

	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;	
	}

	if(_sd_card_query_partitions(hndl, 0xa100, hndl->rw_buff) == SD_OK){
		cnt = 0;
		for( id_cnt = 0; id_cnt < 8; id_cnt++ ){
			hndl->partition_sector_size[id_cnt] = (unsigned long)(hndl->rw_buff[cnt++]);
			hndl->partition_sector_size[id_cnt] += (unsigned long)(hndl->rw_buff[cnt++]) << 8;
			hndl->partition_sector_size[id_cnt] += (unsigned long)(hndl->rw_buff[cnt++]) << 16;
			hndl->partition_sector_size[id_cnt] += (unsigned long)(hndl->rw_buff[cnt++]) << 24;
		}
		*id = hndl->rw_buff[511];
	}
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : get partition ID
 * Include      : 
 * Declaration  : int esd_get_partition_id(int sd_port, int *id);
 * Functions    : 
 * Argument     : int *id      : Partition ID
 * Return       : SD_OK  : end of succeed
 *              : SD_ERR : not initilized
 * Remark       : 
 *****************************************************************************/
int esd_get_partition_id(int sd_port, int *id)
{
	SDHNDL	*hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initialized */
	}

	/* ---- check card is mounted ---- */
	if(!hndl->mount){
		return SD_ERR;	/* not mounted yet */
	}

	if(id != 0){
		*id = hndl->partition_id;
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : get QUERY PARTITIONS information
 * Include      : 
 * Declaration  : int esd_query_partition(int sd_port, int sub, unsigned char *data);
 * Functions    : 
 * Argument     : int sub       : sub-command
 *              : unsigned char *rw_buff   : 512byte area
 * Return       : SD_OK : end of succeed
 * Remark       : 
 *****************************************************************************/
int esd_query_partition(int sd_port, int sub, unsigned char *data)
{
	SDHNDL	*hndl;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initialized */
	}

	/* ---- check card is mounted ---- */
	if(!hndl->mount){
		return SD_ERR;	/* not mounted yet */
	}
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;	
	}

	_sd_card_query_partitions(hndl, sub, data);

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/* End of File */
