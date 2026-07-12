#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "util/misc.h"
#include <iostream>

class MockDisplay : public deluge::hid::Display {
public:
	MockDisplay() = default;

	~MockDisplay() = default;

	constexpr size_t getNumBrowserAndMenuLines() { return 0; };

	void displayPopup(char const* newText, int8_t numFlashes = 3, bool alignRight = false, uint8_t = 255, int32_t = 1,
	                  PopupType type = PopupType::GENERAL) {};

	void popupText(char const* text, PopupType type = PopupType::GENERAL) {};
	void popupTextTemporary(char const* text, PopupType type = PopupType::GENERAL) {};

	void cancelPopup() {};
	void freezeWithError(char const* text) { std::cout << text << std::endl; };
	void displayError(Error error) { std::cout << util::to_underlying(error) << std::endl; };

	void removeWorkingAnimation() {};

	// Loading animations
	void displayLoadingAnimation() {};
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) {};
	void removeLoadingAnimation() {};

	bool hasPopup() { return false; };
	bool hasPopupOfType(PopupType type) { return false; };

	void consoleText(char const* text) {};

	void timerRoutine() {};
};

extern "C" void freezeWithError(char const* error) {
	display->freezeWithError(error);
}

extern "C" void displayPopup(char const* text) {
	display->displayPopup(text);
}

MockDisplay mockdisplay;
deluge::hid::Display* display = &mockdisplay;
