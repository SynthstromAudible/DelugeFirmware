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
*  File Name   : sd_read.c
*  Contents    : Card read
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

#include "RZA1/compiler/asm/inc/asm.h"
#include "deluge/drivers/uart/uart.h"
#include "deluge/deluge.h"

#ifdef __CC_ARM
#pragma arm section code = "CODE_SDHI"
#pragma arm section rodata = "CONST_SDHI"
#pragma arm section rwdata = "DATA_SDHI"
#pragma arm section zidata = "BSS_SDHI"
#endif

static int _sd_single_read(SDHNDL *hndl,unsigned char *buff,unsigned long psn
	,int mode);


int doActualReadRohan(int sd_port, SDHNDL *hndl, unsigned char *buff, long cnt, int mode, int dma_64) {

	int ret;

	/* ---- disable RespEnd and ILA ---- */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	if(mode == SD_MODE_SW){	/* ==== PIO ==== */
		/* enable All end, BRE and errors */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BRE);
		/* software data transfer */
		ret =_sd_software_trans(hndl,buff,cnt,SD_TRANS_READ);
	}
	else{	/* ==== DMA ==== */
		/* disable card ins&rem interrupt for FIFO */
		unsigned short info1_back = (unsigned short)(hndl->int_info1_mask & SD_INFO1_MASK_DET_CD);
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_DET_CD,0);

		/* enable All end and errors */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_ERR);

		// If seems we have to invalidate RAM before as well as after the DMA transfer. Otherwise, there can be crackles in loaded sample data
		// if the same small number of RAM clusters are being reused lots. Also seen a problem with some incorrect values (so a "click")
		// coming in on a single-cycle waveform being loaded - and this was not really a reused-lots scenario - although this was using the main
		// sector-read buffer, so maybe that counts as reuse.
		// Guessing problem occurs because if not invalidated, there might be a cache
		// for this memory which hasn't been written/flushed back out yet, and that happens during the DMA transfer, overwriting the audio data in actual RAM.
		// https://support.xilinx.com/s/article/64839?language=en_US - seems to concur with this, and actually suggests that it is normal and necessary to
		// invalidate both before and after transfer.
		v7_dma_inv_range((intptr_t)buff, (intptr_t)(buff + cnt * 512));

		/* ---- initialize DMAC ---- */
		unsigned long reg_base_here = hndl->reg_base;
		if(TARGET_RZ_A1 != 1 || dma_64 != SD_MODE_DMA_64) /* SD_CMD Address for 64byte transfer */
			reg_base_here += SD_BUF0;

		int result = sddev_init_dma(sd_port, (unsigned long)buff, reg_base_here, cnt*512, SD_TRANS_READ);

		if (result != SD_OK) {
			_sd_set_err(hndl,SD_ERR_CPU_IF);
			return SD_ERR_CPU_IF;
		}

		/* DMA data transfer */
		ret =_sd_dma_trans(hndl,cnt);

		sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) & ~CC_EXT_MODE_DMASDRW));
		_sd_set_int_mask(hndl,info1_back,0);
	}

	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : read sector data from card
 * Include      : 
 * Declaration  : int sd_read_sect(int sd_port, unsigned char *buff,unsigned long psn,long cnt);
 * Functions    : read sector data from physical sector number (=psn) by the 
 *              : number of sectors (=cnt)
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 *              : 
 * Argument     : unsigned char *buff : read data buffer
 *              : unsigned long psn : read physical sector number
 *              : long cnt : number of read sectors
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_read_sect(int sd_port, unsigned char *buff,unsigned long psn,long cnt)
{
	
	SDHNDL *hndl;
	long i,j;
	int ret,mode=0;
	int mmc_lastsect=0;
	unsigned short info1_back,opt_back;
	int dma_64;


	logAudioAction("sd_read_sect");

	routineForSD(); // By Rohan. // called during disk reads but only once per read

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

	/* ---- is stop compulsory? ---- */
	if(hndl->stop){
		hndl->stop = 0;
		_sd_set_err(hndl,SD_ERR_STOP);
		return SD_ERR_STOP;
	}

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
		return SD_ERR_NO_CARD;
	}

	/* access area check */
	if(psn >= hndl->card_sector_size || psn + cnt > hndl->card_sector_size){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* out of area */
	}

	/* if DMA transfer, buffer boundary is quadlet unit */
	if((hndl->trans_mode & SD_MODE_DMA) && ((unsigned long)buff & 0x03u) == 0){
		mode = SD_MODE_DMA;	/* set DMA mode */

	#if		(TARGET_RZ_A1 == 1)
		if(hndl->trans_mode & SD_MODE_DMA_64){
			dma_64 = SD_MODE_DMA_64;
		}
		else{
			dma_64 = SD_MODE_DMA;
		}
	#endif
	}

	else uartPrintln("couldn't do DMA");

	/* transfer size is fixed (512 bytes) */
	sd_outp(hndl,SD_SIZE,512);
	
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;	
	}

	/* ==== check status precede read operation ==== */
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

	/* ==== execute multiple transfer by 256 sectors ==== */
	for(i=cnt; i > 0 ;
		i-=TRANS_SECTORS,psn+=TRANS_SECTORS,buff+=TRANS_SECTORS*512){

		/* ---- is card existed? ---- */
		if(_sd_check_media(hndl) != SD_OK){
			_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
			goto ErrExit;
		}

		/* set transfer sector numbers to SD_SECCNT */
		cnt = i - TRANS_SECTORS;
		if(cnt < 0){	/* remaining sectors are less than TRANS_SECTORS */
			cnt = i;
		}
		else{
			cnt = TRANS_SECTORS;
		}

		if(cnt <= 2){
			/* disable SD_SECCNT */
			sd_outp(hndl,SD_STOP,0x0000);
			for(j=cnt; j>0; j--,psn++,buff+=512){
				ret = _sd_single_read(hndl,buff,psn,mode);
				if(ret != SD_OK){
					opt_back = sd_inp(hndl,SD_OPTION);
					#if		(TARGET_RZ_A1 == 1)
					sd_outp(hndl,SOFT_RST,0x0006);
					sd_outp(hndl,SOFT_RST,0x0007);
					#else
					sd_outp(hndl,SOFT_RST,0);
					sd_outp(hndl,SOFT_RST,1);
					#endif
					sd_outp(hndl,SD_OPTION,opt_back);
					break;
				}
			}
			/* ---- halt clock ---- */
			_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
		
			return hndl->error;
		}

		/* enable SD_SECCNT */
		sd_outp(hndl,SD_STOP,0x0100);

		/* issue CMD12 not automatically, if MMC last sector access */
		mmc_lastsect = 0;
		if( hndl->media_type == SD_MEDIA_MMC && hndl->card_sector_size == psn + cnt){
			mmc_lastsect = 1;
		}

		sd_outp(hndl,SD_SECCNT,(unsigned short)cnt);

		/* ---- enable RespEnd and ILA ---- */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);
		if(mode == SD_MODE_DMA){	/* ==== DMA ==== */
			#if		(TARGET_RZ_A1 == 1)
			if( dma_64 == SD_MODE_DMA_64 ){
				sd_outp(hndl,EXT_SWAP,0x0100);		/* Set DMASEL for 64byte transfer */
			}
			#endif
			sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) | CC_EXT_MODE_DMASDRW));	/* enable DMA */
		}

		/* issue CMD18 (READ_MULTIPLE_BLOCK) */
		if(mmc_lastsect){	/* MMC last sector access */
			if(_sd_send_mcmd(hndl,CMD18 | 0x7c00u,SET_ACC_ADDR) != SD_OK){
				goto ErrExit_DMA;
			}
		}
		else{
			if(_sd_send_mcmd(hndl,CMD18,SET_ACC_ADDR) != SD_OK){
				goto ErrExit_DMA;
			}
		}

		ret = doActualReadRohan(sd_port, hndl, buff, cnt, mode, dma_64);

		if(ret != SD_OK){
			goto ErrExit;
		}
		/* ---- wait All end interrupt ---- */
		logAudioAction("0a");

		// TODO: what if we didn't wait for this interrupt? It can't be that necessary, since we've already read the data... how much time would we save?
		// Or is this just customary as the interrupt's already happened?
		if(sddev_int_wait(sd_port, SD_TIMEOUT_RESP) != SD_OK){
			_sd_set_err(hndl,SD_ERR_HOST_TOE);
			goto ErrExit;
		}

		/* ---- check errors ---- */
		if(hndl->int_info2&SD_INFO2_MASK_ERR){
			_sd_check_info2_err(hndl);
			goto ErrExit;
		}

		if (mode != SD_MODE_SW) {
			// Invalidate ram
			v7_dma_inv_range((uintptr_t)buff, (uintptr_t)(buff + cnt * 512));
		}

		/* clear All end bit */
		_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,0x0000);

		/* disable All end, BRE and errors */
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BRE);
		
		if(mmc_lastsect){
			if(_sd_card_send_cmd_arg(hndl,12,SD_RESP_R1b,0,0) != SD_OK){
				/* check OUT_OF_RANGE error */
				/* ignore errors during last block access */
				if(hndl->resp_status & 0xffffe008ul){
					if(psn + cnt != hndl->card_sector_size){
						goto ErrExit;	/* but for last block */
					}
					if(hndl->resp_status & 0x7fffe008ul){
						goto ErrExit;	/* not OUT_OF_RANGE error */
					}
					/* clear OUT_OF_RANGE error */
					hndl->resp_status &= 0x1f00u;
					hndl->error = SD_OK;
				}
				else{	/* SDHI error, ex)timeout error so on */
					goto ErrExit;
				}
			}
		}

		/* ==== check status after read operation ==== */
		if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) 
			!= SD_OK){
			/* check OUT_OF_RANGE error */
			/* ignore errors during last block access */
			if(hndl->resp_status & 0xffffe008ul){
				if(psn + cnt != hndl->card_sector_size){
					goto ErrExit;	/* but for last block */
				}
				if(hndl->resp_status & 0x7fffe008ul){
					goto ErrExit;	/* not OUT_OF_RANGE error */
				}
				/* clear OUT_OF_RANGE error */
				hndl->resp_status &= 0x1f00u;
				hndl->error = SD_OK;
			}
			else{	/* SDHI error, ex)timeout error so on */
				goto ErrExit;
			}
		}

		if((hndl->resp_status & RES_STATE) != STATE_TRAN){
			hndl->error = SD_ERR;
			goto ErrExit;
		}

		/* ---- is stop compulsory? ---- */
		if(hndl->stop){
			hndl->stop = 0;
			/* data transfer stop (issue CMD12) */
			sd_outp(hndl,SD_STOP,0x0001);
			i=0;	/* set zero to break loop */
			_sd_set_err(hndl,SD_ERR_STOP);
		}
	}

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;

ErrExit_DMA:
ErrExit:
	if(mode == SD_MODE_DMA){
		sddev_disable_dma(sd_port);	/* disable DMA */
	}
	sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) 
			& ~CC_EXT_MODE_DMASDRW));	/* disable DMA */

	mode = hndl->error;

	/* ---- clear error bits ---- */
	_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	/* ---- disable all interrupts ---- */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);

	if((sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_CBSY) == SD_INFO2_MASK_CBSY){
		unsigned short sd_option,sd_clk_ctrl;

		/* ---- enable All end ---- */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,0);
		/* ---- data transfer stop (issue CMD12) ---- */
		sd_outp(hndl,SD_STOP,0x0001);
		/* ---- wait All end ---- */
		logAudioAction("0b");

		sddev_int_wait(sd_port, SD_TIMEOUT_RESP);
		_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,0);

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
		sd_outp(hndl,SD_STOP,0x0000);
		sd_outp(hndl,SD_OPTION,sd_option);
		sd_outp(hndl,SD_CLK_CTRL,sd_clk_ctrl);
		sddev_unl_cpu(sd_port);

	}

	sd_outp(hndl,SD_STOP,0x0001);
	sd_outp(hndl,SD_STOP,0x0000);

	/* Check Current State */
	if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) == SD_OK){
		/* not transfer state? */
		if((hndl->resp_status & RES_STATE) != STATE_TRAN){	
			/* if not tran state, issue CMD12 to transit the SD card to tran state */
			_sd_card_send_cmd_arg(hndl,CMD12,SD_RESP_R1b,hndl->rca[0],0x0000);
			/* not check error because already checked */
		}
	}

	hndl->error = mode;
	
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : read sector data from card by single block transfer
 * Include      : 
 * Declaration  : int _sd_single_read(SDHNDL *hndl,unsigned char *buff,
 *				: unsigned long psn,int mode)
 * Functions    : read sector data from physical sector number (=psn) by the 
 *              : single block transfer
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : read data buffer
 *              : unsigned long psn : read physical sector number
 *              : int mode : data transfer mode
 *              : 	SD_MODE_SW : software
 *              :	SD_MODE_DMA : DMA
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
static int _sd_single_read(SDHNDL *hndl,unsigned char *buff,unsigned long psn,
	int mode)
{
	int ret,error;
	unsigned short info1_back;
	int dma_64;

	logAudioAction("_sd_single_read");

	// Kinda need this because, reading just one sector, we're not gonna be sitting waiting for interrupts for very long, so might not do any audio routine calls later
	//routineForSD();

	/* ---- enable RespEnd and ILA ---- */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	if(mode == SD_MODE_DMA){	/* ==== DMA ==== */
		#if		(TARGET_RZ_A1 == 1)
		if(hndl->trans_mode & SD_MODE_DMA_64){
			dma_64 = SD_MODE_DMA_64;
			sd_outp(hndl,EXT_SWAP,0x0100);	/* Set DMASEL for 64byte transfer */
		}
		else{
			dma_64 = SD_MODE_DMA;
		}
		#endif
		sd_outp(hndl,CC_EXT_MODE,2);	/* enable DMA */
	}

	/* issue CMD17 (READ_SINGLE_BLOCK) */
	if(_sd_send_mcmd(hndl,CMD17,SET_ACC_ADDR) != SD_OK){
		goto ErrExit_DMA;
	}


	ret = doActualReadRohan(hndl->sd_port, hndl, buff, 1, mode, dma_64);

	
	if(ret != SD_OK){
		goto ErrExit;
	}
	/* ---- wait All end interrupt ---- */
	logAudioAction("0c");
	if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		goto ErrExit;
	}

	/* ---- check errors ---- */
	if(hndl->int_info2&SD_INFO2_MASK_ERR){
		_sd_check_info2_err(hndl);
		goto ErrExit;
	}

	if (mode != SD_MODE_SW) {
		// Invalidate ram
		v7_dma_inv_range((uintptr_t)buff, (uintptr_t)(buff + 512));
	}

	/* clear All end bit */
	_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,0x0000);

	/* disable All end, BRE and errors */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BRE);
	
	
	/* ==== check status after read operation ==== */
	if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) != SD_OK){
		/* check OUT_OF_RANGE error */
		/* ignore errors during last block access */
		if(hndl->resp_status & 0xffffe008ul){
			if(psn + 1 != hndl->card_sector_size){
				goto ErrExit;	/* but for last block */
			}
			if(hndl->resp_status & 0x7fffe008ul){
				goto ErrExit;	/* not OUT_OF_RANGE error */
			}
			/* clear OUT_OF_RANGE error */
			hndl->resp_status &= 0x1f00u;
			hndl->error = SD_OK;
		}
		else{	/* SDHI error, ex)timeout error so on */
			goto ErrExit;
		}
	}

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	return hndl->error;
	
ErrExit_DMA:
ErrExit:
	if(mode == SD_MODE_DMA){
		sddev_disable_dma(hndl->sd_port);	/* disable DMA */
	}

	error = hndl->error;
	_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,0);
	_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000);
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	hndl->error = error;
	
	return hndl->error;
}

/* End of File */
