#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/sequencer/modes/lanes_sequencer_mode.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::lanes {

using deluge::model::clip::sequencer::modes::getCurrentLanesEngine;

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
