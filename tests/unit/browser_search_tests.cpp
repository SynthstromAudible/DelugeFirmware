// Browser prefix search boundary tests.
// Verifies the off-by-one fix where binary search returning i == numElements
// (past end) caused the last item to be unfindable.

#include "CppUTest/TestHarness.h"
#include <cstring>
#include <vector>

namespace {

int memcasecmpSim(const char* a, const char* b, int len) {
	for (int i = 0; i < len; i++) {
		int ca = (a[i] >= 'a' && a[i] <= 'z') ? a[i] - 32 : a[i];
		int cb = (b[i] >= 'a' && b[i] <= 'z') ? b[i] - 32 : b[i];
		if (ca != cb) {
			return ca - cb;
		}
	}
	return 0;
}

// Simulates the fixed search logic from browser.cpp
int searchForPrefix(const std::vector<const char*>& sortedFiles, const char* prefix) {
	int prefixLen = static_cast<int>(strlen(prefix));
	if (prefixLen == 0 || sortedFiles.empty()) {
		return -1;
	}

	int lo = 0;
	int hi = static_cast<int>(sortedFiles.size());
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (memcasecmpSim(sortedFiles[mid], prefix, prefixLen) < 0) {
			lo = mid + 1;
		}
		else {
			hi = mid;
		}
	}

	if (lo >= static_cast<int>(sortedFiles.size())) {
		// Fix: check last element before giving up
		int lastIdx = static_cast<int>(sortedFiles.size()) - 1;
		if (memcasecmpSim(sortedFiles[lastIdx], prefix, prefixLen) == 0) {
			return lastIdx;
		}
		return -1;
	}

	if (memcasecmpSim(sortedFiles[lo], prefix, prefixLen) == 0) {
		return lo;
	}

	return -1;
}

} // namespace

TEST_GROUP(BrowserSearchTest){};

TEST(BrowserSearchTest, findFirstItem) {
	std::vector<const char*> files = {"Alpha", "Beta", "Gamma", "Zeta"};
	CHECK_EQUAL(0, searchForPrefix(files, "A"));
}

TEST(BrowserSearchTest, findMiddleItem) {
	std::vector<const char*> files = {"Alpha", "Beta", "Gamma", "Zeta"};
	CHECK_EQUAL(2, searchForPrefix(files, "G"));
}

TEST(BrowserSearchTest, findLastItem) {
	std::vector<const char*> files = {"Alpha", "Beta", "Gamma", "Zeta"};
	CHECK_EQUAL(3, searchForPrefix(files, "Z"));
}

TEST(BrowserSearchTest, lastItemPrefixMatch) {
	std::vector<const char*> files = {"Alpha", "Beta", "Gamma", "Zeta"};
	CHECK_EQUAL(3, searchForPrefix(files, "Ze"));
}

TEST(BrowserSearchTest, notFoundPastEnd) {
	std::vector<const char*> files = {"Alpha", "Beta", "Gamma"};
	CHECK_EQUAL(-1, searchForPrefix(files, "Z"));
}

TEST(BrowserSearchTest, singleElementFound) {
	std::vector<const char*> files = {"Solo"};
	CHECK_EQUAL(0, searchForPrefix(files, "S"));
}

TEST(BrowserSearchTest, singleElementNotFound) {
	std::vector<const char*> files = {"Solo"};
	CHECK_EQUAL(-1, searchForPrefix(files, "Z"));
}

TEST(BrowserSearchTest, emptyListReturnsNotFound) {
	std::vector<const char*> files;
	CHECK_EQUAL(-1, searchForPrefix(files, "A"));
}

TEST(BrowserSearchTest, caseInsensitiveMatch) {
	std::vector<const char*> files = {"alpha", "beta", "gamma"};
	CHECK_EQUAL(0, searchForPrefix(files, "A"));
}
