#pragma once
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::tuning {
class Octave final : public Decimal {
public:
	Octave(l10n::String name, l10n::String title_format_str) : Decimal(name), format_str_(title_format_str) {
		selectedNote = kOctaveSize - 4;
	}

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		Decimal::beginSession(navigatedBackwardFrom);
		format((selectedNote + kOctaveSize + 4) % kOctaveSize);
	}

	[[nodiscard]] int32_t getMinValue() const override { return -20000; }
	[[nodiscard]] int32_t getMaxValue() const override { return 20000; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }
	[[nodiscard]] int32_t getDefaultEditPos() const override { return 2; }
	[[nodiscard]] std::string_view getTitle() const override { return title_; }

	void readCurrentValue() override {
		auto o = selectedNote;
		this->setValue(TuningSystem::tuning->offsets[o]);
	}
	void writeCurrentValue() override {
		auto o = selectedNote;
		TuningSystem::tuning->setOffset(o, this->getValue());
	}

	void horizontalEncoderAction(int32_t offset) override {
		if (offset < 0) {
			if (soundEditor.numberEditSize * 10 >= 10000) {
				return;
			}
		}
		Decimal::horizontalEncoderAction(offset);
	}

	void format(int32_t note) {
		title_ = l10n::get(format_str_);
		noteCodeToString(note, title_.data(), nullptr, false);
	}

	int32_t selectedNote;
	l10n::String format_str_;
	std::string title_;

	void selectNote(int32_t note) {
		format(note);
		selectedNote = TuningSystem::tuning->noteWithinOctave(note - 4).noteWithin;
		readCurrentValue();
		drawValue();
	}
};
extern deluge::gui::menu_item::tuning::Octave octaveTuningMenu;
} // namespace deluge::gui::menu_item::tuning
