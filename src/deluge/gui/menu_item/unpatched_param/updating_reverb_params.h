#pragma once
#include "gui/menu_item/unpatched_param.h"
#include "processing/engines/audio_engine.h"

namespace menu_item::unpatched_param {
class UpdatingReverbParams final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	void writeCurrentValue() {
		UnpatchedParam::writeCurrentValue();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
};
} // namespace menu_item::unpatched_param
