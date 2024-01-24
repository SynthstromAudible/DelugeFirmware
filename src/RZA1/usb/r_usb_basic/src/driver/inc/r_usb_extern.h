/**********************************************************************************************************************
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
 * Copyright (C) 2015 Renesas Electronics Corporation. All rights reserved.
 *********************************************************************************************************************/
/**********************************************************************************************************************
 * File Name    : r_usb_extern.h
 * Description  : USB common extern header
 *********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 31.08.2015 1.00    First Release
 *********************************************************************************************************************/

#ifndef R_USB_EXTERN_H
#define R_USB_EXTERN_H

#include "r_usb_typedef.h"

// Includes by Rohan so I can inline stuff below
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_reg_access.h"
extern uint8_t usb_scheduler_schedule_flag;

/***********************************************************************************************************************
 Public Variables
 *********************************************************************************************************************/
/* r_usbif_api.c */
extern uint32_t g_usb_read_request_size[][USB_MAXPIPE_NUM + 1];
extern uint16_t g_usb_change_device_state[USB_NUM_USBIP];

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
extern usb_utr_t* g_p_usb_pipe[USB_MAX_PIPE_NO + 1u];
extern uint32_t g_usb_data_cnt[USB_MAX_PIPE_NO + 1u]; /* PIPEn Buffer counter */
extern uint8_t* g_p_usb_data[USB_MAX_PIPE_NO + 1u];   /* PIPEn Buffer pointer(8bit) */
#endif                                                /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
// extern usb_utr_t        *g_p_usb_hstd_pipe[][USB_MAX_PIPE_NO + 1u];     /* Message pipe */
// extern uint8_t          *g_p_usb_hstd_data[][USB_MAX_PIPE_NO + 1u];     /* PIPEn Buffer pointer(8bit) */
// extern uint32_t         g_usb_hstd_data_cnt[][USB_MAX_PIPE_NO + 1u];    /* PIPEn Buffer counter */
extern uint16_t g_usb_hstd_hs_enable[]; /* Hi-speed enable */
#endif                                  /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/* r_usb_hdriver.c */
extern uint16_t g_usb_hstd_ignore_cnt[][USB_MAX_PIPE_NO + 1u];    /* Ignore count */
extern usb_hcdreg_t g_usb_hstd_device_drv[][USB_MAXDEVADDR + 1u]; /* Device driver (registration) */
extern uint16_t g_usb_hstd_device_info[][USB_MAXDEVADDR + 1u][8u];

/* port status, config num, interface class, speed, */
extern uint16_t g_usb_hstd_remort_port[];
extern uint16_t g_usb_hstd_ctsq[];                              /* Control transfer stage management */
extern uint16_t g_usb_hstd_mgr_mode[][2u];                      /* Manager mode */
extern uint16_t g_usb_hstd_dcp_register[][USB_MAXDEVADDR + 1u]; /* DEVSEL & DCPMAXP (Multiple device) */
extern uint16_t g_usb_hstd_device_addr[];                       /* Device address */
extern uint16_t g_usb_hstd_device_speed[];                      /* Reset handshake result */
extern uint16_t g_usb_hstd_device_num[];                        /* Device driver number */

/* r_usb_hmanager.c */
extern uint16_t g_usb_hstd_enum_seq[]; /* Enumeration request */
extern uint16_t g_usb_hstd_device_descriptor[][USB_DEVICESIZE / 2u];
extern uint16_t g_usb_hstd_config_descriptor[][USB_CONFIGSIZE / 2u];
extern uint16_t g_usb_hstd_suspend_pipe[][USB_MAX_PIPE_NO + 1u];
extern uint8_t g_usb_hstd_enu_wait[];          /* Class check TaskID */
extern uint16_t g_usb_hstd_check_enu_result[]; /* Enumeration result check */
#if USB_CFG_BC == USB_CFG_ENABLE
extern usb_bc_status_t g_usb_hstd_bc[2u];
extern void (*usb_hstd_bc_func[USB_BC_STATE_MAX][USB_BC_EVENT_MAX])(usb_utr_t* ptr, uint16_t port);
#endif /* USB_CFG_BC == USB_CFG_ENABLE */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/* r_usb_pdriver.c */
extern uint16_t g_usb_pstd_stall_pipe[USB_MAX_PIPE_NO + 1u];   /* Stall Pipe info */
extern usb_cb_t g_usb_pstd_stall_cb;                           /* Stall Callback function */
extern uint16_t g_usb_pstd_config_num;                         /* Configuration Number */
extern uint16_t g_usb_pstd_alt_num[];                          /* Alternate */
extern uint16_t g_usb_pstd_remote_wakeup;                      /* Remote Wakeup Enable Flag */
extern uint16_t g_usb_pstd_test_mode_select;                   /* Test Mode Selectors */
extern uint16_t g_usb_pstd_test_mode_flag;                     /* Test Mode Flag */
extern uint16_t g_usb_pstd_eptbl_index[2][USB_MAX_EP_NO + 1u]; /* Index of Endpoint Information table */
extern uint16_t g_usb_pstd_req_type;                           /* Request type */
extern uint16_t g_usb_pstd_req_value;                          /* Value */
extern uint16_t g_usb_pstd_req_index;                          /* Index */
extern uint16_t g_usb_pstd_req_length;                         /* Length */
extern usb_pcdreg_t g_usb_pstd_driver;                         /* Driver registration */
extern usb_setup_t g_usb_pstd_req_reg;                         /* Request variable */
extern usb_int_t g_usb_pstd_usb_int;
extern uint16_t g_usb_pstd_pipe0_request;

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

extern usb_event_t g_usb_cstd_event;

/***********************************************************************************************************************
 Public Functions
 ***********************************************************************************************************************/
/* r_usb_rx_mcu.c */
usb_err_t usb_module_start(uint8_t ip_type);
usb_err_t usb_module_stop(uint8_t ip_type);
void usb_cpu_delay_xms(uint16_t time);
void usb_cpu_delay_1us(uint16_t time);
void usb_cpu_usbint_init(uint8_t ip_type);
uint16_t usb_chattaring(volatile uint16_t* syssts);

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void usb_cpu_int_enable(usb_utr_t* ptr);
void usb_cpu_int_disable(usb_utr_t* ptr);

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/* r_usb_cdataio.c */
uint8_t usb_get_usepipe(usb_ctrl_t* p_ctrl, uint8_t dir);
usb_er_t usb_data_read(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size);
usb_er_t usb_ctrl_read(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size);
usb_er_t usb_data_write(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size);
usb_er_t usb_ctrl_write(usb_ctrl_t* p_ctrl, uint8_t* buf, uint32_t size);
usb_er_t usb_data_stop(usb_ctrl_t* p_ctrl, uint16_t type);
usb_er_t usb_ctrl_stop(usb_ctrl_t* p_ctrl);
void usb_cstd_debug_hook(uint16_t error_code);
void usb_cstd_select_nak(usb_utr_t* ptr, uint16_t pipe);

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
usb_er_t usb_pstd_transfer_start(usb_utr_t* ptr);
usb_er_t usb_pstd_transfer_end(uint16_t pipe);
void usb_pstd_change_device_state(uint16_t state, uint16_t keyword, usb_cb_t complete);
void usb_pstd_driver_registration(usb_pcdreg_t* callback);
void usb_pstd_driver_release(void);

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void usb_hstd_send_start(usb_utr_t* ptr, uint16_t pipe);
void usb_hstd_buf2fifo(usb_utr_t* ptr, uint16_t pipe, uint16_t useport);
uint16_t usb_hstd_write_data(usb_utr_t* ptr, uint16_t pipe, uint16_t pipemode);
void usb_hstd_receive_start(usb_utr_t* ptr, uint16_t pipe);
void usb_hstd_fifo_to_buf(usb_utr_t* ptr, uint16_t pipe, uint16_t useport);
uint16_t usb_hstd_read_data(usb_utr_t* ptr, uint16_t pipe, uint16_t useport);
void usb_hstd_data_end(usb_utr_t* ptr, uint16_t pipe, uint16_t status);
usb_er_t usb_hstd_set_pipe_registration(usb_utr_t* ptr, uint16_t* table, uint16_t pipe);
usb_er_t usb_hstd_transfer_end(usb_utr_t* ptr, uint16_t pipe, uint16_t status);
usb_er_t usb_hstd_change_device_state(usb_utr_t* ptr, usb_cb_t complete, uint16_t msginfo, uint16_t member);
usb_er_t usb_hstd_mgr_open(usb_utr_t* ptr);
void usb_hstd_driver_registration(usb_utr_t* ptr, usb_hcdreg_t* callback);
void usb_hstd_driver_release(usb_utr_t* ptr, uint8_t devclass);
uint16_t usb_hstd_chk_pipe_info(uint16_t speed, uint16_t* ep_tbl, uint8_t* descriptor);
void usb_hstd_set_pipe_info(uint16_t* dst_ep_tbl, uint16_t* src_ep_tbl, uint16_t length);
void usb_hstd_return_enu_mgr(usb_utr_t* ptr, uint16_t cls_result);
void usb_hstd_enu_wait(usb_utr_t* ptr, uint8_t task_id);
usb_err_t usb_hstd_hcd_open(usb_utr_t* ptr);

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
void usb_pstd_send_start(uint16_t pipe);
void usb_pstd_buf2fifo(uint16_t pipe, uint16_t useport);
uint16_t usb_pstd_write_data(uint16_t pipe, uint16_t pipemode);
void usb_pstd_receive_start(uint16_t pipe);
void usb_pstd_fifo_to_buf(uint16_t pipe, uint16_t useport);
uint16_t usb_pstd_read_data(uint16_t pipe, uint16_t pipemode);
uint16_t usb_read_data_fast_rohan(uint16_t pipe);
void usb_pstd_data_end(uint16_t pipe, uint16_t status);

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

/* r_usb_cintfifo.c */
#if (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI
void usb_pstd_nrdy_pipe_process(uint16_t bitsts);
void usb_pstd_bemp_pipe_process(uint16_t bitsts);
void usb_pstd_bemp_pipe_process_rohan_midi(uint16_t bitsts); // By Rohan
void usb_pstd_brdy_pipe_process(uint16_t bitsts);
void usb_pstd_brdy_pipe_process_rohan_midi(uint16_t bitsts); // By Rohan

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
void usb_hstd_nrdy_pipe_process(usb_utr_t* ptr, uint16_t bitsts);
void usb_hstd_bemp_pipe_process(usb_utr_t* ptr, uint16_t bitsts);
void usb_hstd_bemp_pipe_process_rohan_midi(usb_utr_t* ptr, uint16_t bitsts);
void usb_hstd_brdy_pipe_process(usb_utr_t* ptr, uint16_t bitsts);
void usb_hstd_brdy_pipe_process_rohan_midi_and_hub(usb_utr_t* ptr, uint16_t bitsts);

#endif /* ( (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST ) */

/* r_usb_clibusbip.c */
void usb_cstd_nrdy_enable(usb_utr_t* ptr, uint16_t pipe);
uint16_t usb_cstd_get_pid(usb_utr_t* ptr, uint16_t pipe);
uint16_t usb_cstd_port_speed(usb_utr_t* ptr, uint16_t port);
uint16_t usb_cstd_get_maxpacket_size(usb_utr_t* ptr, uint16_t pipe);
uint16_t usb_cstd_get_pipe_dir(usb_utr_t* ptr, uint16_t pipe);
uint16_t usb_cstd_get_pipe_type(usb_utr_t* ptr, uint16_t pipe);
uint16_t usb_cstd_get_pipe_type_from_memory(uint16_t pipe); // By Rohan
uint16_t usb_cstd_get_pipe_dir_from_memory(uint16_t pipe);  // By Rohan
void usb_cstd_do_aclrm(usb_utr_t* ptr, uint16_t pipe);
void usb_cstd_set_buf(usb_utr_t* ptr, uint16_t pipe);
void usb_cstd_clr_stall(usb_utr_t* ptr, uint16_t pipe);
void usb_cstd_usb_task(void);
void usb_class_task(void);
void usb_set_event(uint16_t event, usb_ctrl_t* ctrl);

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
uint8_t usb_hstd_pipe_to_epadr(usb_utr_t* ptr, uint16_t pipe);
uint16_t usb_hstd_pipe2fport(usb_utr_t* ptr, uint16_t pipe);
void usb_hstd_dummy_function(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_hstd_suspend_complete(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_hstd_resume_complete(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_hstd_berne_enable(usb_utr_t* ptr);
void usb_hstd_sw_reset(usb_utr_t* ptr);
void usb_hstd_set_hse(usb_utr_t* ptr, uint16_t port, uint16_t speed);
void usb_hstd_do_sqtgl(usb_utr_t* ptr, uint16_t pipe, uint16_t toggle);
uint16_t usb_hstd_get_devsel(usb_utr_t* ptr, uint16_t pipe);
uint16_t usb_hstd_get_device_address(usb_utr_t* ptr, uint16_t pipe);

#endif /* ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST) */

#if (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI
uint16_t usb_pstd_epadr2pipe(uint16_t Dir_Ep);
uint16_t usb_pstd_pipe2fport(uint16_t pipe);
uint16_t usb_pstd_hi_speed_enable(void);
void usb_pstd_dummy_function(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_pstd_dummy_trn(usb_setup_t* preq, uint16_t ctsq);

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

/* r_usb_creg_abs.c */
uint16_t usb_cstd_is_set_frdy(usb_utr_t* ptr, uint16_t pipe, uint16_t fifosel, uint16_t isel);
uint16_t usb_cstd_is_set_frdy_rohan(uint16_t pipe);
void usb_cstd_chg_curpipe(usb_utr_t* ptr, uint16_t pipe, uint16_t fifosel, uint16_t isel);
void usb_cstd_set_transaction_counter(usb_utr_t* ptr, uint16_t trnreg, uint16_t trncnt);
void usb_cstd_clr_transaction_counter(usb_utr_t* ptr, uint16_t trnreg);
void usb_cstd_pipe_init(usb_utr_t* ptr, uint16_t pipe, uint16_t* tbl, uint16_t ofs);
void usb_cstd_clr_pipe_cnfg(usb_utr_t* ptr, uint16_t pipeno);
void usb_cstd_set_nak(usb_utr_t* ptr, uint16_t pipe);
void usb_cstd_set_nak_fast_rohan(uint16_t pipe);
uint16_t usb_cstd_get_buf_size(usb_utr_t* ptr, uint16_t pipe);

extern uint16_t fifoSels[];

// usb_cstd_chg_curpipe(USB_NULL, pipe, USB_CUSE, USB_FALSE);
inline static void usb_cstd_chg_curpipe_rohan_fast(uint16_t pipe)
{
    if ((fifoSels[USB_CUSE] & (USB_ISEL | USB_CURPIPE)) != pipe)
    {

        volatile uint16_t* p_reg;
#if USB_CFG_USE_USBIP == USB_CFG_IP0
        p_reg = (void*)&(USB200.CFIFOSEL);
#else
        p_reg = (void*)&(USB201.CFIFOSEL);
#endif /* USB_CFG_USE_USBIP == USB_CFG_IP0 */

        /* ISEL=1, CURPIPE=0 */
        uint16_t data   = (USB_RCNT | pipe);
        uint16_t bitptn = (USB_RCNT | USB_ISEL | USB_CURPIPE);
        // hw_usb_rmw_fifosel(USB_NULL, USB_CUSE, data, bitptn);
        //{
        uint16_t buf = *p_reg;
        buf &= (~bitptn);
        buf |= (data);
        uint16_t buffer;
        // Keep reading it til the change shows
        do
        {
            fifoSels[USB_CUSE] = buf; // This function only changed some of the bits, as specified by bitptn
            *p_reg             = buf;
            //}
            buffer = *p_reg;
        } while ((buffer & (USB_ISEL | USB_CURPIPE)) != pipe); // I've got it getting stuck here sometimes! Rohan
    }
}

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
uint8_t* usb_hstd_write_fifo(usb_utr_t* ptr, uint16_t count, uint16_t pipemode, uint8_t* write_p);
uint8_t* usb_hstd_read_fifo(usb_utr_t* ptr, uint16_t count, uint16_t pipemode, uint8_t* read_p);
void usb_hstd_forced_termination(usb_utr_t* ptr, uint16_t pipe, uint16_t status);
usb_regadr_t usb_hstd_get_usb_ip_adr(uint16_t ipno);
void usb_hstd_nrdy_endprocess(usb_utr_t* ptr, uint16_t pipe);

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
uint8_t* usb_pstd_write_fifo(uint16_t count, uint16_t pipemode, uint8_t* write_p);
uint8_t* usb_pstd_read_fifo(uint16_t count, uint16_t pipemode, uint8_t* read_p);
void usb_pstd_forced_termination(uint16_t pipe, uint16_t status);
void usb_pstd_interrupt_clock(void);
void usb_pstd_self_clock(void);
void usb_pstd_stop_clock(void);
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/* r_usb_cinthandler_usbip0.c */
void usb_hstd_usb_handler(uint32_t sense);
void usb_hstd_dma_handler(void);
void usb_hstd_init_usb_message(usb_utr_t* ptr);

/* r_usb_pinthandler_usbip0.c */
void usb_pstd_usb_handler(uint32_t sense);

/* r_usb_cinthandler_usbip1.c */
void usb2_hstd_usb_handler(void);
void usb2_hstd_dma_handler(void);

/* r_usb_hdriver.c */
uint8_t* usb_hstd_dev_descriptor(usb_utr_t* ptr);
uint8_t* usb_hstd_con_descriptor(usb_utr_t* ptr);
void usb_hstd_device_resume(usb_utr_t* ptr, uint16_t devaddr);
usb_er_t usb_hstd_hcd_snd_mbx(usb_utr_t* ptr, uint16_t msginfo, uint16_t dat, uint16_t* adr, usb_cb_t callback);
void usb_hstd_mgr_snd_mbx(usb_utr_t* ptr, uint16_t msginfo, uint16_t dat, uint16_t res);
void usb_hstd_hcd_task(usb_vp_int_t stacd);
usb_er_t usb_hstd_transfer_start(usb_utr_t* ptr);
void usb_hstd_hcd_rel_mpl(usb_utr_t* ptr, uint16_t n);
usb_er_t usb_hstd_clr_stall(usb_utr_t* ptr, uint16_t pipe, usb_cb_t complete);
usb_er_t usb_hstd_clr_feature(usb_utr_t* ptr, uint16_t addr, uint16_t epnum, usb_cb_t complete);
void usb_hstd_suspend(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bus_int_disable(usb_utr_t* ptr, uint16_t port);
void usb_class_request_complete(usb_utr_t* mess, uint16_t devadr, uint16_t data2);

/* r_usb_hcontrolrw.c */
uint16_t usb_hstd_ctrl_write_start(usb_utr_t* ptr, uint32_t bsize, uint8_t* table);
void usb_hstd_ctrl_read_start(usb_utr_t* ptr, uint32_t bsize, uint8_t* table);
void usb_hstd_status_start(usb_utr_t* ptr);
void usb_hstd_ctrl_end(usb_utr_t* ptr, uint16_t status);
void usb_hstd_setup_start(usb_utr_t* ptr);

/* r_usb_hintfifo.c */
void usb_hstd_brdy_pipe(usb_utr_t* ptr);
void usb_hstd_nrdy_pipe(usb_utr_t* ptr);
void usb_hstd_bemp_pipe(usb_utr_t* ptr);

/* r_usb_hstdfunction.c */
void usb_hstd_bchg0function(usb_utr_t* ptr);
void usb_hstd_ls_connect_function(usb_utr_t* ptr);
void usb_hstd_attach_function(void);
uint16_t usb_hstd_enum_function1(void);
uint16_t usb_hstd_enum_function2(uint16_t* enummode);
void usb_hstd_enum_function4(uint16_t* reqnum, uint16_t* enummode, uint16_t devaddr);
void usb_hstd_enum_function5(void);
void usb_hstd_ovrcr0function(usb_utr_t* ptr);
void usb_registration(usb_utr_t* ptr);
void usb_hdriver_init(usb_utr_t* ptr, usb_cfg_t* cfg);
void usb_hstd_class_driver_start(usb_utr_t* ptr);

/* r_usb_hreg_abs */
void usb_hstd_set_hub_port(usb_utr_t* ptr, uint16_t addr, uint16_t upphub, uint16_t hubport);
int usb_hstd_interrupt_handler(usb_utr_t* ptr);
uint16_t usb_hstd_chk_attach(usb_utr_t* ptr, uint16_t port);
void usb_hstd_chk_clk(usb_utr_t* ptr, uint16_t port, uint16_t event);
void usb_hstd_detach_process(usb_utr_t* ptr, uint16_t port);
void usb_hstd_read_lnst(usb_utr_t* ptr, uint16_t port, uint16_t* buf);
void usb_hstd_attach_process(usb_utr_t* ptr, uint16_t port);
void usb_hstd_chk_sof(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bus_reset(usb_utr_t* ptr, uint16_t port);
void usb_hstd_resume_process(usb_utr_t* ptr, uint16_t port);
uint16_t usb_hstd_support_speed_check(usb_utr_t* ptr, uint16_t port);

/* r_usb_hlibusbip.c */
void usb_hstd_set_dev_addr(usb_utr_t* ptr, uint16_t addr, uint16_t speed, uint16_t port);
void usb_hstd_bchg_enable(usb_utr_t* ptr, uint16_t port);
void usb_hstd_set_uact(usb_utr_t* ptr, uint16_t port);
void usb_hstd_ovrcr_enable(usb_utr_t* ptr, uint16_t port);
void usb_hstd_ovrcr_disable(usb_utr_t* ptr, uint16_t port);
void usb_hstd_attch_enable(usb_utr_t* ptr, uint16_t port);
void usb_hstd_attch_disable(usb_utr_t* ptr, uint16_t port);
void usb_hstd_dtch_enable(usb_utr_t* ptr, uint16_t port);
void usb_hstd_dtch_disable(usb_utr_t* ptr, uint16_t port);
void usb_hstd_set_pipe_register(usb_utr_t* ptr, uint16_t pipeno, uint16_t* tbl);
uint16_t usb_hstd_get_rootport(usb_utr_t* ptr, uint16_t addr);
uint16_t usb_hstd_chk_dev_addr(usb_utr_t* ptr, uint16_t addr, uint16_t rootport);
uint16_t usb_hstd_get_dev_speed(usb_utr_t* ptr, uint16_t addr);
void usb_hstd_bchg_disable(usb_utr_t* ptr, uint16_t port);

/* r_usb_hsignal.c */
void usb_hstd_vbus_control(usb_utr_t* ptr, uint16_t port, uint16_t command);
void usb_hstd_suspend_process(usb_utr_t* ptr, uint16_t port);
void usb_hstd_attach(usb_utr_t* ptr, uint16_t result, uint16_t port);
void usb_hstd_detach(usb_utr_t* ptr, uint16_t port);

/* r_usb_hmanager.c */
void usb_hstd_notif_ator_detach(usb_utr_t* ptr, uint16_t result, uint16_t port);
void usb_hstd_ovcr_notifiation(usb_utr_t* ptr, uint16_t port);
void usb_hstd_status_result(usb_utr_t* ptr, uint16_t port, uint16_t result);
void usb_hstd_enum_get_descriptor(usb_utr_t* ptr, uint16_t addr, uint16_t cnt_value);
void usb_hstd_enum_set_address(usb_utr_t* ptr, uint16_t addr, uint16_t setaddr);
void usb_hstd_enum_set_configuration(usb_utr_t* ptr, uint16_t addr, uint16_t confnum);
void usb_hstd_enum_dummy_request(usb_utr_t* ptr, uint16_t addr, uint16_t CntValue);
void usb_hstd_electrical_test_mode(usb_utr_t* ptr, uint16_t product_id, uint16_t port);
void usb_hstd_mgr_task(usb_vp_int_t stacd);
uint16_t usb_hstd_get_string_desc(usb_utr_t* ptr, uint16_t addr, uint16_t string, usb_cb_t complete);
void usb_hstd_submit_result(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_hstd_mgr_suspend(usb_utr_t* ptr, uint16_t info);
void usb_hstd_device_state_ctrl(usb_utr_t* ptr, uint16_t devaddr, uint16_t msginfo);
void usb_hstd_device_state_ctrl2(
    usb_utr_t* ptr, usb_cb_t complete, uint16_t devaddr, uint16_t msginfo, uint16_t mgr_msginfo);
void usb_hstd_mgr_reset(usb_utr_t* ptr, uint16_t addr);
void usb_hstd_mgr_resume(usb_utr_t* ptr, uint16_t info);
uint16_t usb_hstd_get_config_desc(usb_utr_t* ptr, uint16_t addr, uint16_t length, usb_cb_t complete);
uint16_t usb_hstd_set_feature(usb_utr_t* ptr, uint16_t addr, uint16_t epnum, usb_cb_t complete);
uint16_t usb_hstd_std_req_check(uint16_t errcheck);

/* r_usb_hhubsys.c */
uint16_t usb_hhub_check_descriptor(uint8_t* table, uint16_t spec);
void usb_hhub_open(usb_utr_t* ptr, uint16_t devaddr, uint16_t data2);
void usb_hhub_close(usb_utr_t* ptr, uint16_t hubaddr, uint16_t data2);
void usb_hhub_registration(usb_utr_t* ptr, usb_hcdreg_t* callback);
uint16_t usb_hhub_get_hub_information(usb_utr_t* ptr, uint16_t hubaddr, usb_cb_t complete);
uint16_t usb_hhub_get_port_information(usb_utr_t* ptr, uint16_t hubaddr, uint16_t port, usb_cb_t complete);
void usb_hhub_task(usb_vp_int_t stacd);
uint16_t usb_hhub_get_string_descriptor1(usb_utr_t* ptr, uint16_t devaddr, uint16_t index, usb_cb_t complete);
uint16_t usb_hhub_get_string_descriptor1check(uint16_t errcheck);
uint16_t usb_hhub_get_string_descriptor2(usb_utr_t* ptr, uint16_t devaddr, uint16_t index, usb_cb_t complete);
uint16_t usb_hhub_get_string_descriptor_to_check(uint16_t errcheck);

/* r_usb_hbc.c */
#if USB_CFG_BC == USB_CFG_ENABLE
void usb_hstd_pddetint_process(usb_utr_t* ptr, uint16_t port);
#endif /* USB_CFG_BC == USB_CFG_ENABLE */

/* r_usb_hostelectrical.c */
void usb_hstd_test_stop(usb_utr_t* ptr, uint16_t port);
void usb_hstd_test_signal(usb_utr_t* ptr, uint16_t port, uint16_t command);
void usb_hstd_test_uact_ctrl(usb_utr_t* ptr, uint16_t port, uint16_t command);
void usb_hstd_test_vbus_ctrl(usb_utr_t* ptr, uint16_t port, uint16_t command);
void usb_hstd_test_bus_reset(usb_utr_t* ptr, uint16_t port);
void usb_hstd_test_suspend(usb_utr_t* ptr, uint16_t port);
void usb_hstd_test_resume(usb_utr_t* ptr, uint16_t port);

/* r_usb_cscheduler.c */
usb_er_t usb_cstd_rec_msg(uint8_t id, usb_msg_t** mess, usb_tm_t tm);
usb_er_t usb_cstd_snd_msg(uint8_t id, usb_msg_t* mess);
usb_er_t usb_cstd_isnd_msg(uint8_t id, usb_msg_t* mess);
usb_err_t usb_cstd_wai_msg(uint8_t id, usb_msg_t* mess, usb_tm_t times);
void usb_cstd_wait_scheduler(void);
usb_err_t usb_cstd_pget_blk(uint8_t id, usb_utr_t** blk);
usb_err_t usb_cstd_rel_blk(uint8_t id, usb_utr_t* blk);
void usb_cstd_sche_init(void);
usb_err_t usb_cstd_check(usb_er_t err);
// uint8_t             usb_cstd_check_schedule(void);
void usb_cstd_scheduler(void);
void usb_cstd_set_task_pri(uint8_t tasknum, uint8_t pri);

/***********************************************************************************************************************
 Function Name   : usb_cstd_check_schedule
 Description     : Check schedule flag to see if caller's "time has come", then clear it.
 Argument        : none
 Return          : flg                       : usb_scheduler_schedule_flag
 ***********************************************************************************************************************/
inline static uint8_t usb_cstd_check_schedule(void)
{
    uint8_t flg;

    flg                         = usb_scheduler_schedule_flag;
    usb_scheduler_schedule_flag = USB_FLGCLR;
    return flg;
} /* End of function usb_cstd_check_schedule() */

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/* r_usb_pinthandler_usbip0.c */
void usb_pstd_usb_handler(uint32_t sense);

/* r_usb_pdriver.c */
void usb_pstd_pcd_task(void);
usb_er_t usb_pstd_set_submitutr(usb_utr_t* utrmsg);
void usb_pstd_clr_alt(void);
void usb_pstd_clr_mem(void);
void usb_pstd_set_config_num(uint16_t Value);
void usb_pstd_clr_eptbl_index(void);
uint16_t usb_pstd_get_interface_num(uint16_t con_num);
uint16_t usb_pstd_get_alternate_num(uint16_t con_num, uint16_t int_num);
void usb_pstd_set_eptbl_index(uint16_t con_num, uint16_t int_num, uint16_t alt_num);
uint16_t usb_pstd_chk_remote(void);
uint8_t usb_pstd_get_current_power(void);
void usb_pstd_set_pipe_register(uint16_t pipeno, uint16_t* tbl);
uint16_t usb_pstd_chk_pipe_info(uint16_t speed, uint16_t* ep_tbl, uint8_t* descriptor);
// void                usb_registration(usb_ctrl_t *ctrl, usb_cfg_t *cfg); // Commented out by Rohan

/* r_usb_pcontrolrw.c */
uint16_t usb_pstd_ctrl_read(uint32_t bsize, uint8_t* table);
void usb_pstd_ctrl_write(uint32_t bsize, uint8_t* table);
void usb_pstd_ctrl_end(uint16_t status);

/* r_usb_pintfifo.c */
void usb_pstd_brdy_pipe(uint16_t bitsts);
void usb_pstd_nrdy_pipe(uint16_t bitsts);
void usb_pstd_bemp_pipe(uint16_t bitsts);

/* r_usb_pstdfunction.c */
void usb_pstd_attach_function(void);
void usb_pstd_busreset_function(void);
void usb_pstd_suspend_function(void);
void usb_p_registration(usb_ctrl_t* ctrl, usb_cfg_t* cfg);
void usb_pdriver_init(usb_ctrl_t* ctrl, usb_cfg_t* cfg);

/* r_usb_preg_abs.c */
int usb_pstd_interrupt_handler(uint16_t* type, uint16_t* status);
void usb_pstd_save_request(void);
uint16_t usb_pstd_chk_configured(void);
void usb_pstd_bus_reset(void);
void usb_pstd_remote_wakeup(void);
void usb_pstd_test_mode(void);
void usb_pstd_attach_process(void);
void usb_pstd_detach_process(void);
void usb_pstd_suspend_process(void);
void usb_pstd_resume_process(void);
uint16_t usb_pstd_chk_vbsts(void);
void usb_pstd_set_stall(uint16_t pipe);
void usb_pstd_set_stall_pipe0(void);

/* r_usb_pstdrequest.c */
void usb_pstd_stand_req0(void);
void usb_pstd_stand_req1(void);
void usb_pstd_stand_req2(void);
void usb_pstd_stand_req3(void);
void usb_pstd_stand_req4(void);
void usb_pstd_stand_req5(void);
void usb_pstd_set_feature_function(void);

/* peri_processing.c */
void usb_peri_registration(usb_ctrl_t* ctrl, usb_cfg_t* cfg);
void usb_peri_devdefault(usb_utr_t* ptr, uint16_t mode, uint16_t data2);
uint16_t usb_peri_pipe_info(uint8_t* table, uint16_t speed, uint16_t length);
void usb_peri_configured(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_peri_detach(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_peri_suspended(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_peri_resume(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_peri_interface(usb_utr_t* ptr, uint16_t data1, uint16_t data2);
void usb_peri_class_request(usb_setup_t* preq, uint16_t ctsq);
void usb_peri_class_request_ioss(usb_setup_t* req);
void usb_peri_class_request_rwds(usb_setup_t* req);
void usb_peri_class_request_wds(usb_setup_t* req);
void usb_peri_other_request(usb_setup_t* req);
void usb_peri_class_request_wnss(usb_setup_t* req);
void usb_peri_class_request_rss(usb_setup_t* req);
void usb_peri_class_request_wss(usb_setup_t* req);

#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI */

#if defined(USB_CFG_PVND_USE)
extern void usb_pvnd_read_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
extern void usb_pvnd_write_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
#endif /* defined(USB_CFG_PVND_USE) */

#if defined(USB_CFG_PCDC_USE)
extern void pcdc_read_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
extern void pcdc_write_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
#endif /* defined(USB_CFG_PCDC_USE) */

#if defined(USB_CFG_PHID_USE)
extern void usb_phid_read_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
extern void usb_phid_write_complete(usb_utr_t* mess, uint16_t data1, uint16_t data2);
#endif /* defined(USB_CFG_PHID_USE) */

#if defined(USB_CFG_PMSC_USE)
extern void usb_pmsc_task(void);
#endif /* defined(USB_CFG_PMSC_USE) */

#if defined(USB_CFG_HCDC_USE)
extern void hcdc_read_complete(usb_utr_t* mess, uint16_t devadr, uint16_t data2);
extern void hcdc_write_complete(usb_utr_t* mess, uint16_t devadr, uint16_t data2);
#endif /* defined(USB_CFG_HCDC_USE) */

#if defined(USB_CFG_HHID_USE)
extern void hhid_read_complete(usb_utr_t* mess, uint16_t devadr, uint16_t data2);
extern void hhid_write_complete(usb_utr_t* mess, uint16_t devadr, uint16_t data2);
#endif /* defined(USB_CFG_HHID_USE) */

#if defined(USB_CFG_HMSC_USE)
extern void usb_hmsc_strg_cmd_complete(usb_utr_t* mess, uint16_t devadr, uint16_t data2);
extern void usb_hmsc_drive_complete(usb_utr_t* ptr, uint16_t addr, uint16_t data2);
#endif /* defined(USB_CFG_HMSC_USE) */

extern uint16_t g_usb_pstd_eptbl[];

#if defined(USB_CFG_PMSC_USE)
extern void usb_pmsc_receive_cbw(void);
extern void usb_pmsc_get_max_lun(uint16_t value, uint16_t index, uint16_t length);
extern void usb_pmsc_mass_strage_reset(uint16_t value, uint16_t index, uint16_t length);
#endif /* defined(USB_CFG_PMSC_USE) */

void usb_hstd_device_information(usb_utr_t* ptr, uint16_t devaddr, uint16_t* tbl);
void usb_pstd_device_information(usb_utr_t* ptr, uint16_t* tbl);
usb_er_t usb_pstd_set_stall_clr_feature(usb_utr_t* ptr, usb_cb_t complete, uint16_t pipe);

#if USB_CFG_COMPLIANCE == USB_CFG_ENABLE
void usb_compliance_disp(void*);
#endif /* USB_CFG_COMPLIANCE == USB_CFG_ENABLE */

#if USB_CFG_BC == USB_CFG_ENABLE
/* BC State change function */
void usb_hstd_bc_err(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_init_vb(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_det_at(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_cdp_dt(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_sdp_dt(usb_utr_t* ptr, uint16_t port);
/* BC State entry/exit function */
void usb_hstd_bc_det_entry(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_det_exit(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_cdp_entry(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_cdp_exit(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_sdp_entry(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_sdp_exit(usb_utr_t* ptr, uint16_t port);
void usb_hstd_bc_dcp_entry(usb_utr_t* ptr, uint16_t port);

void usb_hstd_pddetint_process(usb_utr_t* ptr, uint16_t port);
void usb_pstd_bc_detect_process(void);
#endif /* USB_CFG_BC == USB_CFG_ENABLE */
#endif /* R_USB_EXTERN_H */
/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
