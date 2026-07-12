#pragma once
#include "definitions_cxx.hpp"
#include "util/string.h"
#include <array>
#include <optional>
#include <string_view>
#include <vector>

extern "C" {
#include "util/cfunctions.h"
}

class NumericLayer;
class NumericLayerScrollingText;

enum class PopupType {
	NONE,
	/// Default popup type, if not specified.
	GENERAL,
	/// Used for popups generated during file loading.
	LOADING,
	/// Popup shown when editing note or row probability.
	PROBABILITY,
	/// Popup shown when editing note or row iterance.
	ITERANCE,
	/// Popup shown when editing note or row fill.
	FILL,
	/// Swing amount and interval
	SWING,
	/// Tempo
	TEMPO,
	/// Quantize and humanize
	QUANTIZE,
	/// Threshold Recording Mode
	THRESHOLD_RECORDING_MODE,
	/// Used for popups in the horizontal menu when changing value
	NOTIFICATION,
	// Note: Add here more popup types
};

namespace deluge::hid {

enum struct DisplayType { OLED, SEVENSEG };

class Display {
public:
	Display(DisplayType displayType) : displayType(displayType) {}

	virtual ~Display() = default;

	constexpr virtual size_t getNumBrowserAndMenuLines() = 0;

	virtual void displayPopup(char const* newText, int8_t numFlashes = 3, bool alignRight = false,
	                          uint8_t drawDot = 255, int32_t blinkSpeed = 1, PopupType type = PopupType::GENERAL) = 0;

	virtual void displayPopup(uint8_t val, int8_t numFlashes = 3, bool alignRight = false, uint8_t drawDot = 255,
	                          int32_t blinkSpeed = 1, PopupType type = PopupType::GENERAL) {
		char valStr[4] = {0};
		intToString(val, valStr, 1);
		displayPopup(valStr, numFlashes, alignRight, drawDot, blinkSpeed, type);
	}

	virtual void displayPopup(char const* shortLong[2], int8_t numFlashes = 3, bool alignRight = false,
	                          uint8_t drawDot = 255, int32_t blinkSpeed = 1, PopupType type = PopupType::GENERAL) {
		displayPopup(have7SEG() ? shortLong[0] : shortLong[1], numFlashes, alignRight, drawDot, blinkSpeed, type);
	}

	virtual void popupText(char const* text, PopupType type = PopupType::GENERAL) = 0;
	virtual void popupTextTemporary(char const* text, PopupType type = PopupType::GENERAL) = 0;

	virtual void cancelPopup() = 0;
	virtual void freezeWithError(char const* text) = 0;
	virtual bool isLayerCurrentlyOnTop(NumericLayer* layer) = 0;
	virtual void displayError(Error error) = 0;

	virtual void removeWorkingAnimation() = 0;

	// Loading animations
	virtual void displayLoadingAnimation() {};
	virtual void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) = 0;
	virtual void removeLoadingAnimation() = 0;

	virtual void displayNotification(std::string_view paramTitle, std::optional<std::string_view> paramValue) {}

	virtual bool hasPopup() = 0;
	virtual bool hasPopupOfType(PopupType type) = 0;

	virtual void consoleText(char const* text) = 0;

	virtual void timerRoutine() = 0;

	bool haveOLED() { return displayType == DisplayType::OLED; }
	bool have7SEG() { return displayType == DisplayType::SEVENSEG; }

private:
	DisplayType displayType;
};

} // namespace deluge::hid

extern deluge::hid::Display* display;

extern "C" void consoleTextIfAllBootedUp(char const* text);
