#include "scenes/l10n_bridge.h"

#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"

namespace deluge::scenes {

std::string_view text_lookup(uint16_t string_id) {
	return l10n::getView(static_cast<l10n::String>(string_id));
}

// A transcription of deluge::hid::display::getErrorMessage's switch
// (src/deluge/hid/display/display.cpp:7-85): same cases, same fallthroughs, same default. Kept in
// lockstep by hand -- the scenes layer may not include hid/, so this cannot call the original.
std::string_view error_lookup(int16_t error_code) {
	using enum l10n::String;
	switch (static_cast<Error>(error_code)) {
	case Error::INSUFFICIENT_RAM:
		return l10n::getView(STRING_FOR_ERROR_INSUFFICIENT_RAM);

	case Error::INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE:
		return l10n::getView(STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE);

	case Error::SD_CARD:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD);

	case Error::SD_CARD_NOT_PRESENT:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD_NOT_PRESENT);

	case Error::SD_CARD_NO_FILESYSTEM:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM);

	case Error::FILE_CORRUPTED:
		return l10n::getView(STRING_FOR_ERROR_FILE_CORRUPTED);

	case Error::FILE_NOT_FOUND:
		return l10n::getView(STRING_FOR_ERROR_FILE_NOT_FOUND);

	case Error::FILE_UNREADABLE:
		return l10n::getView(STRING_FOR_ERROR_FILE_UNREADABLE);

	case Error::FILE_UNSUPPORTED:
		return l10n::getView(STRING_FOR_ERROR_FILE_UNSUPPORTED);

	case Error::FILE_FIRMWARE_VERSION_TOO_NEW:
		return l10n::getView(STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW);

	case Error::FOLDER_DOESNT_EXIST:
		return l10n::getView(STRING_FOR_ERROR_FOLDER_DOESNT_EXIST);

	case Error::BUG:
		return l10n::getView(STRING_FOR_ERROR_BUG);

	case Error::WRITE_FAIL:
		return l10n::getView(STRING_FOR_ERROR_WRITE_FAIL);

	case Error::FILE_TOO_BIG:
		return l10n::getView(STRING_FOR_ERROR_FILE_TOO_BIG);

	case Error::PRESET_IN_USE:
		return l10n::getView(STRING_FOR_ERROR_PRESET_IN_USE);

	case Error::NO_FURTHER_PRESETS:
	case Error::NO_FURTHER_FILES_THIS_DIRECTION:
		return l10n::getView(STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION);

	case Error::MAX_FILE_SIZE_REACHED:
		return l10n::getView(STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED);

	case Error::SD_CARD_FULL:
		return l10n::getView(STRING_FOR_ERROR_SD_CARD_FULL);

	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE:
		return l10n::getView(STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE);

	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		return l10n::getView(STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO);

	case Error::WRITE_PROTECTED:
		return l10n::getView(STRING_FOR_ERROR_WRITE_PROTECTED);

	case Error::POS_PAST_STRING:
		return l10n::getView(STRING_FOR_STRING_ERROR);

	case Error::FILE_NOT_SAVED:
		return l10n::getView(STRING_FOR_ERROR_FILE_NOT_SAVED);

	case Error::TOO_MANY_OPEN_STREAMS:
		return l10n::getView(STRING_FOR_ERROR_TOO_MANY_OPEN_STREAMS);

	default:
		return l10n::getView(STRING_FOR_ERROR_GENERIC);
	}
}

} // namespace deluge::scenes
