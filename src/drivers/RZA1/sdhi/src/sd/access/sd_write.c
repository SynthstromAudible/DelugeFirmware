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
*  File Name   : sd_write.c
*  Contents    : Card write
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

#include "asm.h"
#include "uart_all_cpus.h"

#ifdef __CC_ARM
#pragma arm section code = "CODE_SDHI"
#pragma arm section rodata = "CONST_SDHI"
#pragma arm section rwdata = "DATA_SDHI"
#pragma arm section zidata = "BSS_SDHI"
#endif

static int _sd_single_write(SDHNDL *hndl,unsigned char *buff,unsigned long psn,int mode);

/*****************************************************************************
 * ID           :
 * Summary      : write sector data to card
 * Include      : 
 * Declaration  : int sd_write_sect(int sd_port, unsigned char *buff,unsigned long psn,long cnt,int writemode);
 * Functions    : write sector data from physical sector number (=psn) by the 
 *              : number of sectors (=cnt)
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 * Argument     : unsigned char *buff : write data buffer
 *              : unsigned long psn : write physical sector number
 *              : long cnt : number of write sectors
 *              : int writemode : memory card write mode
 *              : 	SD_WRITE_WITH_PREERASE : pre-erease write
 *              :	SD_WRITE_OVERWRITE : overwrite
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sd_write_sect(int sd_port, unsigned char *buff,unsigned long psn,long cnt,int writemode)
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

	/* ---- check write protect ---- */
	if(hndl->write_protect){
		_sd_set_err(hndl,SD_ERR_WP);
		return hndl->error;	/* write protect error */
	}
	
	/* ---- is stop compulsory? ---- */
	if(hndl->stop){
		hndl->stop = 0;
		_sd_set_err(hndl,SD_ERR_STOP);
		return hndl->error;
	}
	
	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);
		return hndl->error;
	}
	
	/* ==== write sector data to card ==== */
	_sd_write_sect(hndl,buff,psn,cnt,writemode);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : write sector data to card
 * Include      : 
 * Declaration  : int _sd_write_sect(SDHNDL *hndl,unsigned char *buff,
 *              : 	unsigned long psn,long cnt,int writemode)
 * Functions    : write sector data from physical sector number (=psn) by the 
 *              : number of sectors (=cnt)
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : write data buffer
 *              : unsigned long psn : write physical sector number
 *              : long cnt : number of write sectors
 *              : int writemode : memory card write mode
 *              : 	SD_WRITE_WITH_PREERASE : pre-erease write
 *              :	SD_WRITE_OVERWRITE : overwrite
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_write_sect(SDHNDL *hndl,unsigned char *buff,unsigned long psn,
	long cnt,int writemode)
{
	long i,j;
	int ret,trans_ret,mode=0;
	unsigned char wb[4];
	unsigned long writeblock;
	unsigned short info1_back,opt_back;
	volatile int error;
	int dma_64;
	
	/* access area check */
	if(psn >= hndl->card_sector_size || psn + cnt > hndl->card_sector_size){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* out of area */
	}
	
	/* if DMA transfer, buffer boundary is quadlet unit */
	if((hndl->trans_mode & SD_MODE_DMA) && ((unsigned long)buff & 0x03u) == 0){
		mode = SD_MODE_DMA;	/* set DMA mode */

		// Flush ram
		v7_dma_flush_range(buff, buff + cnt * 512);

		#if		(TARGET_RZ_A1 == 1)
		if(hndl->trans_mode & SD_MODE_DMA_64){
			dma_64 = SD_MODE_DMA_64;
		}
		else{
			dma_64 = SD_MODE_DMA;
		}
	#endif
	}
	
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;
	}

	/* ==== check status precede write operation ==== */
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
			_sd_set_err(hndl,SD_ERR_NO_CARD);
			goto ErrExit;
		}
		
		cnt = i - TRANS_SECTORS;
		if(cnt < 0){	/* remaining sectors is less than TRANS_SECTORS */
			cnt = i;
		}
		else {
			cnt = TRANS_SECTORS;
		}
		
		/* if card is SD Memory card and pre-erease write, issue ACMD23 */
		if((hndl->media_type & SD_MEDIA_SD) && 
			(writemode == SD_WRITE_WITH_PREERASE)){
			if(_sd_send_acmd(hndl,ACMD23,0,(unsigned short)cnt) != SD_OK){
				goto ErrExit;
			}
			if(_sd_get_resp(hndl,SD_RESP_R1) != SD_OK){
				goto ErrExit;
			}
		}

		/* transfer size is fixed (512 bytes) */
		sd_outp(hndl,SD_SIZE,512);

		/* 1 or 2 blocks, apply single read */
		if(cnt <= 2){
			/* disable SD_SECCNT */
			sd_outp(hndl,SD_STOP,0x0000);
			for(j=cnt; j>0; j--,psn++,buff+=512){
				trans_ret = _sd_single_write(hndl,buff,psn,mode);
				if(trans_ret != SD_OK){
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
		
		/* set number of transfer sectors */
		sd_outp(hndl,SD_SECCNT,(unsigned short)cnt);
		
		if(mode == SD_MODE_DMA){	/* ==== DMA ==== */
			#if		(TARGET_RZ_A1 == 1)
			if( dma_64 == SD_MODE_DMA_64 ){
				sd_outp(hndl,EXT_SWAP,0x0100);		/* Set DMASEL for 64byte transfer */
			}
			#endif
			/* enable DMA */
			sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) 
				| CC_EXT_MODE_DMASDRW));	
		}

		/* ---- enable RespEnd and ILA ---- */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);
		/* issue CMD25 (WRITE_MULTIPLE_BLOCK) */
		_sd_send_mcmd(hndl,CMD25,SET_ACC_ADDR);
		/* ---- disable RespEnd and ILA ---- */
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

		if(mode == SD_MODE_SW){	/* ==== PIO ==== */
			/* enable All end, BWE and errors */
			_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
			/* software data transfer */
			trans_ret = _sd_software_trans(hndl,buff,cnt,SD_TRANS_WRITE);
		}
		else{	/* ==== DMA ==== */
			/* disable card ins&rem interrupt for FIFO */
			info1_back = (unsigned short)(hndl->int_info1_mask 
				& SD_INFO1_MASK_DET_CD);
			_sd_clear_int_mask(hndl,SD_INFO1_MASK_DET_CD,0);
			/* enable All end and errors */
			_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_ERR);
			

			/* ---- initialize DMAC ---- */
			#if		(TARGET_RZ_A1 == 1)
			unsigned long reg_base_now = hndl->reg_base;
			if( dma_64 != SD_MODE_DMA_64 ) reg_base_now += SD_BUF0; /* SD_CMD Address for 64byte transfer */

			if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,reg_base_now,
				cnt*512,SD_TRANS_WRITE) != SD_OK){
				uartPrintln("cpu error 1");
				_sd_set_err(hndl,SD_ERR_CPU_IF);
				goto ErrExit;
			}

			#else
			if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,hndl->reg_base+SD_BUF0,
				cnt*512,SD_TRANS_WRITE) != SD_OK){
				_sd_set_err(hndl,SD_ERR_CPU_IF);
				goto ErrExit;
			}
			#endif
			
			/* DMA data transfer */
			trans_ret = _sd_dma_trans(hndl,cnt);
		}
		
		/* ---- wait All end interrupt ---- */
		ret = sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP);
		
		if(mode == SD_MODE_DMA){
			sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) 
				& ~CC_EXT_MODE_DMASDRW));	/* disable DMA */
			_sd_set_int_mask(hndl,info1_back,0);
		}

		/* ---- check result of transfer ---- */
		if(trans_ret != SD_OK){
			goto ErrExit;
		}
		
		/* ---- check result of wait All end interrupt ---- */
		if(ret != SD_OK){
			_sd_set_err(hndl,SD_ERR_HOST_TOE);
			goto ErrExit;
		}
		/* ---- check errors ---- */
		if(hndl->int_info2&SD_INFO2_MASK_ERR){
			_sd_check_info2_err(hndl);
			goto ErrExit;
		}

		/* clear All end bit */
		_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,0x0000);
		
		/* disable All end, BWE and errors */
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
		
		if(hndl->media_type & SD_MEDIA_SD){
			if(_sd_get_resp(hndl,SD_RESP_R1) != SD_OK){
				/* check number of write complete block */
				if(_sd_read_byte(hndl,ACMD22,0,0,wb,4) != SD_OK){
					goto ErrExit;
				}
				/* type cast (unsigned long) remove compiler dependence */
				writeblock = ((unsigned long)wb[0] << 24) | 
						 ((unsigned long)wb[1] << 16) | 
						 ((unsigned long)wb[2] << 8)  |
						  (unsigned long)wb[3];

				if(cnt != writeblock){	/* no write complete block */
					_sd_set_err(hndl,SD_ERR);
					goto ErrExit;
				}
			}
		}
		
		/* ==== check status after write operation ==== */
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

/*
	CMD12 may not be issued when an error occurs.
    When the SDHI does not execute the command sequence and the SD card is not in the transfer state,
	CMD12 must be issued.
	
	Conditions for error processing
	1.The SD card is not in the transfer state when CMD13 is issued.
	2.CMD13 issuing process is failed.
	
	3.DMA transfer processing is failed.
	  The sequence is suspended by the implementation failure of the user-defined function or INFO2 error.
	4.Interrupt for access end or response end is not generated.
	  Implementation failure of the user-defined function caused timeout during the command sequence execution.
	5.Error bit of INFO2 is set.
	6.ACMD22 is in error.
	7.The sector count for writing obtained by ACMD22 and the transmitted sector count are different.
	8.Error bit of CMD13 response is set.
	9.CMD13 issuing process is failed.
	10�DThe SD card is not in the transfer state when CMD13 is issued.
*/
ErrExit:
	error = hndl->error;
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
		sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP);
		_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,0);
		
		sddev_loc_cpu(hndl->sd_port);
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
		sddev_unl_cpu(hndl->sd_port);
	
	}
	
	/* Check Current State */
	if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) == SD_OK){
		/* not transfer state? */
		if((hndl->resp_status & RES_STATE) != STATE_TRAN){	
			/* if not tran state, issue CMD12 to transit the SD card to tran state */
			_sd_card_send_cmd_arg(hndl,CMD12,SD_RESP_R1b,hndl->rca[0],0x0000);
			/* not check error because already checked */
		}
	}

	hndl->error = error;

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
 * Summary      : write sector data to card by single block transfer
 * Include      : 
 * Declaration  : int _sd_single_write(SDHNDL *hndl,unsigned char *buff,
 *				: unsigned long psn,int mode)
 * Functions    : read sector data from physical sector number (=psn) by the 
 *              : single block transfer
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : write data buffer
 *              : unsigned long psn : write physical sector number
 *              : int mode : data transfer mode
 *              : 	SD_MODE_SW : software
 *              :	SD_MODE_DMA : DMA
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
static int _sd_single_write(SDHNDL *hndl,unsigned char *buff,unsigned long psn,
	int mode)
{
	int ret,trans_ret;
	unsigned short info1_back;
	int dma_64;
	
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
		/* enable DMA */
		sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) 
			| CC_EXT_MODE_DMASDRW));	
	}

	/* ---- enable RespEnd and ILA ---- */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	/* issue CMD24 (WRITE_SIGLE_BLOCK) */
	if(_sd_send_mcmd(hndl,CMD24,SET_ACC_ADDR) != SD_OK){
		goto ErrExit;
	}
	/* ---- disable RespEnd and ILA ---- */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

	if(mode == SD_MODE_SW){	/* ==== PIO ==== */
		/* enable All end, BWE and errors */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
		/* software data transfer */
		trans_ret = _sd_software_trans(hndl,buff,1,SD_TRANS_WRITE);
	}
	else{	/* ==== DMA ==== */
		/* disable card ins&rem interrupt for FIFO */
		info1_back = (unsigned short)(hndl->int_info1_mask 
			& SD_INFO1_MASK_DET_CD);
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_DET_CD,0);
	
		/* enable All end and errors */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_ERR);
		
		/* ---- initialize DMAC ---- */
		#if		(TARGET_RZ_A1 == 1)
		if( dma_64 == SD_MODE_DMA_64 ){
											/* SD_CMD Address for 64byte transfer */
			if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,hndl->reg_base,512,
				SD_TRANS_WRITE) != SD_OK){
				_sd_set_err(hndl,SD_ERR_CPU_IF);
				goto ErrExit;
			}
		}
		else{
			if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,hndl->reg_base+SD_BUF0,512,
				SD_TRANS_WRITE) != SD_OK){
				_sd_set_err(hndl,SD_ERR_CPU_IF);
				goto ErrExit;
			}
		}
		#else
		if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,hndl->reg_base+SD_BUF0,512,
			SD_TRANS_WRITE) != SD_OK){
			_sd_set_err(hndl,SD_ERR_CPU_IF);
			goto ErrExit;
		}
		#endif
		
		/* DMA data transfer */
		trans_ret = _sd_dma_trans(hndl,1);
	}
	
	/* ---- wait All end interrupt ---- */
	ret = sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP);
	
	if(mode == SD_MODE_DMA){
		sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) 
			& ~CC_EXT_MODE_DMASDRW));	/* disable DMA */
		_sd_set_int_mask(hndl,info1_back,0);
	}

	/* ---- check result of transfer ---- */
	if(trans_ret != SD_OK){
		goto ErrExit;
	}
	
	/* ---- check result of wait All end interrupt ---- */
	if(ret != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		goto ErrExit;
	}
	/* ---- check errors ---- */
	if(hndl->int_info2&SD_INFO2_MASK_ERR){
		_sd_check_info2_err(hndl);
		goto ErrExit;
	}

	/* clear All end bit */
	_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,0x0000);
	
	/* disable All end, BWE and errors */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
	
	/* ==== skip write complete block number check ==== */
	
	/* ==== check status after write operation ==== */
	if(_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000) 
		!= SD_OK){
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

ErrExit:
	mode = hndl->error;

	/* ---- clear error bits ---- */
	_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	/* ---- disable all interrupts ---- */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	/* ---- enable All end ---- */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,0);
	/* ---- data transfer stop (issue CMD12) ---- */
	sd_outp(hndl,SD_STOP,0x0001);
	/* ---- wait All end ---- */
	sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP);

	_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000);
	hndl->error = mode;

	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);
	
	return hndl->error;
}

/* End of File */
