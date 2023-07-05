#ifndef NoteRenderer_h
#define NoteRenderer_h

class NoteRow;
class TimelineView;

#include "definitions.h"


class NoteRenderer final {
public:
	NoteRenderer();

/*
	// this functions where previously in instrument_clip_view
	void recalculateColours();
	void recalculateColour(uint8_t yDisplay);
	void performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][displayWidth + sideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int renderWidth, int imageWidth,
	                         bool drawUndefinedArea = true);
*/

    // this function was previously in noterow but now has a noterow as parameter
    void renderNoteRow(NoteRow* noteRow, TimelineView* editorScreen, uint8_t rowColour[], uint8_t rowTailColour[],
                        uint8_t rowBlurColour[], uint8_t* image, uint8_t occupancyMask[], bool overwriteExisting,
                        uint32_t effectiveRowLength, bool allowNoteTails, int renderWidth, int32_t xScroll,
                        uint32_t xZoom, int xStartNow, int xEnd, bool drawRepeats);
};

extern NoteRenderer noteRenderer;

#endif