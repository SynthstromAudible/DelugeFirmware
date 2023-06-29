#pragma once
#include "definitions.h"
#include "oled.h"
#include "numericdriver.h"
#include "oled.h"

#ifdef __cplusplus
enum class DisplayType { OLED, SevenSegment };

template <DisplayType display_type> class Display {};

#if HAVE_OLED
template <> class Display<DisplayType::OLED> {
public:
	void setText(char const* newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false) {}
	void displayPopup(char const* newText, int8_t numFlashes = 3, bool = false, uint8_t = 255, int = 1) {
		OLED::popupText(newText, !numFlashes);
	}

	void setNextTransitionDirection(int8_t thisDirection) {}

	void cancelPopup() { OLED::removePopup(); }
	void freezeWithError(char const* text) { OLED::freezeWithError(text); }
	bool isLayerCurrentlyOnTop(NumericLayer* layer);
	void displayError(int error);

	// instance variables
	bool popupActive = false;
	NumericLayer* topLayer = nullptr;
};

using DisplayActual = Display<DisplayType::OLED>;

#else
template <> class Display<DisplayType::SevenSegment> : public NumericDriver {};

using DisplayActual = Display<DisplayType::SevenSegment>;
#endif // HAVE_OLED

extern DisplayActual numericDriver;
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

void displayPopupIfAllBootedUp(char const* text);

#ifdef __cplusplus
}
#endif
