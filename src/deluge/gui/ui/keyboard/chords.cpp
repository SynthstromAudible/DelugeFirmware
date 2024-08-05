#include "gui/ui/keyboard/chords.h"
#include "io/debug/log.h"

namespace deluge::gui::ui::keyboard {

Chords::Chords()
    : chords{{"", {{0, 0, 0, 0, 0, 0}}},
             kMajor,
             kMinor,
             kSus2,
             kSus4,
             k7,
             kM7,
             kMinor7,
             k9,
             kM9,
             kMinor9,
             k11,
             kM11,
             kMinor11,
             k13,
             kM13,
             kMinor13} {
}

Voicing Chords::getVoicing(int32_t chordNo) {

	int32_t voicingNo = voicingOffset[chordNo];
	bool valid;
	if (voicingNo <= 0) {
		return chords[chordNo].voicings[0];
	}
	// Check if voicing is valid
	// voicings after the first should default to 0
	else if (voicingNo > 0) {
		for (int voicingN = voicingNo; voicingN >= 0; voicingN--) {
			Voicing voicing = chords[chordNo].voicings[voicingN];

			bool valid = false;
			for (int j = 0; j < kMaxChordKeyboardSize; j++) {
				if (voicing.offsets[j] != 0) {
					valid = true;
				}
			}
			if (valid) {
				return voicing;
			}
		}
		if (!valid) {
			D_PRINTLN("Voicing is invalid, returning default voicing");
			return chords[0].voicings[0];
		}
	}
	return chords[chordNo].voicings[0];
}

} // namespace deluge::gui::ui::keyboard
