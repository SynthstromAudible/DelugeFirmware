#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/sequencer/modes/lanes_sequencer_mode.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::lanes {

using deluge::model::clip::sequencer::modes::getCurrentLanesEngine;

class LaneLength final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override {
		LanesLane* lane = getCurrentLane();
		this->setValue(lane ? lane->length : 0);
	}

	void writeCurrentValue() override {
		LanesLane* lane = getCurrentLane();
		if (!lane) {
			return;
		}
		int32_t newLen = this->getValue();
		if (newLen > lane->length) {
			for (int32_t i = lane->length; i < newLen; i++) {
				lane->values[i] = 0;
			}
		}
		lane->length = newLen;
		uiNeedsRendering(&instrumentClipView, 1 << instrumentClipView.lastAuditionedYDisplay, 0);
	}

	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxLanesSteps; }

private:
	LanesLane* getCurrentLane() {
		LanesEngine* engine = getCurrentLanesEngine();
		if (!engine) {
			return nullptr;
		}
		int32_t laneIdx = (kDisplayHeight - 1) - instrumentClipView.lastAuditionedYDisplay;
		return engine->getLane(laneIdx);
	}
};

} // namespace deluge::gui::menu_item::lanes
