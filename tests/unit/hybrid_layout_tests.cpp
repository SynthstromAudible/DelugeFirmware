#include "CppUTest/TestHarness.h"
#include "gui/views/session/hybrid_layout.h"

using namespace deluge::gui::session_layout::hybrid;

TEST_GROUP(HybridLayoutTest){};

TEST(HybridLayoutTest, SevenTracksRunFromTopToBottomAndReserveBottomRow) {
	LONGS_EQUAL(7, trackRow(0, 0));
	LONGS_EQUAL(1, trackRow(6, 0));
	LONGS_EQUAL(-1, trackRow(7, 0));
	LONGS_EQUAL(-1, trackIndex(0, 0));
	LONGS_EQUAL(-1, trackIndex(8, 0));
	LONGS_EQUAL(-1, trackIndex(-1, 0));
}

TEST(HybridLayoutTest, ScrolledTracksAndSectionsRoundTripWithoutCrossingRegions) {
	for (int scroll = 0; scroll < 32; ++scroll) {
		for (int track = 0; track < 40; ++track) {
			int row = trackRow(track, scroll);
			if (track >= scroll && track < scroll + 7) {
				CHECK(row >= 1 && row <= 7);
				LONGS_EQUAL(track, trackIndex(row, scroll));
			}
			else {
				LONGS_EQUAL(-1, row);
			}
		}
		for (int section = 0; section < 40; ++section) {
			int column = sectionColumn(section, scroll, 32);
			if (section < 32 && section >= scroll && section < scroll + 8) {
				CHECK(column >= 0 && column < 8);
				LONGS_EQUAL(section, sectionIndex(column, scroll, 32));
			}
			else {
				LONGS_EQUAL(-1, column);
			}
		}
	}
}

TEST(HybridLayoutTest, PreviewPadsNeverResolveToSections) {
	for (int x = 8; x < 18; ++x) {
		LONGS_EQUAL(-1, sectionIndex(x, 0, 32));
	}
	LONGS_EQUAL(-1, sectionIndex(-1, 0, 32));
	LONGS_EQUAL(-1, sectionIndex(7, 30, 32));
}

TEST(HybridLayoutTest, PlayheadStaysInsidePreviewAndPreservesOffscreenSentinel) {
	for (int tick = 0; tick < 16; ++tick) {
		CHECK(previewTick(tick) >= 8 && previewTick(tick) <= 15);
	}
	LONGS_EQUAL(8, previewTick(0));
	LONGS_EQUAL(15, previewTick(15));
	LONGS_EQUAL(255, previewTick(-1));
	LONGS_EQUAL(255, previewTick(16));
	LONGS_EQUAL(255, previewTick(255));
}
