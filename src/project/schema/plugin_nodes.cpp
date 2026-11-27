#include "project/schema/plugin_nodes.h"

#include "plugin/types/plugin_id.h"
#include "project/fx_chain.h"
#include "project/instrument_slot.h"
#include "serialization/schema/plugin_node.h"

namespace deluge::project::schema {
namespace {

using serialization::Value;

[[nodiscard]] const CleanContext& clean(const sch::WalkContext& context) {
	return *static_cast<const CleanContext*>(context.user());
}

void write_chain(const FxChain& chain, sch::Writer& out, const sch::WalkContext& context) {
	const CleanContext& held = clean(context);
	if (held.plugins->chains.encode == nullptr || !chain.effects.has_value()) {
		out.null();
		return;
	}
	Value entries = Value::makeMap();
	held.plugins->chains.encode(chain, entries, held.tick_magnitude);
	sch::write_plugin_node(out, kChainStateVersion, held.tick_magnitude, entries);
}

sch::PluginRead read_chain(FxChain& chain, ChainHome home, const Value& node, sch::WalkContext& context) {
	if (sch::is_null(node)) {
		chain = FxChain{};
		return sch::PluginRead::DECODED;
	}
	const std::optional<sch::PluginNode> parsed = sch::read_plugin_node(node);
	if (!parsed) {
		return sch::PluginRead::BAD;
	}
	const CleanContext& held = clean(context);
	if (held.plugins->chains.decode == nullptr) {
		return sch::PluginRead::KEEP;
	}
	if (parsed->version != kChainStateVersion) {
		context.note(sch::LoadNote::Kind::PLUGIN_VERSION_DIFFERS);
	}
	chain = held.plugins->chains.decode(*parsed->entries, home, parsed->tick_magnitude);
	return sch::PluginRead::DECODED;
}

} // namespace

void write_instrument_state(const void* member, sch::Writer& out, const sch::WalkContext& context) {
	const auto& slot = *static_cast<const InstrumentSlot*>(member);
	const CleanContext& held = clean(context);
	if (held.plugins->instruments.encode == nullptr || !slot.state.has_value()) {
		out.null();
		return;
	}
	Value entries = Value::makeMap();
	held.plugins->instruments.encode(slot.state, entries, slot.tick_magnitude);
	sch::write_plugin_node(out, kInstrumentStateVersion, slot.tick_magnitude, entries);
}

sch::PluginRead read_instrument_state(void* member, const Value& node, sch::WalkContext& context) {
	auto& slot = *static_cast<InstrumentSlot*>(member);
	if (sch::is_null(node)) {
		slot.state = {};
		return sch::PluginRead::DECODED;
	}
	const std::optional<sch::PluginNode> parsed = sch::read_plugin_node(node);
	if (!parsed) {
		return sch::PluginRead::BAD;
	}
	const CleanContext& held = clean(context);
	if (slot.plugin != plugin::kDelugeInstrument.name || held.plugins->instruments.decode == nullptr) {
		return sch::PluginRead::KEEP;
	}
	if (parsed->version != kInstrumentStateVersion) {
		context.note(sch::LoadNote::Kind::PLUGIN_VERSION_DIFFERS);
	}
	slot.tick_magnitude = parsed->tick_magnitude;
	slot.state = held.plugins->instruments.decode(*parsed->entries, slot.tick_magnitude);
	return sch::PluginRead::DECODED;
}

void write_track_chain(const void* member, sch::Writer& out, const sch::WalkContext& context) {
	write_chain(*static_cast<const FxChain*>(member), out, context);
}
sch::PluginRead read_track_chain(void* member, const Value& node, sch::WalkContext& context) {
	return read_chain(*static_cast<FxChain*>(member), ChainHome::TRACK, node, context);
}
void write_master_chain(const void* member, sch::Writer& out, const sch::WalkContext& context) {
	write_chain(*static_cast<const FxChain*>(member), out, context);
}
sch::PluginRead read_master_chain(void* member, const Value& node, sch::WalkContext& context) {
	return read_chain(*static_cast<FxChain*>(member), ChainHome::MASTER, node, context);
}

} // namespace deluge::project::schema
