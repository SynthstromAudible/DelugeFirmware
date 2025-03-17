#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "util/misc.h"
#include <iostream>
#include <string_view>

class MockDisplay : public deluge::hid::Display {
public:
	MockDisplay() : deluge::hid::Display(deluge::hid::DisplayType::SEVENSEG) {}

	~MockDisplay() = default;

	constexpr size_t getNumBrowserAndMenuLines() override { return 0; };

	void setText(std::string_view newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int32_t scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false) {};

	void displayPopup(std::string_view newText, int8_t numFlashes = 3, bool alignRight = false, uint8_t = 255,
	                  int32_t = 1, PopupType type = PopupType::GENERAL) {};

	void popupText(std::string_view text, PopupType type = PopupType::GENERAL) {};
	void popupTextTemporary(std::string_view text, PopupType type = PopupType::GENERAL) {};

	void setNextTransitionDirection(int8_t thisDirection) override {};

	void cancelPopup() override {};
	void freezeWithError(std::string_view text) { std::cout << text << std::endl; };
	bool isLayerCurrentlyOnTop(NumericLayer* layer) override { return false; };
	void displayError(Error error) override { std::cout << util::to_underlying(error) << std::endl; };

	void removeWorkingAnimation() override {};

	// Loading animations
	void displayLoadingAnimation() override {};
	void displayLoadingAnimationText(std::string_view text, bool delayed = false, bool transparent = false) {};
	void removeLoadingAnimation() override {};

	bool hasPopup() override { return false; };
	bool hasPopupOfType(PopupType type) override { return false; };

	void consoleText(std::string_view text) {};

	void timerRoutine() override {};

	void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false) override {}
	int32_t getEncodedPosFromLeft(int32_t textPos, std::string_view text, bool* andAHalf) { return 0; }
	void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                   int32_t blinkPos = -1, bool blinkImmediately = false) override {}
	NumericLayerScrollingText* setScrollingText(std::string_view newText, int32_t startAtPos = 0,
	                                            int32_t initialDelay = 600) {
		return nullptr;
	}

	std::array<uint8_t, kNumericDisplayLength> getLast() { return {0}; }; // to match SevenSegment
};

extern "C" void freezeWithError(const char* error) {
	display->freezeWithError(error);
}

extern "C" void displayPopup(const char* text) {
	display->displayPopup(text);
}

MockDisplay mockdisplay;
deluge::hid::Display* display = &mockdisplay;
