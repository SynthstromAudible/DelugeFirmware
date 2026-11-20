#include "deluge-project/sound_routing_codec.h"

#include "serialization/tree.h"

#include <cstdint>
#include <optional>

namespace deluge::project {

namespace {

using serialization::Value;

int32_t int_of(const Value& value) {
	return serialization::int_of(&value).value_or(0);
}

/// A channel or note as the file writes it: its byte, 255 for none. A file may hold any byte; all but 255 are kept.
std::optional<uint8_t> byte_of(const Value& value) {
	const auto byte = static_cast<uint8_t>(int_of(value));
	return byte == SoundRouting::kNone ? std::nullopt : std::optional{byte};
}

} // namespace

Value encode_routing(const SoundRouting& routing) {
	Value entries = Value::makeMap();
	if (routing.side_chain_send != 0) {
		entries.set("sideChainSend", Value::makeNumber(routing.side_chain_send));
	}
	Value midi = Value::makeMap();
	midi.set("channel", Value::makeNumber(routing.midi_channel.value_or(SoundRouting::kNone)));
	midi.set("noteForDrum", Value::makeNumber(routing.midi_note.value_or(SoundRouting::kNone)));
	entries.set("midiOutput", std::move(midi));
	return entries;
}

SoundRouting decode_routing(const Value& element, SoundRouting onto) {
	SoundRouting routing = onto;
	if (const Value* send = element.find("sideChainSend")) {
		routing.side_chain_send = int_of(*send);
	}
	if (const Value* midi = element.find("midiOutput"); midi != nullptr && midi->isMap()) {
		for (const auto& [key, value] : midi->entries()) {
			if (key == "channel") {
				routing.midi_channel = byte_of(value);
			}
			else if (key == "noteForDrum") {
				routing.midi_note = byte_of(value);
			}
		}
	}
	return routing;
}

} // namespace deluge::project
