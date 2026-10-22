#include "scenes/bind/write_sink.h"

namespace deluge::scenes::bind {

bool RecordingSink::write(const WriteRequest& request) {
	requests.push_back(request);
	return true;
}

bool RecordingSink::write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length) {
	param_writes.push_back({address, value, pos, length});
	return true;
}

bool RecordingSink::delete_automation(const project::ParamAddress& address) {
	deletes.push_back(address);
	return true;
}

bool RecordingSink::write_text(const Target& target, const Addresses& where, std::string_view text) {
	gestures.push_back(
	    {.kind = GestureRequest::Kind::TEXT, .target = target, .where = where, .text = std::string{text}});
	return true;
}

bool RecordingSink::write_ranges(project::SlotAddress slot, uint8_t source,
                                 std::span<const instrument::RangeState> ranges, RangesCause cause) {
	gestures.push_back({.kind = GestureRequest::Kind::RANGES,
	                    .slot = slot,
	                    .source = source,
	                    .ranges = {ranges.begin(), ranges.end()},
	                    .cause = cause});
	return true;
}

bool RecordingSink::recolour_clip(project::ClipAddress clip, int32_t offset) {
	gestures.push_back({.kind = GestureRequest::Kind::RECOLOUR, .clip = clip, .value = offset});
	return true;
}

bool RecordingSink::clear_clip(project::ClipAddress clip, bool clear_automation, bool clear_sequence_and_mpe) {
	gestures.push_back({.kind = GestureRequest::Kind::CLEAR,
	                    .clip = clip,
	                    .clear_automation = clear_automation,
	                    .clear_sequence_and_mpe = clear_sequence_and_mpe});
	return true;
}

bool RecordingSink::move_audio_clip_marker(project::ClipAddress clip, std::string_view path, int64_t value) {
	gestures.push_back({.kind = GestureRequest::Kind::MARKER, .text = std::string{path}, .clip = clip, .value = value});
	return true;
}

void RecordingSink::set_default_audio_input_channel(int32_t channel) {
	gestures.push_back({.kind = GestureRequest::Kind::DEFAULT_INPUT_CHANNEL, .value = channel});
}

bool RecordingSink::attach_recording(project::SlotAddress slot, uint8_t source, uint16_t range, std::string_view path) {
	gestures.push_back({.kind = GestureRequest::Kind::ATTACH,
	                    .text = std::string{path},
	                    .slot = slot,
	                    .source = source,
	                    .range = range});
	return true;
}

bool RecordingSink::resize_audio_clip(project::ClipAddress clip, ResizeKind kind, int32_t new_length,
                                      int32_t old_length) {
	gestures.push_back({.kind = GestureRequest::Kind::RESIZE,
	                    .clip = clip,
	                    .value = new_length,
	                    .resize = kind,
	                    .old_length = old_length});
	return true;
}

void RecordingSink::set_undo_scroll_after(project::ClipAddress clip, int32_t x_scroll) {
	undo_scrolls.emplace_back(clip, x_scroll);
}

bool RecordingSink::learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
                          std::optional<LearnInput> input) {
	gestures.push_back({.kind = GestureRequest::Kind::LEARN,
	                    .mapping = std::move(target),
	                    .command = std::move(command),
	                    .input = input});
	return true;
}

bool RecordingSink::write_source_transpose(const Addresses& where, uint8_t source, std::optional<uint16_t> range,
                                           int32_t menu_value) {
	gestures.push_back({.kind = GestureRequest::Kind::TRANSPOSE,
	                    .where = where,
	                    .source = source,
	                    .range = range,
	                    .value = menu_value});
	return true;
}

} // namespace deluge::scenes::bind
