#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_basic_define.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"

uint16_t check_usb_power_status(void) {
	uint16_t status = usb_pstd_chk_vbsts();
	return status;
}
