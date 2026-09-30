#include "CppUTest/TestHarness.h"
#include "io/c_path.h"

#include <cstring>
#include <string>

using deluge::io::CPath;

TEST_GROUP(CPath){};

TEST(CPath, ASubstringIsTerminatedAtItsEnd) {
	const std::string whole = "SONGS/SONG001.XML trailing";
	auto path = CPath::from(std::string_view{whole}.substr(0, 17));
	CHECK_TRUE(path.has_value());
	STRCMP_EQUAL("SONGS/SONG001.XML", path->c_str());
}

TEST(CPath, TheLongestPathFitsAndOneMoreIsRefused) {
	const std::string longest(DELUGE_PATH_MAX - 1, 'a');
	CHECK_TRUE(CPath::from(longest).has_value());
	CHECK_EQUAL(DELUGE_PATH_MAX - 1, std::strlen(CPath::from(longest)->c_str()));
	const std::string tooLong(DELUGE_PATH_MAX, 'a');
	CHECK_FALSE(CPath::from(tooLong).has_value());
}

TEST(CPath, AnEmbeddedNulIsRefused) {
	const std::string withNul("A\0B", 3);
	CHECK_FALSE(CPath::from(withNul).has_value());
}

TEST(CPath, TheViewIsThePath) {
	auto path = CPath::from("KITS/KIT000.XML");
	CHECK_TRUE(path->view() == "KITS/KIT000.XML");
}
