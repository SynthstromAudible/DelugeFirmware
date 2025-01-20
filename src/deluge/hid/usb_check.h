#pragma once

#ifdef __cplusplus
extern "C" {
#endif

uint16_t check_usb_power_status(void);

#define USB_POWER_ATTACHED 0x0040u

#ifdef __cplusplus
}
#endif
