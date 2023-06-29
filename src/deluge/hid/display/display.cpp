#include "display.h"

DisplayActual numericDriver;

#if HAVE_OLED

bool Display<DisplayType::OLED>::isLayerCurrentlyOnTop(NumericLayer* layer) {
	return (!popupActive && layer == topLayer);
}

void Display<DisplayType::OLED>::displayError(int error) {
	char const* message;

	switch (error) {
	case NO_ERROR:
	case ERROR_ABORTED_BY_USER:
		return;
	case ERROR_INSUFFICIENT_RAM:
		message = "Insufficient RAM";
		break;
	case ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE:
		message = "Too many files in folder";
		break;
	case ERROR_SD_CARD:
		message = "SD card error";
		break;
	case ERROR_SD_CARD_NOT_PRESENT:
		message = "No SD card present";
		break;
	case ERROR_SD_CARD_NO_FILESYSTEM:
		message = "Please use FAT32-formatted card";
		break;
	case ERROR_FILE_CORRUPTED:
		message = "File corrupted";
		break;
	case ERROR_FILE_NOT_FOUND:
		message = "File not found";
		break;
	case ERROR_FILE_UNREADABLE:
		message = "File unreadable";
		break;
	case ERROR_FILE_UNSUPPORTED:
		message = "File unsupported";
		break;
	case ERROR_FILE_FIRMWARE_VERSION_TOO_NEW:
		message = "Your firmware version is too old to open this file";
		break;
	case ERROR_FOLDER_DOESNT_EXIST:
		message = "Folder not found";
		break;
	case ERROR_BUG:
		message = "Bug encountered";
		break;
	case ERROR_WRITE_FAIL:
		message = "SD card write error";
		break;
	case ERROR_FILE_TOO_BIG:
		message = "File too large";
		break;
	case ERROR_PRESET_IN_USE:
		message = "This preset is in-use elsewhere in your song";
		break;
	case ERROR_NO_FURTHER_PRESETS:
	case ERROR_NO_FURTHER_FILES_THIS_DIRECTION:
		message = "No more presets found";
		break;
	case ERROR_MAX_FILE_SIZE_REACHED:
		message = "Maximum file size reached";
		break;
	case ERROR_SD_CARD_FULL:
		message = "SD card full";
		break;
	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE:
		message = "File does not contain wavetable data";
		break;
	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		message = "Stereo files cannot be used as wavetables";
		break;
	case ERROR_WRITE_PROTECTED:
		message = "Card is write-protected";
		break;
	default:
		message = "ERROR";
	}

	displayPopup(message);
}

#else


#endif


extern "C" void freezeWithError(char const* error) {
	if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError(error);
}

extern "C" void displayPopup(char const* text) {
	numericDriver.displayPopup(text);
}

extern uint8_t usbInitializationPeriodComplete;

extern "C" void displayPopupIfAllBootedUp(char const* text) {
	if (usbInitializationPeriodComplete) {
		numericDriver.displayPopup(text);
	}
}
