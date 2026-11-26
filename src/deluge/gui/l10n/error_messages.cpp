#include "gui/l10n/error_messages.h"

#include "gui/l10n/l10n.h"

namespace deluge::l10n {

std::string_view error_message(Error error) {
	using enum l10n::String;
	switch (error) {
	case Error::INSUFFICIENT_RAM:
		return getView(STRING_FOR_ERROR_INSUFFICIENT_RAM);

	case Error::INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE:
		return getView(STRING_FOR_ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE);

	case Error::SD_CARD:
		return getView(STRING_FOR_ERROR_SD_CARD);

	case Error::SD_CARD_NOT_PRESENT:
		return getView(STRING_FOR_ERROR_SD_CARD_NOT_PRESENT);

	case Error::SD_CARD_NO_FILESYSTEM:
		return getView(STRING_FOR_ERROR_SD_CARD_NO_FILESYSTEM);

	case Error::FILE_CORRUPTED:
		return getView(STRING_FOR_ERROR_FILE_CORRUPTED);

	case Error::FILE_NOT_FOUND:
		return getView(STRING_FOR_ERROR_FILE_NOT_FOUND);

	case Error::FILE_UNREADABLE:
		return getView(STRING_FOR_ERROR_FILE_UNREADABLE);

	case Error::FILE_UNSUPPORTED:
		return getView(STRING_FOR_ERROR_FILE_UNSUPPORTED);

	case Error::FILE_FIRMWARE_VERSION_TOO_NEW:
		return getView(STRING_FOR_ERROR_FILE_FIRMWARE_VERSION_TOO_NEW);

	case Error::FOLDER_DOESNT_EXIST:
		return getView(STRING_FOR_ERROR_FOLDER_DOESNT_EXIST);

	case Error::BUG:
		return getView(STRING_FOR_ERROR_BUG);

	case Error::WRITE_FAIL:
		return getView(STRING_FOR_ERROR_WRITE_FAIL);

	case Error::FILE_TOO_BIG:
		return getView(STRING_FOR_ERROR_FILE_TOO_BIG);

	case Error::PRESET_IN_USE:
		return getView(STRING_FOR_ERROR_PRESET_IN_USE);

	case Error::NO_FURTHER_PRESETS:
	case Error::NO_FURTHER_FILES_THIS_DIRECTION:
		return getView(STRING_FOR_ERROR_NO_FURTHER_FILES_THIS_DIRECTION);

	case Error::MAX_FILE_SIZE_REACHED:
		return getView(STRING_FOR_ERROR_MAX_FILE_SIZE_REACHED);

	case Error::SD_CARD_FULL:
		return getView(STRING_FOR_ERROR_SD_CARD_FULL);

	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE:
		return getView(STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE);

	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		return getView(STRING_FOR_ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO);

	case Error::WRITE_PROTECTED:
		return getView(STRING_FOR_ERROR_WRITE_PROTECTED);
	case Error::POS_PAST_STRING:
		return getView(STRING_FOR_STRING_ERROR);

	case Error::FILE_NOT_SAVED:
		return getView(STRING_FOR_ERROR_FILE_NOT_SAVED);

	case Error::TOO_MANY_OPEN_STREAMS:
		return getView(STRING_FOR_ERROR_TOO_MANY_OPEN_STREAMS);

	default:
		return getView(STRING_FOR_ERROR_GENERIC);
	}
}

} // namespace deluge::l10n
