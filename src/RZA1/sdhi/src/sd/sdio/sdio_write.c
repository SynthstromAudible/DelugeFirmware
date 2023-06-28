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
*  File Name   : sdio_write.c
*  Contents    : io write
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
 * Summary      : write io register space
 * Include      : 
 * Declaration  : int sdio_write(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr,long cnt,unsigned long op_code);
 * Functions    : write io register from (or at) the address (=adr) by the 
 *              : number of bytes or blocks (=cnt)
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 * Argument     : unsigned char *buff : write data buffer
 *				: unsigned long func : access function number
 *              : unsigned long adr : read register address
 *              : long cnt : number of write registers (byte)
 *              : unsigned long op_code : operation code
 *              : 	SD_IO_FIXED_ADDR : R/W fixed address
 *              :	SD_IO_INCREMENT_ADDR : R/W increment address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int sdio_write(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr,
	long cnt,unsigned long op_code)
{
	SDHNDL *hndl;
	unsigned short blocklen;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	/* check read register address and function number */
	if((func > 7) || (adr > 0x1ffff)){
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

	/* ---- is io_abort compulsory? ---- */
	if(hndl->io_abort[func]){
		hndl->io_abort[func] = 0;
		_sd_set_err(hndl,SD_ERR_STOP);
		return hndl->error;	/* write protect error */
	}

	/* ---- is card existed? ---- */
	if(_sd_check_media(hndl) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);
		return hndl->error;
	}

	/* ==== write io register space ==== */
	if((op_code & SD_IO_FORCE_BYTE ) == 0){
		if( hndl->io_len[func] == 0 ){				/* not yet */
			if(sdio_get_blocklen(sd_port, &blocklen, func) != SD_OK){
				return hndl->error;	
			}
		}
		blocklen = hndl->io_len[func];				/* judge by io_len */
		if( (blocklen == 0) || (blocklen == 0xffff) ){
			_sd_set_err(hndl,SD_ERR_ILL_FUNC);
			return hndl->error;
		}

		/* ---- supply clock (data-transfer ratio) ---- */
		if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
			return hndl->error;
		}

		_sdio_write(hndl, buff, func, adr, cnt, op_code, blocklen);
	}
	else{
		/* ---- supply clock (data-transfer ratio) ---- */
		if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
			return hndl->error;
		}

		_sdio_write_byte(hndl, buff, func, adr, cnt, (op_code & SD_IO_INCREMENT_ADDR));
	}

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : write io register space
 * Include      : 
 * Declaration  : int _sdio_write(SDHNDL *hndl,unsigned char *buff,
 *              :   unsigned long func,unsigned long adr,long cnt,
 *              :     unsigned long op_code)
 * Functions    : write io register from specified address (=adr) by the 
 *              : number of bytes or blocks (=cnt)
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : write data buffer
 *				: unsigned long func : access function number
 *              : unsigned long adr : read register address
 *              : llong cnt : number of write registers (byte)
 *              : unsigned long op_code : operation code
 *              : 	SD_IO_FIXED_ADDR : R/W fixed address
 *              :	SD_IO_INCREMENT_ADDR : R/W increment address
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sdio_write(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,long cnt,unsigned long op_code,unsigned short blocklen)
{
	long i,sec,rem,ret,trans_ret;
	int mode = 0;
	unsigned long arg = 0;
	int dma_64;
	int tmp_error;
	unsigned short reg;
	unsigned short info1_back;
	unsigned short sd_option,sd_clk_ctrl;
	
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

	if(cnt <= 0){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;
	}

	if( blocklen == 0 ){
		_sd_set_err(hndl,SD_ERR_ILL_FUNC);
		return hndl->error;
	}

	/* clear io_abort */
	reg  = sd_inp(hndl,SDIO_MODE);
	reg &= ~SDIO_MODE_IOABT;
	sd_outp(hndl,SDIO_MODE,reg);

	sec = (cnt / blocklen);		/* set sector count */
	rem = (cnt % blocklen);		/* set remaining bytes */

	if(sec){	/* more than io block size (=blocklen bytes) */
		/* ==== multiple transfer by io block length ==== */
		/* ---- applied to CMD53 (IO_WRITE_EXTENDED_BLOCK) ---- */

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

		/* loop during remaining bytes are more than io block length */
		for(i=sec;i > 0;i-=TRANS_BLOCKS){
		
			/* ---- is card existed? ---- */
			if(_sd_check_media(hndl) != SD_OK){
				_sd_set_err(hndl,SD_ERR_NO_CARD);
				goto ErrExit;
			}

			/* set transfer sector numbers to SD_SECCNT */
			sec = i - TRANS_BLOCKS;
			if(sec < 0){	/* remaining sectors are less than TRANS_BLOCKS */
				sec = i;
			}
			else{
				sec = TRANS_BLOCKS;
			}

			/* transfer size is blocklen bytes */
			sd_outp(hndl, SD_SIZE, blocklen);
			
			/* enable SD_SECCNT */
			sd_outp(hndl,SD_STOP,0x0100);
		
			/* set number of transfer sectors */
			sd_outp(hndl,SD_SECCNT,(unsigned short)sec);
		
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

			/* set argument */
			arg = ((func << 28) | 0x88000000u | (op_code << 26) | (adr << 9) 
				| (unsigned long)sec);

			/* ---- enable RespEnd and ILA ---- */
			_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);

			/* issue CMD53 (IO_WRITE_EXTENDED_BLOCK) */
			_sd_send_iocmd(hndl,CMD53_W_BLOCK,arg);
			
			/* ---- enable RespEnd and ILA ---- */
			_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

			if(mode == 0){
				/* enable All end, BWE and errors */
				_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
				/* software data transfer */
				trans_ret = _sdio_software_trans(hndl,buff,sec,SD_TRANS_WRITE,blocklen);
			}
			else{
				/* disable card ins&rem interrupt for FIFO */
				info1_back = (unsigned short)(hndl->int_info1_mask 
					& SD_INFO1_MASK_DET_CD);
				_sd_clear_int_mask(hndl,SD_INFO1_MASK_DET_CD,0);
				/* enable All end and errors */
				_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_ERR);
				
				/* ---- initialize DMAC ---- */
				#if		(TARGET_RZ_A1 == 1)
				if( dma_64 == SD_MODE_DMA_64 ){
					if(sddev_init_dma(hndl->sd_port, (unsigned long)buff, hndl->reg_base,	/* SD_CMD Address for 64byte transfer */
						(sec * blocklen), SD_TRANS_WRITE) != SD_OK){
						_sd_set_err(hndl,SD_ERR_CPU_IF);
						goto ErrExit;
					}
				}
				else{
					if(sddev_init_dma(hndl->sd_port, (unsigned long)buff, (hndl->reg_base + SD_BUF0),
						(sec * blocklen), SD_TRANS_WRITE) != SD_OK){
						_sd_set_err(hndl,SD_ERR_CPU_IF);
						goto ErrExit;
					}
				}
				#else
				if(sddev_init_dma(hndl->sd_port, (unsigned long)buff, (hndl->reg_base + SD_BUF0),
					(sec * blocklen), SD_TRANS_WRITE) != SD_OK){
					_sd_set_err(hndl,SD_ERR_CPU_IF);
					goto ErrExit;
				}
				#endif
				
				/* DMA data transfer */
				trans_ret = _sdio_dma_trans(hndl,sec,blocklen);
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

			if(op_code == SD_IO_INCREMENT_ADDR){
				adr += (sec * blocklen);
			}
			buff += (sec * blocklen);

			/* ---- is io_abort compulsory? ---- */
			if(hndl->io_abort[func]){
				/* data transfer stop (issue CMD52) */
				reg  = sd_inp(hndl,SDIO_MODE);
				reg |= SDIO_MODE_IOABT;
				sd_outp(hndl,SDIO_MODE,reg);
				i=0;	/* set zero to break loop */
			}
		}
	}

	/* ---- is io_abort compulsory? ---- */
	if(hndl->io_abort[func]){
		hndl->io_abort[func] = 0;
		_sd_set_err(hndl,SD_ERR_STOP);

		#if		(TARGET_RZ_A1 == 1)
		sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
		#endif

		return hndl->error;
	}

	if(rem){	/* remaining bytes are less than io block size or not support multi block transfer */
		/* ==== applied to CMD53 (IO_WRITE_EXTENDED_BYTE) ==== */
		mode = 0;	/* just support PIO */
		
		/* ---- disable SD_SECCNT ---- */
		sd_outp(hndl,SD_STOP,0x0000);

		/* ---- set transfer bytes ---- */
		sd_outp(hndl,SD_SIZE,(unsigned short)rem);

		/* set argument */
		arg = ((func << 28) | 0x80000000u | (op_code << 26) | (adr << 9) 
			| (unsigned long)rem);

		/* ---- enable RespEnd and ILA ---- */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);

		/* issue CMD53 (IO_WRITE_EXTENDED_BYTE) */
		if(_sd_send_iocmd(hndl,CMD53_W_BYTE,arg) != SD_OK){
			_sd_set_err(hndl,SD_ERR);
			goto ErrExit;
		}

		/* ---- enable RespEnd and ILA ---- */
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

		/* enable All end, BWE and errors */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
		/* software data transfer */
		trans_ret = _sdio_software_trans2(hndl,buff,rem,SD_TRANS_WRITE);

		/* ---- wait All end interrupt ---- */
		ret = sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP);

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
	}

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	return hndl->error;

ErrExit:
	if(mode == SD_MODE_DMA){
		sddev_disable_dma(hndl->sd_port);	/* disable DMA */
	}

	sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) & ~CC_EXT_MODE_DMASDRW));	/* disable DMA */

	tmp_error = hndl->error;

	/* ---- clear error bits ---- */
	_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	/* ---- disable all interrupts ---- */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);

	if((sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_CBSY) == SD_INFO2_MASK_CBSY){
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
		sd_outp(hndl,SD_OPTION,sd_option);
		sd_outp(hndl,SD_CLK_CTRL,sd_clk_ctrl);
		sddev_unl_cpu(hndl->sd_port);
	}

	hndl->error = tmp_error;

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	return hndl->error;
}


/*****************************************************************************
 * ID           :
 * Summary      : write io register space
 * Include      : 
 * Declaration  : int _sdio_write_byte(SDHNDL *hndl,unsigned char *buff,
 *              :   unsigned long func,unsigned long adr,long cnt,
 *              :     unsigned long op_code)
 * Functions    : write io register from specified address (=adr) by the 
 *              : number of bytes or blocks (=cnt)
 *              : if SD Driver mode is SD_MODE_SW, data transfer by
 *              : sddev_read_data function
 *              : if SD Driver mode is SD_MODE_DMA, data transfer by DMAC
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : write data buffer
 *				: unsigned long func : access function number
 *              : unsigned long adr : read register address
 *              : llong cnt : number of write registers (byte)
 *              : unsigned long op_code : operation code
 *              : 	SD_IO_FIXED_ADDR : R/W fixed address
 *              :	SD_IO_INCREMENT_ADDR : R/W increment address
 * Remark       : 
 *****************************************************************************/
int _sdio_write_byte(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,long cnt,unsigned long op_code)
{
	long i,sec,rem,ret,trans_ret;
	int mode = 0;
	unsigned long arg = 0;
	int dma_64;
	int tmp_error;
	unsigned short reg;
	unsigned short info1_back;
	unsigned short sd_option,sd_clk_ctrl;

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

	if(cnt <= 0){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;
	}

	/* clear io_abort */
	reg  = sd_inp(hndl,SDIO_MODE);
	reg &= ~SDIO_MODE_IOABT;
	sd_outp(hndl,SDIO_MODE,reg);

	sec = cnt/512;	/* set sector count */
	rem = cnt%512;	/* set remaining bytes */

	if(sec){	/* more than io block size (=512 bytes) */
		/* ==== multiple transfer by io block length ==== */
		/* ---- applied to CMD53 (IO_WRITE_EXTENDED_BLOCK) ---- */

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

		/* loop during remaining bytes are more than io block length */
		for(i=sec;i > 0;i--){
		
			/* ---- is card existed? ---- */
			if(_sd_check_media(hndl) != SD_OK){
				_sd_set_err(hndl,SD_ERR_NO_CARD);
				goto ErrExit;
			}

			/* disable SD_SECCNT */
			sd_outp(hndl,SD_STOP,0x0000);
			
			/* transfer size is fixed (512 bytes) */
			sd_outp(hndl,SD_SIZE,512);

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

			/* set argument */
			arg = ((func << 28) | 0x80000000u | (op_code << 26) | (adr << 9) 
				| (unsigned long)0);

			/* ---- enable RespEnd and ILA ---- */
			_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);

			/* issue CMD53 (IO_WRITE_EXTENDED_BLOCK) */
			_sd_send_iocmd(hndl,CMD53_W_BYTE,arg);
			
			/* ---- enable RespEnd and ILA ---- */
			_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

			if(mode == 0){
				/* enable All end, BWE and errors */
				_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
				/* software data transfer */
				trans_ret = _sdio_software_trans(hndl,buff,1,SD_TRANS_WRITE,512);
			}
			else{
				/* disable card ins&rem interrupt for FIFO */
				info1_back = (unsigned short)(hndl->int_info1_mask 
					& SD_INFO1_MASK_DET_CD);
				_sd_clear_int_mask(hndl,SD_INFO1_MASK_DET_CD,0);
				/* enable All end and errors */
				_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_ERR);
				
				/* ---- initialize DMAC ---- */
				#if		(TARGET_RZ_A1 == 1)
				if( dma_64 == SD_MODE_DMA_64 ){
					if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,hndl->reg_base,	/* SD_CMD Address for 64byte transfer */
						512,SD_TRANS_WRITE) != SD_OK){
						_sd_set_err(hndl,SD_ERR_CPU_IF);
						goto ErrExit;
					}
				}
				else{
					if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,hndl->reg_base+SD_BUF0,
						512,SD_TRANS_WRITE) != SD_OK){
						_sd_set_err(hndl,SD_ERR_CPU_IF);
						goto ErrExit;
					}
				}
				#else
				if(sddev_init_dma(hndl->sd_port, (unsigned long)buff,hndl->reg_base+SD_BUF0,
					512,SD_TRANS_WRITE) != SD_OK){
					_sd_set_err(hndl,SD_ERR_CPU_IF);
					goto ErrExit;
				}
				#endif
				
				/* DMA data transfer */
				trans_ret = _sdio_dma_trans(hndl,1,512);
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

			if(ret != SD_OK){
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

			if(op_code == SD_IO_INCREMENT_ADDR){
				adr+=512;
			}
			buff+= 512;

			/* ---- is io_abort compulsory? ---- */
			if(hndl->io_abort[func]){
				i=0;	/* set zero to break loop */
			}
		}
	}

	/* ---- is io_abort compulsory? ---- */
	if(hndl->io_abort[func]){
		hndl->io_abort[func] = 0;
		_sd_set_err(hndl,SD_ERR_STOP);

		#if		(TARGET_RZ_A1 == 1)
		sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
		#endif

		return hndl->error;
	}

	if(rem){	/* remaining bytes are less than io block size or not support multi block transfer */
		/* ==== applied to CMD53 (IO_WRITE_EXTENDED_BYTE) ==== */
		mode = 0;	/* just support PIO */
		
		/* ---- disable SD_SECCNT ---- */
		sd_outp(hndl,SD_STOP,0x0000);

		/* ---- set transfer bytes ---- */
		sd_outp(hndl,SD_SIZE,(unsigned short)rem);

		/* set argument */
		arg = ((func << 28) | 0x80000000u | (op_code << 26) | (adr << 9) 
			| (unsigned long)rem);

		/* ---- enable RespEnd and ILA ---- */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);

		/* issue CMD53 (IO_WRITE_EXTENDED_BYTE) */
		if(_sd_send_iocmd(hndl,CMD53_W_BYTE,arg) != SD_OK){
			_sd_set_err(hndl,SD_ERR);
			goto ErrExit;
		}

		/* ---- enable RespEnd and ILA ---- */
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_RESP,SD_INFO2_MASK_ILA);

		/* enable All end, BWE and errors */
		_sd_set_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
		/* software data transfer */
		trans_ret = _sdio_software_trans2(hndl,buff,rem,SD_TRANS_WRITE);

		/* ---- wait All end interrupt ---- */
		ret = sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP);

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
		_sd_clear_info(hndl,SD_INFO1_MASK_DATA_TRNS,0x0000);	/* clear All end bit */

		/* disable all interrupts */
		_sd_clear_int_mask(hndl,SD_INFO1_MASK_DATA_TRNS,SD_INFO2_MASK_BWE);
	}

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	return hndl->error;

ErrExit:
	if(mode == SD_MODE_DMA){
		sddev_disable_dma(hndl->sd_port);	/* disable DMA */
	}

	sd_outp(hndl,CC_EXT_MODE,(unsigned short)(sd_inp(hndl,CC_EXT_MODE) & ~CC_EXT_MODE_DMASDRW));	/* disable DMA */

	tmp_error = hndl->error;

	/* ---- clear error bits ---- */
	_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);
	/* ---- disable all interrupts ---- */
	_sd_clear_int_mask(hndl,SD_INFO1_MASK_TRNS_RESP,0x837f);

	if((sd_inp(hndl,SD_INFO2) & SD_INFO2_MASK_CBSY) == SD_INFO2_MASK_CBSY){
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
		sd_outp(hndl,SD_OPTION,sd_option);
		sd_outp(hndl,SD_CLK_CTRL,sd_clk_ctrl);
		sddev_unl_cpu(hndl->sd_port);
	}

	hndl->error = tmp_error;

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);		/* Clear DMASEL for 64byte transfer */
	#endif

	return hndl->error;
}

/* End of File */
