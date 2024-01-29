#include "display.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"

deluge::hid::Display* display = nullptr;
namespace deluge::hid::display {

std::string_view getErrorMessage(int32_t error) {
	using enum l10n::String;
	switch (error) {
	case ERROR_INSUFFICIENT_RAM:
		return l10n::getView(STRING_FOR_ERROR_INSUFFICIENT_RAM);

	case ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE:
		return l10n::getView(STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE);

	case ERROR_SD_CARD:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD);

	case ERROR_SD_CARD_NOT_PRESENT:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD_NOT_PRESENT);

	case ERROR_SD_CARD_NO_FILESYSTEM:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM);

	case ERROR_FILE_CORRUPTED:
		return l10n::getView(STRING_FOR_ERROR_FILE_CORRUPTED);

	case ERROR_FILE_NOT_FOUND:
		return l10n::getView(STRING_FOR_ERROR_FILE_NOT_FOUND);

	case ERROR_FILE_UNREADABLE:
		return l10n::getView(STRING_FOR_ERROR_FILE_UNREADABLE);

	case ERROR_FILE_UNSUPPORTED:
		return l10n::getView(STRING_FOR_ERROR_FILE_UNSUPPORTED);

	case ERROR_FILE_FIRMWARE_VERSION_TOO_NEW:
		return l10n::getView(STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW);

	case ERROR_FOLDER_DOESNT_EXIST:
		return l10n::getView(STRING_FOR_ERROR_FOLDER_DOESNT_EXIST);

	case ERROR_BUG:
		return l10n::getView(STRING_FOR_ERROR_BUG);

	case ERROR_WRITE_FAIL:
		return l10n::getView(STRING_FOR_ERROR_WRITE_FAIL);

	case ERROR_FILE_TOO_BIG:
		return l10n::getView(STRING_FOR_ERROR_FILE_TOO_BIG);

	case ERROR_PRESET_IN_USE:
		return l10n::getView(STRING_FOR_ERROR_PRESET_IN_USE);

	case ERROR_NO_FURTHER_PRESETS:
	case ERROR_NO_FURTHER_FILES_THIS_DIRECTION:
		return l10n::getView(STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION);

	case ERROR_MAX_FILE_SIZE_REACHED:
		return l10n::getView(STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED);

	case ERROR_SD_CARD_FULL:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD_FULL);
		break;
	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE:
		return l10n::getView(STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE);

	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		return l10n::getView(STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO);

	case ERROR_WRITE_PROTECTED:
		return l10n::getView(STRING_FOR_ERROR_WRITE_PROTECTED);

	default:
		return l10n::getView(STRING_FOR_ERROR_GENERIC);
	}
}

bool have_oled_screen = false;

void swapDisplayType() {
	using ::display; // this is c++
	bool was_oled = display->haveOLED();
	delete display;
	if (was_oled) {
		display = new deluge::hid::display::SevenSegment;
	}
	else {
		deluge::l10n::chosenLanguage = nullptr;
		display = new deluge::hid::display::OLED;
	}
	UI* ui = getCurrentUI();
	if (ui) {
		ui->displayOrLanguageChanged();
	}
}

} // namespace deluge::hid::display

extern "C" void freezeWithError(char const* error) {
	display->freezeWithError(error);
}

extern "C" void displayPopup(char const* text) {
	display->displayPopup(text);
}

extern uint8_t usbInitializationPeriodComplete;

extern "C" void consoleTextIfAllBootedUp(char const* text) {
	if (usbInitializationPeriodComplete != 0u) {
		display->consoleText(text);
	}
}
