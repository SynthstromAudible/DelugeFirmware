#include "gui/menu_item/sequence/mdd.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/sequence/mode.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "model/clip/instrument_clip.h"
#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "util/functions.h"
#include <algorithm>
#include <cstdio>

extern deluge::gui::menu_item::sequence::Mode sequencerModeMenu;
namespace deluge::gui::menu_item::sequence::mdd {
static InstrumentClip* clip() {
	return getCurrentInstrumentClip();
}
static snake::Controller& state() {
	return clip()->mdd;
}
static snake::Lane& lane() {
	return state().core.lanes[state().lane];
}
static void changed() {
	uiNeedsRendering(&instrumentClipView);
}
void armTimer() {
	changed();
	PadLEDs::reassessGreyout();
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 20);
}
ActionResult serviceTimer() {
	if (instrumentClipView.mddControls())
		return instrumentClipView.timerCallback();
	// One final refresh after a running, quantized switch back to Normal.
	if (getCurrentClip() && getCurrentClip()->type == ClipType::INSTRUMENT)
		state().back();
	PadLEDs::clearTickSquares();
	display->cancelPopup();
	changed();
	PadLEDs::reassessGreyout();
	return ActionResult::DEALT_WITH;
}
static bool relevant() {
	return display->haveOLED() && getCurrentClip() && getCurrentClip()->type == ClipType::INSTRUMENT
	       && getCurrentOutputType() != OutputType::KIT && (clip()->mddRequested || clip()->mddEnabled);
}
static bool engineValues() {
	auto& s = state();
	if (s.lane < 3)
		return false;
	int target = s.routes[s.lane - 3][0].target;
	return target == modulation::params::LOCAL_PLAITS_A_ENGINE || target == modulation::params::LOCAL_PLAITS_B_ENGINE;
}
static int displayed(uint16_t v) {
	if (state().lane == 0 || state().lane == 2)
		return v;
	if (engineValues())
		return (uint32_t(v) * 2 + 32767) / 65535 + 1;
	return snake::percent(v);
}
static uint16_t stored(int v) {
	if (state().lane == 0 || state().lane == 2)
		return v;
	if (engineValues())
		return v == 1 ? 0 : v == 2 ? 32768 : 65535;
	return snake::fromPercent(v);
}
class Number final : public Integer {
public:
	enum Field { Steps, Shift, Step, Value, Minimum, Maximum, Slot, Depth, Tie, Overlap, Legato };
	Number(Field field, std::string_view label) : field_(field), label_(label) {}
	std::string_view getName() const override { return label_; }
	std::string_view getTitle() const override {
		if (field_ != Value)
			return label_;
		static char title[40];
		static constexpr const char* names[] = {"Notes",    "Gates",    "Velocity", "Custom 1",
		                                        "Custom 2", "Custom 3", "Custom 4"};
		snprintf(title, sizeof(title), "%s Step %d", engineValues() ? "Engine" : names[state().lane],
		         state().selectedStep + 1);
		return title;
	}
	const char* getUnit() override {
		if (field_ == Overlap)
			return "%";
		return (field_ == Value || field_ == Minimum || field_ == Maximum) && state().lane != 0 && state().lane != 2
		               && !engineValues()
		           ? "%"
		           : "";
	}
	bool isRelevant(ModControllableAudio*, int32_t) const override { return relevant(); }
	void beginSession(MenuItem* from) override {
		Integer::beginSession(from);
		armTimer();
	}
	ActionResult timerCallback() override { return serviceTimer(); }
	int32_t getMinValue() const override {
		if (field_ == Depth)
			return -100;
		if (field_ == Steps || field_ == Step || field_ == Slot)
			return 1;
		return engineValues() && (field_ == Value || field_ == Minimum || field_ == Maximum) ? 1 : 0;
	}
	int32_t getMaxValue() const override {
		switch (field_) {
		case Steps:
		case Step:
			return 16;
		case Shift:
			return 15;
		case Slot:
			return 4;
		case Depth:
		case Overlap:
			return 100;
		case Tie:
		case Legato:
			return 1;
		default:
			return state().lane == 0 || state().lane == 2 ? 127 : engineValues() ? 3 : 100;
		}
	}
	void readCurrentValue() override {
		auto& s = state();
		switch (field_) {
		case Steps:
			setValue(lane().length);
			break;
		case Shift:
			setValue(lane().shift);
			break;
		case Step:
			setValue(s.selectedStep + 1);
			break;
		case Value:
			setValue(displayed(lane().values[s.selectedStep]));
			break;
		case Minimum:
			setValue(displayed(s.low[s.lane]));
			break;
		case Maximum:
			setValue(displayed(s.high[s.lane]));
			break;
		case Slot:
			setValue(s.routeSlot + 1);
			break;
		case Legato:
			setValue(s.core.legato ? 1 : 0);
			break;
		case Tie:
			setValue((s.core.ties & (1u << s.selectedStep)) ? 1 : 0);
			break;
		case Overlap:
			setValue(snake::percent(s.core.overlap));
			break;
		case Depth:
			setValue(snake::depthPercent(s.routes[s.lane - 3][s.routeSlot].depth));
			break;
		}
	}
	void writeCurrentValue() override {
		auto& s = state();
		int v = getValue();
		switch (field_) {
		case Steps:
			lane().length = v;
			break;
		case Shift:
			lane().shift = v;
			break;
		case Step:
			s.selectedStep = v - 1;
			break;
		case Value:
			lane().values[s.selectedStep] = stored(v);
			break;
		case Minimum:
			s.low[s.lane] = stored(v);
			s.high[s.lane] = std::max(s.high[s.lane], s.low[s.lane]);
			break;
		case Maximum:
			s.high[s.lane] = stored(v);
			s.low[s.lane] = std::min(s.high[s.lane], s.low[s.lane]);
			break;
		case Slot:
			s.routeSlot = v - 1;
			break;
		case Legato:
			s.core.legato = v != 0;
			break;
		case Tie:
			if (v)
				s.core.ties |= 1u << s.selectedStep;
			else
				s.core.ties &= ~(1u << s.selectedStep);
			break;
		case Overlap:
			s.core.overlap = snake::fromPercent(v);
			break;
		case Depth: {
			auto& route = s.routes[s.lane - 3][s.routeSlot];
			auto previous = route;
			route.depth = v * 32767 / 100;
			char memory[MODEL_STACK_MAX_SIZE];
			if (!clip()->applyMddRoute(currentSong->setupModelStackWithCurrentClip(memory), s.lane - 3, previous,
			                           route)) {
				route = previous;
				readCurrentValue();
				display->displayPopup("Map unavailable");
			}
			break;
		}
		}
		changed();
	}

private:
	Field field_;
	std::string_view label_;
};
class Choice final : public Selection {
public:
	enum Field { Lane, Custom, Direction, Speed, Shape, Root, Scale, Target };
	Choice(Field field, std::string_view label) : field_(field), label_(label) {}
	std::string_view getName() const override { return label_; }
	std::string_view getTitle() const override { return label_; }
	bool isRelevant(ModControllableAudio*, int32_t) const override { return relevant(); }
	void beginSession(MenuItem* from) override {
		Selection::beginSession(from);
		armTimer();
	}
	ActionResult timerCallback() override { return serviceTimer(); }
	bool allowed(int target) {
		if (target < 0)
			return true;
		auto& s = state();
		if (s.lane < 3 || getCurrentOutputType() != OutputType::SYNTH)
			return false;
		for (int owner = 0; owner < 4; ++owner)
			for (int i = 0; i < 4; ++i)
				if ((owner != s.lane - 3 || i != s.routeSlot) && s.routes[owner][i].target == target)
					return false;
		auto* sound = static_cast<Sound*>(clip()->output->toModControllable());
		return sound->maySourcePatchToParam(static_cast<PatchSource>(int(PatchSource::SNAKE_CUSTOM_1) + s.lane - 3),
		                                    target, &clip()->paramManager)
		       != PatchCableAcceptance::DISALLOWED;
	}
	int targetAt(int index) {
		if (index == 0)
			return -1;
		for (int p = 0; p < modulation::params::GLOBAL_NONE; ++p)
			if (allowed(p) && --index == 0)
				return p;
		return -1;
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		if (optType == OptType::SHORT) {
			switch (field_) {
			case Lane:
				return {"Note", "Gate", "Vel", "C1", "C2", "C3", "C4"};
			case Custom:
				return {"1", "2", "3", "4"};
			case Direction:
				return {"Fwd", "Rev", "Pend", "Rnd"};
			case Shape:
				return {"Rows", "Cols", "SRow", "SCol", "Spir"};
			default:
				break;
			}
		}

		switch (field_) {
		case Lane:
			return {"Notes", "Gates", "Velocity", "Custom 1", "Custom 2", "Custom 3", "Custom 4"};
		case Custom:
			return {"Custom 1", "Custom 2", "Custom 3", "Custom 4"};
		case Direction:
			return {"Forward", "Reverse", "Pendulum", "Random"};
		case Speed:
			return {"1/4",  "1/8",   "1/16", "1/32", "1/32T", "1/64",  "1/8T", "1/16T",
			        "1/4T", "1/64T", "1/4D", "1/8D", "1/16D", "1/32D", "1/64D"};
		case Shape:
			return {"Rows", "Columns", "Snake Rows", "Snake Cols", "Spiral"};
		case Root:
			return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		case Scale: {
			deluge::vector<std::string_view> options;
			for (int i = 0; i <= NUM_PRESET_SCALES; ++i)
				options.push_back(getScaleName(static_cast<::Scale>(i)));
			return options;
		}
		case Target: {
			deluge::vector<std::string_view> options{"None"};
			for (int p = 0; p < modulation::params::GLOBAL_NONE; ++p)
				if (allowed(p))
					options.push_back(modulation::params::getParamDisplayName(modulation::params::Kind::PATCHED, p));
			return options;
		}
		}
		return {};
	}
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		Selection::renderInHorizontalMenu(slot);
		auto* current = soundEditor.getCurrentMenuItem();
		if ((field_ == Target || field_ == Scale) && current->isSubmenu()
		    && static_cast<HorizontalMenu*>(current)->getCurrentItem() == this) {
			auto options = getOptions(OptType::FULL);
			if (getValue() >= 0 && getValue() < int(options.size()))
				deluge::hid::display::OLED::setupSideScroller(0, options[getValue()], slot.start_x,
				                                              slot.start_x + slot.width,
				                                              slot.start_y + kHorizontalMenuSlotYOffset,
				                                              slot.start_y + kHorizontalMenuSlotYOffset + kTextSpacingY,
				                                              kTextSpacingX, kTextSpacingY, false, true);
		}
	}
	void readCurrentValue() override {
		auto& s = state();
		switch (field_) {
		case Lane:
			setValue(s.lane);
			break;
		case Custom:
			setValue(s.custom);
			break;
		case Direction:
			setValue(int(lane().direction));
			break;
		case Speed:
			s.syncClock(currentSong->getQuarterNoteLength());
			setValue(s.speedIndex(lane()));
			break;
		case Shape:
			setValue(int(lane().shape));
			break;
		case Root:
			setValue(mod(currentSong->key.rootNote, 12));
			break;
		case Scale:
			setValue(std::min<int>(currentSong->getCurrentScale(), USER_SCALE));
			break;
		case Target: {
			int target = s.routes[s.lane - 3][s.routeSlot].target, index = 0;
			for (int p = 0; p <= target; ++p)
				if (allowed(p))
					++index;
			setValue(index);
			break;
		}
		}
	}
	void writeCurrentValue() override {
		auto& s = state();
		int v = getValue();
		switch (field_) {
		case Lane:
			s.choose(v);
			if (v >= 3)
				s.custom = v - 3;
			break;
		case Custom:
			s.custom = v;
			s.choose(v + 3);
			break;
		case Direction:
			lane().direction = snake::Direction(v);
			break;
		case Speed:
			s.syncClock(currentSong->getQuarterNoteLength());
			s.setSpeed(lane(), v);
			break;
		case Shape:
			lane().shape = snake::Shape(v);
			break;
		case Root:
			// Use the Song's native key transposition: retain the selected scale,
			// move scale-mode clips together, and preserve the root's octave.
			currentSong->transposeAllScaleModeClips(v - mod(currentSong->key.rootNote, 12));
			clip()->inScaleMode = true;
			break;
		case Scale:
			if (currentSong->setScale(static_cast<::Scale>(v)) != static_cast<::Scale>(v)) {
				readCurrentValue();
				display->displayPopup("Scale cannot fit notes");
			}
			else {
				clip()->inScaleMode = true;
			}
			break;
		case Target: {
			auto& route = s.routes[s.lane - 3][s.routeSlot];
			auto previous = route;
			route.target = targetAt(v);
			char memory[MODEL_STACK_MAX_SIZE];
			if (!clip()->applyMddRoute(currentSong->setupModelStackWithCurrentClip(memory), s.lane - 3, previous,
			                           route)) {
				route = previous;
				readCurrentValue();
				display->displayPopup("Map unavailable");
			}
			if (route.target >= 0
			    && static_cast<Sound*>(clip()->output->toModControllable())
			               ->maySourcePatchToParam(
			                   static_cast<PatchSource>(int(PatchSource::SNAKE_CUSTOM_1) + s.lane - 3), route.target,
			                   &clip()->paramManager)
			           != PatchCableAcceptance::ALLOWED)
				display->displayPopup("Target inactive");
			break;
		}
		}
		changed();
	}

private:
	Field field_;
	std::string_view label_;
};
class Menu final : public HorizontalMenu {
public:
	Menu(std::string_view label, std::initializer_list<MenuItem*> items, bool customOnly = false)
	    : HorizontalMenu(l10n::String::EMPTY_STRING, items, HorizontalMenu::FIXED), label_(label),
	      customOnly_(customOnly) {}
	std::string_view getName() const override { return label_; }
	std::string_view getTitle() const override { return label_; }
	bool isRelevant(ModControllableAudio*, int32_t) const override {
		return relevant() && (!customOnly_ || (state().lane >= 3 && getCurrentOutputType() == OutputType::SYNTH));
	}
	void beginSession(MenuItem* from) override {
		lastSelectedItemPosition = -1;
		HorizontalMenu::beginSession(from);
		armTimer();
	}
	ActionResult timerCallback() override { return serviceTimer(); }

	RenderingStyle renderingStyle() const override { return display->haveOLED() ? HORIZONTAL : VERTICAL; }

	void selectEncoderAction(int32_t offset) override {
		getCurrentItem()->readCurrentValue();
		HorizontalMenu::selectEncoderAction(offset);
	}

private:
	std::string_view label_;
	bool customOnly_;
};
static void perform(snake::Control action) {
	auto& s = state();
	switch (action) {
	case snake::Control::Random:
		lane().randomize(s.low[s.lane], s.high[s.lane]);
		break;
	case snake::Control::Reset:
		s.reset(s.lane);
		break;
	case snake::Control::RandomAll:
		for (int i = 0; i < 7; ++i)
			s.core.lanes[i].randomize(s.low[i], s.high[i]);
		break;
	case snake::Control::ResetAll:
		for (int i = 0; i < 7; ++i)
			s.reset(i);
		break;
	default:
		return;
	}
	changed();
	display->displayPopup(action == snake::Control::Reset || action == snake::Control::ResetAll ? "Reset"
	                                                                                            : "Randomized");
}
class Action final : public MenuItem {
public:
	Action(snake::Control action, std::string_view label) : action_(action), label_(label) {}
	std::string_view getName() const override { return label_; }
	std::string_view getTitle() const override { return label_; }
	bool isRelevant(ModControllableAudio*, int32_t) const override { return relevant(); }
	bool shouldEnterSubmenu() override { return false; }
	int32_t getOccupiedSlots() const override { return 2; }
	void getColumnLabel(StringBuf& label) override {
		label.append(action_ == snake::Control::RandomAll || action_ == snake::Control::ResetAll ? "All" : "Lane");
	}
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		deluge::hid::display::OLED::main.drawStringCentered(label_.data(), slot.start_x, slot.start_y + 4, 6, 7,
		                                                    slot.width);
	}
	MenuItem* selectButtonPress() override {
		perform(action_);
		return NO_NAVIGATION;
	}

private:
	snake::Control action_;
	std::string_view label_;
};
class WriteConfirmation final : public Selection {
public:
	std::string_view getName() const override { return "Write"; }
	std::string_view getTitle() const override { return "Replace stock notes?"; }
	void renderOLED() override {
		Selection::renderOLED();
		deluge::hid::display::OLED::setupSideScroller(
		    1, getTitle(), 0, OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_TOPMOST_PIXEL + 1,
		    OLED_MAIN_TOPMOST_PIXEL + 1 + kTextTitleSizeY, kTextTitleSpacingX, kTextTitleSizeY, false, true);
	}
	bool isRelevant(ModControllableAudio*, int32_t) const override { return relevant(); }
	void readCurrentValue() override { setValue(0); }
	void writeCurrentValue() override {}
	deluge::vector<std::string_view> getOptions(OptType) override { return {"No", "Yes"}; }
	MenuItem* selectButtonPress() override {
		if (!getValue())
			return nullptr;
		char memory[MODEL_STACK_MAX_SIZE];
		auto error = clip()->writeMddToNormal(currentSong->setupModelStackWithCurrentClip(memory));
		if (error != Error::NONE) {
			display->displayError(error);
			return NO_NAVIGATION;
		}
		PadLEDs::clearTickSquares();
		changed();
		soundEditor.exitCompletely();
		display->displayPopup("Written to Normal");
		return NO_NAVIGATION;
	}
};
static WriteConfirmation writeConfirmation;
static Choice laneMenu{Choice::Lane, "Lane"}, customMenu{Choice::Custom, "Custom"},
    directionMenu{Choice::Direction, "Direction"}, speedMenu{Choice::Speed, "Speed"},
    shapeMenu{Choice::Shape, "Snake Shape"}, rootMenu{Choice::Root, "Root"}, scaleMenu{Choice::Scale, "Scale"},
    targetMenu{Choice::Target, "Target"};
static Number stepsMenu{Number::Steps, "Steps"}, shiftMenu{Number::Shift, "Shift"}, stepMenu{Number::Step, "Step"},
    stepValueMenu{Number::Value, "Step Value"}, minimumMenu{Number::Minimum, "Minimum"},
    maximumMenu{Number::Maximum, "Maximum"}, slotMenu{Number::Slot, "Slot"}, depthMenu{Number::Depth, "Depth"},
    tieMenu{Number::Tie, "Tie"}, overlapMenu{Number::Overlap, "Overlap"}, legatoMenu{Number::Legato, "Legato"};
static Action randomMenu{snake::Control::Random, "Random"}, resetMenu{snake::Control::Reset, "Reset"},
    randomAllMenu{snake::Control::RandomAll, "Random All"}, resetAllMenu{snake::Control::ResetAll, "Reset All"};
static Menu mapMenu{"Map", {&slotMenu, &targetMenu, &depthMenu, &customMenu}, true};
static Menu rangeMenu{"Range", {&minimumMenu, &maximumMenu, &stepsMenu, &shiftMenu}};
static Menu valuesMenu{"MDD Values", {&stepMenu, &stepValueMenu, &stepsMenu, &shiftMenu}};
static Menu gateValuesMenu{"MDD Gates",
                           {&stepMenu, &stepValueMenu, &stepsMenu, &shiftMenu, &tieMenu, &overlapMenu, &legatoMenu}};
static Menu customValuesMenu{"MDD Custom", {&customMenu, &stepMenu, &stepValueMenu, &stepsMenu}};
static Menu keyMenu{"Root / Scale", {&rootMenu, &scaleMenu}};
static Menu randomPage{"Random", {&randomMenu, &randomAllMenu, &minimumMenu, &maximumMenu}};
static Menu resetPage{"Reset", {&resetMenu, &resetAllMenu, &stepsMenu, &shiftMenu}};
static Menu rootSettings{"MDD Snake",
                         {&directionMenu, &speedMenu, &stepsMenu, &shiftMenu, &shapeMenu, &sequencerModeMenu, &laneMenu,
                          &customMenu, &rangeMenu}};
class WrittenCustom final : public patched_param::Integer {
	int index_;

public:
	explicit WrittenCustom(int i)
	    : patched_param::Integer(l10n::String::STRING_FOR_TYPE, modulation::params::GLOBAL_MDD_CUSTOM_1 + i),
	      index_(i) {}
	std::string_view getName() const override {
		static constexpr const char* names[] = {"C1", "C2", "C3", "C4"};
		return names[index_];
	}
	std::string_view getTitle() const override {
		static constexpr const char* names[] = {"Custom 1 Auto", "Custom 2 Auto", "Custom 3 Auto", "Custom 4 Auto"};
		return names[index_];
	}
	bool isRelevant(ModControllableAudio*, int32_t) const override {
		return display->haveOLED() && getCurrentClip() && getCurrentClip()->type == ClipType::INSTRUMENT
		       && getCurrentOutputType() == OutputType::SYNTH && !clip()->mddRequested
		       && (clip()->mddBakedCustom & (1u << index_));
	}
	int32_t getMinValue() const override { return 0; }
	int32_t getMaxValue() const override { return 100; }
	const char* getUnit() override { return "%"; }
	void readCurrentValue() override {
		setValue(
		    (uint64_t(std::max<int32_t>(0, soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP())))
		         * 100
		     + INT32_MAX / 2)
		    / INT32_MAX);
	}
	int32_t getFinalValue() override { return int64_t(getValue()) * INT32_MAX / 100; }
};
static WrittenCustom written1{0}, written2{1}, written3{2}, written4{3};
class WrittenMenu final : public HorizontalMenu {
public:
	WrittenMenu()
	    : HorizontalMenu(l10n::String::EMPTY_STRING, {&written1, &written2, &written3, &written4},
	                     HorizontalMenu::FIXED) {}
	std::string_view getName() const override { return "MDD Automation"; }
	std::string_view getTitle() const override { return getName(); }
	bool isRelevant(ModControllableAudio*, int32_t) const override {
		return display->haveOLED() && getCurrentClip() && getCurrentClip()->type == ClipType::INSTRUMENT
		       && getCurrentOutputType() == OutputType::SYNTH && !clip()->mddRequested && clip()->mddBakedCustom;
	}
	RenderingStyle renderingStyle() const override { return display->haveOLED() ? HORIZONTAL : VERTICAL; }
};
static WrittenMenu writtenMenu;
MenuItem* automationMenu = &writtenMenu;
MenuItem* settingsMenu = &rootSettings;
MenuItem* valueMenu = &valuesMenu;
bool owns(MenuItem* item) {
	MenuItem* items[] = {&sequencerModeMenu, &rootSettings,  &laneMenu,      &customMenu,       &stepMenu,
	                     &stepValueMenu,     &directionMenu, &speedMenu,     &stepsMenu,        &shiftMenu,
	                     &shapeMenu,         &rootMenu,      &scaleMenu,     &mapMenu,          &rangeMenu,
	                     &minimumMenu,       &maximumMenu,   &slotMenu,      &targetMenu,       &depthMenu,
	                     &randomMenu,        &resetMenu,     &randomAllMenu, &resetAllMenu,     &valuesMenu,
	                     &gateValuesMenu,    &tieMenu,       &overlapMenu,   &legatoMenu,       &customValuesMenu,
	                     &keyMenu,           &randomPage,    &resetPage,     &writeConfirmation};
	return std::find(std::begin(items), std::end(items), item) != std::end(items);
}
void open(MenuItem* item) {
	if (item == valueMenu && state().lane == 1)
		item = &gateValuesMenu;
	// Pad shortcuts share the existing SoundEditor; SELECT release is consumed by opened().
	bool alreadyOpen = getCurrentUI() == &soundEditor;
	if (soundEditor.setup(getCurrentClip(), item)) {
		if (alreadyOpen)
			soundEditor.focusRegained();
		else
			openUI(&soundEditor);
		Buttons::selectButtonPressUsedUp = true;
		armTimer();
		if (item == valueMenu)
			valuesMenu.focusChild(&stepValueMenu);
		if (item == &gateValuesMenu)
			gateValuesMenu.focusChild(&stepValueMenu);
	}
}
void selectFirstPageColumn(int column) {
	open(settingsMenu);
	if (column >= 0 && column < 4)
		rootSettings.focusChild(rootSettings.getItems()[column]);
	renderUIsForOled();
}
void refreshValue() {
	auto* current = soundEditor.getCurrentMenuItem();
	if (!owns(current))
		return;
	if (current->isSubmenu())
		static_cast<HorizontalMenu*>(current)->getCurrentItem()->readCurrentValue();
	else
		current->readCurrentValue();
	renderUIsForOled();
}
void editStep(int offset) {
	auto& s = state();
	if (s.gestures.held < 0 || s.gestures.lengthChord)
		return;
	s.selectedStep = s.gestures.held;
	s.gestures.consumed = true;
	// Read the actual held cell before every edit; no cached value from another step.
	stepValueMenu.readCurrentValue();
	stepValueMenu.selectEncoderAction(offset);
	char popup[64];
	snprintf(popup, sizeof(popup), "%.*s: %ld%s", int(stepValueMenu.getTitle().size()), stepValueMenu.getTitle().data(),
	         long(stepValueMenu.getValue()), stepValueMenu.getUnit());
	display->displayPopup(popup);
	changed();
}
void control(snake::Control c) {
	switch (c) {
	case snake::Control::Notes:
	case snake::Control::Gates:
	case snake::Control::Velocity:
		state().choose(int(c));
		open(&valuesMenu);
		valuesMenu.focusChild(&stepValueMenu);
		break;
	case snake::Control::Custom:
		state().choose(state().custom + 3);
		open(&customValuesMenu);
		customValuesMenu.focusChild(&customMenu);
		break;
	case snake::Control::Root:
		open(&keyMenu);
		keyMenu.focusChild(&rootMenu);
		break;
	case snake::Control::Scale:
		open(&keyMenu);
		keyMenu.focusChild(&scaleMenu);
		break;
	case snake::Control::Map:
		if (state().lane >= 3 && getCurrentOutputType() == OutputType::SYNTH)
			open(&mapMenu);
		else
			display->displayPopup("Select Custom first");
		break;
	case snake::Control::Settings:
		selectFirstPageColumn(0);
		break;
	case snake::Control::Write:
		open(&writeConfirmation);
		break;
	case snake::Control::Range:
		open(&rangeMenu);
		break;
	case snake::Control::Random:
	case snake::Control::RandomAll:
		open(&randomPage);
		randomPage.focusChild(c == snake::Control::RandomAll ? &randomAllMenu : &randomMenu);
		break;
	case snake::Control::Reset:
	case snake::Control::ResetAll:
		open(&resetPage);
		resetPage.focusChild(c == snake::Control::ResetAll ? &resetAllMenu : &resetMenu);
		break;
	default:
		open(settingsMenu);
		break;
	}
	renderUIsForOled();
}
} // namespace deluge::gui::menu_item::sequence::mdd
