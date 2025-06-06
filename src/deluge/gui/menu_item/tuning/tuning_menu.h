#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "octave.h"

extern deluge::gui::menu_item::tuning::Octave octaveTuningMenu;
extern deluge::gui::menu_item::Submenu songMasterMenu;
namespace deluge::gui::menu_item::tuning {
class TuningMenu final : public Selection {
	using Selection::Selection;
	void readCurrentValue() override {
		uint8_t value = 0;
		if (forClip()) {
			auto* clip = currentSong->getCurrentClip();
			if (clip != nullptr) {
				value = clip->selectedTuning;
				this->setValue(value == 128 ? 0 : value + 1);
			}
		}
		else {
			value = currentSong->selectedTuning;
			this->setValue(value);
		}
	}
	void writeCurrentValue() override {
		auto value = getValue();
		if (forClip()) {
			auto* clip = currentSong->getCurrentClip();
			if (clip != nullptr) {
				if (value == 0) {
					clip->selectedTuning = 128;
				}
				else {
					clip->selectedTuning = value - 1;
					TuningSystem::select(value - 1);
				}
			}
		}
		else {
			currentSong->selectedTuning = value;
			TuningSystem::select(value);
		}
	}
	static bool forClip() {
		return soundEditor.menuItemNavigationRecord[soundEditor.navigationDepth - 1] != (MenuItem*)&songMasterMenu;
	}
	[[nodiscard]] virtual std::string_view getTitle() const override {
		if (forClip()) {
			return deluge::l10n::getView(deluge::l10n::String::STRING_FOR_CLIP_TUNING);
		}
		else {
			return deluge::l10n::getView(deluge::l10n::String::STRING_FOR_SONG_TUNING);
		}
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		int size = forClip() ? NUM_TUNINGS + 1 : NUM_TUNINGS;
		deluge::vector<std::string_view> ret(size);
		int j = 0;
		if (forClip()) {
			ret[j++] = deluge::l10n::getView(deluge::l10n::String::STRING_FOR_SONG_TUNING);
		}
		for (int i = 0; i < NUM_TUNINGS; i++) {
			ret[j++] = std::string_view(TuningSystem::tunings[i].name);
		}
		return ret;
	}
	MenuItem* selectButtonPress() override { return &octaveTuningMenu; }
};
} // namespace deluge::gui::menu_item::tuning
