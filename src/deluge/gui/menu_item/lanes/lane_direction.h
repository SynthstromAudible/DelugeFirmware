#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/sequencing/lanes_engine.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::lanes {

// Direction: 0=Forward, 1=Reverse, 2=Pingpong
class LaneDirectionMenu final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override {
		LanesLane* lane = getCurrentLane();
		this->setValue(lane ? static_cast<int32_t>(lane->direction) : 0);
	}

	void writeCurrentValue() override {
		LanesLane* lane = getCurrentLane();
		if (!lane) {
			return;
		}
		lane->direction = static_cast<::LaneDirection>(this->getValue());
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 2; }
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
