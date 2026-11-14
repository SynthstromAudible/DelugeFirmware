#include "gui/l10n/short_label.h"

#include <string_view>

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::l10n::shortForm;
using enum deluge::l10n::String;

// clang-format off
describe short_label_group("short label", ${
	// The horizontal menus' column labels and short options read as main's do.
	it("arp labels have mains short forms", _ {
		// A missing short form reads as empty, which matches none of these labels, so a failure prints both strings.
		expect(shortForm(STRING_FOR_REVERSE_PROBABILITY).value_or(std::string_view{})).to_equal(std::string_view{"RVRS"});
		expect(shortForm(STRING_FOR_NOTE_PROBABILITY).value_or(std::string_view{})).to_equal(std::string_view{"NOTE"});
		expect(shortForm(STRING_FOR_RANDOMIZER_LOCK).value_or(std::string_view{})).to_equal(std::string_view{"LOCK"});
		expect(shortForm(STRING_FOR_UP_DOWN).value_or(std::string_view{})).to_equal(std::string_view{"UPDN"});
		expect(shortForm(STRING_FOR_MODULATOR_1).value_or(std::string_view{})).to_equal(std::string_view{"Mod1"});
	});

	// A string with no short form of its own has none here: the label is the string itself.
	it("other strings have none", _ {
		expect(shortForm(STRING_FOR_SPREAD_VELOCITY).has_value()).to_be_false();
		expect(shortForm(STRING_FOR_NONE).has_value()).to_be_false();
	});
});

DELUGE_SPEC(short_label_spec, short_label_group)
