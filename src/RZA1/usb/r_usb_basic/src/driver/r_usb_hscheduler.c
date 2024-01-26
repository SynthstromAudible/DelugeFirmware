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
 * File Name    : r_usb_hscheduler.c
 * Description  : USB Host scheduler code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"

#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
#include "drivers/usb/r_usb_basic/src/hw/inc/r_usb_dmac.h"
#endif /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Macro definitions
 ***********************************************************************************************************************/
#define USB_IDMAX          (11u)        /* Maximum Task ID +1 */
#define USB_PRIMAX         (8u)         /* Maximum Priority number +1 */
#define USB_BLKMAX         (20u)        /* Maximum block */
#define USB_TABLEMAX       (USB_BLKMAX) /* Maximum priority table */
#define USB_WAIT_EVENT_MAX (5u)

/***********************************************************************************************************************
 Private global variables and functions
 ***********************************************************************************************************************/
/* Priority Table */
static usb_msg_t* p_usb_scheduler_table_add[USB_PRIMAX][USB_TABLEMAX];
static uint8_t usb_scheduler_table_id[USB_PRIMAX][USB_TABLEMAX];
static uint8_t usb_scheduler_pri_r[USB_PRIMAX];
static uint8_t usb_scheduler_pri_w[USB_PRIMAX];
static uint8_t usb_scheduler_pri[USB_IDMAX];

/* Schedule Set Flag  */
uint8_t usb_scheduler_schedule_flag; // Whether something's scheduled to happen right now

/* Fixed-sized memory pools */
static usb_utr_t usb_scheduler_block[USB_BLKMAX];
static uint8_t usb_scheduler_blk_flg[USB_BLKMAX];

usb_msg_t* p_usb_scheduler_add_use; // This seems to be the pointer to the actual message that's been scheduled.
uint8_t usb_scheduler_id_use; // Ok, this is the ID (e.g. USB_HCD_MBX) of what message the scheduler has decided we
                              // should use.

/* Wait MSG */
static usb_msg_t* p_usb_scheduler_wait_add[USB_IDMAX][USB_WAIT_EVENT_MAX];
static uint16_t usb_scheduler_wait_counter[USB_IDMAX][USB_WAIT_EVENT_MAX];

/***********************************************************************************************************************
 Renesas Scheduler API functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_cstd_rec_msg
 Description     : Receive a message to the specified id (mailbox).
 Argument        : uint8_t       id          : ID number (mailbox).
                 : usb_msg_t     **mess      : Message pointer
                 : usb_tm_t      tm          : Timeout Value
 Return          : uint16_t                  : USB_OK / USB_ERROR
 ***********************************************************************************************************************/
usb_er_t usb_cstd_rec_msg(uint8_t id, usb_msg_t** mess, usb_tm_t tm)
{
    if ((id < USB_IDMAX) && (usb_scheduler_id_use < USB_IDMAX))
    {
        if (id == usb_scheduler_id_use)
        {
            *mess = p_usb_scheduler_add_use;
            return USB_OK;
        }
    }
    return USB_ERROR;
} /* End of function usb_cstd_rec_msg() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_snd_msg
 Description     : Send a message to the specified id (mailbox).
 Argument        : uint8_t      id           : ID number (mailbox).
                 : usb_msg_t    *mess        : Message pointer
 Return          : usb_er_t                  : USB_OK / USB_ERROR
 ***********************************************************************************************************************/
usb_er_t usb_cstd_snd_msg(uint8_t id, usb_msg_t* mess)
{
    usb_er_t status;

    /* USB interrupt disable */
    usb_cpu_int_disable((usb_utr_t*)mess);
    status = usb_cstd_isnd_msg(id, mess);

    /* USB interrupt enable */
    usb_cpu_int_enable((usb_utr_t*)mess);
    return status;
} /* End of function usb_cstd_snd_msg() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_isnd_msg
 Description     : Send a message to the specified id (mailbox) while executing
                 : an interrupt.
 Argument        : uint8_t      id           : ID number (mailbox).
                 : usb_msg_t    *mess        : Message pointer
 Return          : usb_er_t                  : USB_OK / USB_ERROR
 ***********************************************************************************************************************/
usb_er_t usb_cstd_isnd_msg(uint8_t id, usb_msg_t* mess)
{
    uint8_t usb_pri;   /* Task Priority */
    uint8_t usb_write; /* Priority Table Writing pointer */

    if (id < USB_IDMAX)
    {
        /* Read priority and table pointer */
        usb_pri   = usb_scheduler_pri[id];
        usb_write = usb_scheduler_pri_w[usb_pri];
        if (usb_pri < USB_PRIMAX)
        {
            /* Renewal write pointer */
            usb_write++;
            if (usb_write >= USB_TABLEMAX)
            {
                usb_write = USB_TBLCLR;
            }

            /* Check pointer */
            if (usb_write == usb_scheduler_pri_r[usb_pri])
            {
                return USB_ERROR;
            }

            /* Save message */
            /* Set priority table */
            usb_scheduler_table_id[usb_pri][usb_write]    = id;
            p_usb_scheduler_table_add[usb_pri][usb_write] = mess;
            usb_scheduler_pri_w[usb_pri]                  = usb_write;
            return USB_OK;
        }
    }
    USB_PRINTF0("SND_MSG ERROR !!\n");
    return USB_ERROR;
} /* End of function usb_cstd_isnd_msg() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_pget_blk
 Description     : Get a memory block for the caller.
 Argument        : uint8_t      id           : ID number (mailbox).
                 : usb_utr_t    **blk        : Memory block pointer.
 Return          : usb_err_t                 : USB_OK / USB_ERROR
 ***********************************************************************************************************************/
usb_err_t usb_cstd_pget_blk(uint8_t id, usb_utr_t** blk)
{
    uint8_t usb_s_pblk_c;

    if (id < USB_IDMAX)
    {
        usb_s_pblk_c = USB_CNTCLR;
        while (USB_BLKMAX != usb_s_pblk_c)
        {
            if (USB_FLGCLR == usb_scheduler_blk_flg[usb_s_pblk_c])
            {
                /* Acquire fixed-size memory block */
                *blk                                = &usb_scheduler_block[usb_s_pblk_c];
                usb_scheduler_blk_flg[usb_s_pblk_c] = USB_FLGSET;
                return USB_SUCCESS;
            }
            usb_s_pblk_c++;
        }

        /* Error of BLK Table Full !!  */
        USB_PRINTF1("usb_scBlkFlg[%d][] Full !!\n", id);
    }
    return USB_ERR_NG;
} /* End of function usb_cstd_pget_blk() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_rel_blk
 Description     : Release a memory block.
 Argument        : uint8_t      id           : ID number (mailbox).
                 : usb_utr_t    *blk         : Memory block pointer.
 Return          : usb_err_t                 : USB_OK / USB_ERROR
 ***********************************************************************************************************************/
usb_err_t usb_cstd_rel_blk(uint8_t id, usb_utr_t* blk)
{
    uint16_t usb_s_rblk_c;

    if (id < USB_IDMAX)
    {
        usb_s_rblk_c = USB_CNTCLR;
        while (USB_BLKMAX != usb_s_rblk_c)
        {
            if ((&usb_scheduler_block[usb_s_rblk_c]) == blk)
            {
                /* Release fixed-size memory block */
                usb_scheduler_blk_flg[usb_s_rblk_c] = USB_FLGCLR;
                return USB_SUCCESS;
            }
            usb_s_rblk_c++;
        }

        /* Error of BLK Flag is not CLR !!  */
        USB_PRINTF0("TskBlk NO CLR !!\n");
    }
    return USB_ERR_NG;
} /* End of function usb_cstd_rel_blk() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_wai_msg
 Description     : Runs USB_SND_MSG after running the scheduler the specified
                 : number of times.
 Argument        : uint8_t      id           : ID number (mailbox).
                 : usb_msg_t    *mess        : Message pointer
                 : uint16_t  times           : Timeout value.
 Return          : usb_er_t                  : USB_OK / USB_ERROR.
 ***********************************************************************************************************************/
usb_err_t usb_cstd_wai_msg(uint8_t id, usb_msg_t* mess, usb_tm_t times)
{
    uint8_t i;

    if (id < USB_IDMAX)
    {
        for (i = 0; i < USB_WAIT_EVENT_MAX; i++)
        {
            if (0 == usb_scheduler_wait_counter[id][i])
            {
                p_usb_scheduler_wait_add[id][i]   = mess;
                usb_scheduler_wait_counter[id][i] = times;
                return USB_SUCCESS;
            }
        }
    }

    /* Error !!  */
    USB_PRINTF0("WAI_MSG ERROR !!\n");
    return USB_ERR_NG;
} /* End of function usb_cstd_wai_msg() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_wait_scheduler
 Description     : Schedules a wait request.
 Argument        : none
 Return          : none
 ***********************************************************************************************************************/
void usb_cstd_wait_scheduler(void)
{
    usb_er_t err;
    uint8_t id;
    uint8_t i;

    for (id = 0; id < USB_IDMAX; id++)
    {
        for (i = 0; i < USB_WAIT_EVENT_MAX; i++)
        {
            if (0 != usb_scheduler_wait_counter[id][i])
            {
                usb_scheduler_wait_counter[id][i]--;
                if (0 == usb_scheduler_wait_counter[id][i])
                {
                    err = usb_cstd_snd_msg(id, p_usb_scheduler_wait_add[id][i]);
                    if (USB_OK != err)
                    {
                        usb_scheduler_wait_counter[id][i]++;
                    }
                }
            }
        }
    }
} /* End of function usb_cstd_wait_scheduler() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_sche_init
 Description     : Scheduler initialization.
 Argument        : none
 Return          : none
 ***********************************************************************************************************************/
void usb_cstd_sche_init(void)
{
    uint8_t i;
    uint8_t j;

    /* Initial Scheduler */
    usb_scheduler_id_use        = USB_NULL;
    usb_scheduler_schedule_flag = USB_NULL;

    /* Initialize  priority table pointer and priority table */
    for (i = 0; USB_PRIMAX != i; i++)
    {
        usb_scheduler_pri_r[i] = USB_NULL;
        usb_scheduler_pri_w[i] = USB_NULL;
        for (j = 0; USB_TABLEMAX != j; j++)
        {
            usb_scheduler_table_id[i][j] = USB_IDMAX;
        }
    }

    /* Initialize block table */
    for (i = 0; USB_BLKMAX != i; i++)
    {
        usb_scheduler_blk_flg[i] = USB_NULL;
    }

    /* Initialize priority */
    for (i = 0; USB_IDMAX != i; i++)
    {
        usb_scheduler_pri[i] = (uint8_t)USB_IDCLR;
        for (j = 0; j < USB_WAIT_EVENT_MAX; j++)
        {
            p_usb_scheduler_wait_add[i][j]   = (usb_msg_t*)USB_NULL;
            usb_scheduler_wait_counter[i][j] = USB_NULL;
        }
    }
} /* End of function usb_cstd_sche_init() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_scheduler
 Description     : The scheduler.
 Argument        : none
 Return          : none
 ***********************************************************************************************************************/
void usb_cstd_scheduler(void)
{
    uint8_t usb_pri;  /* Priority Counter */
    uint8_t usb_read; /* Priority Table read pointer */

    /* wait msg */
    usb_cstd_wait_scheduler(); // I think this is some horribly inefficient way of delaying messages that are meant to
                               // have a delay time. Rohan

    /* Priority Table reading */
    usb_pri = USB_CNTCLR;
    while (usb_pri < USB_PRIMAX) // I think this is cycling through all the priorities, from highest priority, til we
                                 // find a message to action.
    {
        // If there are any un-dealt-with messages at this priority level...
        usb_read = usb_scheduler_pri_r[usb_pri];
        if (usb_read != usb_scheduler_pri_w[usb_pri])
        {
            /* Priority Table read pointer increment */
            usb_read++;
            if (usb_read >= USB_TABLEMAX)
            {
                usb_read = USB_TBLCLR;
            }

            /* Set practise message */
            usb_scheduler_id_use                      = usb_scheduler_table_id[usb_pri][usb_read];
            p_usb_scheduler_add_use                   = p_usb_scheduler_table_add[usb_pri][usb_read];
            usb_scheduler_table_id[usb_pri][usb_read] = USB_IDMAX;
            usb_scheduler_pri_r[usb_pri]              = usb_read;
            usb_scheduler_schedule_flag               = USB_FLGSET;
            break;
        }
        else
        {
            usb_pri++;
        }
    }
#if ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE))
    usb_dma_driver(); /* USB DMA driver */
#endif                /* ((USB_CFG_DTC == USB_CFG_ENABLE) || (USB_CFG_DMA == USB_CFG_ENABLE)) */
} /* End of function usb_cstd_scheduler() */

/***********************************************************************************************************************
 Function Name   : usb_cstd_set_task_pri
 Description     : Set a task's priority.
 Argument        : uint8_t      tasknum      : Task id.
                 : uint8_t      pri          : The task priority to be set.
 Return          : none
 ***********************************************************************************************************************/
void usb_cstd_set_task_pri(uint8_t tasknum, uint8_t pri)
{
    if (tasknum < USB_IDMAX)
    {
        if (pri < USB_PRIMAX)
        {
            usb_scheduler_pri[tasknum] = pri;
        }
        else if ((uint8_t)USB_IDCLR == pri)
        {
            usb_scheduler_pri[tasknum] = (uint8_t)USB_IDCLR;
        }
        else
        {
            /* Non */
        }
    }
} /* End of function usb_cstd_set_task_pri() */

#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
