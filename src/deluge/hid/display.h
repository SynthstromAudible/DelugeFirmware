#pragma once
#include "definitions.h"
#include "display/oled.h"
#include "display/numeric_driver.h"
#include "hid/display.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"

#ifdef __cplusplus
enum class DisplayType { OLED, SevenSegment };

template <DisplayType display_type>
class Display {};

template <>
class Display<DisplayType::OLED> {
public:
	static const DisplayType type = DisplayType::OLED;
	void setText(char const* newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false) {}

	void displayPopup(char const* newText, int8_t numFlashes = 3, bool = false, uint8_t = 255, int = 1) {
		OLED::popupText(newText, !numFlashes);
	}

	void popupText(char const* text) { OLED::popupText(text, true); }
	void popupTextTemporary(char const* text) { OLED::popupText(text, false); }

	void setNextTransitionDirection(int8_t thisDirection) {}

	void cancelPopup() { OLED::removePopup(); }
	void freezeWithError(char const* text) { OLED::freezeWithError(text); }
	bool isLayerCurrentlyOnTop(NumericLayer* layer);
	void displayError(int error);

	void removeWorkingAnimation() { OLED::removeWorkingAnimation(); }

	// Loading animations
	void displayLoadingAnimation() {}
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) {
		OLED::displayWorkingAnimation(text);
	}
	void removeLoadingAnimation() { OLED::removeWorkingAnimation(); }

	bool hasPopup() { return OLED::isPopupPresent(); }

	void consoleText(char const* text) { OLED::consoleText(text); }

	void timerRoutine() { OLED::timerRoutine(); }

	// DUMMIES
	// These should only ever get called by code that works with the 7SEG
	void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false) {}
	int getEncodedPosFromLeft(int textPos, char const* text, bool* andAHalf) { return 0; }
	void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                   int blinkPos = -1, bool blinkImmediately = false) {}
	NumericLayerScrollingText* setScrollingText(char const* newText, int startAtPos = 0, int initialDelay = 600) {
		return nullptr;
	}

	NumericLayer* topLayer = nullptr;
};

template <>
class Display<DisplayType::SevenSegment> : public NumericDriver {
public:
	static const DisplayType type = DisplayType::SevenSegment;
	void consoleText(char const* text) { this->displayPopup(text); }
	void popupText(char const* text) { this->displayPopup(text); }
	void popupTextTemporary(char const* text) { this->displayPopup(text); }

	void removeWorkingAnimation() {}

	// Loading animations
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) {
		NumericDriver::displayLoadingAnimation(delayed, transparent);
	}
	void removeLoadingAnimation() { NumericDriver::removeTopLayer(); }

	bool hasPopup() { return this->popupActive; }
	void displayError(int error);
};

#if HAVE_OLED
using DisplayActual = Display<DisplayType::OLED>;
#else
using DisplayActual = Display<DisplayType::SevenSegment>;
#endif

extern DisplayActual display;
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

void consoleTextIfAllBootedUp(char const* text);

#ifdef __cplusplus
}
#endif
