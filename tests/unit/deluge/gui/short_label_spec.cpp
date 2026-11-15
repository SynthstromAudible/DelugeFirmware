#include "gui/l10n/short_label.h"

#include <string_view>

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::l10n::shortForm;
using enum deluge::l10n::String;

namespace {
// shortForm() returns an optional, which does not print; value_or() turns it into a string that does. No label equals
// kNoShortForm, so an absent form fails and names itself.
constexpr std::string_view kNoShortForm{"<no short form>"};
} // namespace

// clang-format off
describe short_label_group("short label", ${
	// The horizontal menus' column labels and short options read as main's do.
	it("arp labels have main's short forms", _ {
		expect(shortForm(STRING_FOR_REVERSE_PROBABILITY).value_or(kNoShortForm)).to_equal(std::string_view{"RVRS"});
		expect(shortForm(STRING_FOR_NOTE_PROBABILITY).value_or(kNoShortForm)).to_equal(std::string_view{"NOTE"});
		expect(shortForm(STRING_FOR_RANDOMIZER_LOCK).value_or(kNoShortForm)).to_equal(std::string_view{"LOCK"});
		expect(shortForm(STRING_FOR_UP_DOWN).value_or(kNoShortForm)).to_equal(std::string_view{"UPDN"});
		expect(shortForm(STRING_FOR_MODULATOR_1).value_or(kNoShortForm)).to_equal(std::string_view{"Mod1"});
	});

	// A string with no short form of its own has none here: the label is the string itself.
	it("other strings have none", _ {
		expect(shortForm(STRING_FOR_SPREAD_VELOCITY).has_value()).to_be_false();
		expect(shortForm(STRING_FOR_NONE).has_value()).to_be_false();
	});
});

DELUGE_SPEC(short_label_spec, short_label_group)
