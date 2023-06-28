#pragma once
#include "processing/engines/audio_engine.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"

namespace menu_item::compressor {

class VolumeShortcut final : public patch_cable_strength::Fixed {
public:
	using Fixed::Fixed;
	void writeCurrentValue() {
		Fixed::writeCurrentValue();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
};

} // namespace menu_item::compressor
