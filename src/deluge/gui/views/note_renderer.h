#ifndef NoteRenderer_h
#define NoteRenderer_h

class NoteRow;
class TimelineView;
class Note;

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
    void renderNoteRow(NoteRow* noteRow, TimelineView* editorScreen, uint8_t* image, uint8_t occupancyMask[], bool overwriteExisting,
                        uint32_t effectiveRowLength, bool allowNoteTails, int renderWidth, int32_t xScroll,
                        uint32_t xZoom, int xStartNow, int xEnd, bool drawRepeats, int clipColourOffset, int noteRowColourOffset, bool isKit);


	void getNoteColourFromY(int yNote, int clipColourOffset, uint8_t rgb[]);
    void getKitColourFromY(int yNote, int clipColourOffset, int rowColourOffset, uint8_t rgb[]);
private:

	// applies color changes based on note properties such as accidentalTranspose.
	// for kits it does nothing.
	void getNoteSpecificColours(int yNote,
	                       bool isKit,
	                       int clipColourOffset,	
		                   Note* note, 
	                       uint8_t rowDefaultColour[],
	                       uint8_t rowDefaultBlurColour[],
	                       uint8_t rowDefaultTailColour[],
	                       uint8_t noteColour[],
                           uint8_t noteBlurColour[],
                           uint8_t noteTailColour[]);
};

extern NoteRenderer noteRenderer;

#endif