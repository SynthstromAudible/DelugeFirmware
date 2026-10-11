#pragma once
#include "gui/menu_item/integer.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "processing/sound/sound.h"
#include <cstdio>
namespace deluge::gui::menu_item::snake_custom {
using namespace deluge::sequencer;
class NamedSubmenu : public Submenu {
public:
	NamedSubmenu(std::string_view label, std::initializer_list<MenuItem*> items)
	    : Submenu(l10n::String::EMPTY_STRING, items), label_(label) {}
	std::string_view getName() const override { return label_; }
	std::string_view getTitle() const override { return label_; }

private:
	std::string_view label_;
};
class Enabled : public Selection {
public:
	std::string_view getName() const override { return "Enabled"; }
	std::string_view getTitle() const override { return "Snake Custom"; }
	void readCurrentValue() override { setValue(soundEditor.currentSound->snakeCustomEnabled); }
	void writeCurrentValue() override {
		soundEditor.currentSound->snakeCustomEnabled = getValue();
		soundEditor.currentSound->updateSnakeCustom();
	}
	deluge::vector<std::string_view> getOptions(OptType = OptType::FULL) override { return {"Off", "On"}; }
};
class LaneNumber : public Integer {
public:
	enum Field { Length, Division, Step, Value, Phase, RandomizeSeed, Minimum, Maximum };
	LaneNumber(unsigned lane, Field field, std::string_view label, unsigned& selectedStep)
	    : lane_(lane), field_(field), label_(label), selectedStep_(selectedStep) {}
	std::string_view getName() const override { return label_; }
	std::string_view getTitle() const override { return label_; }
	int32_t getMinValue() const override { return field_ == Length || field_ == Division || field_ == Step ? 1 : 0; }
	int32_t getMaxValue() const override {
		switch (field_) {
		case Length:
		case Step:
			return 16;
		case Division:
			return 1536;
		case Phase:
			return 15;
		case RandomizeSeed:
			return 9999;
		default:
			return 100;
		}
	}
	void readCurrentValue() override {
		const auto& lane = soundEditor.currentSound->snakeCustomConfig.modulation[lane_];

		switch (field_) {
		case Length:
			setValue(lane.length);
			break;
		case Division:
			setValue(lane.division);
			break;
		case Phase:
			setValue(lane.phase);
			break;
		case Step:
			setValue(selectedStep_ + 1);
			break;
		case Value:
			setValue((static_cast<uint32_t>(lane.values[selectedStep_]) * 100 + 32767) / 65535);
			break;
		case RandomizeSeed:
			setValue(lane.seed % 10000);
			break;
		case Minimum:
			setValue((static_cast<uint32_t>(lane.randomMinimum) * 100 + 32767) / 65535);
			break;
		case Maximum:
			setValue((static_cast<uint32_t>(lane.randomMaximum) * 100 + 32767) / 65535);
			break;
		}
	}
	void writeCurrentValue() override {
		auto& lane = soundEditor.currentSound->snakeCustomConfig.modulation[lane_];

		const auto value = std::clamp(getValue(), getMinValue(), getMaxValue());
		switch (field_) {
		case Length:
			lane.length = value;
			break;
		case Division:
			lane.division = value;
			break;
		case Phase:
			lane.phase = value;
			break;
		case Step:
			selectedStep_ = value - 1;
			break;
		case Value:
			lane.values[selectedStep_] = static_cast<uint32_t>(value) * 65535 / 100;
			break;
		case RandomizeSeed:
			lane.randomize(lane.randomMinimum, lane.randomMaximum, value);
			break;
		case Minimum:
			lane.randomMinimum = static_cast<uint32_t>(value) * 65535 / 100;
			break;
		case Maximum:
			lane.randomMaximum = static_cast<uint32_t>(value) * 65535 / 100;
			break;
		}
	}

	void drawPixelsForOled() override {
		if (field_ != Step && field_ != Value) {
			Integer::drawPixelsForOled();
			return;
		}
		const auto& lane = soundEditor.currentSound->snakeCustomConfig.modulation[lane_];
		auto& canvas = deluge::hid::display::OLED::main;
		for (unsigned cell = 0; cell < 16; ++cell) {
			const int x = (cell % 4) * 32;
			const int y = 14 + OLED_MAIN_TOPMOST_PIXEL + (cell / 4) * 7;
			char text[4];
			snprintf(text, sizeof(text), "%u",
			         static_cast<unsigned>((static_cast<uint32_t>(lane.values[cell]) * 100 + 32767) / 65535));
			canvas.drawStringCentered(text, x, y, kTextSmallSpacingX, kTextSmallSizeY, 30);
			if (cell == selectedStep_)
				canvas.invertArea(x, 30, y, y + 6);
		}
	}

private:
	unsigned lane_;
	Field field_;
	std::string_view label_;
	unsigned& selectedStep_;
};
class LaneChoice : public Selection {
public:
	enum Kind { Direction, Model, Path, LaneEnabled };
	LaneChoice(unsigned lane, Kind kind) : lane_(lane), kind_(kind) {}
	std::string_view getName() const override {
		switch (kind_) {
		case Model:
			return "Model Route";
		case Path:
			return "Grid Path";
		case LaneEnabled:
			return "Lane Enabled";
		default:
			return "Direction";
		}
	}
	std::string_view getTitle() const override { return getName(); }
	void readCurrentValue() override {
		const auto& config = soundEditor.currentSound->snakeCustomConfig;
		const auto& lane = config.modulation[lane_];
		switch (kind_) {
		case Model:
			setValue(config.mappings[lane_].target == SnakeTarget::PlaitsModel ? config.mappings[lane_].oscillator + 1
			                                                                   : 0);
			break;
		case Path:
			setValue(static_cast<unsigned>(lane.path));
			break;
		case LaneEnabled:
			setValue(lane.enabled);
			break;
		case Direction:
			setValue(static_cast<unsigned>(lane.direction));
			break;
		}
	}
	void writeCurrentValue() override {
		auto& config = soundEditor.currentSound->snakeCustomConfig;
		auto& lane = config.modulation[lane_];
		switch (kind_) {
		case Model: {
			const auto value = std::clamp<int32_t>(getValue(), 0, 2);
			config.mappings[lane_].target = value ? SnakeTarget::PlaitsModel : SnakeTarget::None;
			config.mappings[lane_].oscillator = value ? value - 1 : 0;
			break;
		}
		case Path:
			lane.path = static_cast<SnakePath>(std::clamp<int32_t>(getValue(), 0, 4));
			break;
		case LaneEnabled:
			lane.enabled = getValue() != 0;
			break;
		case Direction:
			lane.direction = static_cast<SnakeDirection>(std::clamp<int32_t>(getValue(), 0, 3));
			break;
		}
		soundEditor.currentSound->updateSnakeCustom();
	}
	deluge::vector<std::string_view> getOptions(OptType = OptType::FULL) override {
		switch (kind_) {
		case Model:
			return {"Matrix only", "Osc 1 Model", "Osc 2 Model"};
		case Path:
			return {"Rows", "Columns", "Snake Rows", "Spiral", "Snake Columns"};
		case LaneEnabled:
			return {"Off", "On"};
		default:
			return {"Forward", "Reverse", "Pendulum", "Random"};
		}
	}

private:
	unsigned lane_;
	Kind kind_;
};
// Menu storage is static; editing and playback allocate no lane state.
struct LaneMenu {
	unsigned selectedStep = 0;
	LaneNumber length, division, phase, step, value, randomize, minimum, maximum;
	LaneChoice direction, model, path, enabled;
	NamedSubmenu menu;
	LaneMenu(unsigned lane, std::string_view name)
	    : length(lane, LaneNumber::Length, "Length", selectedStep),
	      division(lane, LaneNumber::Division, "Ticks per Step", selectedStep),
	      phase(lane, LaneNumber::Phase, "Phase Offset", selectedStep),
	      step(lane, LaneNumber::Step, "Edit Cell", selectedStep),
	      value(lane, LaneNumber::Value, "Cell Value %", selectedStep),
	      randomize(lane, LaneNumber::RandomizeSeed, "Randomize Seed", selectedStep),
	      minimum(lane, LaneNumber::Minimum, "Random Min %", selectedStep),
	      maximum(lane, LaneNumber::Maximum, "Random Max %", selectedStep), direction(lane, LaneChoice::Direction),
	      model(lane, LaneChoice::Model), path(lane, LaneChoice::Path), enabled(lane, LaneChoice::LaneEnabled),
	      menu(name, {&enabled, &path, &length, &division, &phase, &direction, &step, &value, &minimum, &maximum,
	                  &randomize, &model}) {}
};
} // namespace deluge::gui::menu_item::snake_custom
