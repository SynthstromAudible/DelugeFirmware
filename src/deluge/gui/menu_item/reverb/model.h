#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"
#include "processing/engines/audio_engine.h"
#include <string_view>

namespace deluge::gui::menu_item::reverb {
class Model final : public Selection {
public:
	using Selection::Selection;
	// The reverb algorithm is held on the controller as Song::model (the DOM); AudioEngine::reverb is
	// the projected DSP cache. Defined out-of-line in model.cpp because reading currentSong would
	// require song.h, which itself includes this header (an include cycle).
	void readCurrentValue() override;
	void writeCurrentValue() override;

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_FREEVERB), l10n::getView(STRING_FOR_MUTABLE),
		        l10n::getView(STRING_FOR_DIGITAL)};
	}

	void getColumnLabel(etl::istring& label) override {
		label.append(deluge::l10n::get(l10n::String::STRING_FOR_MODEL_SHORT));
	}
};
} // namespace deluge::gui::menu_item::reverb
