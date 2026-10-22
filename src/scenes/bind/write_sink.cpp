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

} // namespace deluge::scenes::bind
