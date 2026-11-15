// The display's C entry points, forwarding to `display` (mock_display.cpp), for a spec binary whose code reaches them
// through FREEZE_WITH_ERROR and the C popup call. A binary with a freeze policy of its own (mock_panic.cpp records,
// mock_storage.cpp aborts) links that instead.
#include "hid/display/display.h"

extern "C" void freezeWithError(char const* error) {
	display->freezeWithError(error);
}

extern "C" void displayPopup(char const* text) {
	display->displayPopup(text);
}
