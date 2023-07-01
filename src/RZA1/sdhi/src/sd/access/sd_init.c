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
*  File Name   : sd_init.c
*  Contents    : SD Driver initialize
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

SDHNDL *SDHandle[NUM_PORT];

/*****************************************************************************
 * ID           :
 * Summary      :  initialize SD Driver (more than 2ports)
 * Include      : 
 * Declaration  : int sd_init(int sd_port, unsigned long base, void *workarea, int cd_port);
 * Functions    : initialize SD Driver work memory started from SDHI register
 *              : base
 *              : address specified by argument (base)
 *              : initialize port specified by argument (cd_port)
 *              : work memory is allocated quadlet boundary
 * Argument     : unsigned long base : SDHI register base address
 *              : void *workarea : SD Driver work memory
 *              : int cd_port : card detect port
 *              : 	SD_CD_SOCKET : card detect by CD pin
 *              :	SD_CD_DAT3 : card detect by DAT3 pin
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 *              : SD_ERR_CPU_IF : CPU-IF function error
 * Remark       : 
 *****************************************************************************/
int sd_init(int sd_port, unsigned long base, void *workarea, int cd_port)
{
//	int i,j;
	int i;
	unsigned short info1;
	unsigned char *ptr;
	SDHNDL *hndl;
	int ret;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	/* ==== initialize work memory  ==== */
	if((unsigned long)workarea == 0){
		ret = SD_ERR;
		goto ErrExit;
	}

	/* ==== work memory boundary check (quadlet unit) ==== */
	if((unsigned long)workarea & 0x3u){
		ret = SD_ERR;
		goto ErrExit;
	}
	
	/* ==== check card detect port ==== */
	if((cd_port != SD_CD_SOCKET) && (cd_port != SD_CD_DAT3)){
		ret = SD_ERR;
		goto ErrExit;
	}

	/* card detect port is fixed at CD pin */
	cd_port = SD_CD_SOCKET;

	/* ==== initialize peripheral module ==== */
	if(sddev_init(sd_port) != SD_OK){
		ret = SD_ERR_CPU_IF;
		goto ErrExit;
	}
	
	/* disable all interrupts */
	sddev_loc_cpu(sd_port);

	hndl = (SDHNDL *)workarea;

	SDHandle[sd_port] = hndl;

	/* ---- clear work memory zero value --- */
	ptr = (unsigned char *)hndl;
	for(i= sizeof(SDHNDL); i > 0 ;i--){
		*ptr++ = 0;
	}

	/* ---- set SDHI register address ---- */
	hndl->reg_base = base;
	hndl->cd_port = (unsigned char)cd_port;
	/* ---- initialize maximum block count ---- */
	hndl->trans_sectors = 256;
	hndl->trans_blocks  = 32;

	hndl->sd_port = sd_port;

	/* return to select port0 */
	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	/* ==== initialize SDHI ==== */
	sd_outp(hndl,SD_INFO1_MASK,0x031d);
	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,SD_INFO2_MASK,0x8b7f);
	#else
	sd_outp(hndl,SD_INFO2_MASK,0x837f);
	#endif
    sd_outp(hndl,SDIO_INFO1_MASK,0xc007);
	sd_outp(hndl,SDIO_MODE,0x0000);
	info1 = sd_inp(hndl,SD_INFO1);
	sd_outp(hndl,SD_INFO1,(unsigned short)(info1&~0x0005u));
	sd_outp(hndl,SD_INFO2,0x0000);
	sd_outp(hndl,SDIO_INFO1,0x0000);
	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,SOFT_RST,0x0006);
	sd_outp(hndl,SOFT_RST,0x0007);
	sd_outp(hndl,SD_OPTION,0x00BD); // Changed by Rohan to NCycle=SDCLK*2^23.			NCycle=SDCLK*2^24 would be 0x00be, but this was too long and meant it sometimes couldn't access the card when first trying, on boot
	#else
	sd_outp(hndl,SOFT_RST,0);
	sd_outp(hndl,SOFT_RST,1);
	sd_outp(hndl,SD_OPTION,0x00ae);			/* NCycle=SDCLK*2^23 */
	#endif

	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,EXT_SWAP,0x0000);
	#else
	sd_outp(hndl,EXT_SWAP,0x00c0);
	#endif

	/* enable all interrupts */
	sddev_unl_cpu(sd_port);

	return SD_OK;
ErrExit:
		SDHandle[sd_port] = 0;	/* relese SD handle */
	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : do finish operation of SD Driver (2ports)
 * Include      : 
 * Declaration  : int sd_finalize(int sd_port);
 * Functions    : finish SD Driver
 *              : reset SDHI include card detection/removal
 *              : 
 * Argument     : none
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : after this function finished, SD handle is unavailable
 *****************************************************************************/
int sd_finalize(int sd_port)
{
	SDHNDL *hndl;
//	int i;
	unsigned short info1;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	if(SDHandle[sd_port] == 0){
		return SD_ERR;	/* not initilized */
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	/* ==== finish peripheral module ==== */
	sddev_finalize(sd_port);

	/* reset SDHI */
	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,SOFT_RST,0x0006);
	#else
	sd_outp(hndl,SOFT_RST,0x0000);
	#endif
	sd_outp(hndl,SD_INFO1_MASK,0x031d);
	#if		(TARGET_RZ_A1 == 1)
	sd_outp(hndl,SD_INFO2_MASK,0x8b7f);
	#else
	sd_outp(hndl,SD_INFO2_MASK,0x837f);
	#endif
	sd_outp(hndl,SDIO_INFO1_MASK,0xc007);
	sd_outp(hndl,SDIO_MODE,0x0000);
	info1 = sd_inp(hndl,SD_INFO1);
	sd_outp(hndl,SD_INFO1,(unsigned short)(info1&~0x0005u));
	sd_outp(hndl,SD_INFO2,0x0000);
	sd_outp(hndl,SDIO_INFO1,0x0000);
	SDHandle[sd_port] = 0;	/* destruct SD Handle */

	return SD_OK;
}


/*****************************************************************************
 * ID           :
 * Summary      : initialize SD handle
 * Include      : 
 * Declaration  : int _sd_init_hndl(SDHNDL *hndl,unsigned long mode,
 *              : 	unsigned long voltage)
 * Functions    : initialize following SD handle members
 *              : media_type : card type
 *              : write_protect : write protect
 *              : resp_status : R1/R1b response status
 *              : error : error detail information
 *              : stop : compulsory stop flag
 *              : prot_sector_size : sector size (protect area)
 *              : card registers : ocr, cid, csd, dsr, rca, scr, sdstatus and
 *              : status_data
 * Argument     : SDHNDL *hndl : SD handle
 *              : unsigned long mode : driver mode
 *              : unsigned long voltage : working voltage
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
int _sd_init_hndl(SDHNDL *hndl,unsigned long mode,unsigned long voltage)
{
	int i;
	
	hndl->media_type = SD_MEDIA_UNKNOWN;
	hndl->write_protect = 0;
	hndl->resp_status = STATE_IDEL;
	hndl->error = SD_OK;
	hndl->stop = 0;
	hndl->prot_sector_size = 0;
	hndl->voltage = voltage;
	hndl->speed_mode = 0;
	hndl->int_mode = (unsigned char)(mode & 0x1u);
	hndl->trans_mode = (unsigned char)(mode & (SD_MODE_DMA | SD_MODE_DMA_64));
	hndl->sup_card = (unsigned char)(mode & 0x30u);
	hndl->sup_speed = (unsigned char)(mode & 0x40u);
	hndl->sup_ver = (unsigned char)(mode & 0x80u);
	if(mode & SD_MODE_1BIT){
		hndl->sup_if_mode = SD_PORT_SERIAL;
	}
	else{
		hndl->sup_if_mode = SD_PORT_PARALLEL;
	}

	/* initialize card registers */
	for(i = 0; i < 4/sizeof(unsigned short); ++i){
		hndl->ocr[i] = 0;
	}
	for(i = 0; i < 16/sizeof(unsigned short); ++i){
		hndl->cid[i] = 0;
	}
	for(i = 0; i < 16/sizeof(unsigned short); ++i){
		hndl->csd[i] = 0;
	}
	for(i = 0; i < 2/sizeof(unsigned short); ++i){
		hndl->dsr[i] = 0;
	}
	for(i = 0; i < 4/sizeof(unsigned short); ++i){
		hndl->rca[i] = 0;
	}
	for(i = 0; i < 8/sizeof(unsigned short); ++i){
		hndl->scr[i] = 0;
	}
	for(i = 0; i < 14/sizeof(unsigned short); ++i){
		hndl->sdstatus[i] = 0;
	}
	for(i = 0; i < 18/sizeof(unsigned short); ++i){
		hndl->status_data[i] = 0;
	}
	for(i = 0; i < 4/sizeof(unsigned short); ++i){
		hndl->if_cond[i] = 0;
	}

	if(hndl->sup_card & SD_MODE_IO){
		int j;
		
		hndl->io_flag = 0;
		hndl->io_info = 0;

		for(i = 0; i < 4/sizeof(unsigned short); ++i){
			hndl->io_ocr[i] = 0;
		}
		
		for(i = 0; i < 8; ++i){
			for(j = 0; j < (SDIO_INTERNAL_REG_SIZE / sizeof(unsigned char)); ++j){
				hndl->io_reg[i][j] = 0;
			}

			hndl->io_len[i]   = 0;
			hndl->io_abort[i] = 0;
		}
	}

	if(hndl->sup_ver == SD_MODE_VER2X){
		hndl->if_cond[0] = 0;
		hndl->if_cond[1] = 0x00aa;
		if(hndl->voltage & 0x00FF8000){
			hndl->if_cond[1] |= 0x0100;	/* high volatege : 2.7V-3.6V */
		}
		if(hndl->voltage & 0x00000F00){
			hndl->if_cond[1] |= 0x0200;	/* low volatege : 1.65V-1.95V */
		}
	}
	
	return SD_OK;
}


/* End of File */
