#pragma once
#include "gui/menu_item/selection.h"
#include "gui/menu_item/sequence/mdd.h"
#include "model/clip/instrument_clip.h"
namespace deluge::gui::menu_item::sequence {
class Mode final : public Selection {
public:
	using Selection::Selection;
	std::string_view getName() const override { return "Sequencer"; }
	std::string_view getTitle() const override { return "Sequencer"; }
	bool isRelevant(ModControllableAudio*, int32_t) const override {
		return display->haveOLED() && getCurrentClip() && getCurrentClip()->type == ClipType::INSTRUMENT
		       && getCurrentOutputType() != OutputType::KIT;
	}
	void readCurrentValue() override { setValue(getCurrentInstrumentClip()->mddRequested ? 1 : 0); }
	void writeCurrentValue() override {
		getCurrentInstrumentClip()->requestMdd(getValue() != 0);
		mdd::armTimer();
	}
	ActionResult timerCallback() override { return mdd::serviceTimer(); }
	MenuItem* selectButtonPress() override {
		if (soundEditor.getCurrentMenuItem() == mdd::settingsMenu)
			return NO_NAVIGATION;
		if (soundEditor.navigationDepth
		    && soundEditor.menuItemNavigationRecord[soundEditor.navigationDepth - 1] == mdd::settingsMenu)
			return Selection::selectButtonPress();
		return getCurrentInstrumentClip()->mddRequested ? mdd::settingsMenu : Selection::selectButtonPress();
	}
	deluge::vector<std::string_view> getOptions(OptType) override { return {"Normal", "MDD Snake"}; }
};
} // namespace deluge::gui::menu_item::sequence
