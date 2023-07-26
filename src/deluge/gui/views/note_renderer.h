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
                        uint32_t xZoom, int xStartNow, int xEnd, bool drawRepeats, int clipColourOffset);

	// based on IntrumentClip::getMainColorFromY , but left out noterow color offset
	// because it also doesnt seem used in instrument clip view.
	void getNoteColourFromY(int yNote, int clipColourOffset, uint8_t rgb[]);

private:

	// applies color changes based on note properties such as accidentalTranspose.
	void getNoteSpecificColours(int yNote,
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