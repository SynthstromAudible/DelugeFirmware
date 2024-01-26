/***********************************************************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
 * other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
 * applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
 * EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
 * SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
 * SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
 * this software. By using this software, you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Copyright (C) 2016 Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/
/***********************************************************************************************************************
 * File Name    : r_usb_bitdefine.h
 * Description  : USB signal control code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/
#ifndef R_USB_BITDEFINE_H
#define R_USB_BITDEFINE_H

/***********************************************************************************************************************
 Structure Types
 ***********************************************************************************************************************/

/* USB200 & USB201 Register definition */

/* System Configuration Control Register */
#define USB_HSE    (0x0080u) /* b7: Hi-speed enable */
#define USB_DCFM   (0x0040u) /* b6: Function select */
#define USB_DRPD   (0x0020u) /* b5: D+/D- pull down control */
#define USB_DPRPU  (0x0010u) /* b4: D+ pull up control */
#define USB_DMRPU  (0x0008u) /* b3: D- pull up control */
#define USB_UCKSEL (0x0004u) /* b2: Clock select */
#define USB_UPLLE  (0x0002u) /* b1: USB PLL enable */
#define USB_USBE   (0x0001u) /* b0: USB module enable */

/* CPU Bus Wait Register */
#define USB_BWAIT    (0x000Fu) /* b3-0: Bus wait bit */
#define USB_BWAIT_15 (0x000Fu) /* 15 wait (access cycle 17) */
#define USB_BWAIT_14 (0x000Eu) /* 14 wait (access cycle 16) */
#define USB_BWAIT_13 (0x000Du) /* 13 wait (access cycle 15) */
#define USB_BWAIT_12 (0x000Cu) /* 12 wait (access cycle 14) */
#define USB_BWAIT_11 (0x000Bu) /* 11 wait (access cycle 13) */
#define USB_BWAIT_10 (0x000Au) /* 10 wait (access cycle 12) */
#define USB_BWAIT_9  (0x0009u) /*  9 wait (access cycle 11) */
#define USB_BWAIT_8  (0x0008u) /*  8 wait (access cycle 10) */
#define USB_BWAIT_7  (0x0007u) /*  7 wait (access cycle  9) */
#define USB_BWAIT_6  (0x0006u) /*  6 wait (access cycle  8) */
#define USB_BWAIT_5  (0x0005u) /*  5 wait (access cycle  7) */
#define USB_BWAIT_4  (0x0004u) /*  4 wait (access cycle  6) */
#define USB_BWAIT_3  (0x0003u) /*  3 wait (access cycle  5) */
#define USB_BWAIT_2  (0x0002u) /*  2 wait (access cycle  4) */
#define USB_BWAIT_1  (0x0001u) /*  1 wait (access cycle  3) */
#define USB_BWAIT_0  (0x0000u) /*  0 wait (access cycle  2) */

/* System Configuration Status Register */
#define USB_HTACT   (0x0040u) /* b6: USB Host Sequencer Status Monitor */
#define USB_SOFEA   (0x0020u) /* b5: SOF monitor */
#define USB_LNST    (0x0003u) /* b1-0: D+, D- line status */
#define USB_SE1     (0x0003u) /* SE1 */
#define USB_FS_KSTS (0x0002u) /* Full-Speed K State */
#define USB_FS_JSTS (0x0001u) /* Full-Speed J State */
#define USB_LS_JSTS (0x0002u) /* Low-Speed J State */
#define USB_LS_KSTS (0x0001u) /* Low-Speed K State */
#define USB_SE0     (0x0000u) /* SE0 */

/* Device State Control Register */
#define USB_WKUP    (0x0100u) /* b8: Remote wakeup */
#define USB_RWUPE   (0x0080u) /* b7: Remote wakeup sense */
#define USB_USBRST  (0x0040u) /* b6: USB reset enable */
#define USB_RESUME  (0x0020u) /* b5: Resume enable */
#define USB_UACT    (0x0010u) /* b4: USB bus enable */
#define USB_RHST    (0x0007u) /* b2-0: Reset handshake status */
#define USB_HSPROC  (0x0004u) /* HS handshake processing */
#define USB_HSMODE  (0x0003u) /* Hi-Speed mode */
#define USB_FSMODE  (0x0002u) /* Full-Speed mode */
#define USB_LSMODE  (0x0001u) /* Low-Speed mode */
#define USB_UNDECID (0x0000u) /* Undecided */

/* DMAn-FIFO Bus Configuration Register */
#define USB_DFACC     (0x3000u) /* b13-b12: FIFO port access mode */
#define USB_DFACC_32  (0x2000u) /* 32byte continuous access mode */
#define USB_DFACC_16  (0x1000u) /* 16byte continuous access mode */
#define USB_DFACC_CYC (0x0000u) /* Cycle steal mode */
#define USB_TENDE     (0x3000u) /* b4: Acceptance control of DMA transfer end signal */

/* Test Mode Register */
#define USB_UTST          (0x000Fu) /* b3-0: Test mode */
#define USB_H_TST_F_EN    (0x000Du) /* HOST TEST FORCE ENABLE */
#define USB_H_TST_PACKET  (0x000Cu) /* HOST TEST Packet */
#define USB_H_TST_SE0_NAK (0x000Bu) /* HOST TEST SE0 NAK */
#define USB_H_TST_K       (0x000Au) /* HOST TEST K */
#define USB_H_TST_J       (0x0009u) /* HOST TEST J */
#define USB_H_TST_NORMAL  (0x0000u) /* HOST Normal Mode */
#define USB_P_TST_PACKET  (0x0004u) /* PERI TEST Packet */
#define USB_P_TST_SE0_NAK (0x0003u) /* PERI TEST SE0 NAK */
#define USB_P_TST_K       (0x0002u) /* PERI TEST K */
#define USB_P_TST_J       (0x0001u) /* PERI TEST J */
#define USB_P_TST_NORMAL  (0x0000u) /* PERI Normal Mode */

/* CFIFO/DxFIFO Port Select Register */
#define USB_RCNT        (0x8000u) /* b15: Read count mode */
#define USB_REW         (0x4000u) /* b14: Buffer rewind */
#define USB_DCLRM       (0x2000u) /* b13: Automatic buffer clear mode */
#define USB_DREQE       (0x1000u) /* b12: DREQ output enable */
#define USB_MBW         (0x0C00u) /* b11-b10: Maximum bit width for FIFO access */
#define USB_MBW_32      (0x0800u) /* FIFO access : 32bit */
#define USB_MBW_16      (0x0400u) /* FIFO access : 16bit */
#define USB_MBW_8       (0x0000u) /* FIFO access : 8bit */
#define USB_BIGEND      (0x0100u) /* b8: Big endian mode */
#define USB_FIFO_BIG    (0x0100u) /* Big endian */
#define USB_FIFO_LITTLE (0x0000u) /* Little endian */
#define USB_ISEL        (0x0020u) /* b5: DCP FIFO port direction select */
#define USB_CURPIPE     (0x000Fu) /* b2-0: PIPE select */

/* CFIFO/DxFIFO Port Control Register */
#define USB_BVAL (0x8000u) /* b15: Buffer valid flag */
#define USB_BCLR (0x4000u) /* b14: Buffer clear */
#define USB_FRDY (0x2000u) /* b13: FIFO ready */
#define USB_DTLN (0x0FFFu) /* b11-0: FIFO data length */

/* Interrupt Enable Register 0 */
#define USB_VBSE  (0x8000u) /* b15: VBUS interrupt */
#define USB_RSME  (0x4000u) /* b14: Resume interrupt */
#define USB_SOFE  (0x2000u) /* b13: Frame update interrupt */
#define USB_DVSE  (0x1000u) /* b12: Device state transition interrupt */
#define USB_CTRE  (0x0800u) /* b11: Control transfer stage transition interrupt */
#define USB_BEMPE (0x0400u) /* b10: Buffer empty interrupt */
#define USB_NRDYE (0x0200u) /* b9: Buffer notready interrupt */
#define USB_BRDYE (0x0100u) /* b8: Buffer ready interrupt */

/* Interrupt Enable Register 1 */
#define USB_BCHGE   (0x4000u) /* b14: USB bus change interrupt */
#define USB_DTCHE   (0x1000u) /* b12: Detach sense interrupt */
#define USB_ATTCHE  (0x0800u) /* b11: Attach sense interrupt */
#define USB_EOFERRE (0x0040u) /* b6: EOF error interrupt */
#define USB_SIGNE   (0x0020u) /* b5: SETUP IGNORE interrupt */
#define USB_SACKE   (0x0010u) /* b4: SETUP ACK interrupt */

/* BRDY Interrupt Enable/Status Register */
#define USB_BRDY15 (0x8000u) /* b15: PIPEF */
#define USB_BRDY14 (0x4000u) /* b14: PIPEE */
#define USB_BRDY13 (0x2000u) /* b13: PIPED */
#define USB_BRDY12 (0x1000u) /* b12: PIPEC */
#define USB_BRDY11 (0x0800u) /* b11: PIPEB */
#define USB_BRDY10 (0x0400u) /* b10: PIPEA */
#define USB_BRDY9  (0x0200u) /* b9: PIPE9 */
#define USB_BRDY8  (0x0100u) /* b8: PIPE8 */
#define USB_BRDY7  (0x0080u) /* b7: PIPE7 */
#define USB_BRDY6  (0x0040u) /* b6: PIPE6 */
#define USB_BRDY5  (0x0020u) /* b5: PIPE5 */
#define USB_BRDY4  (0x0010u) /* b4: PIPE4 */
#define USB_BRDY3  (0x0008u) /* b3: PIPE3 */
#define USB_BRDY2  (0x0004u) /* b2: PIPE2 */
#define USB_BRDY1  (0x0002u) /* b1: PIPE1 */
#define USB_BRDY0  (0x0001u) /* b1: PIPE0 */

/* NRDY Interrupt Enable/Status Register */
#define USB_NRDY15 (0x8000u) /* b15: PIPEF */
#define USB_NRDY14 (0x4000u) /* b14: PIPEE */
#define USB_NRDY13 (0x2000u) /* b13: PIPED */
#define USB_NRDY12 (0x1000u) /* b12: PIPEC */
#define USB_NRDY11 (0x0800u) /* b11: PIPEB */
#define USB_NRDY10 (0x0400u) /* b10: PIPEA */
#define USB_NRDY9  (0x0200u) /* b9: PIPE9 */
#define USB_NRDY8  (0x0100u) /* b8: PIPE8 */
#define USB_NRDY7  (0x0080u) /* b7: PIPE7 */
#define USB_NRDY6  (0x0040u) /* b6: PIPE6 */
#define USB_NRDY5  (0x0020u) /* b5: PIPE5 */
#define USB_NRDY4  (0x0010u) /* b4: PIPE4 */
#define USB_NRDY3  (0x0008u) /* b3: PIPE3 */
#define USB_NRDY2  (0x0004u) /* b2: PIPE2 */
#define USB_NRDY1  (0x0002u) /* b1: PIPE1 */
#define USB_NRDY0  (0x0001u) /* b1: PIPE0 */

/* BEMP Interrupt Enable/Status Register */
#define USB_BEMP15 (0x8000u) /* b15: PIPEF */
#define USB_BEMP14 (0x4000u) /* b14: PIPEE */
#define USB_BEMP13 (0x2000u) /* b13: PIPED */
#define USB_BEMP12 (0x1000u) /* b12: PIPEC */
#define USB_BEMP11 (0x0800u) /* b11: PIPEB */
#define USB_BEMP10 (0x0400u) /* b10: PIPEA */
#define USB_BEMP9  (0x0200u) /* b9: PIPE9 */
#define USB_BEMP8  (0x0100u) /* b8: PIPE8 */
#define USB_BEMP7  (0x0080u) /* b7: PIPE7 */
#define USB_BEMP6  (0x0040u) /* b6: PIPE6 */
#define USB_BEMP5  (0x0020u) /* b5: PIPE5 */
#define USB_BEMP4  (0x0010u) /* b4: PIPE4 */
#define USB_BEMP3  (0x0008u) /* b3: PIPE3 */
#define USB_BEMP2  (0x0004u) /* b2: PIPE2 */
#define USB_BEMP1  (0x0002u) /* b1: PIPE1 */
#define USB_BEMP0  (0x0001u) /* b0: PIPE0 */

/* SOF Pin Configuration Register */
#define USB_TRNENSEL (0x0100u) /* b8: Select transaction enable period */
#define USB_BRDYM    (0x0040u) /* b6: BRDY clear timing */

/* PHYSET */
#define USB_HSEB (0x8000u) /* b15: CL only mode bit */

#define USB_REPSTART    (0x0800u) /* b11: Terminator adjustment forcible starting bit */
#define USB_REPSEL      (0x0300u) /* b9-8: Terminator adjustment cycle setting */
#define USB_REPSEL_128  (0x0300u) /* 128 sec */
#define USB_REPSEL_64   (0x0200u) /* 64 sec */
#define USB_REPSEL_16   (0x0100u) /* 16 sec */
#define USB_REPSEL_NONE (0x0000u) /* - */
#define USB_CLKSEL      (0x0030u) /* b5-4: System clock setting */
#define USB_CLKSEL_24   (0x0030u) /* 24MHz */
#define USB_CLKSEL_20   (0x0020u) /* 20MHz */
#define USB_CLKSEL_48   (0x0010u) /* 48MHz */
#define USB_CLKSEL_30   (0x0000u) /* 30MHz */
#define USB_CDPEN       (0x0008u) /* b3: Charging downstream port enable */
#define USB_PLLRESET    (0x0002u) /* b1: PLL reset control */
#define USB_DIRPD       (0x0001u) /* b0: Power down control */

/* Interrupt Status Register 0 */
#define USB_VBINT       (0x8000u) /* b15: VBUS interrupt */
#define USB_RESM        (0x4000u) /* b14: Resume interrupt */
#define USB_SOFR        (0x2000u) /* b13: SOF update interrupt */
#define USB_DVST        (0x1000u) /* b12: Device state transition interrupt */
#define USB_CTRT        (0x0800u) /* b11: Control transfer stage transition interrupt */
#define USB_BEMP        (0x0400u) /* b10: Buffer empty interrupt */
#define USB_NRDY        (0x0200u) /* b9: Buffer notready interrupt */
#define USB_BRDY        (0x0100u) /* b8: Buffer ready interrupt */
#define USB_VBSTS       (0x0080u) /* b7: VBUS input port */
#define USB_DVSQ        (0x0070u) /* b6-4: Device state */
#define USB_DS_SPD_CNFG (0x0070u) /* Suspend Configured */
#define USB_DS_SPD_ADDR (0x0060u) /* Suspend Address */
#define USB_DS_SPD_DFLT (0x0050u) /* Suspend Default */
#define USB_DS_SPD_POWR (0x0040u) /* Suspend Powered */
#define USB_DS_SUSP     (0x0040u) /* Suspend */
#define USB_DS_CNFG     (0x0030u) /* Configured */
#define USB_DS_ADDS     (0x0020u) /* Address */
#define USB_DS_DFLT     (0x0010u) /* Default */
#define USB_DS_POWR     (0x0000u) /* Powered */
#define USB_DVSQS       (0x0030u) /* b5-4: Device state */
#define USB_VALID       (0x0008u) /* b3: Setup packet detect flag */
#define USB_CTSQ        (0x0007u) /* b2-0: Control transfer stage */
#define USB_CS_SQER     (0x0006u) /* Sequence error */
#define USB_CS_WRND     (0x0005u) /* Ctrl write nodata status stage */
#define USB_CS_WRSS     (0x0004u) /* Ctrl write status stage */
#define USB_CS_WRDS     (0x0003u) /* Ctrl write data stage */
#define USB_CS_RDSS     (0x0002u) /* Ctrl read status stage */
#define USB_CS_RDDS     (0x0001u) /* Ctrl read data stage */
#define USB_CS_IDST     (0x0000u) /* Idle or setup stage */

/* Interrupt Status Register 1 */
#define USB_BCHG   (0x4000u) /* b14: USB bus change interrupt */
#define USB_DTCH   (0x1000u) /* b12: Detach sense interrupt */
#define USB_ATTCH  (0x0800u) /* b11: Attach sense interrupt */
#define USB_EOFERR (0x0040u) /* b6: EOF-error interrupt */
#define USB_SIGN   (0x0020u) /* b5: Setup ignore interrupt */
#define USB_SACK   (0x0010u) /* b4: Setup ack interrupt */

/* Frame Number Register */
#define USB_OVRN (0x8000u) /* b15: Overrun error */
#define USB_CRCE (0x4000u) /* b14: Received data error */
#define USB_FRNM (0x07FFu) /* b10-0: Frame number */

/* Micro Frame Number Register */
#define USB_UFRNM (0x0007u) /* b2-0: Micro frame number */

/* USB Address / Low Power Status Recovery Register */
#define USB_USBADDR_MASK (0x007Fu) /* b6-0: USB address */

/* USB Request Type Register */
#define USB_BMREQUESTTYPE      (0x00FFu) /* b7-0: USB_BMREQUESTTYPE */
#define USB_BMREQUESTTYPEDIR   (0x0080u) /* b7  : Data transfer direction */
#define USB_BMREQUESTTYPETYPE  (0x0060u) /* b6-5: Type */
#define USB_BMREQUESTTYPERECIP (0x001Fu) /* b4-0: Recipient */

/* USB Request Value Register */
#define USB_WVALUE         (0xFFFFu) /* b15-0: wValue */
#define USB_DT_TYPE        (0xFF00u)
#define USB_GET_DT_TYPE(v) (((v) & USB_DT_TYPE) >> 8)
#define USB_DT_INDEX       (0x00FFu)
#define USB_CONF_NUM       (0x00FFu)
#define USB_ALT_SET        (0x00FFu)

/* USB Request Index Register */
#define USB_WINDEX            (0xFFFFu) /* b15-0: wIndex */
#define USB_TEST_SELECT       (0xFF00u) /* b15-b8: Test Mode Selectors */
#define USB_TEST_J            (0x0100u) /* Test_J */
#define USB_TEST_K            (0x0200u) /* Test_K */
#define USB_TEST_SE0_NAK      (0x0300u) /* Test_SE0_NAK */
#define USB_TEST_PACKET       (0x0400u) /* Test_Packet */
#define USB_TEST_FORCE_ENABLE (0x0500u) /* Test_Force_Enable */
#define USB_TEST_STSelectors  (0x0600u) /* Standard test selectors */
#define USB_TEST_RESERVED     (0x4000u) /* Reserved */
#define USB_TEST_VSTMODES     (0xC000u) /* VendorSpecific test modes */
#define USB_EP_DIR            (0x0080u) /* b7: Endpoint Direction */
#define USB_EP_DIR_IN         (0x0080u)
#define USB_EP_DIR_OUT        (0x0000u)

/* USB Request Length Register */
#define USB_WLENGTH (0xFFFFu) /* b15-0: wLength */

/* Default Control Pipe Configuration Register */
/* Pipe Configuration Register */
#define USB_TYPE   (0xC000u) /* b15-14: Transfer type */
#define USB_BFRE   (0x0400u) /* b10: Buffer ready interrupt mode select */
#define USB_DBLB   (0x0200u) /* b9: Double buffer mode select */
#define USB_CBTMD  (0x0100u) /* b8: Continuous transfer mode select */
#define USB_SHTNAK (0x0080u) /* b7: Transfer end NAK */
#define USB_DIR    (0x0010u) /* b4: Transfer direction select */
#define USB_EPNUM  (0x000Fu) /* b3-0: Endpoint number select */

/* Default Control Pipe Maxpacket Size Register */
/* Pipe Maxpacket Size Register */
#define USB_DEVSEL (0xF000u) /* b15-14: Device address select */
#define USB_MAXP   (0x007Fu) /* b6-0: Maxpacket size of default control pipe */
#define USB_MXPS   (0x07FFu) /* b10-0: Maxpacket size */

/* Default Control Pipe Control Register */
/* Pipex Control Register */
#define USB_BSTS      (0x8000u) /* b15: Buffer status */
#define USB_SUREQ     (0x4000u) /* b14: Send USB request  */
#define USB_INBUFM    (0x4000u) /* b14: IN buffer monitor (Only for PIPE1 to 5) */
#define USB_CSCLR     (0x2000u) /* b13: c-split status clear */
#define USB_CSSTS     (0x1000u) /* b12: c-split status */
#define USB_SUREQCLR  (0x0800u) /* b11: stop setup request */
#define USB_ATREPM    (0x0400u) /* b10: Auto repeat mode */
#define USB_ACLRM     (0x0200u) /* b9: buffer auto clear mode */
#define USB_SQCLR     (0x0100u) /* b8: Sequence bit clear */
#define USB_SQSET     (0x0080u) /* b7: Sequence bit set */
#define USB_SQMON     (0x0040u) /* b6: Sequence bit monitor */
#define USB_PBUSY     (0x0020u) /* b5: pipe busy */
#define USB_PINGE     (0x0010u) /* b4: ping enable */
#define USB_CCPL      (0x0004u) /* b2: Enable control transfer complete */
#define USB_PID       (0x0003u) /* b1-0: Response PID */
#define USB_PID_STALL (0x0002u) /* STALL */
#define USB_PID_BUF   (0x0001u) /* BUF */
#define USB_PID_NAK   (0x0000u) /* NAK */

/* Pipe Window Select Register */
#define USB_PIPENM (0x000Fu) /* b3-0: Pipe select */

/* Pipe Buffer Configuration Register */
#define USB_BUFSIZE  (0x7C00u) /* b14-10: Pipe buffer size */
#define USB_BUFNMB   (0x00FFu) /* b7-0: Pipe buffer number */
#define USB_PIPE0BUF (256u)
#define USB_PIPEXBUF (64u)

/* Pipe Cycle Configuration Register */
#define USB_IFIS (0x1000u) /* b12: Isochronous in-buffer flash mode select */
#define USB_IITV (0x0007u) /* b2-0: Isochronous interval */

/* PIPExTRE */
#define USB_TRENB (0x0200u) /* b9: Transaction count enable */
#define USB_TRCLR (0x0100u) /* b8: Transaction count clear */

/* PIPExTRN */
#define USB_TRNCNT (0xFFFFu) /* b15-0: Transaction counter */

/* DEVADDx */
#define USB_UPPHUB  (0x7800u) /* b14-11: HUB register */
#define USB_HUBPORT (0x0700u) /* b10-8: HUB port */
#define USB_USBSPD  (0x00C0u) /* b7-6: Device speed */
#define USB_RTPORT  (0x0001u)

/* Suspend Mode Register */
#define USB_SUSPM (0x4000u) /* b14: Control SuspendM */

/* DMA0-FIFO bus configuration Register */
/* DMA1-FIFO bus configuration Register */
#define USB_DFACC (0x3000u) /* b13-12: EMAx FIFO access mode */

#define USB_DFACC_CS (0x0000u) /* Cycle still mode */
#define USB_DFACC_32 (0x2000u) /* 32 bytes continuous access mode */
#define USB_DFACC_16 (0x1000u) /* 16 bytes continuous access mode */
#define USB_TEND     (0x0010u) /* b4: enable to input TENDx_N signal */

/* USB IO Register Reserved bit mask */
#define INTSTS1_MASK (0xD870u) /* INTSTS1 Reserved bit mask */
#define BRDYSTS_MASK (0x03FFu) /* BRDYSTS Reserved bit mask */
#define NRDYSTS_MASK (0x03FFu) /* NRDYSTS Reserved bit mask */
#define BEMPSTS_MASK (0x03FFu) /* BEMPSTS Reserved bit mask */

#endif /* R_USB_BITDEFINE_H */
/***********************************************************************************************************************
 End of file
 ***********************************************************************************************************************/
