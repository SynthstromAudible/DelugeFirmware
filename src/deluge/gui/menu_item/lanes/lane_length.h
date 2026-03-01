#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/sequencing/lanes_engine.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::lanes {

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
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

private:
	LanesLane* getCurrentLane() {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (!clip || !clip->lanesEngine) {
			return nullptr;
		}
		int32_t laneIdx = (kDisplayHeight - 1) - instrumentClipView.lastAuditionedYDisplay;
		return clip->lanesEngine->getLane(laneIdx);
	}
};

} // namespace deluge::gui::menu_item::lanes
