#include "octave.h"

namespace deluge::gui::menu_item::tuning {

using enum l10n::String;
Octave octaveTuningMenu{STRING_FOR_TUNING, STRING_FOR_TUNING_FORMAT};

void Octave::beginSession(MenuItem* navigatedBackwardFrom) {
	Decimal::beginSession(navigatedBackwardFrom);
	format((selectedNote + kOctaveSize + 4) % kOctaveSize);
}

void Octave::readCurrentValue() {
	auto o = selectedNote;
	this->setValue(TuningSystem::tuning->offsets[o]);
}

void Octave::writeCurrentValue() {
	auto o = selectedNote;
	TuningSystem::tuning->setOffset(o, this->getValue());
}

void Octave::horizontalEncoderAction(int32_t offset) {
	if (offset < 0) {
		if (soundEditor.numberEditSize * 10 >= 10000) {
			return;
		}
	}
	Decimal::horizontalEncoderAction(offset);
}

void Octave::format(int32_t note) {
	title_ = l10n::get(format_str_);
	noteCodeToString(note, title_.data(), nullptr, false);
}

void Octave::selectNote(int32_t note) {
	format(note);
	selectedNote = TuningSystem::tuning->noteWithinOctave(note - 4).noteWithin;
	readCurrentValue();
	drawValue();
}

} // namespace deluge::gui::menu_item::tuning
