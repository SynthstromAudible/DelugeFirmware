#define STRINGIFY(s) #s
#define ITOA(i) STRINGIFY(i)
#define WREN_BUTTON(name, index)                                                                                       \
	"\nstatic " #name " { "                                                                                            \
	"__buttons[" #index "]"                                                                                            \
	" }"

// clang-format off

const button_s buttonValues[] = {
	[ButtonIndex::none]         = { 127, 127 },
	[ButtonIndex::affectEntire] = { affectEntireButtonX, affectEntireButtonY },
	[ButtonIndex::song]         = { sessionViewButtonX, sessionViewButtonY },
	[ButtonIndex::clip]         = { clipViewButtonX, clipViewButtonY },
	[ButtonIndex::synth]        = { synthButtonX, synthButtonY },
	[ButtonIndex::kit]          = { kitButtonX, kitButtonY },
	[ButtonIndex::midi]         = { midiButtonX, midiButtonY },
	[ButtonIndex::cv]           = { cvButtonX, cvButtonY },
	[ButtonIndex::keyboard]     = { keyboardButtonX, keyboardButtonY },
	[ButtonIndex::scale]        = { scaleModeButtonX, scaleModeButtonY },
	[ButtonIndex::crossScreen]  = { crossScreenEditButtonX, crossScreenEditButtonY },
	[ButtonIndex::back]         = { backButtonX, backButtonY },
	[ButtonIndex::load]         = { loadButtonX, loadButtonY },
	[ButtonIndex::save]         = { saveButtonX, saveButtonY },
	[ButtonIndex::learn]        = { learnButtonX, learnButtonY },
	[ButtonIndex::tapTempo]     = { tapTempoButtonX, tapTempoButtonY },
	[ButtonIndex::syncScaling]  = { syncScalingButtonX, syncScalingButtonY },
	[ButtonIndex::triplets]     = { tripletsButtonX, tripletsButtonY },
	[ButtonIndex::play]         = { playButtonX, playButtonY },
	[ButtonIndex::record]       = { recordButtonX, recordButtonY },
	[ButtonIndex::shift]        = { shiftButtonX, shiftButtonY },
};

const char* buttonsSource =
	"foreign "
	"class Button {"
	"\n  construct new(index) {}"
	"\n  foreign index"
	"\n}"
	"\nclass Buttons {"
	"\n  static setup() {"
	"\n    __buttons = []"
	"\n    for (i in 0..20) {"
	"\n      __buttons.add(Button.new(i))"
	"\n    }"
	"\n  }"
	WREN_BUTTON(affectEntire, 1)
	WREN_BUTTON(song, 2)
	WREN_BUTTON(clip, 3)
	WREN_BUTTON(synth, 4)
	WREN_BUTTON(kit, 5)
	WREN_BUTTON(midi, 6)
	WREN_BUTTON(cv, 7)
	WREN_BUTTON(keyboard, 8)
	WREN_BUTTON(scale, 9)
	WREN_BUTTON(crossScreen, 10)
	WREN_BUTTON(back, 11)
	WREN_BUTTON(load, 12)
	WREN_BUTTON(save, 13)
	WREN_BUTTON(learn, 14)
	WREN_BUTTON(tapTempo, 15)
	WREN_BUTTON(syncScaling, 16)
	WREN_BUTTON(triplets, 17)
	WREN_BUTTON(play, 18)
	WREN_BUTTON(record, 19)
	WREN_BUTTON(shift, 20)
	"\n}"
	"\nButtons.setup()"
	"\n";

// clang-format on
