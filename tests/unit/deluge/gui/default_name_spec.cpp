#include "gui/ui/browser/default_name.h"
#include "util/name_compare.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::gui::browser::NameFamilySurvey;
using deluge::gui::browser::nextDefaultName;
using deluge::gui::browser::nextSlotName;

namespace {
/// A folder's file names, as the browser's read passes them.
using Names = std::vector<std::string>;

/// What the browser does: survey `folder` for `current`'s family, then name the next variation.
std::string nextIn(std::string_view current, std::string_view slotPrefix, Names const& folder) {
	NameFamilySurvey family{current, slotPrefix};
	for (auto const& name : folder) {
		family.observe(name);
	}
	return nextDefaultName(current, slotPrefix, family);
}

/// A card holding SONG001.XML .. SONGnnn.XML, as a vanilla card looks.
Names cardWithSongsUpTo(int32_t highest) {
	Names names;
	for (int32_t i = 1; i <= highest; i++) {
		char buf[32];
		snprintf(buf, sizeof buf, "SONG%03d.XML", i);
		names.emplace_back(buf);
	}
	return names;
}
} // namespace

// clang-format off
describe default_name_group("default name", ${
	before_each([] {
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;
	});

	// --- Issue #1069: a default-named song takes a letter suffix ----------------------------
	it("issue 1069: default named song gets a letter suffix", _ {
		auto files = cardWithSongsUpTo(185);
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185A"});
	});

	it("issue 1069: never emits the slot number as a suffix", _ {
		// "185_186" would be the slot number leaking in through a wrong string offset.
		auto files = cardWithSongsUpTo(185);
		std::string result = nextIn("SONG185", "SONG", files);
		expect(result.find("186")).to_equal(std::string::npos);
		expect(result.find('_')).to_equal(std::string::npos);
	});

	it("letter suffixes advance", _ {
		Names files{"SONG185.XML", "SONG185A.XML", "SONG185B.XML"};
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185C"});
		expect(nextIn("SONG185B", "SONG", files)).to_equal(std::string{"SONG185C"});
	});

	it("letter suffix fills the first gap", _ {
		Names files{"SONG185.XML", "SONG185A.XML", "SONG185C.XML"};
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185B"});
	});

	it("letter suffix exhausted returns input unchanged", _ {
		std::vector<std::string> names{"SONG185.XML"};
		for (char c = 'A'; c <= 'Z'; c++) {
			names.push_back(std::string{"SONG185"} + c + ".XML");
		}
		Names files{std::move(names)};
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185"});
	});

	// --- Numeric suffixes: the OLED's naming of renamed songs, kept as it is -----------------
	it("renamed song gets a numeric suffix", _ {
		Names files{"MYTRACK.XML"};
		expect(nextIn("MYTRACK", "SONG", files)).to_equal(std::string{"MYTRACK 2"});
	});

	it("numeric suffix advances", _ {
		Names files{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 3.XML"};
		expect(nextIn("MYTRACK", "SONG", files)).to_equal(std::string{"MYTRACK 4"});
	});

	// --- Names come from the whole family, never by probing candidates against the listing ----
	it("numeric suffix advances past eleven", _ {
		// Saving over "<name> 11" offers "<name> 12", not "<name> 2". Probing "<name> 2", "<name> 3", ... against the
		// listing's window would get this wrong as soon as the low members sorted out of the window.
		Names folder;
		for (int32_t i = 1; i <= 11; i++) {
			folder.push_back("MY PROJECT " + std::to_string(i) + ".XML");
		}
		expect(nextIn("MY PROJECT 11", "SONG", folder)).to_equal(std::string{"MY PROJECT 12"});
	});

	it("letter suffix advances past a whole alphabet of variations", _ {
		Names folder{"SONG185.XML"};
		for (char c = 'A'; c <= 'L'; c++) {
			folder.push_back(std::string{"SONG185"} + c + ".XML");
		}
		expect(nextIn("SONG185", "SONG", folder)).to_equal(std::string{"SONG185M"});
	});

	it("letter suffixes are case insensitive", _ {
		// The card may hold either case; they are the same file to the filesystem, so the same variation here.
		Names files{"SONG185.XML", "song185a.xml"};
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185B"});
	});

	it("a longer suffix is not a letter variation", _ {
		// "SONG185AB" is its own name, not the "A" variation of SONG185.
		Names files{"SONG185.XML", "SONG185AB.XML"};
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185A"});
	});

	it("numeric suffix never goes below the name being saved over", _ {
		// The name being saved over is on the card, so its own number floors the result even when the survey saw
		// nothing of the family.
		expect(nextIn("MY PROJECT 11", "SONG", Names{})).to_equal(std::string{"MY PROJECT 12"});
	});

	it("numeric suffix counts from the highest sibling not the lowest gap", _ {
		// Gaps are left alone: a deleted "MYTRACK 3" is not handed back to the next save.
		Names files{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 5.XML"};
		expect(nextIn("MYTRACK 2", "SONG", files)).to_equal(std::string{"MYTRACK 6"});
	});

	it("numeric suffix ignores siblings that only look numbered", _ {
		Names files{"MYTRACK.XML", "MYTRACK NOTES.XML"};
		expect(nextIn("MYTRACK", "SONG", files)).to_equal(std::string{"MYTRACK 2"});
	});

	it("numeric suffix ignores date-suffixed neighbours", _ {
		// The date-suffixed name below starts with "JAM " and a digit, but its number doesn't run to the extension, so it
		// is not in the "JAM " family. Counted as one it would send the next save to "JAM 2025".
		Names files{"JAM 3.XML", "JAM 2024-01-05.XML"};
		expect(nextIn("JAM 3", "SONG", files)).to_equal(std::string{"JAM 4"});
	});

	it("in memory names without an extension count", _ {
		Names files{"MYTRACK.XML", "MYTRACK 4"};
		expect(nextIn("MYTRACK", "", files)).to_equal(std::string{"MYTRACK 5"});
	});

	it("numbers exhausted returns input unchanged", _ {
		Names files{"MYTRACK 9999.XML"};
		expect(nextIn("MYTRACK 2", "SONG", files)).to_equal(std::string{"MYTRACK 2"});
	});

	it("restart forgets what was observed", _ {
		NameFamilySurvey family{"MYTRACK", ""};
		family.observe("MYTRACK 7.XML");
		family.restart();
		family.observe("MYTRACK 2.XML");
		expect(nextDefaultName("MYTRACK", "", family)).to_equal(std::string{"MYTRACK 3"});
	});

	it("existing underscore delimiter is reused", _ {
		// A name that already carries an "_<number>" suffix keeps the underscore.
		Names files{"TRACK_1.XML"};
		expect(nextIn("TRACK_1", "SONG", files)).to_equal(std::string{"TRACK_2"});
	});

	it("non-numeric name sharing the prefix is not treated as a slot", _ {
		// "SONGIDEA" starts with SONG but has no digits - it is just a name.
		Names files{"SONGIDEA.XML"};
		expect(nextIn("SONGIDEA", "SONG", files)).to_equal(std::string{"SONGIDEA 2"});
	});

	// --- Zero-padded names (vanilla cards write songs 1-99 as SONG001..SONG099) -------------
	it("zero padded song gets a letter suffix", _ {
		Names files{"SONG001.XML"};
		expect(nextIn("SONG001", "SONG", files)).to_equal(std::string{"SONG001A"});
	});

	it("zero padded letter suffixes advance", _ {
		Names files{"SONG001.XML", "SONG001A.XML"};
		expect(nextIn("SONG001A", "SONG", files)).to_equal(std::string{"SONG001B"});
	});

	it("number part skips zero padding", _ {
		// getSlot() reads a leading '0' as a whole one-digit slot and then chokes on the next digit, so it rejects "001"
		// outright. Everything feeding it must skip the padding first, or 7SEG scrolls "SONG001" instead of showing "1".
		expect(std::string_view{deluge::gui::browser::numberPartOf("SONG001", "SONG")}).to_equal(std::string_view{"1"});
		expect(std::string_view{deluge::gui::browser::numberPartOf("SONG010", "SONG")}).to_equal(std::string_view{"10"});
		expect(std::string_view{deluge::gui::browser::numberPartOf("SONG185", "SONG")}).to_equal(std::string_view{"185"});
		expect(std::string_view{deluge::gui::browser::numberPartOf("SONG001A", "SONG")}).to_equal(std::string_view{"1A"});
		// The last digit always survives, so slot 0 is still reachable.
		expect(std::string_view{deluge::gui::browser::numberPartOf("SONG000", "SONG")}).to_equal(std::string_view{"0"});
		expect(std::string_view{deluge::gui::browser::numberPartOf("SONG0", "SONG")}).to_equal(std::string_view{"0"});
		// A name that does not carry the prefix has no number part. A `const char*` handed to expect reads as a
		// string, so the pointer itself is checked.
		expect(static_cast<void const*>(deluge::gui::browser::numberPartOf("MYTRACK", "SONG"))).to_be_null();
	});

	// --- Extensions -------------------------------------------------------------------------
	it("a json file also takes the name", _ {
		// The browser lists .XML and .json alike (any case), and songs save as either. Probing only .XML would hand back
		// SONG185A when SONG185A.json already exists on the card, or the older SONG185A.Json.
		Names files{"SONG185.XML", "SONG185A.Json"};
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185B"});
	});

	it("a lowercase json file also takes the name", _ {
		Names files{"SONG185.XML", "SONG185A.json"};
		expect(nextIn("SONG185", "SONG", files)).to_equal(std::string{"SONG185B"});
	});

	it("presets keep the numeric suffix", _ {
		// Only songs get letter suffixes. Presets pass an empty slotPrefix, so SYNT005 takes the
		// numeric path, as OLED presets are named.
		Names files{"SYNT005.XML"};
		expect(nextIn("SYNT005", "", files)).to_equal(std::string{"SYNT005 2"});
	});
});

describe next_slot_name_group("next slot name", ${
	it("no entry is slot one", _ {
		expect(nextSlotName("", "SYNT")).to_equal(std::string{"SYNT1"});
	});

	it("follows the last number", _ {
		expect(nextSlotName("SYNT6", "SYNT")).to_equal(std::string{"SYNT7"});
	});

	it("keeps the digit count", _ {
		expect(nextSlotName("SYNT007", "SYNT")).to_equal(std::string{"SYNT008"});
	});

	it("grows a digit", _ {
		expect(nextSlotName("SYNT9", "SYNT")).to_equal(std::string{"SYNT10"});
	});

	it("non-digit after prefix is slot one", _ {
		// nextSlotName's parse stops at the first non-digit, having read none: slot 0 + 1, no padding.
		expect(nextSlotName("SYNTHY", "SYNT")).to_equal(std::string{"SYNT1"});
	});

	it("digits then suffix use the digits", _ {
		expect(nextSlotName("SYNT12A", "SYNT")).to_equal(std::string{"SYNT13"});
	});

	it("shorter than prefix is slot one", _ {
		// A plain parse would index past the end of such a name; nextSlotName reads it as "no digits".
		expect(nextSlotName("SY", "SYNT")).to_equal(std::string{"SYNT1"});
	});

	it("kit prefix", _ {
		expect(nextSlotName("KIT20", "KIT")).to_equal(std::string{"KIT21"});
	});
});

DELUGE_SPEC(default_name_spec, default_name_group, next_slot_name_group)
