#include "libdeluge/system.h"
#include <cstdint>

// HAL state read by the scheduler's ISR guard (declared in
// RZA1/intc/devdrv_intc.h): the id of the interrupt being serviced, 0 when
// none. Tests never run from an ISR.
volatile uint32_t intc_func_active = 0;

// Host/test build has no preemptive interrupts — code never runs in ISR context.
extern "C" bool deluge_in_interrupt(void) {
	return false;
}
