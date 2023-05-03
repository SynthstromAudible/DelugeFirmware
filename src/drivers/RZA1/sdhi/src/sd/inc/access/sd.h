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
*  File Name   : sd.h
*  Contents    : SD Driver header file
*  Version     : 4.01.00
*  Device      : RZ/A1 Group
*  Tool-Chain  : 
*  OS          : None
*
*  Note        : 
*              
*
*  History     : May.30.2013 ver.4.00.00 Initial release
*              : Jun.30.2014 ver.4.01.00 Modified file comment
*******************************************************************************/
#ifndef _SD_H_
#define _SD_H_

#include "../sys_sel.h"

/* ==== option ==== */
#define	SD_UNMOUNT_CARD				0x00
#define	SD_MOUNT_UNLOCKED_CARD		0x01
#define	SD_MOUNT_LOCKED_CARD		0x02
#define	SD_CARD_LOCKED				0x04

/* ==== SDHI register address ==== */
#define SD_CMD				((0x00u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Command */
#define SD_ARG0				((0x04u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Command Argument (low 16bits) */
#define SD_ARG1				((0x06u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Command Argument (high 16bits) */
#define SD_STOP				((0x08u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Data Stop */
#define SD_SECCNT			((0x0au<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Block Count */
#define SD_RESP0			((0x0cu<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R23-8 */
#define SD_RESP1			((0x0eu<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R39-24 */
#define SD_RESP2			((0x10u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R55-40 */
#define SD_RESP3			((0x12u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R71-56 */
#define SD_RESP4			((0x14u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R87-72 */
#define SD_RESP5			((0x16u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R103-88 */
#define SD_RESP6			((0x18u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R119-104 */
#define SD_RESP7			((0x1au<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Response R127-120 */
#define SD_INFO1			((0x1cu<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Interrupt Flag(1) */
#define SD_INFO2			((0x1eu<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Interrupt Flag(2) */
#define SD_INFO1_MASK		((0x20u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Interrupt Flag(1) Mask */
#define SD_INFO2_MASK		((0x22u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Interrupt Flag(2) Mask */
#define SD_CLK_CTRL			((0x24u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SD Clock Control */
#define SD_SIZE				((0x26u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Block Size */
#define SD_OPTION			((0x28u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Access Option */
#define SD_ERR_STS1			((0x2cu<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* CMD,CRC,END Error Status */
#define SD_ERR_STS2			((0x2eu<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Timeout Error Status  */
#define SD_BUF0				(0x30u<<SD_REG_SHIFT)						/* SD Buffer */
#define SDIO_MODE			((0x34u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SDIO Mode */
#define SDIO_INFO1			((0x36u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SDIO Interrupt Flag */
#define SDIO_INFO1_MASK		((0x38u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SDIO Interrupt Flag Mask */
#define CC_EXT_MODE			((0xd8u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* DMA Mode Enable */
#define SOFT_RST			((0xe0u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Soft Reset */
#define VERSION				((0xe2u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* Version */
#define EXT_SWAP			((0xf0u<<SD_REG_SHIFT)+SD_BYTE_OFFSET)		/* SWAP Control */

/* ==== command type ==== */
/* ---- SD commands ---- */
#define CMD0			0u				/* GO_IDLE_STATE */
#define CMD1			1u				/* SD_SEND_OP_COND for MMC */
#define CMD2			2u				/* ALL_SEND_CID */
#define CMD3			3u				/* SEND_RELATIVE_ADDR */
#define CMD4			4u				/* SET_DSR */
#define CMD7			7u				/* SELECT/DESELECT_CARD */
#define CMD9			9u				/* SEND_CSD */
#define CMD10			10u				/* SEND_CID */
#define CMD12			12u				/* STOP_TRANSMISSION */
#define CMD13			13u				/* SEND_STATUS */
#define CMD15			15u				/* GO_INACTIVE_STATE */
#define CMD16			16u				/* SET_BLOCK_LEN */
#define CMD17			17u				/* READ_SINGLE_BLOCK */
#define CMD18			18u				/* READ_MULTIPLE_BLOCK */
#define CMD24			24u				/* WRITE_SINGLE_BLOCK */
#define CMD25			25u				/* WRITE_MULTIPLE_BLOCK */
#define CMD27			27u				/* PROGRAM_CSD */
#define CMD28			28u				/* SET_WRITE_PROT */
#define CMD29			29u				/* CLR_WRITE_PROT */
#define CMD30			30u				/* SEND_WRITE_PROT */
#define CMD32			32u				/* ERASE_WR_BLK_START */
#define CMD33			33u				/* ERASE_WR_BLK_END */
#define CMD35			35u				/* ERASE_GROUP_START */
#define CMD36			36u				/* ERASE_GROUP_END */
#define CMD38			38u				/* ERASE */
#define CMD42			42u				/* LOCK_UNLOCK */
#define CMD55			55u				/* APP_CMD */

/* ---- IO commnds ---- */	/* add for IO */
#define CMD5			0x4705u			/* IO_SEND_OP_COND */
#define CMD52_W			0x4434u			/* IO_WRITE_DIRECT */
#define CMD52_R			0x5434u			/* IO_READ_DIRECT */
#define CMD53_W_BLOCK	0x6c35u			/* IO_WRITE_EXTENDED_BLOCK */
#define CMD53_W_BYTE	0x4c35u			/* IO_WRITE_EXTENDED_BYTE */
#define CMD53_R_BLOCK	0x7c35u			/* IO_READ_EXTENDED_BLOCK */
#define CMD53_R_BYTE	0x5c35u			/* IO_READ_EXTENDED_BYTE */

/* ---- switch function command (phys spec ver1.10) ---- */
#define CMD6			0x1C06u			/* SWITCH_FUNC */

/* ---- dual voltage inquiry command (phys spec ver2.0) ---- */
#define CMD8			0x0408u			/* SEND_IF_COND */

/* ---- application specific commands ---- */
#define ACMD6			(0x40u|6u)		/* SET_BUS_WIDTH */
#define ACMD13			(0x40u|13u)		/* SD_STATUS */
#define ACMD22			(0x40u|22u)		/* SEND_NUM_WR_BLOCKS */
#define ACMD23			(0x40u|23u)		/* SET_WR_BLK_ERASE_COUNT */
#define ACMD41			(0x40u|41u)		/* SD_SEND_OP_COND */
#define ACMD42			(0x40u|42u)		/* SET_CLR_CARD_DETECT */
#define ACMD51			(0x40u|51u)		/* SEND_SCR */

/* ---- security commands (security spec ver1.01) ---- */
#define ACMD18			(0x40u|18u)		/* SECURE_READ_MULTIPLE_BLOCK */
#define ACMD25			(0x40u|25u)		/* SECURE_WRITE_MULTIPLE_BLOCK */
#define ACMD26			(0x40u|26u)		/* SECURE_WRITE_MKB */
#define ACMD38			(0x40u|38u)		/* SECURE_ERASE */
#define ACMD43			(0x40u|43u)		/* GET_MKB */
#define ACMD44			(0x40u|44u)		/* GET_MID */
#define ACMD45			(0x40u|45u)		/* SET_CER_RN1 */
#define ACMD46			(0x40u|46u)		/* GET_CER_RN2 */
#define ACMD47			(0x40u|47u)		/* SET_CER_RES2 */
#define ACMD48			(0x40u|48u)		/* GET_CER_RES1 */
#define ACMD49			(0x40u|49u)		/* CHANGE_SECURE_AREA */

/* ==== constants ==== */
/* --- command arg --- */
#define ARG_ACMD6_1bit		0
#define ARG_ACMD6_4bit		2

/* ---- response type  ---- */
#define SD_RESP_NON			0			/* no response */
#define SD_RESP_R1			1			/* nomal response */
#define SD_RESP_R1b			2			/* nomal response with an optional busy signal */
#define SD_RESP_R1_SCR		3			/* nomal response with an optional busy signal */
#define SD_RESP_R2_CID		4			/* CID register */
#define SD_RESP_R2_CSD		5			/* CSD register */
#define SD_RESP_R3			6			/* OCR register */
#define SD_RESP_R6			7			/* Published RCA response */
#define SD_RESP_R4			8			/* IO OCR register */
#define SD_RESP_R5			9			/* IO RW response */
#define SD_RESP_R7			10			/* Card Interface Condition response */

/* --- R1 response error bit ---- */
#define RES_SW_INTERNAL				0xe8400000ul	/* Driver illegal process */
													/* OUT_OF_RANGE */
													/* ADDRESS_ERROR */
													/* BLOCK_LEN_ERROR */
													/* ERASE_PARAM */
													/* RES_ILLEGAL_COMMAND */
#define RES_ERASE_SEQ_ERROR			0x10008000ul	/* ERASE_SEQ_ERROR + WP_ERASE_SKIP */
#define RES_WP_VIOLATION			0x04000000ul
#define RES_CARD_IS_LOCKED			0x02000000ul
#define RES_CARD_UNLOCKED_FAILED	0x01000000ul
#define RES_COM_CRC_ERROR			0x00800000ul
#define RES_CARD_ECC_FAILED			0x00200000ul
#define RES_CC_ERROR				0x00100000ul
#define RES_ERROR					0x00080000ul
#define RES_AKE_SEQ_ERROR			0x00000008ul
#define RES_STATE					0x00001e00ul

/* --- current_state --- */
#define STATE_IDEL			0
#define STATE_READY			(1u<<9u)
#define STATE_IDENT			(2u<<9u)
#define STATE_STBY			(3u<<9u)
#define STATE_TRAN			(4u<<9u)
#define STATE_DATA			(5u<<9u)
#define STATE_RCV			(6u<<9u)
#define STATE_PRG			(7u<<9u)
#define STATE_DIS			(8u<<9u)

/* ---- maximum block count per multiple command ---- */
#define TRANS_SECTORS		(hndl->trans_sectors)	/* max 65535 blocks */
#define TRANS_BLOCKS		(hndl->trans_blocks)	/* max 65535 blocks */

/* ---- set block address, if HC card ---- */
#define SET_ACC_ADDR		((hndl->csd_structure == 0x01) ? (psn) : (psn*512))

/* ---- SD clock control ---- */
#define SD_CLOCK_ENABLE		1	/* supply clock */
#define SD_CLOCK_DISABLE	0	/* halt clock */

/* ---- info1 interrupt mask ---- */
#define SD_INFO1_MASK_DET_DAT3		0x0300u		/* Card Insert and Remove (DAT3) */
#define SD_INFO1_MASK_DET_CD		0x0018u		/* Card Insert and Remove (CD) */
#define SD_INFO1_MASK_INS_DAT3		0x0200u		/* Card Insert (DAT3) */
#define SD_INFO1_MASK_INS_CD		0x0010u		/* Card Insert (CD) */
#define SD_INFO1_MASK_REM_DAT3		0x0100u		/* Card Remove (DAT3) */
#define SD_INFO1_MASK_REM_CD		0x0008u		/* Card Remove (CD) */
#define SD_INFO1_MASK_DATA_TRNS		0x0004u		/* Command sequence end */
#define SD_INFO1_MASK_TRNS_RESP		0x0005u		/* Command sequence end and Response end */
#define SD_INFO1_MASK_RESP			0x0001u		/* Response end */
#define SD_INFO1_MASK_DET_DAT3_CD	(SD_INFO1_MASK_DET_DAT3|SD_INFO1_MASK_DET_CD)

/* ---- info2 interrupt mask ---- */
#define SD_INFO2_MASK_BWE			0x827fu		/* Write enable and All errors */
#define SD_INFO2_MASK_BRE			0x817fu		/* Read enable and All errors */
#define SD_INFO2_MASK_ERR			0x807fu		/* All errors */
#define SD_INFO2_MASK_ILA			0x8000u
#define SD_INFO2_MASK_CBSY			0x4000u		/* Command type register busy */
#define SD_INFO2_MASK_SCLKDIVEN		0x2000u		/* SD bus busy */
#define SD_INFO2_MASK_ERR6			0x0040u
#define SD_INFO2_MASK_ERR5			0x0020u
#define SD_INFO2_MASK_ERR4			0x0010u
#define SD_INFO2_MASK_ERR3			0x0008u
#define SD_INFO2_MASK_ERR2			0x0004u
#define SD_INFO2_MASK_ERR1			0x0002u
#define SD_INFO2_MASK_ERR0			0x0001u
#define SD_INFO2_MASK_WE			0x0200u		/* Write enable */
#define SD_INFO2_MASK_RE			0x0100u		/* Read enable */

/* ---- sdio_info interrupt mask ---- */
#define SDIO_INFO1_MASK_EXWT		0x8000u
#define SDIO_INFO1_MASK_EXPUB52		0x4000u
#define SDIO_INFO1_MASK_IOIRQ		0x0001u		/* interrupt from IO Card */

/* ---- ext_cd interrupt mask ---- */
#define SD_EXT_CD_MASK_DET_P1		0x0003u
#define SD_EXT_CD_MASK_DET_P2		0x0018u
#define SD_EXT_CD_MASK_DET_P3		0x00c0u

#define SD_EXT_CD_MASK_CD_P1		0x0004u
#define SD_EXT_CD_MASK_CD_P2		0x0020u
#define SD_EXT_CD_MASK_CD_P3		0x0100u


/* ---- sdio mode ---- */
#define SDIO_MODE_C52PUB			0x0200u
#define SDIO_MODE_IOABT				0x0100u
#define SDIO_MODE_RWREQ				0x0004u
#define SDIO_MODE_IOMOD				0x0001u		/* interrupt from IO Card */

/* ---- cc extmode register ---- */
#define CC_EXT_MODE_DMASDRW			0x0002u

/* ---- time out count ---- */
#define SD_TIMEOUT_CMD				100			/* commnad timeout */
#define SD_TIMEOUT_MULTIPLE			1000		/* block transfer timeout */
#define SD_TIMEOUT_RESP				1000		/* command sequence timeout */
#define SD_TIMEOUT_DMA_END			1000		/* DMA transfer timeout */
#define SD_TIMEOUT_ERASE_CMD		10000		/* erase timeout */
#define SD_TIMEOUT_PROG_CMD			10000		/* programing timeout */

/* ---- data transafer direction ---- */
#define SD_TRANS_READ				0			/* Host <- SD */
#define SD_TRANS_WRITE				1			/* SD -> Host */

/* ---- card register size ---- */
#define STATUS_DATA_BYTE			64			/* STATUS_DATA size */
#define SD_STATUS_BYTE				64			/* SD STATUS size */
#define SD_SCR_REGISTER_BYTE		8			/* SCR register size */

/* ---- area distinction ---- */
#define SD_USER_AREA				1u
#define SD_PROT_AREA				2u

/* --- SD specification version ---- */
#define SD_SPEC_10					0			/* SD physical spec 1.01 (phys spec ver1.01) */
#define SD_SPEC_11					1			/* SD physical spec 1.10 (phys spec ver1.10) */
#define SD_SPEC_20					2			/* SD physical spec 2.00 (phys spec ver2.00) */

/* --- SD Card Speed ---- */
#define SD_CUR_SPEED				0x01u		/* current speed mode */
#define SD_SUP_SPEED				0x10u		/* supported speed mode */

/* ==== format parameter ==== */
#define SIZE_CARD_256KB			(256*1024/512)					/*  256*1KB/(sector size) */
#define SIZE_CARD_1MB			(1024*1024/512)					/* 1024*1KB/(sector size) */
#define SIZE_CARD_2MB			(2*1024*1024/512)				/*    2*1MB/(sector size) */
#define SIZE_CARD_4MB			(4*1024*1024/512)				/*    4*1MB/(sector size) */
#define SIZE_CARD_8MB			(8*1024*1024/512)				/*    8*1MB/(sector size) */
#define SIZE_CARD_16MB			(16*1024*1024/512)				/*   16*1MB/(sector size) */
#define SIZE_CARD_32MB			(32*1024*1024/512)				/*   32*1MB/(sector size) */
#define SIZE_CARD_64MB			(64*1024*1024/512)				/*   64*1MB/(sector size) */
#define SIZE_CARD_128MB			(128*1024*1024/512)				/*  128*1MB/(sector size) */
#define SIZE_CARD_256MB			(256*1024*1024/512)				/*  256*1MB/(sector size) */
#define SIZE_CARD_504MB			(504*1024*1024/512)				/*  504*1MB/(sector size) */
#define SIZE_CARD_1008MB		(1008*1024*1024/512)			/* 1008*1MB/(sector size) */
#define SIZE_CARD_1024MB		(1024*1024*1024/512)			/* 1024*1MB/(sector size) */
#define SIZE_CARD_2016MB		(2016*1024*1024/512)			/* 2016*1MB/(sector size) */
#define SIZE_CARD_2048MB		(2048ul*1024ul*1024ul/512ul)	/* 2048*1MB/(sector size) */
#define SIZE_CARD_4032MB		(4032ul*1024ul*2ul)				/* 4032*(1MB/sector size) */
#define SIZE_CARD_4096MB		(4096ul*1024ul*2ul)				/* 4096*(1MB/sector size) */
#define SIZE_CARD_8192MB		(8192ul*1024ul*2ul)				/* 2048*(1MB/sector size) */
#define SIZE_CARD_16384MB		(16384ul*1024ul*2ul)			/* 2048*(1MB/sector size) */
#define SIZE_CARD_32768MB		(32768ul*1024ul*2ul)			/* 2048*(1MB/sector size) */
#define SIZE_CARD_128GB			(128ul*1024ul*1024ul*2ul)		/*  128*(1GB/sector size) */
#define SIZE_CARD_512GB			(512ul*1024ul*1024ul*2ul)		/*  512*(1GB/sector size) */
#define SIZE_CARD_2TB			0xFFFFFFFF						/*    2*(1TB/sector size) over 32bit max value! */

#define NUM_HEAD_2				2
#define NUM_HEAD_4				4
#define NUM_HEAD_8				8
#define NUM_HEAD_16				16
#define NUM_HEAD_32				32
#define NUM_HEAD_64				64
#define NUM_HEAD_128			128
#define NUM_HEAD_255			255

#define SEC_PER_TRACK_16		16
#define SEC_PER_TRACK_32		32
#define SEC_PER_TRACK_63		63

#define SEC_PER_CLUSTER_1		1
#define SEC_PER_CLUSTER_2		2
#define SEC_PER_CLUSTER_8		8
#define SEC_PER_CLUSTER_16		16
#define SEC_PER_CLUSTER_32		32
#define SEC_PER_CLUSTER_64		64
#define SEC_PER_CLUSTER_256		256
#define SEC_PER_CLUSTER_512		512
#define SEC_PER_CLUSTER_1024	1024

/* Boundary Unit Size(sectors) */
#define SIZE_OF_BU_1			1
#define SIZE_OF_BU_2			2
#define SIZE_OF_BU_8			8
#define SIZE_OF_BU_16			16
#define SIZE_OF_BU_32			32
#define SIZE_OF_BU_64			64
#define SIZE_OF_BU_128			128
#define SIZE_OF_BU_8192			8192
#define SIZE_OF_BU_32768		32768
#define SIZE_OF_BU_65536		65536
#define SIZE_OF_BU_131072		131072

#define SD_SECTOR_SIZE			512

/* Maximum AU size */
#define SD_ERASE_SECTOR			((4096*1024)/512)

#define SCLKDIVEN_LOOP_COUNT	10000	/* check SCLKDIVEN bit loop count */

/* ==== macro functions ==== */
#define _sd_get_hndls(a)		SDHandle[a]

#define sd_outp(h,offset,data)	(*(volatile unsigned short *)((h)->reg_base+(offset)) = (data))
#define sd_inp(h,offset)		(*(volatile unsigned short *)((h)->reg_base+(offset)))

/* ==== command type ==== */
/* ---- eSD commands ---- */
#define CMD43			0x052B			/* SELECT_PARTITIONS */
#define CMD44			0x0C2C			/* MANAGE_PARTITIONS */
#define CMD45			0x1C2D			/* QUERY_PARTITIONS */

/* ==== constants ==== */
#define SD_QUERY_PARTITION_LIST_BYTE		512
#define SD_SPLIT_PARTITION_BYTE				512

#define SDIO_INTERNAL_REG_SIZE		0x20
#define SDIO_INTERNAL_CIS_SIZE		0x20

/* ==== encrypted device key structure ==== */
/* ==== format parameter structure ==== */
typedef struct _chs_recom{
	unsigned long capa;
	unsigned char heads;
	unsigned char spt;
}CHS_RECOM;

typedef struct _scbu_recom{
	unsigned long capa;
	unsigned short sc;
	unsigned long bu;
}SCBU_RECOM;

/* ==== SD Driver work buffer (allocated by File system) ==== */
typedef struct __sdhndl{				/* SD handle */
	unsigned long	reg_base;			/* SDHI base address */
	unsigned long	card_sector_size;	/* sector size (user area) */
	unsigned long	prot_sector_size;	/* sector size (protect area) */
	unsigned long	erase_sect;			/* erase block size */
	unsigned char	fat_type;			/* FAT type (FAT12:1 FAT16:2 FAT32:3 unknown:0)  */
	unsigned char	csd_structure;		/* CSD structure (Standard capacity:0 High capacity:1) */
	unsigned char	csd_tran_speed;		/* CSD transfer speed */
	unsigned short	csd_ccc;			/* CSD command class */
	unsigned char	csd_copy;			/* CSD copy flag (not used) */
	unsigned char	csd_file_format;	/* CSD file format group */
	unsigned char	sd_spec;			/* SCR spec version (Ver1.0-1.01:0 Ver1.10:1) */
	unsigned char	if_mode;			/* bus width (1bit:0 4bits:1) */
	unsigned char	speed_mode;			/* card speed mode;					*/
										/*			current speed  : 0		*/
                                        /*			supported speed:0x10 	*/
	unsigned char	speed_class;		/* card speed class */
	unsigned char	perform_move;		/* card move performance */
	unsigned char	media_type;			/* card type */
	unsigned char	write_protect;		/* write protect:       OFF : 0 	*/
										/*                   H/W WP : 1 	*/
										/*   CSD  TMP_WRITE_PROTECT : 2 	*/
										/*   CSD PERM_WRITE_PROTECT : 4 	*/
										/*   SD ROM                 : 0x10	*/
	unsigned char	io_flag;			/* io initialize flag */
										/*	interrupt enable        : bit4 	*/
										/*	power on initialized    : bit2 	*/
										/*	memory initialized 		: bit1 	*/
										/*	io func initialized	    : bit0	*/
	unsigned char	io_info;			/* io function's information */
										/*	io ready			: bit7									*/
										/*	number of io func 	: bit6-bit4 memory present 	    : bit3 	*/
										/*	reserved 			: bit2-bit0 							*/
	unsigned short	int_info1;			/* SD_INFO1 status */
	unsigned short	int_info2;			/* SD_INFO2 status */
	unsigned short	int_info1_mask;		/* SD_INFO1_MASK status */
	unsigned short	int_info2_mask;		/* SD_INFO2_MASK status */
	unsigned short	int_io_info;		/* SDIO_INFO1 status */
	unsigned short	int_io_info_mask;	/* SDIO_INFO1_MASK status */
	unsigned long	voltage;			/* system supplied voltage */
	int				error;				/* error detail information */
	unsigned short	stop;				/* compulsory stop flag */
	unsigned char	mount;				/* mount flag (mount:0 unmount:1) */
	unsigned char	int_mode;			/* interrupt flag detect method (polling:0 H/W interrupt:1) */
	unsigned char	trans_mode;			/* data transfer method  PIO : 0	*/
										/*                SD_BUF DMA : 2 	*/
	unsigned char	sup_card;			/* support card; 				*/
										/*  Memory (include MMC) : 0	*/
										/*	                  IO : 1 	*/
	unsigned char	sup_speed;			/* support speed (Default:0 High-speed:1) */
	unsigned char	sup_ver;			/* support version (ver1.1:0 ver2.x:1) */
	unsigned char	cd_port;			/* card detect method (CD pin:0 DAT3:1) */
	unsigned char	sd_port;			/* card port number */
	short			trans_sectors;							/* maximum block counts per multiple command */
	short			trans_blocks;							/* maximum block counts per multiple command */
	int 			(*int_cd_callback)(int,int);			/* callback function for card detection */
	int 			(*int_format_callback)(int);			/* callback function for card format */
	int 			(*int_callback)(int,int);				/* callback function for interrupt flags */
	int 			(*int_io_callback)(int);				/* callback function for interrupt flags */
	unsigned long 	resp_status;							/* R1/R1b response status */
	unsigned short	ocr[4/sizeof(unsigned short)];			/* OCR value */
	unsigned short	io_ocr[4/sizeof(unsigned short)];		/* IO OCR value */
	unsigned short	if_cond[4/sizeof(unsigned short)];		/* IF_COND value */
	unsigned short	cid[16/sizeof(unsigned short)];			/* CID value */
	unsigned short	csd[16/sizeof(unsigned short)];			/* CSD value */
	unsigned short	dsr[2/sizeof(unsigned short)];			/* DSR value */
	unsigned short	rca[4/sizeof(unsigned short)];			/* RCA value */
	unsigned short	scr[8/sizeof(unsigned short)];			/* SCR value */
	unsigned short	sdstatus[14/sizeof(unsigned short)];	/* SD STATUS value */
	unsigned short	status_data[18/sizeof(unsigned short)];	/* STATUS DATA value (phys spec ver1.10) */
	unsigned short  io_len[8];								/* io block length common:0 func:more than 1 */
	unsigned char	io_reg[8][SDIO_INTERNAL_REG_SIZE/sizeof(unsigned char)];	/* CCCR(=0) and FBR(1 to 7) value */
	unsigned char	cis[8][SDIO_INTERNAL_CIS_SIZE/sizeof(unsigned char)];		/* CIS value (to be fixed) */
	unsigned short	io_abort[8];		/* compulsory stop flag */
	unsigned char	*rw_buff;								/* work buffer pointer */
	unsigned long	buff_size;								/* work buffer size */
	int				sup_if_mode;							/* supported bus width (1bit:0 4bits:1) */
	int				partition_id;							/* Partition ID for eSD */
	unsigned long	partition_sector_size[8];				/* CHG01 Partition sector size */
}SDHNDL;

extern SDHNDL *SDHandle[NUM_PORT];

/* ---- variables used for format ---- */
typedef struct _sd_format_param{
	/* format work buffer */
	unsigned char *pbuff;			/* work buffer address */
	unsigned long buff_sec_size;	/* work buffer size (sector) */
	
	/* format parameter */
	unsigned long	area_size;		/* sectors per area(user/protect) */
	unsigned long	format_size;	/* number of format sectors */

	unsigned short	fmt_spt;		/* number of tracks */
	unsigned short	fmt_hn;			/* number of heads */
	unsigned short	fmt_sc;			/* sectors per cluster */
	unsigned long	fmt_bu;			/* boundary unit (sector) */

	unsigned long	fmt_max;		/* max cluster number */
	unsigned long	fmt_sf;			/* sectors per FAT 
									   FAT12 and FAT16: BP22-BP23
									   FAT 32 : BP36-BP39
									*/

	unsigned long	fmt_nom;		/* sectors per MBR */
	unsigned long	fmt_ssa;		/* sectors per sysytem area */

	unsigned char	fmt_fatbit;		/* FAT12, FAT16 or FAT32 */
	unsigned char	fmt_exfat;		/* exFAT format or not (not exFAT:0 exFAT:1) */
	unsigned short	fmt_rsc;		/* reserved sector count */
	int				volid_enable;	/* 'volid' is enable (disable:0 enable:1) */
	unsigned long	volid;			/* volume ID number or volume serial number */

	CHS_RECOM *chs;
	SCBU_RECOM *scbu;
	int area;						/* format area */
	/* format write function */
	int (*write)(SDHNDL *hndl,struct _sd_format_param *sdfmt,
		unsigned char fill,
			unsigned long secno, long seccnt,
				int (*callback)(unsigned long,unsigned long));
	/* format erase function  */
	int (*erase)(SDHNDL *hndl,struct _sd_format_param *sdfmt,
		unsigned long secno, long seccnt,
			int (*callback)(unsigned long,unsigned long));
}SD_FMT_WORK;

/* ==== proto type ==== */
/* ---- sd_init.c ---- */
int _sd_init_hndl(SDHNDL *hndl,unsigned long mode,unsigned long voltage);
int _sd_select_port(SDHNDL *hndl,int portno);

/* ---- sd_mount.c ---- */
int _sd_card_init(SDHNDL *hndl);
int _sd_io_mount(SDHNDL *hndl);
int _sd_mem_mount(SDHNDL *hndl);
int _sd_set_io_speed(SDHNDL *hndl);
int _sd_set_mem_speed(SDHNDL *hndl);
int _sd_card_switch_func(SDHNDL *hndl,unsigned short h_arg,
	unsigned short l_arg);
int _sd_card_switch_func_access_mode0(SDHNDL *hndl,unsigned short h_arg,unsigned short l_arg);
int _sd_card_switch_func_access_mode1(SDHNDL *hndl,unsigned short h_arg,unsigned short l_arg);
int _sd_card_get_status(SDHNDL *hndl);
int _sd_card_get_scr(SDHNDL *hndl);
int _sd_read_byte(SDHNDL *hndl,unsigned short cmd,unsigned short h_arg,
	unsigned short l_arg,unsigned char *readbuff,unsigned short byte);
int _sd_write_byte(SDHNDL *hndl,unsigned short cmd,unsigned short h_arg,
	unsigned short l_arg,unsigned char *writebuff,unsigned short byte);
int _sd_card_select_partition(SDHNDL *hndl, int id);
int _esd_get_partition_id(SDHNDL *hndl, int *id);

/* ---- sd_trns.c ---- */
int _sd_software_trans(SDHNDL *hndl,unsigned char *buff,long cnt,int dir);
int _sd_dma_trans(SDHNDL *hndl,long cnt);

/* ---- sdio_trns.c ---- */
int _sdio_software_trans(SDHNDL *hndl,unsigned char *buff,long cnt,int dir,unsigned short blocklen);
int _sdio_software_trans2(SDHNDL *hndl,unsigned char *buff,long cnt,int dir);
int _sdio_dma_trans(SDHNDL *hndl,long cnt,unsigned short blocklen);

/* ---- sd_read.c ---- */
 /* no function */

/* ---- sd_write.c ---- */
int _sd_write_sect(SDHNDL *hndl,unsigned char *buff,unsigned long psn,
	long cnt,int writemode);

/* ---- sd_io_read.c ---- */
int _sdio_read(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,long cnt,unsigned long op_code,unsigned short blocklen);

int _sdio_read_byte(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,long cnt,unsigned long op_code);

/* ---- sd_io_write.c ---- */
int _sdio_write(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,long cnt,unsigned long op_code,unsigned short blocklen);

int _sdio_write_byte(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,long cnt,unsigned long op_code);

/* ---- sd_io_direct.c ---- */
int _sdio_direct(SDHNDL *hndl,unsigned char *buff,unsigned long func,
	unsigned long adr,unsigned long rw_flag,unsigned long raw_flag);

/* ---- sd_cd.c ---- */
int _sd_check_media(SDHNDL *hndl);
int _sd_get_ext_cd_int(SDHNDL *hndl);

/* ---- sd_cmd.c ---- */
int _sd_send_cmd(SDHNDL *hndl,unsigned short cmd);
int _sd_send_acmd(SDHNDL *hndl,unsigned short cmd,unsigned short h_arg,unsigned short l_arg);
int _sd_send_mcmd(SDHNDL *hndl,unsigned short cmd,unsigned long startaddr);
int _sd_card_send_cmd_arg(SDHNDL *hndl,unsigned short cmd, int resp,unsigned short h_arg,unsigned short l_arg);
void _sd_set_arg(SDHNDL *hndl,unsigned short h_arg,unsigned short l_arg);
int _sd_card_send_ocr(SDHNDL *hndl,int type);
int _sd_check_resp_error(SDHNDL *hndl);
int _sd_get_resp(SDHNDL *hndl,int resp);
int _sd_check_csd(SDHNDL *hndl);
int _sd_check_info2_err(SDHNDL *hndl);
int _sd_send_iocmd(SDHNDL *hndl,unsigned short cmd,unsigned long arg);

/* ---- sd_int.c ---- */
int	_sd_set_int_mask(SDHNDL *hndl,unsigned short mask1,unsigned short mask2);
int	_sd_clear_int_mask(SDHNDL *hndl,unsigned short mask1,
	unsigned short mask2);
int _sd_get_int(SDHNDL *hndl);
int	_sd_clear_info(SDHNDL *hndl,unsigned short clear_info1,
	unsigned short clear_info2);

/* ---- sdio_int.c ---- */
int	_sdio_set_int_mask(SDHNDL *hndl,unsigned short mask);
int	_sdio_clear_int_mask(SDHNDL *hndl,unsigned short mask);
int	_sdio_clear_info(SDHNDL *hndl,unsigned short clear);
int _sdio_get_int(SDHNDL *hndl);

/* ---- sd_util.c ---- */
int _sd_set_clock(SDHNDL *hndl,int clock,int enable);
int _sd_set_port(SDHNDL *hndl,int port);
int _sd_iswp(SDHNDL *hndl);
int _sd_set_err(SDHNDL *hndl,int error);
int _sd_bit_search(unsigned short data);
int _sd_get_size(SDHNDL *hndl,unsigned int area);
int _sd_standby(SDHNDL *hndl);
int _sd_active(SDHNDL *hndl);
int _sd_inactive(SDHNDL *hndl);
int _sdio_set_blocklen(SDHNDL *hndl,unsigned short len,unsigned long func);
int _sd_memset(unsigned char *p,unsigned char data, unsigned long cnt);
int _sd_memcpy(unsigned char *dst,unsigned char *src,unsigned long cnt);
unsigned short _sd_rand(void);
void _sd_srand(unsigned long seed);
int _sd_wait_rbusy(SDHNDL *hndl,int time);

#endif	/* _SD_H_	*/

/* End of File */
