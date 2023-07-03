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
*  File Name   : sd_format.c
*  Contents    : Card format
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

/* CHS parameter table */
static const CHS_RECOM chs_tbl[] = {
{ SIZE_CARD_2MB,		NUM_HEAD_2,		SEC_PER_TRACK_16},
{ SIZE_CARD_16MB,		NUM_HEAD_2,		SEC_PER_TRACK_32},
{ SIZE_CARD_32MB,		NUM_HEAD_4,		SEC_PER_TRACK_32},
{ SIZE_CARD_128MB,		NUM_HEAD_8,		SEC_PER_TRACK_32},
{ SIZE_CARD_256MB,		NUM_HEAD_16,	SEC_PER_TRACK_32},
{ SIZE_CARD_504MB,		NUM_HEAD_16,	SEC_PER_TRACK_63},
{ SIZE_CARD_1008MB,		NUM_HEAD_32,	SEC_PER_TRACK_63},
{ SIZE_CARD_2016MB,		NUM_HEAD_64,	SEC_PER_TRACK_63},
{ SIZE_CARD_2048MB,		NUM_HEAD_128,	SEC_PER_TRACK_63},
{ SIZE_CARD_4032MB,		NUM_HEAD_128,	SEC_PER_TRACK_63},
{ SIZE_CARD_32768MB,	NUM_HEAD_255,	SEC_PER_TRACK_63},
{ SIZE_CARD_2TB,		NUM_HEAD_255,	SEC_PER_TRACK_63},
{ 0,0,0}
};

/* SC,BU table */
static const SCBU_RECOM scbu_tbl[] = {
{ SIZE_CARD_8MB,		SEC_PER_CLUSTER_16,		SIZE_OF_BU_16},
{ SIZE_CARD_64MB,		SEC_PER_CLUSTER_32,		SIZE_OF_BU_32},
{ SIZE_CARD_256MB,		SEC_PER_CLUSTER_32,		SIZE_OF_BU_64},
{ SIZE_CARD_1024MB,		SEC_PER_CLUSTER_32,		SIZE_OF_BU_128},
{ SIZE_CARD_2048MB,		SEC_PER_CLUSTER_64,		SIZE_OF_BU_128},
{ SIZE_CARD_32768MB,	SEC_PER_CLUSTER_64,		SIZE_OF_BU_8192},
{ SIZE_CARD_128GB,		SEC_PER_CLUSTER_256,	SIZE_OF_BU_32768},
{ SIZE_CARD_512GB,		SEC_PER_CLUSTER_512,	SIZE_OF_BU_65536},
{ SIZE_CARD_2TB,		SEC_PER_CLUSTER_1024,	SIZE_OF_BU_131072},
{ 0,0,0}
};

/* Up-case Table in Compressed Format */
extern const unsigned short sd_upcase_tbl[0xB66];


/* ==== prototype ==== */
static int _sd_format(SDHNDL *hndl,SD_FMT_WORK *sdfmt,
	int format_mode,int (*callback)(unsigned long,unsigned long));
static int _sd_get_fmt_parm(SDHNDL *hndl,SD_FMT_WORK *sdfmt);
static int _sd_calc_parameter(SD_FMT_WORK *sdfmt);
static int _sd_init_mbr(SD_FMT_WORK *sdfmt);
static int _sd_init_pbr(SD_FMT_WORK *sdfmt);
static int _sd_init_fat(SD_FMT_WORK *sdfmt);
static int _sd_init_fsinfo(SD_FMT_WORK *sdfmt);
static int _sd_init_rsvb(SD_FMT_WORK *sdfmt);
static int _sd_init_br_bs(SD_FMT_WORK *sdfmt);
static int _sd_init_br_ebs(SD_FMT_WORK *sdfmt);
static int _sd_init_br_chksum(SD_FMT_WORK *sdfmt, unsigned long checksum);
static int _sd_init_allocation_bitmap(SD_FMT_WORK *sdfmt);
static int _sd_init_upcase_table(SD_FMT_WORK *sdfmt, unsigned long secoffset);
static int _sd_init_root_directory(SD_FMT_WORK *sdfmt);
static int _sd_format_write(SDHNDL *hndl,SD_FMT_WORK *sdfmt,unsigned char fill,
	unsigned long secno, long seccnt,
		int (*callback)(unsigned long,unsigned long));
static int _sd_format_erase( SDHNDL *hndl,SD_FMT_WORK *sdfmt,
	unsigned long secno, long seccnt,
		int (*callback)(unsigned long,unsigned long));

/*****************************************************************************
 * ID           :
 * Summary      : format SD memory card
 * Include      : 
 * Declaration  : int sd_format(int sd_port, int mode,int (*callback)(unsigned long,unsigned long));
 * Functions    : format SD memory card based on SD apec part2
 *              : 
 * Argument     : int mode : format mode SD_FORMAT_QUICK or SD_FORMAT_FULL
 *              : int (*callback)(unsigned long,unsigned long) :
 *              :     format callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : File system allocate the format work buffer
 *****************************************************************************/
int sd_format(int sd_port, int mode,int (*callback)(unsigned long,unsigned long))
{
	SDHNDL *hndl;
	SD_FMT_WORK sdfmt;
	unsigned long seed;
//	unsigned short info1_back;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	hndl->error = SD_OK;

	/* ---- check mount ---- */
	if(hndl->mount != SD_MOUNT_UNLOCKED_CARD){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* not mounted yet */
	}

	/* ---- check mode ---- */
	if (mode != SD_FORMAT_QUICK && mode != SD_FORMAT_FULL){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* parameter error */
	}
	
	if (mode == SD_FORMAT_QUICK){
		callback = 0;	/* not used callback function at quick format */
	}
	/* ---- is card existed? ---- */
	if(sd_check_media(sd_port) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);
		return hndl->error;	/* no card */
	}

	/* ---- check card type ---- */
	if(hndl->media_type != SD_MEDIA_SD && hndl->media_type != SD_MEDIA_MMC && hndl->media_type != SD_MEDIA_IO && hndl->media_type != SD_MEDIA_COMBO){
		_sd_set_err(hndl,SD_ERR_CARD_TYPE);
		return hndl->error;	/* unknown type */
	}
	
	/* ---- check write protect ---- */
	if(hndl->write_protect){
		_sd_set_err(hndl,SD_ERR_WP);
		return hndl->error;	/* write protect error */
	}
	hndl->error = SD_OK;
	
	/* ---- initialize work buffer ---- */
	sdfmt.pbuff = hndl->rw_buff;
	sdfmt.buff_sec_size = hndl->buff_size/SD_SECTOR_SIZE;

	_sd_memset( sdfmt.pbuff, 0x00, sdfmt.buff_sec_size*SD_SECTOR_SIZE );

	/* ---- initialize Volume ID Number ---- */
	/* initial value is calculate based on CID */
	seed = (unsigned long)(hndl->cid[5] + hndl->cid[6] + hndl->cid[7]);
	if(!seed){
		seed = 0x32104;
	}
	_sd_srand(seed);
	
	sdfmt.area = SD_USER_AREA;
	sdfmt.write = _sd_format_write;
	sdfmt.erase = _sd_format_erase;
	sdfmt.chs = (CHS_RECOM *)&chs_tbl[0];
	sdfmt.scbu = (SCBU_RECOM *)&scbu_tbl[0];
	sdfmt.volid_enable = 0;
	/* ---- execute format ---- */
	return  _sd_format(hndl, &sdfmt, mode, callback);
}

/*****************************************************************************
 * ID           :
 * Summary      : format SD memory card 2
 * Include      : 
 * Declaration  : int sd_format2(int sd_port, int mode,unsigned long volserial,int (*callback)(unsigned long,unsigned long));
 * Functions    : format SD memory card based on SD apec part2
 *              : 
 * Argument     : int mode : format mode SD_FORMAT_QUICK or SD_FORMAT_FULL
 *              : unsigned long volserial : volume serial number
 *              : int (*callback)(unsigned long,unsigned long) :
 *              :     format callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : File system allocate the format work buffer
 *****************************************************************************/
int sd_format2(int sd_port, int mode,unsigned long volserial,int (*callback)(unsigned long,unsigned long))
{
	SDHNDL *hndl;
	SD_FMT_WORK sdfmt;
	unsigned long seed;
//	unsigned short info1_back;

	if( (sd_port != 0) && (sd_port != 1) ){
		return SD_ERR;
	}

	hndl = _sd_get_hndls(sd_port);
	if(hndl == 0){
		return SD_ERR;	/* not initilized */
	}

	hndl->error = SD_OK;

	/* ---- check mount ---- */
	if(hndl->mount != SD_MOUNT_UNLOCKED_CARD){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* not mounted yet */
	}

	/* ---- check mode ---- */
	if (mode != SD_FORMAT_QUICK && mode != SD_FORMAT_FULL){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* parameter error */
	}
	
	if (mode == SD_FORMAT_QUICK){
		callback = 0;	/* not used callback function at quick format */
	}
	/* ---- is card existed? ---- */
	if(sd_check_media(sd_port) != SD_OK){
		_sd_set_err(hndl,SD_ERR_NO_CARD);
		return hndl->error;	/* no card */
	}

	/* ---- check card type ---- */
	if(hndl->media_type != SD_MEDIA_SD && hndl->media_type != SD_MEDIA_MMC && hndl->media_type != SD_MEDIA_IO && hndl->media_type != SD_MEDIA_COMBO){
		_sd_set_err(hndl,SD_ERR_CARD_TYPE);
		return hndl->error;	/* unknown type */
	}
	
	/* ---- check write protect ---- */
	if(hndl->write_protect){
		_sd_set_err(hndl,SD_ERR_WP);
		return hndl->error;	/* write protect error */
	}
	hndl->error = SD_OK;
	
	/* ---- initialize work buffer ---- */
	sdfmt.pbuff = hndl->rw_buff;
	sdfmt.buff_sec_size = hndl->buff_size/SD_SECTOR_SIZE;

	_sd_memset( sdfmt.pbuff, 0x00, sdfmt.buff_sec_size*SD_SECTOR_SIZE );

	/* ---- initialize Volume ID Number ---- */
	/* initial value is calculate based on CID */
	seed = (unsigned long)(hndl->cid[5] + hndl->cid[6] + hndl->cid[7]);
	if(!seed){
		seed = 0x32104;
	}
	_sd_srand(seed);
	
	sdfmt.area = SD_USER_AREA;
	sdfmt.write = _sd_format_write;
	sdfmt.erase = _sd_format_erase;
	sdfmt.chs = (CHS_RECOM *)&chs_tbl[0];
	sdfmt.scbu = (SCBU_RECOM *)&scbu_tbl[0];
	sdfmt.volid_enable = 1;
	sdfmt.volid = volserial;

	/* ---- execute format ---- */
	return  _sd_format(hndl, &sdfmt, mode, callback);
}

/*****************************************************************************
 * ID           :
 * Summary      : format SD memory card
 * Include      : 
 * Declaration  : int _sd_format(SDHNDL *hndl,SD_FMT_WORK *sdfmt,
 *              :   int format_mode,int (*callback)(unsigned long,unsigned long))
 * Functions    : format SD memory card based on SD apec part2
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : SD_FMT_WORK *sdfmt : format parameter
 *              : int format_mode : format mode SD_FORMAT_QUICK or 
 *              : SD_FORMAT_FULL
 *              : int (*callback)(unsigned long,unsigned long) :
 *              :     format callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       :
 *****************************************************************************/
static int _sd_format(SDHNDL *hndl,SD_FMT_WORK *sdfmt,int format_mode,
	int (*callback)(unsigned long,unsigned long))
{
	unsigned long offset=0;
	unsigned int i,j,k;
	int ret;
	long total_sec;
	unsigned long erase_offset;

	/* ---- get card information to format card ---- */
	ret = _sd_get_fmt_parm(hndl,sdfmt);
	if(ret != SD_OK){
		return ret;
	}
	
	if(sdfmt->fmt_exfat == 1){
		if(format_mode == SD_FORMAT_FULL){
			/* number of sectors at full format */
			sdfmt->format_size = sdfmt->area_size;
		}
		else{
			/* number of sectors at quick format */
			sdfmt->format_size = sdfmt->fmt_nom + sdfmt->fmt_ssa + sdfmt->fmt_sc * 3;
		}
		
		/* call callback function */
		if(callback){
			(*callback)(0,sdfmt->format_size);
		}

		/* ---- MBR ---- */
		_sd_init_mbr(sdfmt);
		ret = (*sdfmt->write)(hndl,sdfmt,0,offset,sdfmt->fmt_nom,callback);
		if ( ret != SD_OK ){
			return ret;
		}
		offset += sdfmt->fmt_nom;

		/* ---- BR ---- */
		for(i=0;i<2;i++){
			unsigned long checksum = 0;

			/* Boot Sector */
			_sd_init_br_bs(sdfmt);
			for(k=0;k<512;k++){
				if ((k == 106) || (k == 107) || (k == 112)){
					continue;
				}
				/* calculate checksum */
				checksum = ((checksum&1) ? 0x80000000 : 0) + (checksum>>1) + (unsigned long)sdfmt->pbuff[k];
			}
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset+12*i,1,callback);
			if(ret != SD_OK){
				return ret;
			}

			/* Extended Boot Sectors */
			for(j=1;j<=8;j++){
				_sd_init_br_ebs(sdfmt);
				for(k=0;k<512;k++){
					/* calculate checksum */
					checksum = ((checksum&1) ? 0x80000000 : 0) + (checksum>>1) + (unsigned long)sdfmt->pbuff[k];
				}
				ret = (*sdfmt->write)(hndl,sdfmt,0,offset+12*i+j,1,callback);
				if(ret != SD_OK){
					return ret;
				}
			}

			/* OEM Parameters */
			j = 9;
			ret = sd_read_sect(hndl->sd_port, sdfmt->pbuff,offset+12*i+j,1);
			if(ret != SD_OK){
				return ret;
			}
			for(k=0;k<512;k++){
				/* calculate checksum */
				checksum = ((checksum&1) ? 0x80000000 : 0) + (checksum>>1) + (unsigned long)sdfmt->pbuff[k];
			}

			/* Reserved */
			j = 10;
			_sd_memset(sdfmt->pbuff,0,512);		/* all 00h */
			for(k=0;k<512;k++){
				/* calculate checksum */
				checksum = ((checksum&1) ? 0x80000000 : 0) + (checksum>>1) + (unsigned long)sdfmt->pbuff[k];
			}
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset+12*i+j,1,callback);
			if(ret != SD_OK){
				return ret;
			}

			/* Boot Checksum */
			j = 11;
			_sd_init_br_chksum(sdfmt, checksum);
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset+12*i+j,1,callback);
			if(ret != SD_OK){
				return ret;
			}
		}
		offset += sdfmt->fmt_bu/2;

		/* ---- FAT ---- */
		_sd_init_fat(sdfmt);
		ret = (*sdfmt->write)(hndl,sdfmt,0,offset,(long)sdfmt->fmt_sf,callback);
		if(ret != SD_OK){
			return ret;
		}
		offset += sdfmt->fmt_sf;

		/* ---- Cluster Heap ---- */
		/* allocation bitmap */
		_sd_init_allocation_bitmap(sdfmt);
		ret = (*sdfmt->write)(hndl,sdfmt,0,offset,(long)sdfmt->fmt_sc,callback);
		if(ret != SD_OK){
			return ret;
		}
		offset += sdfmt->fmt_sc;
		/* up-case table */
		for(i=0;i<sdfmt->fmt_sc;i++){
			_sd_init_upcase_table(sdfmt,i);
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset+i,1,callback);
			if(ret != SD_OK){
				return ret;
			}
		}
		offset += sdfmt->fmt_sc;
		/* root directory */
		_sd_init_root_directory(sdfmt);
		ret = (*sdfmt->write)(hndl,sdfmt,0,offset,(long)sdfmt->fmt_sc,callback);
		if(ret != SD_OK){
			return ret;
		}
		offset += sdfmt->fmt_sc;
		/* other cluster */
		if(format_mode == SD_FORMAT_FULL){
			/* initialize data area only at full format */
			/* initialized by ERASE command */
			total_sec = sdfmt->area_size - offset;
			ret = (*sdfmt->erase)(hndl,sdfmt,offset,total_sec,callback);
			if ( ret != SD_OK ){
				return ret;
			}
		}

		return ret;
	}

	if(format_mode == SD_FORMAT_FULL){
		/* number of sectors at full format */
		sdfmt->format_size = sdfmt->area_size;
	}
	else{
		/* number of sectors at quick format */
		sdfmt->format_size = sdfmt->fmt_nom + sdfmt->fmt_ssa;
	}
	
	/* call callback function */
	if(callback){
		(*callback)(0,sdfmt->format_size);
	}

	/* erase MBR, PBR, FAT areas so on previously at full format */
	if(format_mode == SD_FORMAT_FULL){
		erase_offset = sdfmt->fmt_nom + sdfmt->fmt_ssa;
		ret = (*sdfmt->erase)(hndl,sdfmt,0L,(long)erase_offset,0);
		if(ret != SD_OK){
			return ret;
		}
	}

	/* ---- MBR ---- */
	_sd_init_mbr(sdfmt);

	ret = (*sdfmt->write)(hndl,sdfmt,0,offset,(long)sdfmt->fmt_nom,callback);
	if ( ret != SD_OK ){
		return ret;
	}

	offset += sdfmt->fmt_nom;

	if(sdfmt->fmt_fatbit == 32){	/* FAT32 */
		/* ---- write PBR ---- */
		for(i = 0;i < 2;i++){
			_sd_init_pbr(sdfmt);
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset+6*i,1L,callback);
			if(ret != SD_OK){
				return ret;
			}
		}
		
		/* ---- write FS info Sector ---- */
		for(i = 0;i < 2;i++){
			_sd_init_fsinfo(sdfmt);
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset+1+6*i,1L,callback);
			if(ret != SD_OK){
				return ret;
			}
		}
		
		/* ---- write reserved for boot sector ---- */
		for(i = 0;i < 2;i++){
			_sd_init_rsvb(sdfmt);
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset+2+6*i,1L,callback);
			if(ret != SD_OK){
				return ret;
			}
		}
		
		offset += sdfmt->fmt_rsc;

		/* ---- FAT1 and FAT2 ---- */
		for(i=0; i<2; i++){
			_sd_init_fat(sdfmt);
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset,(long)sdfmt->fmt_sf,
				callback);
			if(ret != SD_OK){
				return ret;
			}
			offset += sdfmt->fmt_sf;
		}

		ret = (*sdfmt->write)(hndl,sdfmt,0,offset,(long)sdfmt->fmt_sc,callback);	/* already buffer data are cleared zero due to previous FAT writing */
		if(ret != SD_OK){
			return ret;
		}
		erase_offset += 64;
	}
	else{	/* FAT16 */
		/* ---- PBR ---- */
		_sd_init_pbr(sdfmt);

		/* ---- write PBR ---- */
		ret = (*sdfmt->write)(hndl,sdfmt,0,offset,1L,callback);
		if(ret != SD_OK){
			return ret;
		}

		offset++;

		/* ---- FAT1 and FAT2 ---- */
		for(i=0; i<2; i++){
			_sd_init_fat(sdfmt);
			ret = (*sdfmt->write)(hndl,sdfmt,0,offset,(long)sdfmt->fmt_sf,
				callback);
			if(ret != SD_OK){
				return ret;
			}
			offset += sdfmt->fmt_sf;
		}

		/* ---- Root Directory ---- */
		ret = (*sdfmt->write)(hndl,sdfmt,0,offset,32L,callback);	/* already buffer data are cleared zero due to previous FAT writing */
		if(ret != SD_OK){
			return ret;
		}
	}

	/* ---- data area ---- */
	if(format_mode == SD_FORMAT_FULL){
		/* initialize data area only at full format */
		/* initialized by ERASE command */
		total_sec = sdfmt->area_size - erase_offset;
		ret = (*sdfmt->erase)(hndl,sdfmt,erase_offset,total_sec,callback);
		if ( ret != SD_OK ){
			return ret;
		}
	}

	return ret;
}

/*****************************************************************************
 * ID           :
 * Summary      : get format parameter
 * Include      : 
 * Declaration  : static int _sd_get_fmt_parm(SDHNDL *hndl,SD_FMT_WORK *sdfmt)
 * Functions    : get format parameter from all sector size and set it sdfmt
 *              : num of heads, sectors per track, sectors per cluster 
 *              : and boundary are decide by all sector size and parameter
 *              : table
 *              : reserved sectors, FAT size , sectors per FATs are calculated
 *              : from previos parameter
 * Argument     : SDHNDL *hndl : SD handle
 *              : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       :
 *****************************************************************************/
static int _sd_get_fmt_parm(SDHNDL *hndl,SD_FMT_WORK *sdfmt)
{
	int i,ret;
	unsigned long size;	/* total size */
	CHS_RECOM *chs;
	SCBU_RECOM *scbu;
	
	/* ---- get card all sector size ---- */
	size = hndl->card_sector_size + hndl->prot_sector_size;
	/* size�́ANumbr of Heads, Sectors per Track, Sectors per Cluster, Boundary Unit�̌v�Z�Ɏg�p����B*/
	/* size��0x100000000sectors(2TB)�̏ꍇ�A0xFFFFFFFFsectors�Ƃ��Čv�Z���Ă������l�ɂȂ邽�߁A       */
	/* SIZE_CARD_2TB �� 0xFFFFFFFF(unsigned long�̍ő�l) �ƒ�`���Ă���                              */
	if(hndl->card_sector_size > 0 && size < hndl->card_sector_size){
		size = SIZE_CARD_2TB;
	}
	
	if(sdfmt->area == SD_USER_AREA){
		sdfmt->area_size = hndl->card_sector_size;	/* user area size */
	}
	else{
		sdfmt->area_size = hndl->prot_sector_size;	/* protect area size */
	}
	
	if(size == 0){
		_sd_set_err(hndl,SD_ERR);
		return hndl->error;	/* not card mounted yet */
	}
	
	chs = sdfmt->chs;
	scbu = sdfmt->scbu;
	
	/* ---- get CHS parameter ---- */
	for(i=0; ; i++,chs++){
		if(chs->capa == 0){
			_sd_set_err(hndl,SD_ERR);
			return hndl->error;
		}
		if(size <= chs->capa){
			sdfmt->fmt_hn = chs->heads;
			sdfmt->fmt_spt = chs->spt;
			break;
		}
	}
	
	/* ---- get cluster size and boundary unit ---- */
	for(i=0; ; i++ ,scbu++){
		if(scbu->capa == 0){
			_sd_set_err(hndl,SD_ERR);
			return hndl->error;
		}
		if(size <= scbu->capa){
			sdfmt->fmt_sc = scbu->sc;
			sdfmt->fmt_bu = scbu->bu;
			break;
		}
	}
	
	/* ---- calculate format parameter ---- */
	ret = _sd_calc_parameter(sdfmt);
	if(ret != SD_OK){
		_sd_set_err(hndl,ret);
		return hndl->error;
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : calculate FAT format parameter
 * Include      : 
 * Declaration  : static int _sd_calc_parameter(SD_FMT_WORK *sdfmt)
 * Functions    : following parameter are calculated from all number of sectors
 *              : and boundary unit so on and set sdfmt
 *              : 
 *              : FAT type�FFAT12, FAT16 or FAT32
 *              : max cluster number
 *              : sectors per FAT
 *              : sectors per sysytem area
 *              : number of reserved sectors
 *              : sectors per MBR
 * Argument     : SDHNDL *hndl : SD handle
 *              : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : sdfmt parameter must be set previosly
 *              : calculation is based on SD spec part2 for user area
 *              : calculation is based on SD spec part3 for protect area
 *****************************************************************************/
static int _sd_calc_parameter(SD_FMT_WORK *sdfmt)
{
	unsigned long ts,max,nom,sf,sfx,ssa,rsc;
	unsigned char fatbit;
	unsigned short sc;
	unsigned long bu;
	
	sc = sdfmt->fmt_sc;	/* sectors per cluster */
 	ts = sdfmt->area_size;	/* total sectors */
	bu = sdfmt->fmt_bu;	/* boundary unit */
	
	max = ts/sc;
	
	sdfmt->fmt_exfat = 0;
	if(ts > SIZE_CARD_32768MB){
		sdfmt->fmt_fatbit = 32;
		sdfmt->fmt_exfat  = 1;				/* exFAT */
		sdfmt->fmt_max = (ts-bu*2)/sc+1;	/* max cluster number */
		sdfmt->fmt_nom = bu;				/* sectors per MBR */
		sdfmt->fmt_ssa = bu; 				/* sectors per sysytem area */
		sdfmt->fmt_sf  = bu/2; 				/* sectors per FAT */
		return SD_OK;
	}
	/* examine FAT12 can be applied from the number of cluster */
	/* since max cluster number of FAT12 is 0xFF5, the number of max clusters is 0xFF4 */
	/* 0xFF4 means 0xff5 + 1 - 2 (FAT offset) */
	if(max > 0xfff4){
		fatbit = 32;	/* FAT32 */
	}
	else if(max > 0xff4){		
		fatbit = 16;	/* FAT16 */
	}
	else{
		fatbit = 12;	/* FAT12 */
	}
	
	/* 512 means sector size, 8 means bits per byte */
	sf = (max*fatbit+512*8-1)/(512*8);
	
	if(fatbit == 32){	/* FAT32 */
		nom = bu;	/* number of sectors in MBR is boundary unit */
		
		while(1){	/* iterative calculation for reserved sector count */
			/* set reserved sector count */
			rsc = (bu-((2*sf)%bu));
			
			if(rsc < 9){	/* reserved sector count minimum size is 9 sectors */
				rsc += bu;
			}
			
			ssa = (rsc+2*sf);  /* RSC + 2*FAT */
			
			while(1){	/* iterative calculation for sectors per FAT */
				/* calculate max cluster number (max is just number) */
				max = (ts-nom-ssa)/sc+1;
			
				/* calculate sectors per FAT */
				sfx = ((2+(max-1))*fatbit+512*8-1)/(512*8);
			
				if(sfx > sf){	/* if sf is greater than previous one, add boundary unit */

					ssa += bu;
					rsc += bu;
				}
				else{
					break;
				}
			}
			
			if(sfx == sf){	/* if same as initial one, calculation end */
				break;
			}
			else{	/* if different each other, continue calculation */
				sf = (sf-1);
			}
		}
	}
	else{	/* FAT12 or FAT16 */
		rsc = 1;	/* constant */
		
		while(1){	/* iterative calculation for number of sectors in MBR */
			ssa = rsc+2*sf+32;  /* PBR + 2*FAT + RDE */
			
			/* set number of sectors in MBR */
			nom = (bu-(ssa%bu));
			
			/* is same size of boundary unit? */
			if(nom != bu){
				nom += bu;	/* if different each other, add */
			}
		
			while(1){	/* iterative calculation for sectors per FAT */
				/* calculate max cluster number (max is just number) */
				max = (ts-nom-ssa)/sc+1;
				
				/* check FAT type */
				if((max <= 0xff5) && (fatbit == 16)){
					fatbit = 12;	/* if max cluster number is 0xFF5, set FAT12 */
				}
			
				/* calculate sectors per FAT */
				sfx = ((2+(max-1))*fatbit+512*8-1)/(512*8);
			
				if(sfx > sf){	/* if sf is greater than previous one, add boundary unit */
					nom += bu;
				}
				else{
					break;
				}
			}
			
			if(sfx == sf){	/* if same as previous one, calculation end */
				break;
			}
			else{	/* if different each other, continue calculation */
				sf = sfx;
			}
		}
	}
	
	sdfmt->fmt_fatbit = fatbit;	/* FAT12 or FAT16 */
	sdfmt->fmt_max = max;		/* max cluster number */
	sdfmt->fmt_sf = sf; 		/* sectors per FAT */
	sdfmt->fmt_ssa = ssa; 		/* sectors per sysytem area */
	sdfmt->fmt_nom = nom;		/* sectors per MBR */
	sdfmt->fmt_rsc = (unsigned short)rsc;		/* reserved sector count */
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create MBR (Master Boot Record) image
 * Include      : 
 * Declaration  : static int _sd_init_mbr(SD_FMT_WORK *sdfmt)
 * Functions    : calculate start and end CHS parameter refer to sdfmt
 *              : set MBR parameter to image buffer
 *              : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *****************************************************************************/
static int _sd_init_mbr(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;
	unsigned short tn;
	unsigned short sn;
	unsigned long size;

	/* ---- set image buffer ---- */
	ptr = sdfmt->pbuff;
	_sd_memset(ptr,0,SD_SECTOR_SIZE);
	ptr += 0x1be;

	/* active flag */
	*ptr++ = 0x00;

	/* ---- partition size ----*/

	/* start head */
	if(sdfmt->fmt_nom > 16450560){
		/* exceed 8032.5 MB LBA FAT32  */
		/* set to maximum value        */
		/* start head */
		*ptr++ = 0xfe;
		/* start cylinder high and start sector */
		*ptr++ = 0xff;
		/* start cylinder low */
		*ptr++ = 0xff;
	}
	else{
		/* start head */
		*ptr++ = (unsigned char)(sdfmt->fmt_nom%(sdfmt->fmt_spt * 
			sdfmt->fmt_hn)/sdfmt->fmt_spt);
		
		sn = (unsigned short)(sdfmt->fmt_nom%sdfmt->fmt_spt + 1);
		tn = (unsigned short)(sdfmt->fmt_nom/(sdfmt->fmt_spt * sdfmt->fmt_hn));
		
		/* start cylinder high and start sector */
		*ptr++ = (unsigned char)(((tn&0x300u)>> 2u) | sn);
		/* start cylinder low */
		*ptr++ = (unsigned char)tn;
	}
		
	/* area_size or size(=area_size-nom)? */
	if(sdfmt->area_size > 0x400000){
		/* case of FAT32 is ending location */
		size = sdfmt->area_size - 1;
		/* exceed 8032.5 MB LBA FAT32  */
		if(size > 16450560){
			if(sdfmt->fmt_exfat == 1){
				*ptr++ = 0x07;		/* exFAT */
			}
			else{
				*ptr++ = 0x0c;
			}
		}
		else{
			/* less than 8032.5 MB  CHS FAT32 */
			*ptr++ = 0x0b;
		}
	}
	else{	/* FAT12 or FAT16 */
		/* case of FAT12,FAT16 is partition size */
		size = sdfmt->area_size - sdfmt->fmt_nom;
		if(size < 32680){
			/* FAT12 */
			*ptr++ = 0x01;
		}
		else if(size < 65536){
			/* FAT16 under 32MB */
			*ptr++ = 0x04;
		}
		else{
			/* FAT16 over 32MB */
			*ptr++ = 0x06;
		}
	}

	/* ---- end CHS ---- */
	/* end head */
	size = sdfmt->area_size - 1;
	if(size > 16450560){
		/* end head */
		*ptr++ = 0xfe;
		/* end cylinder high and end sector */
		*ptr++ = 0xff;
		/* end cylinder low */
		*ptr++ = 0xff;
	}
	else{
		*ptr++ = (unsigned char)(size%(sdfmt->fmt_spt * sdfmt->fmt_hn)/sdfmt->fmt_spt);
		
		sn = (unsigned short)(size%sdfmt->fmt_spt + 1);
		tn = (unsigned short)(size/(sdfmt->fmt_spt * sdfmt->fmt_hn));
		
		*ptr++ = (unsigned char)(((tn&0x300u)>> 2u) | sn);	/* end cylinder high and end sector */
		*ptr++ = (unsigned char)tn;	/* end cylinder low */
	}

	/* ---- LBA start sector number (Relative Sector) ---- */
	size = sdfmt->fmt_nom;
	*ptr++ = (unsigned char )size ;
	*ptr++ = (unsigned char )(size >> 8);
	*ptr++ = (unsigned char )(size >> 16);
	*ptr++ = (unsigned char )(size >> 24);
	
	/* ---- number of all partition sectors (Total Sector) ---- */
	size = sdfmt->area_size - sdfmt->fmt_nom;
	*ptr++ = (unsigned char )size ;
	*ptr++ = (unsigned char )(size >> 8);
	*ptr++ = (unsigned char )(size >> 16);
	*ptr++ = (unsigned char )(size >> 24);
	
	/* ---- Signature Word ---- */
	ptr = sdfmt->pbuff + 0x1fe;
	*ptr++ = 0x55;
	*ptr = 0xaa;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create PBR (Partition Boot Record) image
 * Include      : 
 * Declaration  : static int _sd_init_pbr(SD_FMT_WORK *sdfmt)
 * Functions    : calculate PBR parameter refer to sdfmt
 *              : set PBR parameter to image buffer
 *              : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *              : OEM name and Volume Label are filled with Space code
 *****************************************************************************/
static int _sd_init_pbr(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;
	unsigned long size;

	ptr = sdfmt->pbuff;
	_sd_memset(ptr,0,SD_SECTOR_SIZE);
	
	/* Jump Command (3 bytes) */
	*ptr++ = 0xeb;
	*ptr++ = 0x00;
	*ptr++ = 0x90;

	/* OEM name filled with space code */
	_sd_memset(ptr,0x20,8);
	ptr+=8;

	/* bytes per sector (512 bytes) */
	*ptr++ = 0x00;
	*ptr++ = 0x02;

	/* sectors per cluster */
	*ptr++ = (unsigned char)(sdfmt->fmt_sc);

	if(sdfmt->fmt_fatbit == 32){	/* FAT32 */
		*ptr++ = (unsigned char)(sdfmt->fmt_rsc);
		*ptr++ = (unsigned char)((sdfmt->fmt_rsc)>>8u);
	}
	else{
		*ptr++ = 1;
		ptr++;
	}

	/* number of FATs */
	*ptr++ = 2;

	/* number of root directory (512) */
	if(sdfmt->fmt_fatbit == 32){	/* FAT32 */
		/* no root directory */
		ptr += 2;
	}
	else{
		*ptr++ = 0x00;
		*ptr++ = 0x02;
	}

	/* number of total sectors */
	size = sdfmt->area_size - sdfmt->fmt_nom;
	if(size < 65536){
		*ptr++ = (unsigned char)size;
		*ptr++ = (unsigned char)(size>>8u);
	}
	else{
		ptr+=2;	/* skip */
	}

	/* media type */
	*ptr++ = 0xf8;

	/* sectors per FAT */
	if(sdfmt->fmt_fatbit == 32){	/* FAT32 */
		ptr += 2;
	}
	else{
		*ptr++ = (unsigned char)sdfmt->fmt_sf;
		*ptr++ = (unsigned char)(sdfmt->fmt_sf>>8u);
	}

	/* sectors per track */
	*ptr++ = (unsigned char)sdfmt->fmt_spt;
	*ptr++ = (unsigned char)(sdfmt->fmt_spt>>8u);

	/* number of heads */
	*ptr++ = (unsigned char)sdfmt->fmt_hn;
	*ptr++ = (unsigned char)(sdfmt->fmt_hn>>8u);

	/* number of hidden sectors */
	*ptr++ = (unsigned char)sdfmt->fmt_nom;
	*ptr++ = (unsigned char)(sdfmt->fmt_nom>>8u);
	*ptr++ = (unsigned char)(sdfmt->fmt_nom>>16u);
	*ptr++ = (unsigned char)(sdfmt->fmt_nom>>24u);
	
	/* number of total sectors (32bits picture) */
	if(size > 65535){
		*ptr++ = (unsigned char)size;
		*ptr++ = (unsigned char)(size>>8u);
		*ptr++ = (unsigned char)(size>>16u);
		*ptr++ = (unsigned char)(size>>24u);
	}
	else{
		ptr+=4;	/* skip */
	}

	if(sdfmt->fmt_fatbit == 32){	/* FAT32 */
		/* sectors per FAT for FAT32 */
		*ptr++ = (unsigned char)sdfmt->fmt_sf;
		*ptr++ = (unsigned char)(sdfmt->fmt_sf>>8u);
		*ptr++ = (unsigned char)(sdfmt->fmt_sf>>16u);
		*ptr++ = (unsigned char)(sdfmt->fmt_sf>>24u);
		
		/* Extension Flag */
		ptr += 2;
		
		/* FS Version 0000'h */
		ptr += 2;

		/* Root Cluster (case of not defect) */
		*ptr++ = 0x02;
		ptr += 3;
		
		/* FS Info (offset 1) */
		*ptr++ = 0x01;
		ptr++;
		
		/* backup boot sector (offset 6) */
		*ptr++ = 0x06;
		ptr++;

		/* reserved */
		ptr += 12;

		/* physical disk number */
		*ptr++ = 0x80;
		ptr++;

		/* Extended Boot Record Signature */
		*ptr++ = 0x29;

		/* Volume Serial (ID) Number */
		if(sdfmt->volid_enable){
			*ptr++ = (unsigned char)sdfmt->volid;
			*ptr++ = (unsigned char)(sdfmt->volid >> 8);
			*ptr++ = (unsigned char)(sdfmt->volid >> 16);
			*ptr++ = (unsigned char)(sdfmt->volid >> 24);
		}
		else{
			*ptr++ = 0xb1;
			*ptr++ = 0x77;
			*ptr++ = 0x2f;
			*ptr++ = 0x57;
		}

		/* Volume Lavel */
		_sd_memcpy(ptr,(unsigned char*)"NO NAME    ",11);

		ptr+=11;

		/* File System Type */
		_sd_memcpy(ptr,(unsigned char *)"FAT32   ",8);
	}
	else{
		/* physical disk number */
		*ptr++ = 0x80;

		/* reserved */
		ptr++;	/* skip */

		/* Extended Boot Record Signature */
		*ptr++ = 0x29;

		/* Volume Serial (ID) Number */
		if(sdfmt->volid_enable){
			*ptr++ = (unsigned char)sdfmt->volid;
			*ptr++ = (unsigned char)(sdfmt->volid >> 8);
			*ptr++ = (unsigned char)(sdfmt->volid >> 16);
			*ptr++ = (unsigned char)(sdfmt->volid >> 24);
		}
		else{
		    /* set volume number, original 0x00000000 fixed */
			*ptr++ = (unsigned char)_sd_rand();
			*ptr++ = (unsigned char)_sd_rand();
			*ptr++ = (unsigned char)_sd_rand();
			*ptr++ = (unsigned char)_sd_rand();
		}

		/* Volume Lavel */
		_sd_memcpy(ptr,(unsigned char*)"NO NAME    ",11);
		ptr+=11;

		/* File System Type */
		if(sdfmt->fmt_fatbit == 12){
			_sd_memcpy(ptr,(unsigned char *)"FAT12   ",8);
		}
		else{
			_sd_memcpy(ptr,(unsigned  char *)"FAT16   ",8);
		}
	}

	/* ---- Signature Word ---- */
	ptr = sdfmt->pbuff + 0x1fe;
	*ptr++= 0x55;
	*ptr++= 0xaa;
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create Boot Sector image
 * Include      : 
 * Declaration  : static int _sd_init_br_bs(SD_FMT_WORK *sdfmt)
 * Functions    : calculate Boot Sector parameter refer to sdfmt
 *              : set Boot Sector parameter to image buffer
 *              : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *****************************************************************************/
static int _sd_init_br_bs(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;
	unsigned long size;
	unsigned int i;

	ptr = sdfmt->pbuff;
	/* (PB0-2)jump boot */
	*ptr++ = 0xeb;
	*ptr++ = 0x76;
	*ptr++ = 0x90;
	/* (PB3-10)file system name */
	_sd_memcpy(ptr,(unsigned char *)"EXFAT   ",8);
	ptr+=8;
	/* (PB11-63)must be zero */
	_sd_memset(ptr,0,53);		/* all 00h */
	ptr += 53;
	/* (PB64-71)partition offset */
	size = sdfmt->fmt_bu;
	*ptr++ = (unsigned char)size;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	/* (PB72-79)volume length */
	size = sdfmt->area_size - sdfmt->fmt_bu;
	*ptr++ = (unsigned char)size;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	/* (PB80-83)fat offset */
	size = sdfmt->fmt_bu / 2;
	*ptr++ = (unsigned char)size;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	/* (PB84-87)fat length */
	size = sdfmt->fmt_sf;
	*ptr++ = (unsigned char)size;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	/* (PB88-91)cluster heap offset */
	size = sdfmt->fmt_ssa;
	*ptr++ = (unsigned char)size;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	/* (PB92-95)cluster count */
	size = (sdfmt->area_size - sdfmt->fmt_nom - sdfmt->fmt_ssa) / sdfmt->fmt_sc;
	*ptr++ = (unsigned char)size;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	/* (PB96-99)first cluster of root directory */
	*ptr++ = 4;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	/* (PB100-103)volume serial number */
	if(sdfmt->volid_enable){
		*ptr++ = (unsigned char)sdfmt->volid;
		*ptr++ = (unsigned char)(sdfmt->volid >> 8);
		*ptr++ = (unsigned char)(sdfmt->volid >> 16);
		*ptr++ = (unsigned char)(sdfmt->volid >> 24);
	}
	else{
		*ptr++ = 0xb1;
		*ptr++ = 0x77;
		*ptr++ = 0x2f;
		*ptr++ = 0x57;
	}
	/* (PB104-105)file system revision */
	/* Ver1.00 */
	*ptr++ = 0x00;
	*ptr++ = 0x01;
	/* (PB106-107)volume flags */
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	/* (PB108)bytes per sector shift */
	*ptr++ = 9;				/* 2^9(=512)byte per sector */
	/* (PB109)sectors per cluster shift */
	for(i=0;i<16;i++){
		if((sdfmt->fmt_sc >> i) == 1){
			break;
		}
	}
	*ptr++ = i;
	/* (PB110)number of fats */
	*ptr++ = 1;
	/* (PB111)drive select */
	*ptr++ = 0x80;			/* using extended INT 13h */
	/* (PB112)percent in use */
	*ptr++ = 0;
	/* (PB113-119)reserved */
	_sd_memset(ptr,0,7);		/* all 00h */
	ptr += 7;
	/* (BP120-509)boot code */
	_sd_memset(ptr,0x00,390);	/* all 00h */
	ptr += 390;
	/* (BP510-511)boot signature */
	*ptr++ = 0x55;
	*ptr++ = 0xaa;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create Extended Boot Sector image
 * Include      : 
 * Declaration  : static int _sd_init_br_ebs(SD_FMT_WORK *sdfmt)
 * Functions    : calculate Extended Boot Sector parameter refer to sdfmt
 *              : set Extended Boot Sector parameter to image buffer
 *              : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *****************************************************************************/
static int _sd_init_br_ebs(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;

	ptr = sdfmt->pbuff;

	/* (BP0-507)extended boot code */
	_sd_memset(ptr,0,508);		/* all 00h */
	ptr += 508;
	/* (BP508-511)ExtendedBootSignature */
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x55;
	*ptr++ = 0xaa;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create Boot Checksum image
 * Include      : 
 * Declaration  : static int _sd_init_br_chksum(SD_FMT_WORK *sdfmt, unsigned long checksum)
 * Functions    : set Checksum to image buffer
 *              : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *****************************************************************************/
static int _sd_init_br_chksum(SD_FMT_WORK *sdfmt, unsigned long checksum)
{
	unsigned char *ptr;
	unsigned int i;

	ptr = sdfmt->pbuff;
	for(i=0;i<(512/4);i++){
		*ptr++ = (unsigned char)checksum;
		*ptr++ = (unsigned char)(checksum >> 8);
		*ptr++ = (unsigned char)(checksum >> 16);
		*ptr++ = (unsigned char)(checksum >> 24);
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create FS info Sector image
 * Include      : 
 * Declaration  : static int _sd_init_fsinfo(SD_FMT_WORK *sdfmt)
 * Functions    : set each fields of FS info Sector
 *              : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *****************************************************************************/
static int _sd_init_fsinfo(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;

	/* ---- initialize image buffer ---- */
	ptr = sdfmt->pbuff;
	_sd_memset(ptr,0,SD_SECTOR_SIZE);

	/* Lead Signature */
	*ptr++ = 0x52;
	*ptr++ = 0x52;
	*ptr++ = 0x61;
	*ptr++ = 0x41;
	
	ptr += 0x1e0;

	/* Structure Signature */
	*ptr++ = 0x72;
	*ptr++ = 0x72;
	*ptr++ = 0x41;
	*ptr++ = 0x61;

	/* Free Cluster Count (unknown) */
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	*ptr++ = 0xff;

	/* Next Free Cluster (unknown) */
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	
	ptr += 0xc;

	/* Trail Signature */
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x55;
	*ptr = 0xaa;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create reserved for boot sector image
 * Include      : 
 * Declaration  : static int _sd_init_rsvb( SD_FMT_WORK *sdfmt)
 * Functions    : create reserved for boot sector image
 *              : BP510 and BP511 are only specified
 *              : other field are not specified
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *****************************************************************************/
static int _sd_init_rsvb(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;
	
	ptr = sdfmt->pbuff;
	
	_sd_memset(ptr,0,SD_SECTOR_SIZE);
	
	ptr += 0x1fe;

	/* signature word */
	*ptr++ = 0x55;
	*ptr = 0xaa;
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create initial FAT sector image
 * Include      : 
 * Declaration  : static int _sd_init_fat( SD_FMT_WORK *sdfmt)
 * Functions    : set FAT Entry Value depend on the FAT type of sdfmt to image
 *              : buffer
 *              : other buffer area are initialized by zero
 *              : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : image buffer size is one sector
 *****************************************************************************/
static int _sd_init_fat(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;
	
	ptr = sdfmt->pbuff;
	
	if(sdfmt->fmt_exfat == 1){
		_sd_memset(ptr,0,sdfmt->buff_sec_size*SD_SECTOR_SIZE);

		/* The first two entries (8 bytes) in the FAT are reserved */
		*ptr++ = 0xf8;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		/* cluster2 (allocation bitmap) */
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		/* cluster3 (up-case table) */
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		/* cluster4 (root directory) */
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		
		return SD_OK;
	}

	_sd_memset(ptr,0,SD_SECTOR_SIZE);
	
	/* set FAT Entry Value */
	*ptr++ = 0xf8;
	*ptr++ = 0xff;
	*ptr++ = 0xff;

	if(sdfmt->fmt_fatbit == 16){
		*ptr++ = 0xff;
	}
	else if(sdfmt->fmt_fatbit == 32){
		*ptr++ = 0x0f;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0x0f;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0xff;
		*ptr++ = 0x0f;
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create initial Allocation Bitmap image
 * Include      : 
 * Declaration  : static int _sd_init_allocation_bitmap(SD_FMT_WORK *sdfmt)
 * Functions    : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
static int _sd_init_allocation_bitmap(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;
	
	ptr = sdfmt->pbuff;
	_sd_memset(ptr,0,sdfmt->buff_sec_size*SD_SECTOR_SIZE);

	/* records the allocation state of the clusters in the Cluster Heap */
	*ptr++ = 0x07;		/* bit0 : cluster2 (allocation bitmap) */
						/* bit1 : cluster3 (up-case table) */
						/* bit2 : cluster4 (root directory) */

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create initial Up-case Table image
 * Include      : 
 * Declaration  : static int _sd_init_upcase_table(SD_FMT_WORK *sdfmt, unsigned long secoffset)
 * Functions    : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 *              : unsigned long secoffset : sector offset
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
static int _sd_init_upcase_table(SD_FMT_WORK *sdfmt, unsigned long secoffset)
{
	unsigned char *ptr;
	int i;
	
	ptr = sdfmt->pbuff;
	_sd_memset(ptr,0,sdfmt->buff_sec_size*SD_SECTOR_SIZE);

	/* copy up-case table */
	for(i=secoffset*512/sizeof(unsigned short); i<(secoffset+1)*512/sizeof(unsigned short); i++){
		if(i>=sizeof(sd_upcase_tbl)/sizeof(unsigned short)){
			break;
		}
		*ptr++ = (unsigned char)sd_upcase_tbl[i];
		*ptr++ = (unsigned char)(sd_upcase_tbl[i] >> 8);
	}

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : create initial Root Directory image
 * Include      : 
 * Declaration  : static int _sd_init_root_directory(SD_FMT_WORK *sdfmt)
 * Functions    : 
 * Argument     : SD_FMT_WORK *sdfmt : format parameter
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       : 
 *****************************************************************************/
static int _sd_init_root_directory(SD_FMT_WORK *sdfmt)
{
	unsigned char *ptr;
	unsigned long clustercnt;
	unsigned long size;
	unsigned long checksum;
//	unsigned int i;
	
	ptr = sdfmt->pbuff;
	_sd_memset(ptr,0,sdfmt->buff_sec_size*SD_SECTOR_SIZE);

	/* ---- Allocation Bitmap Directory Entry ---- */
	/* entry type */
	*ptr++ = 0x81;
	/* bitmap flags */
	*ptr++ = 0x00;
	/* reserved */
	_sd_memset(ptr,0,18);		/* all 00h */
	ptr += 18;
	/* first cluster */
	*ptr++ = 2;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	/* data length */
	clustercnt = (sdfmt->area_size - sdfmt->fmt_nom - sdfmt->fmt_ssa) / sdfmt->fmt_sc;	/* cluster count */
	size = clustercnt / 8;		/* size = ceil(cluster count/8) */
	if(clustercnt % 8){
		size++;
	}
	*ptr++ = (unsigned char)size ;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;

	/* ---- Up-case Table Directory Entry ---- */
	/* entry type */
	*ptr++ = 0x82;
	/* reserved1 */
	_sd_memset(ptr,0,3);		/* all 00h */
	ptr += 3;
	/* table checksum */
	checksum = 0xE619D30D;
	*ptr++ = (unsigned char)checksum ;
	*ptr++ = (unsigned char)(checksum >> 8);
	*ptr++ = (unsigned char)(checksum >> 16);
	*ptr++ = (unsigned char)(checksum >> 24);
	/* reserved2 */
	_sd_memset(ptr,0,12);		/* all 00h */
	ptr += 12;
	/* first cluster */
	*ptr++ = 3;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	/* data length */
	size = sizeof(sd_upcase_tbl);
	*ptr++ = (unsigned char)size ;
	*ptr++ = (unsigned char)(size >> 8);
	*ptr++ = (unsigned char)(size >> 16);
	*ptr++ = (unsigned char)(size >> 24);
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 0;

	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : write initial FAT sector image
 * Include      : 
 * Declaration  : static int _sd_format_write(SDHNDL *hndl,SD_FMT_WORK *sdfmt,
 *              : unsigned char fill,unsigned long secno, long seccnt,
 *				:		int (*callback)(unsigned long,unsigned long))
 * Functions    : write FAT sector image saved sdfmt from secno by seccnt
 *              : if FAT sector image size is less than seccnt, remaining 
 *              : are filled with fill value, hearafter, write it card by
 *              : seccnt
 * Argument     : SDHNDL *hndl : SD handle
 *              : SD_FMT_WORK *sdfmt : format parameter
 *              : unsigned char fill : initialize value
 *              : unsigned long secno : write start physical sector number
 *              : long seccnt : number of write sectors
 *              : int (*callback)(unsigned long,unsigned long) :
 *              :     format callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       :
 *****************************************************************************/
static int _sd_format_write(SDHNDL *hndl,SD_FMT_WORK *sdfmt,
	unsigned char fill,unsigned long secno, long seccnt,
		int (*callback)(unsigned long,unsigned long))
{
	long writecnt;
	int ret;
	int flag=0;
	
	while(seccnt){	/* loop for transfer sector size */
		/* adjust transfer sector size */
		if(seccnt < sdfmt->buff_sec_size){
			writecnt = seccnt;
		}
		else{
			writecnt = sdfmt->buff_sec_size;
		}
		/* write FAT sector image to card */
		ret = _sd_write_sect(hndl,sdfmt->pbuff,secno,writecnt,SD_WRITE_WITH_PREERASE);
		if(ret != SD_OK){
			return ret;
		}
		seccnt-=writecnt;
		secno+=writecnt;

		/* call callback function */
		if(callback){
			(*callback)(secno,sdfmt->format_size);
		}
		
		/* clear buffer to next write */
		if(flag == 0){
			_sd_memset(sdfmt->pbuff,fill,sdfmt->buff_sec_size * SD_SECTOR_SIZE);
			flag = 1;
		}
	}
	
	return SD_OK;
}

/*****************************************************************************
 * ID           :
 * Summary      : erase format area previously
 * Include      : 
 * Declaration  : static int _sd_format_erase(SDHNDL *hndl,SD_FMT_WORK *sdfmt,
 *              : 	unsigned long secno, long seccnt,
 *				:		int (*callback)(unsigned long,unsigned long))
 * Functions    : erase format area previously from start sector(=secno)
 *              : by number of sectors(=seccnt)
 *              : 
 * Argument     : SDHNDL *hndl : SD handle
 *              : SD_FMT_WORK *sdfmt : format parameter
 *              : unsigned long secno : write start physical sector number
 *              : long seccnt : number of write sectors
 *              : int (*callback)(unsigned long,unsigned long) :
 *              :     format callback function
 * Return       : SD_OK : end of succeed
 *              : SD_ERR: end of error
 * Remark       :
 *****************************************************************************/
static int _sd_format_erase( SDHNDL *hndl,SD_FMT_WORK *sdfmt,
	unsigned long secno, long seccnt,
		int (*callback)(unsigned long,unsigned long))
{
	long writecnt;
	int ret=SD_OK;
	unsigned long startaddres,endaddres;
	int error;
	long erasesector;
	unsigned short startcom,endcom;

    /* partial erase is faster than all erase */
	/* check upper limit of erase sectors */
	erasesector = hndl->erase_sect;
	/* ---- supply clock (data-transfer ratio) ---- */
	if(_sd_set_clock(hndl,(int)hndl->csd_tran_speed,SD_CLOCK_ENABLE) != SD_OK){
		return hndl->error;
	}
	
	while(seccnt){
		/* ---- is stop compulsory? ---- */
		if(hndl->stop){
			hndl->stop = 0;
			_sd_set_err(hndl,SD_ERR_STOP);
			break;
		}
		
		/* ---- is card existed? ---- */
		if(_sd_check_media(hndl) != SD_OK){
			_sd_set_err(hndl,SD_ERR_NO_CARD);	/* no card */
			break;
		}

		/* erase by erase sector unit specified CSD register */
		if(seccnt < erasesector){
			if(hndl->media_type == SD_MEDIA_MMC){
				/* no remaining group */
				break;
			}
			else{
				writecnt = seccnt;
			}
		}
		else{
			writecnt = erasesector;
		}

		/* change sector address to byte one */
		if(hndl->csd_structure == 0x01){	/* HC card */
			startaddres = secno;
			endaddres = secno + writecnt - 1;
		}
		else{	/* SC card */
			startaddres = secno*512;
			endaddres = (secno + writecnt - 1)*512;
		}

		if(hndl->media_type == SD_MEDIA_MMC){
			/* issue CMD35 and CMD36 for MMC card */
			startcom = CMD35;
			endcom = CMD36;
		}
		else{
			/* issue CMD32 and CMD33 for SD Memory card */
			startcom = CMD32;
			endcom = CMD33;
		}

		/* set erase start sector */
		ret = _sd_card_send_cmd_arg(hndl,startcom,SD_RESP_R1,
			(unsigned short)(startaddres>>16),(unsigned short)startaddres);
		if( ret != SD_OK){
			goto ErrExit;
		}

		/* set erase end sector */
		ret = _sd_card_send_cmd_arg(hndl,endcom,SD_RESP_R1,
			(unsigned short )(endaddres>>16),(unsigned short)endaddres);
		if( ret != SD_OK){
			goto ErrExit;
		}

		/* execute erase (issue CMD38) */
		ret = _sd_card_send_cmd_arg(hndl,CMD38,SD_RESP_R1,0,0);
		if(ret != SD_OK){
			/* timeout error possibly occur during erase */
			if(hndl->error == SD_ERR_CARD_TOE){
				if(_sd_wait_rbusy(hndl,10000000) != SD_OK){
					goto ErrExit;
				}
			}
			else{
				goto ErrExit;
			}
		}
		
		seccnt-=writecnt;
		secno+=writecnt;

		/* call callbackfunction */
		if(callback){
			(*callback)(secno,sdfmt->format_size);
		}
		
	}
	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;

ErrExit:
	error = hndl->error;

	/* check resp end to avoid next CMD13 error */
	_sd_set_int_mask(hndl,SD_INFO1_MASK_RESP,0);	/* enable resp end */
	sddev_int_wait(hndl->sd_port, SD_TIMEOUT_RESP);				/* wait resp end */

	_sd_clear_info(hndl,SD_INFO1_MASK_TRNS_RESP,SD_INFO2_MASK_ILA);
	/* clear error information by reading card status */
	_sd_card_send_cmd_arg(hndl,CMD13,SD_RESP_R1,hndl->rca[0],0x0000);

	hndl->error = error;

	/* ---- halt clock ---- */
	_sd_set_clock(hndl,0,SD_CLOCK_DISABLE);

	return hndl->error;
}


/* End of File */
