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
*  File Name   : sdio_trns.c
*  Contents    : Data transfer
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
 * Summary      : transfer data by software
 * Include      : 
 * Declaration  : int _sdio_software_trans(SDHNDL *hndl,unsigned char *buff,
 *              : 	long cnt,int dir)
 * Functions    : transfer data to/from card by software
 *              : this operations are used multiple command data phase
 *              : if dir is SD_TRANS_READ, data is from card to host
 *              : if dir is SD_TRANS_WRITE, data is from host to card
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : destination/source data buffer
 *              : long cnt : number of transfer bytes
 *              : int dir : transfer direction
 * Return       : hndl->error : SD handle error value
 *              : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : transfer finished, check CMD12 sequence refer to All end
 *****************************************************************************/
int _sdio_software_trans(SDHNDL *hndl,unsigned char *buff,long cnt,int dir,unsigned short blklen)
{
	long j;
	int (*func)(int, unsigned char *, unsigned long, long);
	
	if(dir == SD_TRANS_READ){
		func = sddev_read_data;
	}
	else{
		func = sddev_write_data;
	}
	
	for(j=cnt; j>0 ;j--){
		/* ---- wait BWE/BRE interrupt ---- */
		if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_MULTIPLE) != SD_OK){
			_sd_set_err(hndl,SD_ERR_HOST_TOE);
			break;
		}	

		if(hndl->int_info2&SD_INFO2_MASK_ERR){
			_sd_check_info2_err(hndl);
			break;
		}
		if(dir == SD_TRANS_READ){
			_sd_clear_info(hndl,0x0000,SD_INFO2_MASK_RE);	/* clear BRE and errors bit */
		}
		else{
			_sd_clear_info(hndl,0x0000,SD_INFO2_MASK_WE);	/* clear BWE and errors bit */
		}
	
		/* write/read to/from SD_BUF by 1 sector */
		if((*func)(hndl->sd_port, buff,(unsigned long)hndl->reg_base+SD_BUF0,(long)blklen) != SD_OK){
			_sd_set_err(hndl,SD_ERR_CPU_IF);
			break;
		}
		
		/* update buffer */
		buff+=blklen;
	
	}

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer data by software
 * Include      : 
 * Declaration  : int _sdio_software_trans2(SDHNDL *hndl,unsigned char *buff,
 *              : 	long cnt,int dir)
 * Functions    : transfer data to/from card by software
 *              : this operations are used multiple command data phase
 *              : if dir is SD_TRANS_READ, data is from card to host
 *              : if dir is SD_TRANS_WRITE, data is from host to card
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned char *buff : destination/source data buffer
 *              : long cnt : number of transfer bytes
 *              : int dir : transfer direction
 * Return       : hndl->error : SD handle error value
 *              : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : transfer finished, check CMD12 sequence refer to All end
 *****************************************************************************/
int _sdio_software_trans2(SDHNDL *hndl,unsigned char *buff,long cnt,int dir)
{
//	long j;
	int (*func)(int, unsigned char *, unsigned long, long);
	
	if(dir == SD_TRANS_READ){
		func = sddev_read_data;
	}
	else{
		func = sddev_write_data;
	}
	
	/* ---- wait BWE/BRE interrupt ---- */
	if(sddev_int_wait(hndl->sd_port, SD_TIMEOUT_MULTIPLE) != SD_OK){
		_sd_set_err(hndl,SD_ERR_HOST_TOE);
		return hndl->error;
	}

	if(hndl->int_info2&SD_INFO2_MASK_ERR){
		_sd_check_info2_err(hndl);
		return hndl->error;
	}
	if(dir == SD_TRANS_READ){
		_sd_clear_info(hndl,0x0000,SD_INFO2_MASK_RE);	/* clear BRE and errors bit */
	}
	else{
		_sd_clear_info(hndl,0x0000,SD_INFO2_MASK_WE);	/* clear BWE and errors bit */
	}
	
	/* write/read to/from SD_BUF */
	if((*func)(hndl->sd_port, buff,(unsigned long)hndl->reg_base+SD_BUF0,cnt) != SD_OK){
		_sd_set_err(hndl,SD_ERR_CPU_IF);
		return hndl->error;
	}

	return hndl->error;
}

/*****************************************************************************
 * ID           :
 * Summary      : transfer data by DMA
 * Include      : 
 * Declaration  : int _sdio_dma_trans(SDHNDL *hndl,long cnt)
 * Functions    : transfer data to/from card by DMA
 *              : this operations are multiple command data phase
 *              :
 * Argument     : SDHNDL *hndl : SD handle
 *              : long cnt : number of transfer bytes
 * Return       : hndl->error : SD handle error value
 *              : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : transfer finished, check CMD12 sequence refer to All end
 *****************************************************************************/
int _sdio_dma_trans(SDHNDL *hndl,long cnt,unsigned short blocklen)
{
	/* ---- check DMA transfer end  --- */
	/* timeout value is depend on transfer size */
	if(sddev_wait_dma_end(hndl->sd_port, (cnt*blocklen)) != SD_OK){
		sddev_disable_dma(hndl->sd_port);	/* disable DMAC */
		_sd_set_err(hndl,SD_ERR_CPU_IF);
		goto ErrExit;
	}
	
	/* ---- disable DMAC ---- */
	if(sddev_disable_dma(hndl->sd_port) != SD_OK){
		_sd_set_err(hndl,SD_ERR_CPU_IF);
		goto ErrExit;
	}
	
ErrExit:
	return hndl->error;
}

/* End of File */
