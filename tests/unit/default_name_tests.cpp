#include "CppUTest/TestHarness.h"
#include "gui/ui/browser/default_name.h"
#include "util/name_compare.h"
#include <cstdio>
#include <string>
#include <vector>

using deluge::gui::browser::FileListView;
using deluge::gui::browser::nextDefaultName;

namespace {
/// Stands in for Browser::fileItems.
class FakeFileList : public FileListView {
public:
	explicit FakeFileList(std::vector<std::string> names) : names_{std::move(names)} {}
	bool contains(char const* nameWithExtension) const override {
		for (auto const& n : names_) {
			if (strcmpspecial(n.c_str(), nameWithExtension) == 0) {
				return true;
			}
		}
		return false;
	}

private:
	std::vector<std::string> names_;
};

/// A card holding SONG001.XML .. SONGnnn.XML, as a vanilla card looks.
FakeFileList cardWithSongsUpTo(int32_t highest) {
	std::vector<std::string> names;
	for (int32_t i = 1; i <= highest; i++) {
		char buf[32];
		snprintf(buf, sizeof buf, "SONG%03d.XML", i);
		names.emplace_back(buf);
	}
	return FakeFileList{std::move(names)};
}
} // namespace

TEST_GROUP(DefaultNameTests){void setup() override{shouldInterpretNoteNames = false;
octaveStartsFromA = false;
}
}
;

// --- Issue #1069: the actual regression -------------------------------------------------

TEST(DefaultNameTests, issue1069_defaultNamedSongGetsALetterSuffix) {
	auto files = cardWithSongsUpTo(185);
	CHECK_EQUAL(std::string{"SONG185A"}, nextDefaultName("SONG185", "SONG", files));
}

TEST(DefaultNameTests, issue1069_neverEmitsTheSlotNumberAsASuffix) {
	// The bug produced "185_186": the slot number leaking in through a wrong string offset.
	auto files = cardWithSongsUpTo(185);
	std::string result = nextDefaultName("SONG185", "SONG", files);
	CHECK(result.find("186") == std::string::npos);
	CHECK(result.find('_') == std::string::npos);
}

TEST(DefaultNameTests, letterSuffixesAdvance) {
	FakeFileList files{{"SONG185.XML", "SONG185A.XML", "SONG185B.XML"}};
	CHECK_EQUAL(std::string{"SONG185C"}, nextDefaultName("SONG185", "SONG", files));
	CHECK_EQUAL(std::string{"SONG185C"}, nextDefaultName("SONG185B", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixFillsTheFirstGap) {
	FakeFileList files{{"SONG185.XML", "SONG185A.XML", "SONG185C.XML"}};
	CHECK_EQUAL(std::string{"SONG185B"}, nextDefaultName("SONG185", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixExhaustedReturnsInputUnchanged) {
	std::vector<std::string> names{"SONG185.XML"};
	for (char c = 'A'; c <= 'Z'; c++) {
		names.push_back(std::string{"SONG185"} + c + ".XML");
	}
	FakeFileList files{std::move(names)};
	CHECK_EQUAL(std::string{"SONG185"}, nextDefaultName("SONG185", "SONG", files));
}

// --- Behaviour that must be preserved (this is what OLED does today) ---------------------

TEST(DefaultNameTests, renamedSongGetsANumericSuffix) {
	FakeFileList files{{"MYTRACK.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 2"}, nextDefaultName("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixAdvances) {
	FakeFileList files{{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 3.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 4"}, nextDefaultName("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, existingUnderscoreDelimiterIsReused) {
	// A name that already carries an "_<number>" suffix keeps the underscore.
	FakeFileList files{{"TRACK_1.XML"}};
	CHECK_EQUAL(std::string{"TRACK_2"}, nextDefaultName("TRACK_1", "SONG", files));
}

TEST(DefaultNameTests, nonNumericNameSharingThePrefixIsNotTreatedAsASlot) {
	// "SONGIDEA" starts with SONG but has no digits - it is just a name.
	FakeFileList files{{"SONGIDEA.XML"}};
	CHECK_EQUAL(std::string{"SONGIDEA 2"}, nextDefaultName("SONGIDEA", "SONG", files));
}

TEST(DefaultNameTests, presetsKeepTheNumericSuffix) {
	// Only songs get letter suffixes. Presets pass an empty slotPrefix, so SYNT005 takes the
	// numeric path - preserving current OLED preset behaviour. See Global Constraints.
	FakeFileList files{{"SYNT005.XML"}};
	CHECK_EQUAL(std::string{"SYNT005 2"}, nextDefaultName("SYNT005", "", files));
}
