#include "display_mock.h"

namespace deluge::test {
Error lastDisplayedError = Error::NONE;
} // namespace deluge::test

namespace {

/// Shows nothing; records the last error. Every other call is a no-op.
class TestDisplay final : public deluge::hid::Display {
public:
	constexpr size_t getNumBrowserAndMenuLines() override { return 0; }

	using Display::displayPopup;
	void displayPopup(char const* /*newText*/, int8_t /*numFlashes*/, bool /*alignRight*/, uint8_t /*drawDot*/,
	                  int32_t /*blinkSpeed*/, PopupType /*type*/) override {}

	void popupText(char const* /*text*/, PopupType /*type*/) override {}
	void popupTextTemporary(char const* /*text*/, PopupType /*type*/) override {}

	void cancelPopup() override {}
	void freezeWithError(char const* /*text*/) override {}
	void displayError(Error error) override { deluge::test::lastDisplayedError = error; }

	void removeWorkingAnimation() override {}

	void displayLoadingAnimationText(char const* /*text*/, bool /*delayed*/, bool /*transparent*/) override {}
	void removeLoadingAnimation() override {}

	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType /*type*/) override { return false; }

	void consoleText(char const* /*text*/) override {}

	void timerRoutine() override {}
};

TestDisplay testDisplay;

} // namespace

deluge::hid::Display* display = &testDisplay;
