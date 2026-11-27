#pragma once

/// @file
/// @brief Records covering every shape a schema has, for the generic walk's specs.

#include "serialization/schema/containers.h"
#include "serialization/schema/extras.h"
#include "serialization/schema/fields.h"
#include "serialization/schema/plugin_node.h"
#include "serialization/tree.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace deluge::serialization::schema::test {

enum class Colour : uint8_t { RED, GREEN, BLUE };
inline constexpr std::array<std::string_view, 3> kColourWords{"red", "green", "blue"};

/// An open record: unknown keys are kept.
struct Knob {
	uint8_t number{0};
	std::string label;
	Extras extras;
	bool operator==(const Knob&) const = default;
};

/// A closed record: unknown keys are dropped.
struct Point {
	int16_t x{0};
	int16_t y{0};
	bool operator==(const Point&) const = default;
};

struct Circle {
	int32_t radius{0};
	bool operator==(const Circle&) const = default;
};
struct Square {
	int32_t side{0};
	bool operator==(const Square&) const = default;
};
using Figure = std::variant<Circle, Square>;

struct Panel {
	int8_t level{0};
	Colour colour{Colour::RED};
	bool lit{false};
	std::string name;
	float gain{0};
	uint64_t length{0};
	Point origin;
	std::optional<int32_t> offset;
	std::optional<Knob> spare;
	std::vector<Knob> knobs;
	std::array<int8_t, 2> pair{};
	std::map<uint8_t, std::string> labels;
	Figure figure;
	std::vector<Figure> figures;
	int32_t plugin_state{0};
	std::optional<int32_t> spare_plugin;
	Extras extras;
	bool operator==(const Panel&) const = default;
};

/// The test plugin: its state is one number, `{"v": n}` in the node's entries; a node without `v` is one it cannot
/// read.
inline void write_test_plugin(const void* member, Writer& out, const WalkContext& /*context*/) {
	Value entries = Value::makeMap();
	entries.set("v", Value::makeNumber(*static_cast<const int32_t*>(member)));
	write_plugin_node(out, 1, 0, entries);
}
inline PluginRead read_test_plugin(void* member, const Value& node, WalkContext& /*context*/) {
	auto& state = *static_cast<int32_t*>(member);
	if (is_null(node)) {
		state = 0;
		return PluginRead::DECODED;
	}
	const std::optional<PluginNode> parsed = read_plugin_node(node);
	if (!parsed) {
		return PluginRead::BAD;
	}
	const std::optional<int32_t> v = int_of(parsed->entries->find("v"));
	if (!v) {
		return PluginRead::KEEP;
	}
	state = *v;
	return PluginRead::DECODED;
}
/// The test plugin holds a state of its own when its number is not 0.
inline bool test_plugin_has_state(const void* member) {
	return *static_cast<const int32_t*>(member) != 0;
}
inline constexpr PluginOps kTestPlugin{.name = "test.plugin",
                                       .version = 1,
                                       .has_state = &test_plugin_has_state,
                                       .write = &write_test_plugin,
                                       .read = &read_test_plugin};

inline constexpr std::array kKnobFields{scalar<&Knob::number>("number"), scalar<&Knob::label>("label")};
inline constexpr RecordDesc kKnobDesc = open_record<&Knob::extras>("knob", kKnobFields);

inline constexpr std::array kPointFields{scalar<&Point::x>("x"), scalar<&Point::y>("y")};
inline constexpr RecordDesc kPointDesc = closed_record("point", kPointFields);

inline constexpr std::array kCircleFields{scalar<&Circle::radius>("radius")};
inline constexpr RecordDesc kCircleDesc = closed_record("circle", kCircleFields);
inline constexpr std::array kSquareFields{scalar<&Square::side>("side")};
inline constexpr RecordDesc kSquareDesc = closed_record("square", kSquareFields);
inline constexpr std::array kFigureAlternatives{Alternative{"circle", &kCircleDesc},
                                                Alternative{"square", &kSquareDesc}};
inline constexpr VariantDesc kFigureDesc = variant_of<Figure>(kFigureAlternatives);

using Label = std::pair<uint8_t, std::string>;
inline constexpr std::array kLabelFields{scalar<&Label::first>("cc"), scalar<&Label::second>("label")};
inline constexpr RecordDesc kLabelDesc = closed_record("label", kLabelFields);

inline constexpr FieldDesc kOffsetValue = value_scalar<int32_t>();
inline constexpr FieldDesc kKnobValue = value_record(kKnobDesc);
inline constexpr FieldDesc kPairValue = value_scalar<int8_t>({.min = -5, .max = 5});
inline constexpr FieldDesc kLabelValue = value_record(kLabelDesc);
inline constexpr FieldDesc kFigureValue = value_variant(kFigureDesc);
inline constexpr FieldDesc kPluginValue = value_plugin(kTestPlugin);

inline constexpr std::array kPanelFields{
    scalar<&Panel::level>("level", {.min = -10, .max = 10}),
    scalar<&Panel::colour>("colour", {.words = kColourWords}),
    scalar<&Panel::lit>("lit"),
    scalar<&Panel::name>("name"),
    scalar<&Panel::gain>("gain"),
    scalar<&Panel::length>("length"),
    record<&Panel::origin>("origin", kPointDesc),
    optional<&Panel::offset>("offset", kOffsetValue),
    optional<&Panel::spare>("spare", kKnobValue),
    list<&Panel::knobs>("knobs", kKnobValue),
    list<&Panel::pair>("pair", kPairValue),
    list<&Panel::labels>("labels", kLabelValue, "cc"),
    variant<&Panel::figure>("figure", kFigureDesc),
    list<&Panel::figures>("figures", kFigureValue),
    plugin<&Panel::plugin_state>("plugin", kTestPlugin),
    optional<&Panel::spare_plugin>("sparePlugin", kPluginValue),
};
static_assert(keys_unique(kPanelFields));
inline constexpr RecordDesc kPanelDesc = open_record<&Panel::extras>("panel", kPanelFields);
inline constexpr Schema<Panel> kPanelSchema{&kPanelDesc};

/// A panel with every field away from its default.
inline Panel full_panel() {
	Panel panel;
	panel.level = -7;
	panel.colour = Colour::BLUE;
	panel.lit = true;
	panel.name = "Lead \"A\"";
	panel.gain = 0.3F;
	panel.length = 1ULL << 40;
	panel.origin = {.x = 3, .y = -4};
	panel.offset = 12;
	panel.spare = Knob{.number = 9, .label = "spare"};
	panel.knobs = {Knob{.number = 1, .label = "one"}, Knob{.number = 2, .label = "two"}};
	panel.pair = {-5, 5};
	panel.labels = {{1, "mod"}, {74, "cutoff"}};
	panel.figure = Square{.side = 6};
	panel.figures = {Circle{.radius = 2}, Square{.side = 3}};
	panel.plugin_state = 42;
	panel.spare_plugin = 5;
	return panel;
}

} // namespace deluge::serialization::schema::test
