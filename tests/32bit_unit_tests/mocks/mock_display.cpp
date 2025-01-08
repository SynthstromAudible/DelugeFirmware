#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "util/misc.h"
#include <iostream>

class MockDisplay : public deluge::hid::Display {
public:
	MockDisplay() : deluge::hid::Display(deluge::hid::DisplayType::SEVENSEG) {}

	~MockDisplay() = default;

	constexpr size_t getNumBrowserAndMenuLines() { return 0; };

	void setText(std::string_view newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int32_t scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false) {};

	void displayPopup(char const* newText, int8_t numFlashes = 3, bool alignRight = false, uint8_t = 255, int32_t = 1,
	                  PopupType type = PopupType::GENERAL) {};

	void popupText(char const* text, PopupType type = PopupType::GENERAL) {};
	void popupTextTemporary(char const* text, PopupType type = PopupType::GENERAL) {};

	void setNextTransitionDirection(int8_t thisDirection) {};

	void cancelPopup() {};
	void freezeWithError(char const* text) { std::cout << text << std::endl; };
	bool isLayerCurrentlyOnTop(NumericLayer* layer) { return false; };
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

	void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false) {}
	int32_t getEncodedPosFromLeft(int32_t textPos, char const* text, bool* andAHalf) { return 0; }
	void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                   int32_t blinkPos = -1, bool blinkImmediately = false) {}
	NumericLayerScrollingText* setScrollingText(char const* newText, int32_t startAtPos = 0,
	                                            int32_t initialDelay = 600) {
		return nullptr;
	}

	std::array<uint8_t, kNumericDisplayLength> getLast() { return {0}; }; // to match SevenSegment
};

extern "C" void freezeWithError(char const* error) {
	display->freezeWithError(error);
}

extern "C" void displayPopup(char const* text) {
	display->displayPopup(text);
}

MockDisplay mockdisplay;
deluge::hid::Display* display = &mockdisplay;
