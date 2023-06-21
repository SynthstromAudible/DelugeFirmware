#pragma once
#include "definitions_cxx.hpp"
#include <array>
#include <string_view>

extern "C" {
#include "util/cfunctions.h"
}

class NumericLayer;
class NumericLayerScrollingText;

enum DisplayPopupType {
	NONE,
	GENERAL,     // Default popup type, if not specified
	PROBABILITY, // Popup shown when editing note or row probability
	             // Note: Add here more popup types
};

namespace deluge::hid {

enum struct DisplayType { OLED, SEVENSEG };

class Display {
public:
	Display(DisplayType displayType) : displayType(displayType) {}

	virtual ~Display() = default;

	constexpr virtual size_t getNumBrowserAndMenuLines() = 0;

	virtual void setText(std::string_view newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	                     uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	                     int32_t scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false){};

	virtual void displayPopup(char const* newText, int8_t numFlashes = 3, bool alignRight = false,
	                          uint8_t drawDot = 255, int32_t blinkSpeed = 1,
	                          DisplayPopupType type = DisplayPopupType::GENERAL) = 0;

	virtual void displayPopup(uint8_t val, int8_t numFlashes = 3, bool alignRight = false, uint8_t drawDot = 255,
	                          int32_t blinkSpeed = 1, DisplayPopupType type = DisplayPopupType::GENERAL) {
		char valStr[4] = {0};
		intToString(val, valStr, 1);
		displayPopup(valStr, numFlashes, alignRight, drawDot, blinkSpeed, type);
	}

	virtual void displayPopup(char const* shortLong[2], int8_t numFlashes = 3, bool alignRight = false,
	                          uint8_t drawDot = 255, int32_t blinkSpeed = 1,
	                          DisplayPopupType type = DisplayPopupType::GENERAL) {
		displayPopup(have7SEG() ? shortLong[0] : shortLong[1], numFlashes, alignRight, drawDot, blinkSpeed, type);
	}

	virtual void popupText(char const* text, DisplayPopupType type = DisplayPopupType::GENERAL) = 0;
	virtual void popupTextTemporary(char const* text, DisplayPopupType type = DisplayPopupType::GENERAL) = 0;

	virtual void setNextTransitionDirection(int8_t thisDirection){};

	virtual void cancelPopup() = 0;
	virtual void freezeWithError(char const* text) = 0;
	virtual bool isLayerCurrentlyOnTop(NumericLayer* layer) = 0;
	virtual void displayError(Error error) = 0;

	virtual void removeWorkingAnimation() = 0;

	// Loading animations
	virtual void displayLoadingAnimation(){};
	virtual void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) = 0;
	virtual void removeLoadingAnimation() = 0;

	virtual bool hasPopup() = 0;
	virtual bool hasPopupOfType(DisplayPopupType type) = 0;

	virtual void consoleText(char const* text) = 0;

	virtual void timerRoutine() = 0;

	virtual void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false) {}
	virtual int32_t getEncodedPosFromLeft(int32_t textPos, char const* text, bool* andAHalf) { return 0; }
	virtual void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                           int32_t blinkPos = -1, bool blinkImmediately = false) {}
	virtual NumericLayerScrollingText* setScrollingText(char const* newText, int32_t startAtPos = 0,
	                                                    int32_t initialDelay = 600, int count = -1) {
		return nullptr;
	}

	virtual std::array<uint8_t, kNumericDisplayLength> getLast() { return {0}; }; // to match SevenSegment

	bool haveOLED() { return displayType == DisplayType::OLED; }
	bool have7SEG() { return displayType == DisplayType::SEVENSEG; }

private:
	DisplayType displayType;
};

} // namespace deluge::hid

extern deluge::hid::Display* display;

namespace deluge::hid::display {
void swapDisplayType();
// physical screen is oled
extern bool have_oled_screen;
} // namespace deluge::hid::display

extern "C" void consoleTextIfAllBootedUp(char const* text);
