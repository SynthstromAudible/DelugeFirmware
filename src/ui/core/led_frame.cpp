#include "ui/core/led_frame.h"

namespace deluge::ui {

bool LedFrame::set(LedId id, LedState s) {
	for (size_t i = 0; i < count_; ++i) {
		if (entries_[i].first == id) {
			entries_[i].second = s;
			return true;
		}
	}
	if (count_ < kMaxEntries) {
		entries_[count_++] = {id, s};
		return true;
	}
	return false; // frame full: this declaration is dropped, and the caller is told
}

} // namespace deluge::ui
