#include "ui/core/led_frame.h"

namespace deluge::ui {

void LedFrame::set(LedId id, LedState s) {
	for (size_t i = 0; i < count_; ++i) {
		if (entries_[i].first == id) {
			entries_[i].second = s;
			return;
		}
	}
	if (count_ < kMaxEntries) {
		entries_[count_++] = {id, s};
	}
}

} // namespace deluge::ui
