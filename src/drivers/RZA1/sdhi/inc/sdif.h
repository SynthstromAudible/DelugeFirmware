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
*  File Name   : sdif.h
*  Abstract    : SD Memory card driver I/F
*  Version     : 4.00.01
*  Device      : RZ/A1 Group
*  Tool-Chain  : 
*  OS          : None
*
*  Note        : 
*              
*
*  History     : May.30.2013 ver.4.00.00 Initial release
*              : Jun.30.2014 ver.4.00.01 Modified file comment
*******************************************************************************/
#ifndef _SDDRV_H_
#define _SDDRV_H_

#ifdef    __cplusplus
extern "C" {
#endif    /* __cplusplus    */

#include "r_typedefs.h"

/* ==== define  ==== */
/* ---- SD Driver work buffer ---- */
#define SD_SIZE_OF_INIT           800

/* ---- error code ---- */
#define SD_OK_LOCKED_CARD         1                  /* OK but card is locked status */
#define SD_OK                     0                  /* OK */
#define SD_ERR                    -1                 /* general error */
#define SD_ERR_WP                 -2                 /* write protect error */
#define SD_ERR_RO                 -3                 /* read only error */
#define SD_ERR_RES_TOE            -4                 /* response time out error */
#define SD_ERR_CARD_TOE           -5                 /* card time out error */
#define SD_ERR_END_BIT            -6                 /* end bit error */
#define SD_ERR_CRC                -7                 /* CRC error */
#define SD_ERR_CARD_RES           -8                 /* card response error */
#define SD_ERR_HOST_TOE           -9                 /* host time out error */
#define SD_ERR_CARD_ERASE         -10                /* card erase error */
#define SD_ERR_CARD_LOCK          -11                /* card lock error */
#define SD_ERR_CARD_UNLOCK        -12                /* card unlock error */
#define SD_ERR_HOST_CRC           -13                /* host CRC error */
#define SD_ERR_CARD_ECC           -14                /* card internal ECC error */
#define SD_ERR_CARD_CC            -15                /* card internal error */
#define SD_ERR_CARD_ERROR         -16                /* unknown card error */
#define SD_ERR_CARD_TYPE          -17                /* non support card type */
#define SD_ERR_NO_CARD            -18                /* no card */
#define SD_ERR_ILL_READ           -19                /* illegal buffer read */
#define SD_ERR_ILL_WRITE          -20                /* illegal buffer write */
#define SD_ERR_AKE_SEQ            -21                /* the sequence of authentication process */
#define SD_ERR_OVERWRITE          -22                /* CID/CSD overwrite error */
/* 23-29 */
#define SD_ERR_CPU_IF             -30                /* target CPU interface function error  */
#define SD_ERR_STOP               -31                /* user stop */
/* 32-49 */
#define SD_ERR_CSD_VER            -50                /* CSD register version error */
#define SD_ERR_SCR_VER            -51                /* SCR register version error */
#define SD_ERR_FILE_FORMAT        -52                /* CSD register file format error  */
#define SD_ERR_NOTSUP_CMD         -53                /* not supported command  */
/* 54-59 */
#define SD_ERR_ILL_FUNC           -60                /* invalid function request error */
#define SD_ERR_IO_VERIFY          -61                /* direct write verify error */
#define SD_ERR_IO_CAPAB           -62                /* IO capability error */
/* 63-69 */
#define SD_ERR_IFCOND_VER         -70                /* Interface condition version error */
#define SD_ERR_IFCOND_VOLT        -71                /* Interface condition voltage error */
#define SD_ERR_IFCOND_ECHO        -72                /* Interface condition echo back pattern error */
/* 73-79 */
#define SD_ERR_OUT_OF_RANGE       -80                /* the argument was out of range */
#define SD_ERR_ADDRESS_ERROR      -81                /* misassigned address */
#define SD_ERR_BLOCK_LEN_ERROR    -82                /* transfered block length is not allowed */
#define SD_ERR_ILLEGAL_COMMAND    -83                /* Command not legal  */
#define SD_ERR_RESERVED_ERROR18   -84                /* Reserved bit 18 Error */
#define SD_ERR_RESERVED_ERROR17   -85                /* Reserved bit 17 Error */
#define SD_ERR_CMD_ERROR          -86                /* SD_INFO2 bit  0 CMD error */
#define SD_ERR_CBSY_ERROR         -87                /* SD_INFO2 bit 14 CMD Type Reg Busy error */
#define SD_ERR_NO_RESP_ERROR      -88                /* SD_INFO1 bit  0 No Response error */
/* 89 */
/* 90-95 */
#define SD_ERR_ERROR              -96                /* SDIO ERROR */
#define SD_ERR_FUNCTION_NUMBER    -97                /* SDIO FUNCTION NUMBER ERROR */
#define SD_ERR_COM_CRC_ERROR      -98                /* SDIO CRC ERROR */
#define SD_ERR_INTERNAL           -99                /* driver software internal error */

/* ---- driver mode ---- */
#define SD_MODE_POLL              0x0000ul           /* status check mode is software polling */
#define SD_MODE_HWINT             0x0001ul           /* status check mode is hardware interrupt */
#define SD_MODE_SW                0x0000ul           /* data transfer mode is software */
#define SD_MODE_DMA               0x0002ul           /* data transfer mode is DMA */
#define SD_MODE_DMA_64            0x0004ul           /* data transfer mode is DMA with 64 byte burst mode */

/* ---- support mode ---- */
#define SD_MODE_MEM               0x0000ul           /* memory cards only are supported */
#define SD_MODE_IO                0x0010ul           /* memory and io cards are supported */
#define SD_MODE_COMBO             0x0030ul           /* memory ,io and combo cards are supported */
#define SD_MODE_DS                0x0000ul           /* only default speed mode is supported */
#define SD_MODE_HS                0x0040ul           /* high speed mode is also supported */
#define SD_MODE_VER1X             0x0000ul           /* ver1.1 host */
#define SD_MODE_VER2X             0x0080ul           /* ver2.x host (high capacity and dual voltage) */
#define SD_MODE_1BIT              0x0100ul           /* SD Mode 1bit only is supported */
#define SD_MODE_4BIT              0x0000ul           /* SD Mode 1bit and 4bit is supported */

/* ---- media voltage ---- */
#define SD_VOLT_1_7               0x00000010ul       /* low voltage card minimum */
#define SD_VOLT_1_8               0x00000020ul
#define SD_VOLT_1_9               0x00000040ul
#define SD_VOLT_2_0               0x00000080ul
#define SD_VOLT_2_1               0x00000100ul       /* basic communication minimum */
#define SD_VOLT_2_2               0x00000200ul
#define SD_VOLT_2_3               0x00000400ul
#define SD_VOLT_2_4               0x00000800ul
#define SD_VOLT_2_5               0x00001000ul
#define SD_VOLT_2_6               0x00002000ul
#define SD_VOLT_2_7               0x00004000ul
#define SD_VOLT_2_8               0x00008000ul       /* memory access minimum */
#define SD_VOLT_2_9               0x00010000ul
#define SD_VOLT_3_0               0x00020000ul
#define SD_VOLT_3_1               0x00040000ul
#define SD_VOLT_3_2               0x00080000ul
#define SD_VOLT_3_3               0x00100000ul
#define SD_VOLT_3_4               0x00200000ul
#define SD_VOLT_3_5               0x00400000ul
#define SD_VOLT_3_6               0x00800000ul

/* ---- memory card write mode ---- */
#define SD_WRITE_WITH_PREERASE    0x0000u            /* pre-erease write */
#define SD_WRITE_OVERWRITE        0x0001u            /* overwrite  */

/* ---- io register write mode ---- */
#define SD_IO_SIMPLE_WRITE        0x0000u            /* just write */
#define SD_IO_VERIFY_WRITE        0x0001u            /* read after write */

/* ---- io operation code ---- */
#define SD_IO_FIXED_ADDR          0x0000u            /* R/W fixed address */
#define SD_IO_INCREMENT_ADDR      0x0001u            /* R/W increment address */
#define SD_IO_FORCE_BYTE          0x0010u            /* byte access only  */

 /* ---- media type ---- */
#define SD_MEDIA_UNKNOWN          0x0000u            /* unknown media */
#define SD_MEDIA_MMC              0x0010u            /* MMC card */
#define SD_MEDIA_SD               0x0020u            /* SD Memory card */
#define SD_MEDIA_IO               0x0001u            /* SD IO card */
#define SD_MEDIA_MEM              0x0030u            /* Memory card */
#define SD_MEDIA_COMBO            0x0021u            /* SD COMBO card */
#define SD_MEDIA_EMBEDDED         0x8000u            /* embedded media */

/* ---- write protect info --- */
#define SD_WP_OFF                 0x0000u            /* card is not write protect */
#define SD_WP_HW                  0x0001u            /* card is H/W write protect */
#define SD_WP_TEMP                0x0002u            /* card is TEMP_WRITE_PROTECT */
#define SD_WP_PERM                0x0004u            /* card is PERM_WRITE_PROTECT */
#define SD_WP_ROM                 0x0010u            /* card is SD-ROM */

/* ---- SD clock div ---- */    /* IMCLK is host controller clock */
#define SD_DIV_512                0x0080u            /* SDCLOCK = IMCLK/512 */
#define SD_DIV_256                0x0040u            /* SDCLOCK = IMCLK/256 */
#define SD_DIV_128                0x0020u            /* SDCLOCK = IMCLK/128 */
#define SD_DIV_64                 0x0010u            /* SDCLOCK = IMCLK/64 */
#define SD_DIV_32                 0x0008u            /* SDCLOCK = IMCLK/32 */
#define SD_DIV_16                 0x0004u            /* SDCLOCK = IMCLK/16 */
#define SD_DIV_8                  0x0002u            /* SDCLOCK = IMCLK/8 */
#define SD_DIV_4                  0x0001u            /* SDCLOCK = IMCLK/4 */
#define SD_DIV_2                  0x0000u            /* SDCLOCK = IMCLK/2 */
#define SD_DIV_1                  0x00FFu            /* SDCLOCK = IMCLK (option) */

/* ---- SD clock define ---- */    /* max frequency */
#define SD_CLK_400kHz             0x0000u            /* 400kHz */
#define SD_CLK_1MHz               0x0001u            /* 1MHz */
#define SD_CLK_5MHz               0x0002u            /* 5MHz */
#define SD_CLK_10MHz              0x0003u            /* 10MHz */
#define SD_CLK_20MHz              0x0004u            /* 20MHz */
#define SD_CLK_25MHz              0x0005u            /* 25MHz */
#define SD_CLK_50MHz              0x0006u            /* 50MHz (phys spec ver1.10) */

/* ---- speed class ---- */
#define SD_SPEED_CLASS_0          0x00u              /* not defined, or less than ver2.0 */
#define SD_SPEED_CLASS_2          0x01u              /* 2MB/sec */
#define SD_SPEED_CLASS_4          0x02u              /* 4MB/sec */
#define SD_SPEED_CLASS_6          0x03u              /* 6MB/sec */

/* ---- IO initialize flags define ---- */    /* add for IO */
#define SD_IO_INT_ENAB            0x10u              /* interrupt enable */
#define SD_IO_POWER_INIT          0x04u              /* power on initialized */
#define SD_IO_MEM_INIT            0x02u              /* memory initialized */
#define SD_IO_FUNC_INIT           0x01u              /* io func initialized */

/* ---- IO function's information ---- */    /* add for IO */
#define SD_IO_FUNC_READY          0x80u              /* io redy */
#define SD_IO_FUNC_NUM            0x70u              /* number of io func */
#define SD_IO_FUNC_EXISTS         0x04u              /* memory present */

/* ---- SD port mode ---- */
#define SD_PORT_SERIAL            0x0000u            /* 1bit mode */
#define SD_PORT_PARALLEL          0x0001u            /* 4bits mode */

/* ---- SD Card detect port ---- */
#define SD_CD_SOCKET              0x0000u            /* CD pin */
#define SD_CD_DAT3                0x0001u            /* DAT3 pin */

/* ---- SD Card detect interrupt ---- */
#define SD_CD_INT_DISABLE         0x0000u            /* card detect interrupt disable */
#define SD_CD_INT_ENABLE          0x0001u            /* card detect interrupt enable */

/* ---- format mode ---- */
#define SD_FORMAT_QUICK           0x0000u            /* quick format */
#define SD_FORMAT_FULL            0x0001u            /* full format */

/* ---- lock/unlock mode ---- */
#define SD_FORCE_ERASE            0x08
#define SD_LOCK_CARD              0x04
#define SD_UNLOCK_CARD            0x00
#define SD_CLR_PWD                0x02
#define SD_SET_PWD                0x01

/* ==== API prototype ===== */
/* ---- access library I/F ---- */
int sd_init(int sd_port, unsigned long base, void *workarea, int cd_port);
int sd_cd_int(int sd_port, int enable,int (*callback)(int, int));
int sd_check_media(int sd_port);
int sd_format(int sd_port, int mode,int (*callback)(unsigned long,unsigned long));
int sd_format2(int sd_port, int mode,unsigned long volserial,int (*callback)(unsigned long,unsigned long));
int sd_mount(int sd_port, unsigned long mode,unsigned long voltage);
int sd_read_sect(int sd_port, unsigned char *buff,unsigned long psn,long cnt);
int sd_write_sect(int sd_port, unsigned char const *buff,unsigned long psn,long cnt,int writemode);
int sd_get_type(int sd_port, unsigned char *type,unsigned char *speed,unsigned char *capa);
int sd_get_size(int sd_port, unsigned long *user,unsigned long *protect);
int sd_iswp(int sd_port);
int sd_unmount(int sd_port);
void sd_stop(int sd_port);
int sd_set_intcallback(int sd_port, int (*callback)(int, int));
void sd_int_handler(int sd_port);
int sd_get_error(int sd_port);
int sd_check_int(int sd_port);
int sd_get_reg(int sd_port, unsigned char *ocr,unsigned char *cid,unsigned char *csd, unsigned char *dsr,unsigned char *scr);
int sd_get_rca(int sd_port, unsigned char *rca);
int sd_get_sdstatus(int sd_port, unsigned char *sdstatus);
int sd_get_speed(int sd_port, unsigned char *clss,unsigned char *move);
int sd_finalize(int sd_port);
int sd_set_seccnt(int sd_port, short sectors);
int sd_get_seccnt(int sd_port);
int sd_get_ver(int sd_port, unsigned short *sdhi_ver,char *sddrv_ver);
int sd_set_cdtime(int sd_port, unsigned short cdtime);
int sd_set_responsetime(int sd_port, unsigned short responsetime);
int sd_set_buffer(int sd_port, void *buff,unsigned long size);
int sd_inactive(int sd_port);
int sd_set_softwp(int sd_port, int is_set,unsigned long data);
int sd_set_tmpwp(int sd_port, int is_set);
int sd_lock_unlock(int sd_port, unsigned char code,unsigned char *pwd,unsigned char len);

int esd_get_partition_id(int sd_port, int *id);
int esd_select_partition(int sd_port, int id);
int esd_query_partition(int sd_port, int sub, unsigned char *data);

int sdio_read_direct(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr);
int sdio_write_direct(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr,unsigned long raw_flag);
int sdio_check_int(int sd_port);
void sdio_int_handler(int sd_port);
int sdio_set_intcallback(int sd_port, int (*callback)(int));
int sdio_enable_int(int sd_port);
int sdio_disable_int(int sd_port);
int sdio_read(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr,long cnt,unsigned long op_code);
int sdio_write(int sd_port, unsigned char *buff,unsigned long func,unsigned long adr,long cnt,unsigned long op_code);
int sdio_reset(int sd_port);
int sdio_get_ioocr(int sd_port, unsigned long *ioocr);
int sdio_get_ioinfo(int sd_port, unsigned char *ioinfo);
int sdio_get_cia(int sd_port, unsigned char *reg, unsigned char *cis, unsigned long func_num, long cnt);
int sdio_set_enable(int sd_port, unsigned char func_bit);
int sdio_get_ready(int sd_port, unsigned char *func_bit);
int sdio_set_int(int sd_port, unsigned char func_bit,int enab);
int sdio_get_int(int sd_port, unsigned char *func_bit,int *enab);
int sdio_set_blocklen(int sd_port, unsigned short len, unsigned long func_num);
int sdio_get_blocklen(int sd_port, unsigned short *len, unsigned long func_num);
void sdio_abort(int sd_port, unsigned long func_num);
int sdio_set_blkcnt(int sd_port, short blocks);
int sdio_get_blkcnt(int sd_port);

/* ---- target CPU I/F ---- */
int sddev_init(int sd_port);
int32_t sddev_power_on(int32_t sd_port);
int sddev_power_off(int sd_port);
int sddev_read_data(int sd_port, unsigned char *buff,unsigned long reg_addr,long num);
int sddev_write_data(int sd_port, unsigned char *buff,unsigned long reg_addr,long num);
unsigned int sddev_get_clockdiv(int sd_port, int clock);
int sddev_set_port(int sd_port, int mode);
int32_t sddev_int_wait(int32_t sd_port, int32_t msec);
int sddev_init_dma(int sd_port, unsigned long buffadr,unsigned long regadr,long cnt,int dir);
int sddev_wait_dma_end(int sd_port, long cnt);
int sddev_disable_dma(int sd_port);
int sddev_finalize(int sd_port);
int sddev_loc_cpu(int sd_port);
int sddev_unl_cpu(int sd_port);
int sddev_cmd0_sdio_mount(int sd_port);
int sddev_cmd8_sdio_mount(int sd_port);
void sddev_start_timer(int msec);
int sddev_check_timer(void);
void sddev_end_timer(void);

#ifdef    __cplusplus
}
#endif    /* __cplusplus    */

#endif    /* _SDDRV_H_ */

/* End of File */
