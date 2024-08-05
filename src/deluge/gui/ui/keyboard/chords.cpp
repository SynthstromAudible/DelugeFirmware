#include "io/debug/log.h"
#include "gui/ui/keyboard/chords.h"

namespace deluge::gui::ui::keyboard {

Chords::Chords()
    : chords{
        {"", {{0, 0, 0, 0, 0, 0}}},
        {"M", {
            {4, 7, 0, 0, 0, 0},
            {16, 7, 0, 0, 0, 0},
            {16, 7, -12, 0, 0, 0}
            }},
        {"-", {{3, 7, 0, 0, 0, 0}}},
        {"SUS2", {{2, 7, 0, 0, 0, 0}}},
        {"SUS4", {{5, 7, 0, 0, 0, 0}}},
        {"7", {{4, 7, 10, 0, 0, 0}}},
        {"M7", {{0, 4, 7, 11, 0, 0}}},
        {"-7", {{3, 7, 10, 0, 0, 0}}},
        {"9", {{4, 7, 10, 14, 0, 0}}},
        {"M9", {{4, 7, 11, 14, 0, 0}}},
        {"-9", {{3, 7, 10, 14, 0, 0}}},
        {"11", {{4, 7, 10, 14, 17, 0}}},
        {"M11", {
            {4, 7, 11, 14, 17, 0},
            {4+12, 7, 11, 14, 17, 0}
            }},
        {"-11", {
            {3, 7, 10, 14, 17, 0},
            {3+12, 7, 10, 14, 17, 0}
            }},
        {"13", {
            {4, 7, 10, 14, 17, 21},
            {4+12, 7, 10, 14, 17, 21}
            }},
        {"M13", {
            {4, 7, 11, 14, 17, 21},
            {4+12, 7, 11, 14, 17, 21}
            }},
        {"-13", {
            {3, 7, 10, 14, 17, 21},
            {3+12, 7, 10, 14, 17, 21}
            }}
    }
{}

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
