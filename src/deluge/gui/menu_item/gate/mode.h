#pragma once
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"

namespace menu_item::gate {
#if HAVE_OLED
// Why'd I put two NULLs? (Rohan)
// Gets updated in gate::Selection lol (Kate)
static char const* mode_options[] = {"V-trig", "S-trig", NULL, NULL};
#else
static char const* mode_options[] = {"VTRI", "STRI", NULL, NULL};
#endif

#if HAVE_OLED
static char mode_title[] = "Gate outX mode";
#else
static char* mode_title = nullptr;
#endif

class Mode final : public Selection {
public:
	Mode() : Selection(mode_title) { basicOptions = mode_options; }
	void readCurrentValue() { soundEditor.currentValue = cvEngine.gateChannels[soundEditor.currentSourceIndex].mode; }
	void writeCurrentValue() { cvEngine.setGateType(soundEditor.currentSourceIndex, soundEditor.currentValue); }
};

} // namespace menu_item::gate
