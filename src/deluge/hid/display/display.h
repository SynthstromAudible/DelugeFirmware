#pragma once
#include "definitions.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"
#include <array>

// This is distinct from the display _interface_ which is the actual communication system
enum class DisplayType { OLED, SevenSegment };

namespace deluge::hid {

class Display {
public:
	virtual ~Display() = default;

	constexpr virtual DisplayType type() = 0;
	constexpr virtual size_t getNumBrowserAndMenuLines() = 0;

	virtual void setText(std::string_view newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	                     uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	                     int scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false) = 0;

	virtual void displayPopup(char const* newText, int8_t numFlashes = 3, bool = false, uint8_t = 255, int = 1) = 0;

	virtual void popupText(char const* text) = 0;
	virtual void popupTextTemporary(char const* text) = 0;

	virtual void setNextTransitionDirection(int8_t thisDirection) = 0;

	virtual void cancelPopup() = 0;
	virtual void freezeWithError(char const* text) = 0;
	virtual bool isLayerCurrentlyOnTop(NumericLayer* layer) = 0;
	virtual void displayError(int error) = 0;

	virtual void removeWorkingAnimation() = 0;

	// Loading animations
	virtual void displayLoadingAnimation() = 0;
	virtual void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) = 0;
	virtual void removeLoadingAnimation() = 0;

	virtual bool hasPopup() = 0;

	virtual void consoleText(char const* text) = 0;

	virtual void timerRoutine() = 0;

	virtual void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false) {}
	virtual int getEncodedPosFromLeft(int textPos, char const* text, bool* andAHalf) { return 0; }
	virtual void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                           int blinkPos = -1, bool blinkImmediately = false) {}
	virtual NumericLayerScrollingText* setScrollingText(char const* newText, int startAtPos = 0,
	                                                    int initialDelay = 600) {
		return nullptr;
	}

	virtual std::array<uint8_t, kNumericDisplayLength> getLast() { return {0}; }; // to match NumericDriver
};

namespace display {
class OLED : public Display {
public:
	constexpr DisplayType type() override { return DisplayType::OLED; }

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }

	void setText(std::string_view newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false) override {}

	void displayPopup(char const* newText, int8_t numFlashes = 3, bool = false, uint8_t = 255, int = 1) override {
		::OLED::popupText(newText, !numFlashes);
	}

	void popupText(char const* text) override { ::OLED::popupText(text, true); }
	void popupTextTemporary(char const* text) override { ::OLED::popupText(text, false); }

	void setNextTransitionDirection(int8_t thisDirection) override {}

	void cancelPopup() override { ::OLED::removePopup(); }
	void freezeWithError(char const* text) override { ::OLED::freezeWithError(text); }
	bool isLayerCurrentlyOnTop(NumericLayer* layer) override;
	void displayError(int error) override;

	void removeWorkingAnimation() override { ::OLED::removeWorkingAnimation(); }

	// Loading animations
	void displayLoadingAnimation() override {}
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) override {
		::OLED::displayWorkingAnimation(text);
	}
	void removeLoadingAnimation() override { ::OLED::removeWorkingAnimation(); }

	bool hasPopup() override { return ::OLED::isPopupPresent(); }

	void consoleText(char const* text) override { ::OLED::consoleText(text); }

	void timerRoutine() override { ::OLED::timerRoutine(); }

	NumericLayer* topLayer = nullptr;
};

class SevenSegment : public Display, public NumericDriver {
public:
	constexpr DisplayType type() override { return DisplayType::SevenSegment; }

	constexpr size_t getNumBrowserAndMenuLines() override { return 1; }

	void consoleText(char const* text) override { NumericDriver::displayPopup(text); }
	void popupText(char const* text) override { NumericDriver::displayPopup(text); }
	void popupTextTemporary(char const* text) override { NumericDriver::displayPopup(text); }

	void removeWorkingAnimation() override {}

	// Loading animations
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) override {
		NumericDriver::displayLoadingAnimation(delayed, transparent);
	}
	void removeLoadingAnimation() override { NumericDriver::removeTopLayer(); }

	void displayError(int error) override;
	std::array<uint8_t, kNumericDisplayLength> getLast() override { return lastDisplay; } // to match NumericDriver
};
} // namespace display
} // namespace deluge::hid

extern deluge::hid::Display* display;

extern "C" void consoleTextIfAllBootedUp(char const* text);
