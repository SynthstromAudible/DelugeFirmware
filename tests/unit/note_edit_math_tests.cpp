#include "CppUTest/TestHarness.h"
#include "gui/views/note_edit_math.h"
#include <climits>

using namespace deluge::gui;
TEST_GROUP(NoteEditMath){};

static int64_t findMove(std::initializer_list<NoteMoveRegion> selected, std::initializer_list<NoteMoveRegion> obstacles,
                        int32_t steps, int32_t zoom = 4) {
	return findNoteGridMove({selected.begin(), selected.size()}, steps, zoom, [&](auto visit) {
		for (const auto& obstacle : obstacles) {
			visit(obstacle);
		}
	});
}

TEST(NoteEditMath, ClearMoveKeepsFineTiming) {
	CHECK_EQUAL(4, findMove({{0, 3, 2, 32}}, {}, 1));
	CHECK_EQUAL(-4, findMove({{0, 7, 2, 32}}, {}, -1));
}

TEST(NoteEditMath, SkipsOccupiedNotesInEitherDirection) {
	CHECK_EQUAL(12, findMove({{0, 0, 4, 32}}, {{0, 4, 8, 32}}, 1));
	CHECK_EQUAL(-12, findMove({{0, 16, 4, 32}}, {{0, 8, 8, 32}}, -1));
}

TEST(NoteEditMath, EntireSelectionMovesBySameDistanceAcrossRows) {
	CHECK_EQUAL(12, findMove({{0, 0, 4, 32}, {1, 8, 4, 32}}, {{1, 12, 8, 32}}, 1));
	// A note on another row is not an obstacle.
	CHECK_EQUAL(4, findMove({{0, 0, 4, 32}}, {{1, 4, 24, 32}}, 1));
}

TEST(NoteEditMath, ProtectsBothSelectedAndUnselectedNoteTails) {
	CHECK_EQUAL(12, findMove({{0, 0, 6, 32}}, {{0, 8, 4, 32}}, 1));
	CHECK_EQUAL(-20, findMove({{0, 24, 4, 32}}, {{0, 8, 14, 32}}, -1));
}

TEST(NoteEditMath, WrapsWholeSelectionAndStopsWhenNoGapFits) {
	CHECK_EQUAL(0, findMove({{0, 0, 4, 16}}, {{0, 4, 12, 16}}, 1));
	CHECK_EQUAL(4, findMove({{0, 0, 4, 32}, {1, 28, 4, 32}}, {}, 1));
	CHECK_EQUAL(-4, findMove({{0, 0, 4, 32}, {1, 8, 4, 32}}, {}, -1));
	CHECK_EQUAL(12, findMove({{0, 0, 4, 32}, {1, 8, 4, 16}}, {{0, 4, 8, 32}}, 1));
	CHECK_EQUAL(0, wrapNotePosition(28, 4, 32));
	CHECK_EQUAL(28, wrapNotePosition(0, -4, 32));
	CHECK_EQUAL(4, wrapNotePosition(8, 12, 16));
}

TEST(NoteEditMath, WrappingPreservesFineTimingAndSkipsObstaclesAcrossZero) {
	CHECK_EQUAL(1, wrapNotePosition(29, 4, 32));
	CHECK_EQUAL(29, wrapNotePosition(1, -4, 32));
	CHECK_EQUAL(8, findMove({{0, 12, 4, 16}}, {{0, 0, 4, 16}}, 1));
	CHECK_EQUAL(-8, findMove({{0, 0, 4, 16}}, {{0, 12, 4, 16}}, -1));
	CHECK_EQUAL(0, findMove({{0, 0, 4, 16}}, {}, 4));
}

TEST(NoteEditMath, MovingSourcesAreAvailableToOtherSelectedNotes) {
	CHECK_EQUAL(4, findMove({{0, 0, 4, 32}, {0, 4, 4, 32}}, {}, 1));
}

TEST(NoteEditMath, ExistingWrappedTailBlocksStartOfLoop) {
	CHECK_EQUAL(-4, findMove({{0, 8, 2, 16}}, {{0, 14, 6, 16}}, -1));
	CHECK_EQUAL(-8, findMove({{0, 4, 2, 16}}, {{0, 14, 6, 16}}, -1));
}

TEST(NoteEditMath, SelectedTailsCanWrapWithoutBeingTruncated) {
	CHECK_EQUAL(-1, findMove({{0, 14, 4, 16}}, {}, -1, 1));
	CHECK_EQUAL(1, findMove({{0, 14, 4, 16}}, {}, 1, 1));
	CHECK_EQUAL(-6, findMove({{0, 14, 4, 16}}, {{0, 12, 2, 16}}, -1, 1));
}

TEST(NoteEditMath, AcceleratedMoveWrapsWithoutIntegerOverflow) {
	auto right = findMove({{0, 5, 4, 32}}, {}, INT32_MAX, INT32_MAX);
	CHECK_EQUAL(6, wrapNotePosition(5, right, 32));
	auto left = findMove({{0, 5, 4, 31}}, {}, INT32_MIN, INT32_MAX);
	CHECK_EQUAL(3, wrapNotePosition(5, left, 31));
}

TEST(NoteEditMath, SearchMatchesExhaustiveGridPlacement) {
	// Exhaust all obstacle patterns on a small grid, testing two selected notes
	// as a rigid shape in both directions. Selected sources are excluded.
	constexpr NoteMoveRegion selected[] = {{0, 4, 2, 16}, {0, 8, 2, 16}};
	for (int mask = 0; mask < 256; ++mask) {
		for (int direction : {-1, 1}) {
			auto obstacles = [&](auto visit) {
				for (int cell = 0; cell < 8; ++cell) {
					if (cell != 2 && cell != 4 && (mask & (1 << cell))) {
						visit(NoteMoveRegion{0, cell * 2, 2, 16});
					}
				}
			};
			int expected = 0;
			for (int distance = 2; distance <= 16; distance += 2) {
				bool fits = true;
				for (const auto& note : selected) {
					int start = wrapNotePosition(note.position, direction * distance, 16);
					obstacles([&](const auto& obstacle) {
						fits &=
						    !(start < obstacle.position + obstacle.length && start + note.length > obstacle.position);
					});
				}
				if (fits) {
					expected = distance == 16 ? 0 : direction * distance;
					break;
				}
			}
			CHECK_EQUAL(expected, findNoteGridMove(selected, direction, 2, obstacles));
		}
	}
}

TEST(NoteEditMath, WrappedCollisionSearchMatchesOccupiedTicks) {
	for (int loop = 2; loop <= 10; ++loop) {
		for (int length = 1; length <= loop; ++length) {
			for (int other = 0; other < loop; ++other) {
				for (int otherLength = 1; otherLength <= loop; ++otherLength) {
					for (int step : {-3, -1, 1, 3}) {
						int expected = 0;
						int direction = step < 0 ? -1 : 1;
						int stride = step < 0 ? -step : step;
						for (int distance = stride; distance < stride + loop; distance += stride) {
							bool fits = true;
							int start = wrapNotePosition(loop - 1, direction * distance, loop);
							for (int tick = 0; tick < length; ++tick) {
								for (int o = 0; o < otherLength; ++o) {
									fits &= (start + tick) % loop != (other + o) % loop;
								}
							}
							if (fits) {
								expected = distance % loop == 0 ? 0 : direction * distance;
								break;
							}
						}
						CHECK_EQUAL(expected, findMove({{0, loop - 1, length, loop}}, {{0, other, otherLength, loop}},
						                               direction, stride));
					}
				}
			}
		}
	}
}

TEST(NoteEditMath, NudgeWrapDisplaysSignedDistanceFromOriginalSquare) {
	bool nextSquare = false;
	CHECK_EQUAL(-1, noteNudgeDisplayOffset(95, 96, 24, true, nextSquare));
	CHECK_EQUAL(1, noteNudgeDisplayOffset(-95, 96, 24, true, nextSquare));
}

TEST(NoteEditMath, InitialLateNoteDisplaysDistanceFromNextSquare) {
	bool nextSquare = false;
	CHECK_EQUAL(-1, noteNudgeDisplayOffset(23, 96, 24, false, nextSquare));
	CHECK_TRUE(nextSquare);
	CHECK_EQUAL(-2, noteNudgeDisplayOffset(22, 96, 24, true, nextSquare));
	CHECK_EQUAL(12, noteNudgeDisplayOffset(12, 96, 24, true, nextSquare));
	CHECK_FALSE(nextSquare);
}

TEST(NoteEditMath, OngoingNudgeDoesNotJumpToNextSquareReference) {
	bool nextSquare = false;
	CHECK_EQUAL(13, noteNudgeDisplayOffset(13, 96, 24, true, nextSquare));
	CHECK_FALSE(nextSquare);
}

TEST(NoteEditMath, PasteRequiresRoomForTheCompleteNote) {
	CHECK_TRUE(notePasteFits(12, 4, 16));
	CHECK_FALSE(notePasteFits(13, 4, 16));
	CHECK_FALSE(notePasteFits(-1, 4, 16));
	CHECK_FALSE(notePasteFits(0, 0, 16));
	CHECK_FALSE(notePasteFits(INT32_MAX, INT32_MAX, INT32_MAX));
}

TEST(NoteEditMath, PasteCanTouchButNotOverlapExistingNotes) {
	CHECK_FALSE(notePasteOverlaps(4, 4, 0, 4, 32));
	CHECK_FALSE(notePasteOverlaps(4, 4, 8, 4, 32));
	CHECK_TRUE(notePasteOverlaps(4, 4, 4, 4, 32));
	CHECK_TRUE(notePasteOverlaps(4, 4, 2, 4, 32));
	CHECK_TRUE(notePasteOverlaps(4, 8, 8, 4, 32));
}

TEST(NoteEditMath, PasteProtectsWrappedTailsAtBeginningOfRow) {
	CHECK_TRUE(notePasteOverlaps(0, 2, 14, 6, 16));
	CHECK_TRUE(notePasteOverlaps(3, 2, 14, 6, 16));
	CHECK_FALSE(notePasteOverlaps(4, 2, 14, 6, 16));
	CHECK_TRUE(notePasteOverlaps(12, 4, 14, 6, 16));
}

TEST(NoteEditMath, PasteOverlapMatchesEveryOccupiedTickOnSmallLoops) {
	for (int loop = 1; loop <= 16; ++loop) {
		for (int start = 0; start < loop; ++start) {
			for (int length = 1; length <= loop - start; ++length) {
				for (int other = 0; other < loop; ++other) {
					for (int otherLength = 1; otherLength <= loop; ++otherLength) {
						bool overlaps = false;
						for (int tick = other; tick < other + otherLength; ++tick) {
							int occupied = tick % loop;
							overlaps |= occupied >= start && occupied < start + length;
						}
						CHECK_EQUAL(overlaps, notePasteOverlaps(start, length, other, otherLength, loop));
					}
				}
			}
		}
	}
}

TEST(NoteEditMath, GentlePasteSkipsOccupiedStartsAndClipsOnlyNewTails) {
	CHECK_EQUAL(0, gentleNotePasteLength(4, 8, 2, 4, 32));
	CHECK_EQUAL(0, gentleNotePasteLength(4, 8, 4, 4, 32));
	CHECK_EQUAL(4, gentleNotePasteLength(4, 8, 8, 8, 32));
	CHECK_EQUAL(8, gentleNotePasteLength(4, 8, 0, 4, 32));
	CHECK_EQUAL(0, gentleNotePasteLength(0, 4, 30, 4, 32));
}

TEST(NoteEditMath, FirmPasteClearsHeadsAndTrimsTailsWithinItsFootprint) {
	CHECK_EQUAL(0, noteLengthAfterPasteClear(8, 12, 32, 4, 16));
	CHECK_EQUAL(4, noteLengthAfterPasteClear(0, 12, 32, 4, 16));
	CHECK_EQUAL(8, noteLengthAfterPasteClear(16, 8, 32, 4, 16));
	CHECK_EQUAL(6, noteLengthAfterPasteClear(30, 10, 32, 4, 16));
	CHECK_EQUAL(2, noteLengthAfterPasteClear(30, 10, 32, 0, 16));
	CHECK_EQUAL(10, noteLengthAfterPasteClear(30, 10, 32, 36, 40));
}

TEST(NoteEditMath, GentlePasteAddsOnlyUnoccupiedTicksWithoutChangingExistingNotes) {
	for (int loop = 1; loop <= 16; ++loop) {
		for (int start = 0; start < loop; ++start) {
			for (int length = 1; length <= loop - start; ++length) {
				for (int other = 0; other < loop; ++other) {
					for (int otherLength = 1; otherLength <= loop; ++otherLength) {
						int expected = 0;
						for (int tick = start; tick < start + length; ++tick) {
							bool occupied = false;
							for (int o = other; o < other + otherLength; ++o) {
								occupied |= o % loop == tick;
							}
							if (occupied) {
								break;
							}
							++expected;
						}
						CHECK_EQUAL(expected, gentleNotePasteLength(start, length, other, otherLength, loop));
					}
				}
			}
		}
	}
}

TEST(NoteEditMath, LengthEditsUseCurrentZoomAndKeepFineTiming) {
	CHECK_EQUAL(27, resizeNoteLength(3, 1, 24, 96));
	CHECK_EQUAL(3, resizeNoteLength(27, -1, 24, 96));
	CHECK_EQUAL(51, resizeNoteLength(3, 2, 24, 96));
}

TEST(NoteEditMath, LengthEditsStopAtOneTickAndAtTheNextNote) {
	CHECK_EQUAL(1, resizeNoteLength(3, -1, 24, 96));
	CHECK_EQUAL(30, resizeNoteLength(24, 1, 24, 30));
	CHECK_EQUAL(1, resizeNoteLength(1, 1, 24, 1));
	CHECK_EQUAL(96, resizeNoteLength(72, 2, 24, 96));
}

TEST(NoteEditMath, AcceleratedLengthEditsCannotOverflow) {
	CHECK_EQUAL(96, resizeNoteLength(24, INT32_MAX, INT32_MAX, 96));
	CHECK_EQUAL(1, resizeNoteLength(24, INT32_MIN, INT32_MAX, 96));
}
