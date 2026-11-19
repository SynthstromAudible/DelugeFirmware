#include "scenes/bind/write_sink.h"

namespace deluge::scenes::bind {

bool RecordingSink::write(const WriteRequest& request) {
	requests.push_back(request);
	return true;
}

bool RecordingSink::write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
                                controls::Resolution resolution) {
	param_writes.push_back({address, value, pos, length, resolution});
	return true;
}

void RecordingSink::learned_written(const project::ParamAddress& address, int32_t knob_pos) {
	learned_follows.emplace_back(address, knob_pos);
}

std::optional<uint32_t> RecordingSink::take_for(uint32_t output_id) {
	take_requests.push_back(output_id);
	return take_answer;
}

bool RecordingSink::make_param(const project::ParamAddress& address) {
	made.push_back(address);
	return true;
}

void RecordingSink::knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) {
	absolutes.emplace_back(target, source);
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

void RecordingSink::set_default_audio_input_channel(int32_t channel) {
	gestures.push_back({.kind = GestureRequest::Kind::DEFAULT_INPUT_CHANNEL, .value = channel});
}

bool RecordingSink::learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
                          std::optional<LearnInput> input) {
	gestures.push_back({.kind = GestureRequest::Kind::LEARN,
	                    .mapping = std::move(target),
	                    .command = std::move(command),
	                    .input = input});
	return true;
}

bool RecordingSink::learn_knob(const project::OwnerParamTarget& target, ::MIDICable* cable, int32_t which_knob,
                               int32_t midi_channel) {
	gestures.push_back({.kind = GestureRequest::Kind::LEARN_KNOB,
	                    .value = which_knob,
	                    .mapping = project::MappingTarget{target},
	                    .cable = cable,
	                    .midi_channel = midi_channel});
	return true;
}

bool RecordingSink::knob_mode(project::KnobModeOwner owner, uint8_t mode) {
	gestures.push_back({.kind = GestureRequest::Kind::KNOB_MODE, .value = mode, .knob_owner = owner});
	return true;
}

bool RecordingSink::chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) {
	gestures.push_back(
	    {.kind = GestureRequest::Kind::CHAIN_VIEW, .text = std::string{word}, .chain = chain, .key = std::string{key}});
	return true;
}

bool RecordingSink::write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) {
	gestures.push_back(
	    {.kind = GestureRequest::Kind::CONTROLS, .controls_address = owner, .controls = std::move(controls)});
	return true;
}

bool RecordingSink::stutter(const KnobFocus& focus, bool begin) {
	gestures.push_back({.kind = GestureRequest::Kind::STUTTER, .focus = focus, .begin = begin});
	return true;
}

bool RecordingSink::write_effect(const EffectOwner& owner, std::string_view path, const serialization::Value& value) {
	gestures.push_back({.kind = GestureRequest::Kind::EFFECT_WORD,
	                    .text = value.isScalar() ? value.str() : std::string{},
	                    .key = std::string{path},
	                    .effect_owner = owner});
	return true;
}

bool RecordingSink::midi_cc_select(const KnobFocus& focus, int32_t knob, bool on) {
	gestures.push_back({.kind = GestureRequest::Kind::MIDI_CC_SELECT, .value = knob, .focus = focus, .begin = on});
	return true;
}

bool RecordingSink::velocity_to_level_everywhere(const ::MIDICable& cable) {
	gestures.push_back({.kind = GestureRequest::Kind::VELOCITY_EVERYWHERE, .input = LearnInput{.cable = &cable}});
	return true;
}

bool RecordingSink::delete_knob_automation(const project::ParamAddress& address) {
	gestures.push_back({.kind = GestureRequest::Kind::DELETE_KNOB_AUTOMATION});
	deletes.push_back(address);
	return true;
}

bool RecordingSink::instrument_edited(const KnobFocus& focus) {
	gestures.push_back({.kind = GestureRequest::Kind::INSTRUMENT_EDITED, .focus = focus});
	return true;
}

bool RecordingSink::grab_region(const KnobFocus& focus, uint32_t pos) {
	gestures.push_back({.kind = GestureRequest::Kind::GRAB_REGION, .value = pos, .focus = focus});
	return true;
}

} // namespace deluge::scenes::bind
