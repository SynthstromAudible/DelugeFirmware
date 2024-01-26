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
 * File Name    : r_usb_hintfifo.c
 * Description  : USB Host and FIFO interrupt code
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
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_reg_access.h"

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Renesas Abstracted Host FIFO Interrupt functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_hstd_brdy_pipe
 Description     : BRDY Interrupt
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_brdy_pipe(usb_utr_t* ptr)
{
    uint16_t bitsts;

    bitsts = ptr->status;

    /* When operating by the host function, usb_hstd_brdy_pipe() is executed without fail because */
    /* only one BRDY message is issued even when the demand of PIPE0 and PIPEx has been generated at the same time. */
    if (true || (bitsts & USB_BRDY0) == USB_BRDY0) // Changed by Rohan - now we only come to this function for pipe0
    {
        /* Branch  by the Control transfer stage management */
        switch (g_usb_hstd_ctsq[ptr->ip])
        {
            /* Data stage of Control read transfer */
            case USB_DATARD:

                switch (usb_pstd_read_data(
                    USB_PIPE0, USB_CUSE)) // Modified by Rohan to call the PSTD function, since they're the same now
                {
                    /* End of data read */
                    case USB_READEND:
                    /* End of data read */
                    case USB_READSHRT:

                        usb_hstd_status_start(ptr);

                        break;

                    /* Continue of data read */
                    case USB_READING:
                        break;

                    /* FIFO access error */
                    case USB_READOVER:

                        USB_PRINTF0("### Receive data over PIPE0 \n");
                        /* Control Read/Write End */
                        usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_OVR);

                        break;

                    /* FIFO access error */
                    case USB_FIFOERROR:

                        USB_PRINTF0("### FIFO access error \n");
                        /* Control Read/Write End */
                        usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_ERR);

                        break;

                    default:
                        break;
                }

                break;

            /* Data stage of Control read transfer */
            case USB_DATARDCNT:

                switch (usb_pstd_read_data(
                    USB_PIPE0, USB_CUSE)) // Modified by Rohan to call the PSTD function, since they're the same now
                {
                    /* End of data read */
                    case USB_READEND:

                        /* Control Read/Write End */
                        usb_hstd_ctrl_end(ptr, (uint16_t)USB_CTRL_READING);

                        break;

                    /* End of data read */
                    case USB_READSHRT:

                        /* Control Read/Write Status */
                        usb_hstd_status_start(ptr);

                        break;

                    /* Continue of data read */
                    case USB_READING:
                        break;

                    /* FIFO access error */
                    case USB_READOVER:

                        USB_PRINTF0("### Receive data over PIPE0 \n");
                        /* Control Read/Write End */
                        usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_OVR);

                        break;

                    /* FIFO access error */
                    case USB_FIFOERROR:

                        USB_PRINTF0("### FIFO access error \n");
                        /* Control Read/Write End */
                        usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_ERR);

                        break;

                    default:
                        break;
                }

                break;

            /* Status stage of Control write (NoData control) transfer */
            case USB_STATUSWR:

                /* Control Read/Write End */
                usb_hstd_ctrl_end(ptr, (uint16_t)USB_CTRL_END);

                break;

            default:
                break;
        }
    }
    /* BRDY interrupt */
    // usb_hstd_brdy_pipe_process(ptr, bitsts);
} /* End of function usb_hstd_brdy_pipe() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_nrdy_pipe
 Description     : NRDY Interrupt
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_nrdy_pipe(usb_utr_t* ptr)
{
    uint16_t buffer;
    uint16_t bitsts;

    bitsts = ptr->status;

    /* When operating by the host function, usb_hstd_nrdy_pipe() is executed without fail because */
    /* only one NRDY message is issued even when the demand of PIPE0 and PIPEx has been generated at the same time. */
    if ((bitsts & USB_NRDY0) == USB_NRDY0)
    {
        /* Get Pipe PID from pipe number */
        buffer = usb_cstd_get_pid(ptr, (uint16_t)USB_PIPE0);

        /* STALL ? */
        if ((buffer & USB_PID_STALL) == USB_PID_STALL)
        {
            USB_PRINTF0("### STALL Pipe 0\n");
            /* PIPE0 STALL call back */
            usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_STALL);
        }
        else
        {
            /* Ignore count */
            g_usb_hstd_ignore_cnt[ptr->ip][USB_PIPE0]++;
            USB_PRINTF2("### IGNORE Pipe %d is %d times \n", USB_PIPE0, g_usb_hstd_ignore_cnt[ptr->ip][USB_PIPE0]);

            /* Pipe error check */
            if (USB_PIPEERROR == g_usb_hstd_ignore_cnt[ptr->ip][USB_PIPE0])
            {
                /* Control Data Stage Device Ignore X 3 call back */
                usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_ERR);
            }
            else
            {
                /* Control Data Stage Retry */
                /* 5ms wait */
                usb_cpu_delay_xms((uint16_t)5);

                /* PIPE0 Send IN or OUT token */
                usb_cstd_set_buf(ptr, (uint16_t)USB_PIPE0);
            }
        }
    }
    /* Nrdy Pipe interrupt */
    usb_hstd_nrdy_pipe_process(ptr, bitsts);
} /* End of function usb_hstd_nrdy_pipe() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_bemp_pipe
 Description     : BEMP interrupt
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_bemp_pipe(usb_utr_t* ptr)
{
    uint16_t buffer;
    uint16_t bitsts;

    bitsts = ptr->status;

    /* When operating by the host function, usb_hstd_bemp_pipe() is executed without fail because */
    /* only one BEMP message is issued even when the demand of PIPE0 and PIPEx has been generated at the same time. */
    if (true || (bitsts & USB_BEMP0) == USB_BEMP0) // Modified by Rohan - now we only come here for BEMP0
    {

        buffer = usb_cstd_get_pid(ptr, (uint16_t)USB_PIPE0); /* Get Pipe PID from pipe number */
        if ((buffer & USB_PID_STALL) == USB_PID_STALL)       /* MAX packet size error ? */
        {
            USB_PRINTF0("### STALL Pipe 0\n");
            usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_STALL); /* PIPE0 STALL call back */
        }
        else
        {
            /* Branch  by the Control transfer stage management */
            switch (g_usb_hstd_ctsq[ptr->ip])
            {
                /* Continuas of data stage (Control write) */
                case USB_DATAWR:

                    /* Buffer to CFIFO data write */
                    switch (usb_hstd_write_data(ptr, USB_PIPE0, USB_CUSE))
                    {
                        /* End of data write */
                        case USB_WRITESHRT:

                            g_usb_hstd_ctsq[ptr->ip] = USB_STATUSWR;      /* Next stage is Control write status stage */
                            hw_usb_set_bempenb(ptr, (uint16_t)USB_PIPE0); /* Enable Empty Interrupt */

                            // usb_cstd_nrdy_enable(ptr, (uint16_t)USB_PIPE0); /* Enable Not Ready Interrupt */
                            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling
                            //  them at all was causing freezes (or was it UART / SD lockups?), right since we first
                            //  added this "new" (2016) USB driver (in 2019).

                            break;

                        /* End of data write (not null) */
                        case USB_WRITEEND:
                        /* Continue of data write */
                        case USB_WRITING:

                            hw_usb_set_bempenb(ptr, (uint16_t)USB_PIPE0); /* Enable Empty Interrupt */

                            // usb_cstd_nrdy_enable(ptr, (uint16_t)USB_PIPE0); /* Enable Not Ready Interrupt */
                            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling
                            //  them at all was causing freezes (or was it UART / SD lockups?), right since we first
                            //  added this "new" (2016) USB driver (in 2019).

                            break;

                        /* FIFO access error */
                        case USB_FIFOERROR:

                            USB_PRINTF0("### FIFO access error \n");
                            usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_ERR); /* Control Read/Write End */

                            break;

                        default:
                            break;
                    }

                    break;

                /* Next stage to Control write data */
                case USB_DATAWRCNT:

                    /* Buffer to CFIFO data write */
                    switch (usb_hstd_write_data(ptr, USB_PIPE0, USB_CUSE))
                    {
                        /* End of data write */
                        case USB_WRITESHRT:

                            g_usb_hstd_ctsq[ptr->ip] = USB_STATUSWR;      /* Next stage is Control write status stage */
                            hw_usb_set_bempenb(ptr, (uint16_t)USB_PIPE0); /* Enable Empty Interrupt */

                            // usb_cstd_nrdy_enable(ptr, (uint16_t)USB_PIPE0); /* Enable Not Ready Interrupt */
                            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling
                            //  them at all was causing freezes (or was it UART / SD lockups?), right since we first
                            //  added this "new" (2016) USB driver (in 2019).

                            break;

                        /* End of data write (not null) */
                        case USB_WRITEEND:

                            usb_hstd_ctrl_end(ptr, (uint16_t)USB_CTRL_WRITING); /* Control Read/Write End */

                            break;

                        /* Continue of data write */
                        case USB_WRITING:

                            hw_usb_set_bempenb(ptr, (uint16_t)USB_PIPE0); /* Enable Empty Interrupt */

                            // usb_cstd_nrdy_enable(ptr, (uint16_t)USB_PIPE0); /* Enable Not Ready Interrupt */
                            //  We ignore NRDY interrupts anyway, as there are tons of them continuously, and enabling
                            //  them at all was causing freezes (or was it UART / SD lockups?), right since we first
                            //  added this "new" (2016) USB driver (in 2019).

                            break;

                        /* FIFO access error */
                        case USB_FIFOERROR:

                            USB_PRINTF0("### FIFO access error \n");
                            usb_hstd_ctrl_end(ptr, (uint16_t)USB_DATA_ERR); /* Control Read/Write End */

                            break;

                        default:
                            break;
                    }
                    break;

                /* End of data stage (Control write) */
                case USB_STATUSWR:

                    usb_hstd_status_start(ptr);

                    break;

                /* Status stage of Control read transfer */
                case USB_STATUSRD:

                    usb_hstd_ctrl_end(ptr, (uint16_t)USB_CTRL_END); /* Control Read/Write End */

                    break;

                default:
                    break;
            }
        }
    }
    /* BEMP interrupt */
    // usb_hstd_bemp_pipe_process(ptr, bitsts);
} /* End of function usb_hstd_bemp_pipe() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
