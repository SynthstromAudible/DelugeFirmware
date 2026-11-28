#include "serialization/schema/plugin_node.h"

#include "serialization/tree.h"

namespace deluge::serialization::schema {

void write_plugin_node(Writer& out, uint32_t version, int32_t tick_magnitude, const Value& entries) {
	out.begin_record();
	out.key(kVersionKey);
	out.integer(version);
	out.key(kTickMagnitudeKey);
	out.integer(tick_magnitude);
	out.key(kEntriesKey);
	out.value(entries);
	out.end_record();
}

std::optional<PluginNode> read_plugin_node(const Value& node) {
	if (!node.isMap()) {
		return std::nullopt;
	}
	const std::optional<int32_t> version = int_of(node.find(kVersionKey));
	const std::optional<int32_t> magnitude = int_of(node.find(kTickMagnitudeKey));
	const Value* entries = node.find(kEntriesKey);
	if (!version || *version < 0 || !magnitude || entries == nullptr || !entries->isMap()) {
		return std::nullopt;
	}
	return PluginNode{.version = static_cast<uint32_t>(*version), .tick_magnitude = *magnitude, .entries = entries};
}

bool is_null(const Value& node) {
	return node.isScalar() && node.scalarKind() == ScalarKind::LITERAL && node.str() == "null";
}

} // namespace deluge::serialization::schema
