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
 * File Name    : r_usb_pintfifo.c
 * Description  : USB Peripheral FIFO access code
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

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Renesas Abstracted Peripheral FIFO access functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_pstd_brdy_pipe
 Description     : Execute data transfer for the PIPE for which a BRDY interrupt
                 : occurred.
 Arguments       : uint16_t     bitsts       : BRDYSTS register & BRDYENB register.
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_brdy_pipe(uint16_t bitsts)
{
    /* When operating by the peripheral function, usb_pstd_brdy_pipe() is executed with PIPEx request because */
    /* two BRDY messages are issued even when the demand of PIPE0 and PIPEx has been generated at the same time. */
    // if ((bitsts & USB_BRDY0) == USB_BRDY0) { // Changed by Rohan - now we only come here for pipe0
    switch (usb_pstd_read_data(USB_PIPE0, USB_CUSE))
    {
        /* End of data read */
        case USB_READEND:
        /* End of data read */
        case USB_READSHRT:

            hw_usb_clear_brdyenb(USB_NULL, (uint16_t)USB_PIPE0);

            break;

        /* Continue of data read */
        case USB_READING:

            /* PID = BUF */
            usb_cstd_set_buf(USB_NULL, (uint16_t)USB_PIPE0);

            break;

        /* FIFO access error */
        case USB_READOVER:

            USB_PRINTF0("### Receive data over PIPE0 \n");
            /* Clear BVAL */
            hw_usb_set_bclr(USB_NULL, USB_CUSE);
            /* Control transfer stop(end) */
            usb_pstd_ctrl_end((uint16_t)USB_DATA_OVR);

            break;

        /* FIFO access error */
        case USB_FIFOERROR:

            USB_PRINTF0("### FIFO access error \n");

            /* Control transfer stop(end) */
            usb_pstd_ctrl_end((uint16_t)USB_DATA_ERR);

            break;

        default:
            break;
    }

    /*
    }
    else
    {
        // Not PIPE0
        usb_pstd_brdy_pipe_process(bitsts);
    }
*/
} /* End of function usb_pstd_brdy_pipe() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_nrdy_pipe
 Description     : Execute appropriate processing for the PIPE for which a NRDY
                 : interrupt occurred.
 Arguments       : uint16_t     bitsts       : NRDYSTS register & NRDYENB register.
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_nrdy_pipe(uint16_t bitsts)
{
    /* The function for peripheral driver is created here. */
    if ((bitsts & USB_NRDY0) == USB_NRDY0)
    {
        /* Non processing. */
    }
    else
    {
        /* Nrdy Pipe interrupt */
        usb_pstd_nrdy_pipe_process(bitsts);
    }
} /* End of function usb_pstd_nrdy_pipe() */

/***********************************************************************************************************************
 Function Name   : usb_pstd_bemp_pipe
 Description     : Execute data transfer for the PIPE for which a BEMP interrupt
                 : occurred.
 Arguments       : uint16_t     bitsts       : BEMPSTS register & BEMPENB register.
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_bemp_pipe(uint16_t bitsts)
{

    /* When operating by the peripheral function, usb_pstd_bemp_pipe() is executed with PIPEx request because */
    /* two BEMP messages are issued even when the demand of PIPE0 and PIPEx has been generated at the same time. */

    // if ((bitsts & USB_BEMP0) == USB_BEMP0) // Changed by Rohan - now only bemp for pipe 0 comes to this function
    //{
    switch (usb_pstd_write_data(USB_PIPE0, USB_CUSE))
    {
        /* End of data write (not null) */
        case USB_WRITEEND:
        /* End of data write */
        case USB_WRITESHRT:

            /* Enable empty interrupt */
            hw_usb_clear_bempenb(USB_NULL, (uint16_t)USB_PIPE0);

            break;

        /* Continue of data write */
        case USB_WRITING:

            /* PID = BUF */
            usb_cstd_set_buf(USB_NULL, (uint16_t)USB_PIPE0);

            break;

        /* FIFO access error */
        case USB_FIFOERROR:

            USB_PRINTF0("### FIFO access error \n");
            /* Control transfer stop(end) */
            usb_pstd_ctrl_end((uint16_t)USB_DATA_ERR);

            break;

        default:
            break;
    }
#if 0
    }
    else
    {
        /* BEMP interrupt */
        usb_pstd_bemp_pipe_process(bitsts);
    }
#endif
} /* End of function usb_pstd_bemp_pipe() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
