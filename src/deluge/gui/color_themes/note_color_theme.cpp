#include "gui/color_themes/note_color_theme.h"
#include "model/note/note.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include <string.h>

NoteColorTheme noteColorTheme;

NoteColorTheme::NoteColorTheme() {


}

void NoteColorTheme::getRowMainColourForKit(int clipColourOffset, int kitColourOffset, uint8_t rgb[]){
	// there are no drum themes yet.	
	getThemeColorForKit(clipColourOffset, kitColourOffset, rgb);

}
void NoteColorTheme::getRowColoursForKit(int clipColourOffset, int kitColourOffset, 
	uint8_t mainColour[],uint8_t blurColour[], uint8_t tailColour[]) {
	
	getRowMainColourForKit(clipColourOffset, kitColourOffset, mainColour);
	getBlurColour(blurColour, mainColour);
	getTailColour(tailColour, mainColour);
}

void NoteColorTheme::getRowMainColourForPitch(int yNote, int clipColourOffset, uint8_t rgb[]) {
    getThemeColorForPitch(yNote, clipColourOffset, rgb);
}


void NoteColorTheme::getRowColoursForPitch(int yNote, int clipColourOffset, 
	uint8_t mainColour[],uint8_t blurColour[], uint8_t tailColour[]) {

	getRowMainColourForPitch(yNote,clipColourOffset, mainColour);
	getBlurColour(blurColour, mainColour);
	getTailColour(tailColour, mainColour);

}


    // this is currently not supported for kits, so we dont pass in rowColourOffset
    // inputs: rowDefaultColor... rowDefaultTailColor, cached color for notes without transpose.

void NoteColorTheme::getNoteSpecificMainColour(int yNote,
	                       int clipColourOffset,	
		                   Note* note, uint8_t noteColour[]) {

	// copy either the defaults or tranpose
	int transpose = note->getAccidentalTranspose();
	if ( transpose == 0) {
		getThemeColorForPitch(yNote,clipColourOffset, noteColour);
	}
	else {
    	// use hard red and green if the transpose is less than 1 octave.
    	if (transpose > 0 && (transpose % 12 != 0 )) {
    		noteColour[0] = 0;  
    		noteColour[1] = 64; 
    		noteColour[2] = 0;  
    	}
    	else if (transpose < 0 && (transpose + 240) % 12 != 0) {
    		noteColour[0] = 64; 
    		noteColour[1] = 0;  
    		noteColour[2] = 0; 
    	}
	}
}
    
void NoteColorTheme::getNoteSpecificColours(int yNote,
	                       int clipColourOffset,	
		                   Note* note, 
	                       uint8_t noteColour[],
                           uint8_t noteBlurColour[],
                           uint8_t noteTailColour[]) {

	getNoteSpecificMainColour(yNote, clipColourOffset, note,noteColour);
	getBlurColour(noteBlurColour, noteColour);
	getTailColour(noteTailColour, noteColour);


}



void NoteColorTheme::getThemeColorForKit(int clipColourOffset, int kitColourOffset, uint8_t rgb[]) {
	// no kit themes yet.
	hueToRGB((clipColourOffset + kitColourOffset) * -8 / 3, rgb);
}

void NoteColorTheme::getThemeColorForPitch(int yNote, int clipColourOffset, uint8_t rgb[])  {
	int colorScheme = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ColorScheme);
	if (colorScheme == RuntimeFeatureStateColorScheme::Classic) {
		hueToRGB((yNote + clipColourOffset) * -8 / 3, rgb);
	}
	else if (colorScheme == RuntimeFeatureStateColorScheme::Octaves) {
    	int octaveOffset = (yNote/12)  % 12; 
    	int offsetWithinOctave = yNote % 12;
		int tableRGB[][3] = {
			{  16,  0, 32  },
			{   0, 32,  0  },
			{  32,  0,  0  },
			{   0, 32, 32  },
            {  32, 32,  0  },
            {  32,  0, 32  },
            {   0, 32, 48  },
            {  32, 48,  0  },
            {  48,  0, 32  },
            {   0, 64, 32  },
            {  48, 32,  0  },
            {  32,  0, 46  }
		};	

    	rgb[0] = tableRGB[octaveOffset][0];
    	rgb[1] = tableRGB[octaveOffset][1];
    	rgb[2] = tableRGB[octaveOffset][2];
	}
    else if (colorScheme == RuntimeFeatureStateColorScheme::Stripes) {
    	int octaveOffset = (yNote/12) % 12; // clipColourOfsset removed.
    	int offsetWithinOctave = yNote % 12;
		int tableRGB[][3] = {
			{  16,  0, 32  },
			{   0, 32, 16  },
			{  32, 16,  0  },
			{   0, 32, 32  },
            {  32, 32,  0  },
            {  32,  0, 32  },
            {   0, 32, 48  },
            {  32, 48,  0  },
            {  48,  0, 32  },
            {   0, 48, 32  },
            {  48, 32,  0  },
            {  32,  0, 48  }
		};	

        if (offsetWithinOctave % 2  == 1) {
        	rgb[0] = 64 - tableRGB[offsetWithinOctave][0];
        	rgb[1] = 64 - tableRGB[offsetWithinOctave][1];
        	rgb[2] = 64 - tableRGB[offsetWithinOctave][2];

        }
        else {
        	rgb[0] = tableRGB[offsetWithinOctave][0];
        	rgb[1] = tableRGB[offsetWithinOctave][1];
        	rgb[2] = tableRGB[offsetWithinOctave][2];
        }

	
	}
	else if (colorScheme == RuntimeFeatureStateColorScheme::Blue) {
		int octaveOffset = (yNote/12) % 12;// + clipColourOffset;
		int offsetWithinOctave = yNote % 12;
		int blackKeys[] = {0,1,0,1,0,0,1,0,1,0,1,0};
		// r and g run from 0 .. 22 + 8 extra in case of a black key.
		rgb[0] = octaveOffset * 2 + blackKeys[offsetWithinOctave] * 8;
		rgb[1] = octaveOffset * 2 + blackKeys[offsetWithinOctave] * 8;
		// b runs from 8 ... 8 + 33+22 = 0..63 and is divided by 2 for black keys.
		// the +8 is needed to not get a black row
		rgb[2] = 8 + (octaveOffset * 3 + offsetWithinOctave * 2 ) >> blackKeys[offsetWithinOctave];      
	}
	else {
		__builtin_unreachable();
	}


}

