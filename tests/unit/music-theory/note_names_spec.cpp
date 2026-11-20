#include "music-theory/note_names.h"

#include <cppspec.hpp>

#include <array>
#include <cstdint>

using namespace deluge::music;

namespace {

// The pitch-class tables the app's note text is spelled from (`noteCodeToNoteLetter`, `noteCodeToNoteLetterFlats`
// and `noteCodeIsSharp` in `origin/main:src/deluge/util/lookuptables/lookuptables.cpp`), written out as their values.
constexpr std::array<uint8_t, 12> kSharpLetters{67, 67, 68, 68, 69, 70, 70, 71, 71, 65, 65, 66};
constexpr std::array<uint8_t, 12> kFlatLetters{67, 68, 68, 69, 69, 70, 71, 71, 65, 65, 66, 66};
constexpr std::array<bool, 12> kBlackKeys{false, true,  false, true,  false, false,
                                          true,  false, true,  false, true,  false};

} // namespace

// clang-format off
describe note_names("note names", ${
	it("spells every MIDI note's letter and accidental with sharps and with flats", _ {
		for (int32_t note = 0; note < 128; note++) {
			const auto pc = static_cast<size_t>(note % 12);
			expect(note_letter(note, true)).to_equal(static_cast<char>(kSharpLetters[pc]));
			expect(note_letter(note, false)).to_equal(static_cast<char>(kFlatLetters[pc]));
			expect(is_accidental(note)).to_equal(kBlackKeys[pc]);
			const NoteName sharp = note_name(note, true);
			const NoteName flat = note_name(note, false);
			expect(sharp.letter).to_equal(static_cast<char>(kSharpLetters[pc]));
			expect(flat.letter).to_equal(static_cast<char>(kFlatLetters[pc]));
			expect(sharp.accidental).to_equal(kBlackKeys[pc] ? Accidental::Sharp : Accidental::None);
			expect(flat.accidental).to_equal(kBlackKeys[pc] ? Accidental::Flat : Accidental::None);
		}
	});
	it("numbers the octave so that MIDI note 60 is C3", _ {
		for (int32_t note = 0; note < 128; note++) {
			expect(note_octave(note)).to_equal(note / 12 - 2);
			expect(note_name(note, true).octave).to_equal(note / 12 - 2);
		}
		expect(note_name(60, true).letter).to_equal('C');
		expect(note_octave(60)).to_equal(3);
	});
	it("puts a note below 0 in the pitch class and octave under it", _ {
		expect(note_letter(-1, true)).to_equal('B');
		expect(note_octave(-1)).to_equal(-3);
		expect(note_letter(-12, true)).to_equal('C');
		expect(note_octave(-12)).to_equal(-3);
		expect(note_letter(-13, false)).to_equal('B');
		expect(note_octave(-13)).to_equal(-4);
		expect(is_accidental(-2)).to_be_true();
		expect(note_octave(-2)).to_equal(-3);
	});
});

CPPSPEC_SPEC(note_names)
