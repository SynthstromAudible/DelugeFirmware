#include "gui/views/note_renderer.h"
#include "gui/views/timeline_view.h"
#include "model/note/note_row.h"
#include "model/note/note.h"
#include <string.h>

NoteRenderer noteRenderer;

NoteRenderer::NoteRenderer() {


}

/*
void NoteRenderer::recalculateColours() {

}

void NoteRenderer::recalculateColour(uint8_t yDisplay) {

}

void NoteRenderer::performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][displayWidth + sideBarWidth],
                         int32_t xScroll, uint32_t xZoom, int renderWidth, int imageWidth,
                         bool drawUndefinedArea = true) {

}
*/

// this function was previously in noterow but now has a noterow as parameter
void NoteRenderer::renderNoteRow(NoteRow* noteRow, TimelineView* editorScreen, uint8_t rowColour[], uint8_t rowTailColour[],
                    uint8_t rowBlurColour[], uint8_t* image, uint8_t occupancyMask[], bool overwriteExisting,
                    uint32_t effectiveRowLength, bool allowNoteTails, int renderWidth, int32_t xScroll,
                    uint32_t xZoom, int xStartNow, int xEnd, bool drawRepeats) {

	if (overwriteExisting) {
		memset(image, 0, renderWidth * 3);
		if (occupancyMask) memset(occupancyMask, 0, renderWidth);
	}

	if (noteRow->hasNoNotes()) return;

	int32_t squareEndPos[MAX_IMAGE_STORE_WIDTH];
	int32_t searchTerms[MAX_IMAGE_STORE_WIDTH];

	int whichRepeat = 0;

	int xEndNow;

	do {
		// Start by presuming we'll do all the squares now... though we may decide in a second to do a smaller number now, and come back for more batches
		xEndNow = xEnd;

		// For each square we might do now...
		for (int square = xStartNow; square < xEndNow; square++) {

			// Work out its endPos...
			int32_t thisSquareEndPos =
			    editorScreen->getPosFromSquare(square + 1, xScroll, xZoom) - effectiveRowLength * whichRepeat;

			// If we're drawing repeats and the square ends beyond end of Clip...
			if (drawRepeats && thisSquareEndPos > effectiveRowLength) {

				// If this is the first square we're doing now, we can take a shortcut to skip forward some repeats
				if (square == xStartNow) {
					int numExtraRepeats = (uint32_t)(thisSquareEndPos - 1) / effectiveRowLength;
					whichRepeat += numExtraRepeats;
					thisSquareEndPos -= numExtraRepeats * effectiveRowLength;
				}

				// Otherwise, we'll come back in a moment to do the rest of the repeats - just record that we want to stop rendering before this square until then
				else {
					xEndNow = square;
					break;
				}
			}

			squareEndPos[square - xStartNow] = thisSquareEndPos;
		}

		memcpy(searchTerms, squareEndPos, sizeof(squareEndPos));

		noteRow->notes.searchMultiple(searchTerms, xEndNow - xStartNow);

		int squareStartPos =
		    editorScreen->getPosFromSquare(xStartNow, xScroll, xZoom) - effectiveRowLength * whichRepeat;

		for (int xDisplay = xStartNow; xDisplay < xEndNow; xDisplay++) {
			if (xDisplay != xStartNow) squareStartPos = squareEndPos[xDisplay - xStartNow - 1];
			int i = searchTerms[xDisplay - xStartNow];

			Note* note = noteRow->notes.getElement(i - 1); // Subtracting 1 to do "LESS"

			uint8_t* pixel = image + xDisplay * 3;

			// If Note starts somewhere within square, draw the blur colour
			if (note && note->pos > squareStartPos) {
				pixel[0] = rowBlurColour[0];
				pixel[1] = rowBlurColour[1];
				pixel[2] = rowBlurColour[2];
				if (occupancyMask) occupancyMask[xDisplay] = 64;
			}

			// Or if Note starts exactly on square...
			else if (note && note->pos == squareStartPos) {
				pixel[0] = rowColour[0];
				pixel[1] = rowColour[1];
				pixel[2] = rowColour[2];
				if (occupancyMask) occupancyMask[xDisplay] = 64;
			}

			// Draw wrapped notes
			else if (!drawRepeats || whichRepeat) {
				bool wrapping = (i == 0); // Subtracting 1 to do "LESS"
				if (wrapping) note = noteRow->notes.getLast();
				int noteEnd = note->pos + note->length;
				if (wrapping) noteEnd -= effectiveRowLength;
				if (noteEnd > squareStartPos && allowNoteTails) {
					pixel[0] = rowTailColour[0];
					pixel[1] = rowTailColour[1];
					pixel[2] = rowTailColour[2];
					if (occupancyMask) occupancyMask[xDisplay] = 64;
				}
			}
		}

		xStartNow = xEndNow;
		whichRepeat++;
		// TODO: if we're gonna repeat, we probably don't need to do the multi search again...
	} while (
	    xStartNow
	    != xEnd); // This will only do another repeat if we'd modified xEndNow, which can only happen if drawRepeats

}
