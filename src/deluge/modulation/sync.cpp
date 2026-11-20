#include "modulation/sync.h"
#include "music-theory/note_length.h"
#include "plugin-api/types/display_mapping.h"

#include <utility>

int32_t wrapSwingIntervalSyncLevel(int32_t value) {
	return deluge::plugin::wrap_swing_interval(value);
}

int32_t clampSwingIntervalSyncLevel(int32_t value) {
	return deluge::plugin::clamp_swing_interval(value);
}

SyncType syncValueToSyncType(int32_t value) {
	if (value < std::to_underlying(SYNC_TYPE_TRIPLET)) {
		return SYNC_TYPE_EVEN;
	}
	else if (value < std::to_underlying(SYNC_TYPE_DOTTED)) {
		return SYNC_TYPE_TRIPLET;
	}
	else {
		return SYNC_TYPE_DOTTED;
	}
}

SyncLevel syncValueToSyncLevel(int32_t option) {
	if (option < std::to_underlying(SYNC_TYPE_TRIPLET)) {
		return static_cast<SyncLevel>(option);
	}
	else if (option < std::to_underlying(SYNC_TYPE_DOTTED)) {
		return static_cast<SyncLevel>(option - std::to_underlying(SYNC_TYPE_TRIPLET) + 1);
	}
	else {
		return static_cast<SyncLevel>(option - std::to_underlying(SYNC_TYPE_DOTTED) + 1);
	}
}

void syncValueToString(uint32_t value, etl::istring& buffer, int32_t tickMagnitude) {
	const auto name =
	    deluge::music::sync_value_name(static_cast<int32_t>(value), tickMagnitude, deluge::music::SyncNameForm::FULL);
	buffer.append(name.view().data(), name.view().size());
}

void syncValueToStringForHorzMenuLabel(SyncType /*type*/, SyncLevel level, etl::istring& buffer,
                                       int32_t tickMagnitude) {
	// The label ignores the type: the level's index is its even combined value.
	const auto name = deluge::music::sync_value_name(deluge::plugin::index_of(level), tickMagnitude,
	                                                 deluge::music::SyncNameForm::HORIZONTAL_LABEL);
	buffer.clear();
	buffer.append(name.view().data(), name.view().size());
}
