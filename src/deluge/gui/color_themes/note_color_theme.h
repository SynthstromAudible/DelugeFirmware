#ifndef NoteColorTheme_h
#define NoteColorTheme_h

class Note;

#include "definitions.h"


class NoteColorTheme final {
public:
	NoteColorTheme();

    // kit colors
	void getRowMainColourForKit(int clipColourOffset, int kitColourOffset, uint8_t rgb[]);
	void getRowColoursForKit(int clipColourOffset, int kitColourOffset, 
		uint8_t mainColour[],uint8_t blurColour[], uint8_t tailColour[]);
    
    // pitch colors
    void getRowMainColourForPitch(int yNote, int clipColourOffset, uint8_t rgb[]);
	void getRowColoursForPitch(int yNote, int clipColourOffset, 
		uint8_t mainColour[],uint8_t blurColour[], uint8_t tailColour[]);


    // colors for transposed notes (only pitches, no kits)
    void getNoteSpecificMainColour(int yNote,
	                       int clipColourOffset,	
		                   Note* note, uint8_t noteColour[]); 
    
	void getNoteSpecificColours(int yNote,
	                       int clipColourOffset,	
		                   Note* note, 
	                       uint8_t noteColour[],
                           uint8_t noteBlurColour[],
                           uint8_t noteTailColour[]);

private:
    // the methods that provide the raw colors to the public methods
    void getThemeColorForKit(int clipColourOffset, int kitColourOffset,  uint8_t rgb[]);
    void getThemeColorForPitch(int yNote, int clipColourOffset, uint8_t rgb[]); 
};

extern NoteColorTheme noteColorTheme;

#endif