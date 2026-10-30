#include "CppUTest/TestHarness.h"

#include "gui/menu_item/short_label.h"

#include <string_view>

using deluge::gui::menu_item::shortForm;
using enum deluge::l10n::String;

TEST_GROUP(ShortLabel){};

// The horizontal menus' column labels and short options read as main's do.
TEST(ShortLabel, ArpLabelsHaveMainsShortForms) {
	CHECK(shortForm(STRING_FOR_REVERSE_PROBABILITY) == std::string_view{"RVRS"});
	CHECK(shortForm(STRING_FOR_NOTE_PROBABILITY) == std::string_view{"NOTE"});
	CHECK(shortForm(STRING_FOR_RANDOMIZER_LOCK) == std::string_view{"LOCK"});
	CHECK(shortForm(STRING_FOR_UP_DOWN) == std::string_view{"UPDN"});
	CHECK(shortForm(STRING_FOR_MODULATOR_1) == std::string_view{"Mod1"});
}

// A string with no short form of its own has none here: the label is the string itself.
TEST(ShortLabel, OtherStringsHaveNone) {
	CHECK_FALSE(shortForm(STRING_FOR_SPREAD_VELOCITY).has_value());
	CHECK_FALSE(shortForm(STRING_FOR_NONE).has_value());
}
