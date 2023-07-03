#define STRINGIFY(s) #s
#define ITOA(i) STRINGIFY(i)
#define WREN_BUTTON(shortname, name) "static " #shortname " { [" ITOA(name##ButtonX) "," ITOA(name##ButtonY) "] }\n"

const char* WrenAPI::buttonsSource = "(class Buttons {\n"
	WREN_BUTTON(affectEntire, affectEntire)
	WREN_BUTTON(song, sessionView)
	WREN_BUTTON(clip, clipView)
	WREN_BUTTON(synth, synth)
	WREN_BUTTON(kit, kit)
	WREN_BUTTON(midi, midi)
	WREN_BUTTON(cv, cv)
	WREN_BUTTON(keyboard, keyboard)
	WREN_BUTTON(scale, scaleMode)
	WREN_BUTTON(crossScreen, crossScreenEdit)
	WREN_BUTTON(back, back)
	WREN_BUTTON(load, load)
	WREN_BUTTON(save, save)
	WREN_BUTTON(learn, learn)
	WREN_BUTTON(tapTempo, tapTempo)
	WREN_BUTTON(syncScaling, syncScaling)
	WREN_BUTTON(triplets, triplets)
	WREN_BUTTON(play, play)
	WREN_BUTTON(record, record)
	WREN_BUTTON(shift, shift)
"}\n";
