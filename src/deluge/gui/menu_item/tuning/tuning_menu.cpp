#include "tuning_menu.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/ui/sound_editor.h"

extern gui::menu_item::HorizontalMenu songMasterMenu;
namespace deluge::gui::menu_item::tuning {

void TuningMenu::readCurrentValue() {
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

void TuningMenu::writeCurrentValue() {
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

deluge::vector<std::string_view> TuningMenu::getOptions(OptType optType) {
	(void)optType;
	int size = forClip() ? NUM_TUNINGS + 1 : NUM_TUNINGS;
	deluge::vector<std::string_view> ret(size);
	int j = 0;
	if (forClip()) {
		ret[j++] = deluge::l10n::getView(deluge::l10n::String::STRING_FOR_SONG_TUNING);
	}
	for (auto tuning : TuningSystem::tunings) {
		ret[j++] = std::string_view(tuning.name);
	}
	return ret;
}

bool TuningMenu::forClip() {
	return soundEditor.getPreviousMenuItem() != static_cast<MenuItem*>(&songMasterMenu);
}

std::string_view TuningMenu::getTitle() const {
	return forClip() ? deluge::l10n::getView(deluge::l10n::String::STRING_FOR_CLIP_TUNING)
	                 : deluge::l10n::getView(deluge::l10n::String::STRING_FOR_SONG_TUNING);
}

MenuItem* TuningMenu::selectButtonPress() {
	return &octaveTuningMenu;
}

} // namespace deluge::gui::menu_item::tuning
