#include "display.h"
#include "deluge/deluge.h"
#include "io/debug/log.h" // D_PRINTLN — freezeWithError logs the code before it stops the world
#include <cstdio>
#include <cstdlib>

deluge::hid::Display* display = nullptr;
extern "C" void freezeWithError(char const* error) {
	// Log the code BEFORE freezing. Without this the error string only ever reaches the panel, so a
	// freeze captured over RTT shows the crash reporter's stack (fault_pattern.c) with no indication
	// of WHICH assertion fired — and the codes are the whole diagnostic (E380, "periodic walk", ...).
	// Cheap, and it makes an RTT capture self-describing instead of needing someone to read the
	// screen. Deliberately first: `display->freezeWithError` does not return.
	D_PRINTLN("FREEZE_WITH_ERROR: %s", error);
#ifdef DELUGE_HOST
	// On host D_PRINTLN is compiled out of release builds and the display logs nothing either (deluge-rust's display
	// freeze aborts with a fixed message), so a harness run that freezes would not say which code fired.
	std::fprintf(stderr, "FREEZE_WITH_ERROR: %s\n", error);
	std::fflush(stderr);
#endif
	if (display == nullptr) {
		// Before boot has made the display (a failure in deluge_app_init's own boot) there is no panel to freeze on.
		// The code is logged above, and on the device FREEZE_WITH_ERROR has already drawn the pad-grid report.
#ifdef DELUGE_HOST
		std::abort();
#else
		while (true) {
			__asm__ volatile("nop");
		}
#endif
	}
	display->freezeWithError(error);
}

extern "C" void displayPopup(char const* text) {
	display->displayPopup(text);
}

extern "C" void consoleTextIfAllBootedUp(char const* text) {
	if (usbInitializationPeriodComplete()) {
		display->consoleText(text);
	}
}
