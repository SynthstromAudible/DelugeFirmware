#include "model/project_bridge/setting_projection.h"

#include "deluge-project/audio_input.h"
#include "deluge-project/fraction.h"
#include "deluge-project/sequence_direction.h"
#include "dsp/reverb/reverb.hpp"
#include "engine-api/types/sync.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "serialization/tree.h"

#include <utility>

namespace deluge::bridge {

namespace {

using project::SettingGroupId;
using serialization::Value;

/// @brief The song's output with durable id @p id, playing or hibernating.
::Output* findOutput(::Song& song, uint32_t id) {
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->id() == id) {
			return output;
		}
	}
	for (::Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		if (sleeping->id() == id) {
			return sleeping;
		}
	}
	return nullptr;
}

/// @brief Drum @p drum_id of kit @p kit_id.
::Drum* findDrum(::Song& song, uint32_t kit_id, uint32_t drum_id) {
	::Output* output = findOutput(song, kit_id);
	if (output == nullptr || output->type != OutputType::KIT) {
		return nullptr;
	}
	for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->id() == drum_id) {
			return drum;
		}
	}
	return nullptr;
}

/// @brief The song's clip with durable id @p id and type @p type.
::Clip* findClip(::Song& song, uint32_t id, ClipType type) {
	for (::Clip* clip : AllClips::everywhere(&song)) {
		if (clip->id() == id && clip->type == type) {
			return clip;
		}
	}
	return nullptr;
}

/// @brief The song's clip with durable id @p id, of either type.
::Clip* findAnyClip(::Song& song, uint32_t id) {
	::Clip* clip = findClip(song, id, ClipType::INSTRUMENT);
	return clip != nullptr ? clip : findClip(song, id, ClipType::AUDIO);
}

/// @brief @p clip's row @p address names: a melodic row by its y, a kit row by its sound drum's id.
::NoteRow* findNoteRow(::InstrumentClip& clip, const project::NoteRowAddress& address) {
	for (int32_t i = 0; ::NoteRow* row = clip.noteRows.tryGet(i); i++) {
		const bool matches = address.is_kit
		                         ? (row->drum != nullptr && row->drum->id() == static_cast<uint32_t>(address.row_key))
		                         : row->y() == address.row_key;
		if (matches) {
			return row;
		}
	}
	return nullptr;
}

std::optional<int32_t> integer_of(const Value& value) {
	return serialization::int_of(&value);
}

/// @brief Copy reverb path @p path's value @p v onto the engine's one reverb, and have the render re-read its
///        parameters.
/// @return Error::UNSPECIFIED when @p path is not a reverb path.
Error applyReverbSetting(::Song& song, std::string_view path, int32_t v) {
	using project::fraction_of;
	if (path == "reverb/roomSize") {
		AudioEngine::reverb.setRoomSize(fraction_of(v));
	}
	else if (path == "reverb/dampening") {
		AudioEngine::reverb.setDamping(fraction_of(v));
	}
	else if (path == "reverb/width") {
		AudioEngine::reverb.setWidth(fraction_of(v));
	}
	else if (path == "reverb/hpf") {
		AudioEngine::reverb.setHPF(fraction_of(v));
	}
	else if (path == "reverb/lpf") {
		AudioEngine::reverb.setLPF(fraction_of(v));
	}
	else if (path == "reverb/pan") {
		AudioEngine::reverbPan = v;
	}
	else if (path == "reverb/model") {
		AudioEngine::reverb.setModel(static_cast<dsp::Reverb::Model>(v));
	}
	else if (path == "reverb/compressor/attack") {
		AudioEngine::reverbSidechainSettings.attack = v;
	}
	else if (path == "reverb/compressor/release") {
		AudioEngine::reverbSidechainSettings.release = v;
	}
	else if (path == "reverb/compressor/volume") {
		AudioEngine::reverbSidechainVolume = v;
	}
	else if (path == "reverb/compressor/shape") {
		AudioEngine::reverbSidechainShape = v;
	}
	else if (path == "reverb/compressor/syncLevel") {
		AudioEngine::reverbSidechainSettings.sync_level = song.project.reverb.sidechain_sync;
	}
	else {
		return Error::UNSPECIFIED;
	}
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	return Error::NONE;
}

/// @brief The engine reverb's value at reverb path @p path, in the record's form.
std::optional<Value> engineReverbValue(std::string_view path) {
	using project::q31_of;
	auto number = [](int64_t v) { return Value::makeNumber(v); };
	if (path == "reverb/roomSize") {
		return number(q31_of(AudioEngine::reverb.settings().room_size));
	}
	if (path == "reverb/dampening") {
		return number(q31_of(AudioEngine::reverb.settings().damping));
	}
	if (path == "reverb/width") {
		return number(q31_of(AudioEngine::reverb.settings().width));
	}
	if (path == "reverb/hpf") {
		return number(q31_of(AudioEngine::reverb.settings().hpf));
	}
	if (path == "reverb/lpf") {
		return number(q31_of(AudioEngine::reverb.settings().lpf));
	}
	if (path == "reverb/pan") {
		return number(AudioEngine::reverbPan);
	}
	if (path == "reverb/model") {
		return number(std::to_underlying(AudioEngine::reverb.getModel()));
	}
	if (path == "reverb/compressor/attack") {
		return number(AudioEngine::reverbSidechainSettings.attack);
	}
	if (path == "reverb/compressor/release") {
		return number(AudioEngine::reverbSidechainSettings.release);
	}
	if (path == "reverb/compressor/volume") {
		return number(AudioEngine::reverbSidechainVolume);
	}
	if (path == "reverb/compressor/shape") {
		return number(AudioEngine::reverbSidechainShape);
	}
	if (path == "reverb/compressor/syncLevel") {
		return number(deluge::engine::index_of(AudioEngine::reverbSidechainSettings.sync_level));
	}
	return std::nullopt;
}

} // namespace

project::SettingGroupId trackGroupOf(std::string_view path) {
	return path == "name" ? SettingGroupId::TRACK : SettingGroupId::INSTRUMENT_DEVICE;
}

// ---------------------------------------------------------------------------------------------
// Applying

Error applySongSetting(::Song& song, std::string_view path, const Value& value) {
	const std::optional<int32_t> v = integer_of(value);
	if (!v) {
		return Error::UNSPECIFIED;
	}
	if (path == "swingInterval") {
		song.changeSwingInterval(*v);
		return Error::NONE;
	}
	if (path == "syncScalingClip") {
		::Clip* clip = *v < 0 ? nullptr : findAnyClip(song, static_cast<uint32_t>(*v));
		if (*v >= 0 && clip == nullptr) {
			return Error::UNSPECIFIED;
		}
		// The input clock's scale follows the clip's length; the internal ticks re-sync to it.
		song.setInputTickScaleClip(clip);
		playbackHandler.resyncInternalTicksToInputTicks(&song);
		return Error::NONE;
	}
	if (path.starts_with("scales/disabled/")) {
		// The engine reads the scale record itself: there is no copy to make.
		return Error::NONE;
	}
	if (path.starts_with("reverb/")) {
		// The engine has one reverb, the current song's; another song's reaches it when that song becomes current
		// (`AudioEngine::getReverbParamsFromSong`).
		return &song == currentSong ? applyReverbSetting(song, path, *v) : Error::NONE;
	}
	return Error::UNSPECIFIED;
}

Error applyTrackSetting(::Output& output, std::string_view path, const Value& value) {
	if (path == "name") {
		output.setName(value.str());
		return Error::NONE;
	}
	const std::optional<int32_t> v = integer_of(value);
	const bool non_audio = output.type == OutputType::MIDI_OUT || output.type == OutputType::CV;
	if (path == "channel" && non_audio && v) {
		static_cast<::NonAudioInstrument&>(output).setChannel(*v);
		return Error::NONE;
	}
	if (output.type == OutputType::CV) {
		if (path == "cv2Source" && v) {
			static_cast<::CVInstrument&>(output).setCV2Mode(static_cast<CVMode>(*v));
			return Error::NONE;
		}
		return Error::UNSPECIFIED;
	}
	if (output.type != OutputType::MIDI_OUT) {
		return Error::UNSPECIFIED;
	}
	auto& midi = static_cast<::MIDIInstrument&>(output);
	if (path == "deviceDefinitionFileName") {
		midi.setDeviceDefinitionFileName(value.str());
		return Error::NONE;
	}
	if (!v) {
		return Error::UNSPECIFIED;
	}
	if (path == "channelSuffix") {
		midi.setChannelSuffix(*v);
	}
	else if (path == "outputMpeY") {
		midi.setOutputMPEY(static_cast<CCNumber>(*v));
	}
	else if (path == "collapseAftertouch") {
		midi.setCollapseAftertouch(*v != 0);
	}
	else if (path == "collapseMpe") {
		midi.setCollapseMPE(*v != 0);
	}
	else {
		return Error::UNSPECIFIED;
	}
	return Error::NONE;
}

Error applyLaneSetting(::Drum& drum, std::string_view path, const Value& value) {
	if (path != "name") {
		return Error::UNSPECIFIED;
	}
	drum.setName(value.str());
	return Error::NONE;
}

Error applyNoteRowSetting(::NoteRow& row, ::InstrumentClip& clip, ::Song& song, std::string_view path,
                          const Value& value, const Value* previous) {
	if (path == "bendRange") {
		const std::optional<int32_t> v = integer_of(value);
		ExpressionParamSet* expression = row.paramManager.getOrCreateExpressionParamSet(true);
		if (!v || expression == nullptr) {
			return Error::UNSPECIFIED;
		}
		expression->bendRanges[BEND_RANGE_FINGER_LEVEL] = static_cast<uint8_t>(*v);
		return Error::NONE;
	}
	const std::optional<SequenceDirection> direction = project::parse_sequence_direction(value.str());
	if (path != "sequenceDirection" || !direction) {
		return Error::UNSPECIFIED;
	}
	int32_t index = 0;
	while (clip.noteRows.tryGet(index) != &row) {
		if (clip.noteRows.tryGet(index) == nullptr) {
			return Error::UNSPECIFIED;
		}
		index++;
	}
	// The row's setter recomputes its play position from the clip's, which it reads off the model stack.
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* stack =
	    setupModelStackWithTimelineCounter(memory, &song, &clip)->addNoteRow(clip.getNoteRowId(&row, index), &row);
	if (previous != nullptr) {
		if (const auto before = project::parse_sequence_direction(previous->str())) {
			row.setSequenceDirectionModeRaw(*before);
		}
	}
	row.setSequenceDirectionMode(stack, *direction);
	return Error::NONE;
}

void withVoiceReseated(::AudioClip& clip, ::Song& song, const std::function<void()>& change) {
	const bool active =
	    playbackHandler.isEitherClockActive() && song.isClipActive(&clip) && clip.voiceSample != nullptr;
	clip.unassignVoiceSample(false);
	change();
	if (clip.sampleHolder.audioFile == nullptr) {
		return;
	}
	clip.sampleHolder.claimClusterReasons(clip.sampleControls.isCurrentlyReversed(), CLUSTER_ENQUEUE);
	if (active) {
		char memory[MODEL_STACK_MAX_SIZE];
		clip.resumePlayback(setupModelStackWithTimelineCounter(memory, &song, &clip), true);
	}
}

Error applyAudioInputSetting(::AudioOutput& output, ::Song& song, std::string_view path, const Value& value) {
	if (path == "mode") {
		const auto mode = project::index_of_word(project::kAudioModeWords, value.str());
		if (!mode) {
			return Error::UNSPECIFIED;
		}
		output.setMode(static_cast<AudioOutputMode>(*mode));
		return Error::NONE;
	}
	if (path == "inputChannel") {
		const auto channel = project::index_of_word(project::kInputChannelWords, value.str());
		if (!channel) {
			return Error::UNSPECIFIED;
		}
		// Leaving a specific track lets go of it and of its monitoring.
		if (output.inputChannel == AudioInputChannel::SPECIFIC_OUTPUT
		    && *channel != project::kInputChannelSpecificTrack) {
			output.clearRecordingFrom();
		}
		output.setInputChannel(static_cast<AudioInputChannel>(*channel));
		return Error::NONE;
	}
	if (path == "outputRecordingFrom") {
		const std::optional<int32_t> id = integer_of(value);
		if (!id) {
			return Error::UNSPECIFIED;
		}
		::Output* source = *id < 0 ? nullptr : findOutput(song, static_cast<uint32_t>(*id));
		if (*id >= 0 && (source == nullptr || !output.canRecordFrom(source))) {
			return Error::UNSPECIFIED;
		}
		output.setOutputRecordingFrom(source);
		return Error::NONE;
	}
	return Error::UNSPECIFIED;
}

Error applyClipSetting(::Clip& clip, ::Song& song, std::string_view path, const Value& value) {
	if (path == "name") {
		clip.setName(value.str());
		return Error::NONE;
	}
	const std::optional<SequenceDirection> direction = project::parse_sequence_direction(value.str());
	if (path != "sequenceDirection" || !direction) {
		return Error::UNSPECIFIED;
	}
	// The setter mirrors the play position when the direction turns, and resumes a playing clip.
	char memory[MODEL_STACK_MAX_SIZE];
	clip.setSequenceDirectionMode(setupModelStackWithTimelineCounter(memory, &song, &clip), *direction);
	return Error::NONE;
}

Error applyClipValueSetting(::InstrumentClip& clip, ::Song& /*song*/, std::string_view path, const Value& value) {
	const std::optional<int32_t> v = integer_of(value);
	if (!v) {
		return Error::UNSPECIFIED;
	}
	if (path == "bendRange" || path == "bendRangeMPE") {
		const int32_t which = path == "bendRange" ? BEND_RANGE_MAIN : BEND_RANGE_FINGER_LEVEL;
		ExpressionParamSet* expression = clip.paramManager.getOrCreateExpressionParamSet();
		if (expression == nullptr) {
			return Error::UNSPECIFIED;
		}
		expression->bendRanges[which] = static_cast<uint8_t>(*v);
		// A MIDI or CV instrument bends with the ranges it cached from its active clip.
		if (clip.output != nullptr && clip.isActiveOnOutput()
		    && (clip.output->type == OutputType::MIDI_OUT || clip.output->type == OutputType::CV)) {
			auto& instrument = static_cast<::NonAudioInstrument&>(*clip.output);
			instrument.cachedBendRanges[which] = expression->bendRanges[which];
			if (clip.output->type == OutputType::MIDI_OUT) {
				auto& midi = static_cast<::MIDIInstrument&>(instrument);
				midi.ratio = float(midi.cachedBendRanges[BEND_RANGE_FINGER_LEVEL])
				             / float(midi.cachedBendRanges[BEND_RANGE_MAIN]);
			}
		}
		return Error::NONE;
	}
	if (path == "midiBank") {
		clip.midiBank = static_cast<uint8_t>(*v);
	}
	else if (path == "midiSub") {
		clip.midiSub = static_cast<uint8_t>(*v);
	}
	else if (path == "midiPGM") {
		clip.midiPGM = static_cast<uint8_t>(*v);
	}
	else {
		return Error::UNSPECIFIED;
	}
	// A playing MIDI clip's device hears its new program at once.
	if (clip.output != nullptr && clip.output->type == OutputType::MIDI_OUT && clip.isActiveOnOutput()) {
		clip.sendMIDIPGM();
	}
	return Error::NONE;
}

Error applyAudioClipSetting(::AudioClip& clip, ::Song& song, std::string_view path, const Value& value) {
	const std::optional<int32_t> found = integer_of(value);
	if (!found) {
		return Error::UNSPECIFIED;
	}
	const int32_t v = *found;
	// A marker is copied alone: a gesture that moves one while the clip plays re-seats the voice around its Edit
	// (withVoiceReseated), and a clip-length change resumes the clip itself.
	if (path == "startSamplePos") {
		clip.setSampleStartPos(static_cast<uint64_t>(v));
	}
	else if (path == "endSamplePos") {
		clip.setSampleEndPos(static_cast<uint64_t>(v));
	}
	else if (path == "reversed") {
		withVoiceReseated(clip, song, [&] {
			clip.setReversed(v != 0);
			// Played backwards, the clip starts at its end marker, which must lie within the sample.
			if (clip.sampleHolder.audioFile != nullptr && clip.sampleControls.isCurrentlyReversed()) {
				const uint64_t length = static_cast<::Sample*>(clip.sampleHolder.audioFile)->lengthInSamples;
				if (clip.sampleHolder.endPos > length) {
					clip.setSampleEndPos(length);
				}
			}
		});
	}
	else if (path == "transpose" || path == "cents") {
		if (path == "transpose") {
			clip.setSampleTranspose(v);
		}
		else {
			clip.setSampleCents(v);
		}
		clip.sampleHolder.recalculateNeutralPhaseIncrement();
	}
	else if (path == "linearInterpolation") {
		clip.setInterpolationMode(v != 0 ? InterpolationMode::LINEAR : InterpolationMode::SMOOTH);
	}
	else if (path == "pitchSpeedIndependent") {
		clip.setPitchAndSpeedIndependent(v != 0);
	}
	else if (path == "attack") {
		clip.setAttack(v);
	}
	else if (path == "priority") {
		clip.setVoicePriority(static_cast<VoicePriority>(v));
	}
	else if (path == "overdubsShouldCloneAudioTrack") {
		clip.setOverdubsShouldCloneOutput(v != 0);
	}
	else {
		return Error::UNSPECIFIED;
	}
	return Error::NONE;
}

// ---------------------------------------------------------------------------------------------
// Projecting and reading back

Error projectSetting(::Song& song, const project::SettingAddress& address, std::string_view path,
                     const Value* previous) {
	const std::optional<Value> value = project::read_setting(song.project, address, path);
	if (!value) {
		return Error::UNSPECIFIED;
	}
	switch (address.group) {
	case SettingGroupId::SONG:
		return applySongSetting(song, path, *value);
	case SettingGroupId::TRACK:
	case SettingGroupId::INSTRUMENT_DEVICE: {
		::Output* output = findOutput(song, std::get<project::TrackAddress>(address.owner).output_id);
		return output != nullptr ? applyTrackSetting(*output, path, *value) : Error::UNSPECIFIED;
	}
	case SettingGroupId::LANE: {
		const auto& lane = std::get<project::LaneAddress>(address.owner);
		::Drum* drum = findDrum(song, lane.kit_id, lane.drum_id);
		return drum != nullptr ? applyLaneSetting(*drum, path, *value) : Error::UNSPECIFIED;
	}
	case SettingGroupId::NOTE_ROW: {
		const auto& row_address = std::get<project::NoteRowAddress>(address.owner);
		auto* clip = static_cast<::InstrumentClip*>(findClip(song, row_address.clip_id, ClipType::INSTRUMENT));
		::NoteRow* row = clip != nullptr ? findNoteRow(*clip, row_address) : nullptr;
		return row != nullptr ? applyNoteRowSetting(*row, *clip, song, path, *value, previous) : Error::UNSPECIFIED;
	}
	case SettingGroupId::AUDIO_CLIP: {
		auto* clip = static_cast<::AudioClip*>(
		    findClip(song, std::get<project::ClipAddress>(address.owner).clip_id, ClipType::AUDIO));
		return clip != nullptr ? applyAudioClipSetting(*clip, song, path, *value) : Error::UNSPECIFIED;
	}
	case SettingGroupId::CLIP: {
		::Clip* clip = findAnyClip(song, std::get<project::ClipAddress>(address.owner).clip_id);
		return clip != nullptr ? applyClipSetting(*clip, song, path, *value) : Error::UNSPECIFIED;
	}
	case SettingGroupId::AUDIO_INPUT: {
		::Output* output = findOutput(song, std::get<project::TrackAddress>(address.owner).output_id);
		return output != nullptr && output->type == OutputType::AUDIO
		           ? applyAudioInputSetting(static_cast<::AudioOutput&>(*output), song, path, *value)
		           : Error::UNSPECIFIED;
	}
	case SettingGroupId::CLIP_VALUES: {
		auto* clip = static_cast<::InstrumentClip*>(
		    findClip(song, std::get<project::ClipAddress>(address.owner).clip_id, ClipType::INSTRUMENT));
		return clip != nullptr ? applyClipValueSetting(*clip, song, path, *value) : Error::UNSPECIFIED;
	}
	}
	return Error::UNSPECIFIED;
}

/// @brief @p output's engine copy at track or instrument-device path @p path, in the record's form.
std::optional<Value> engineTrackValue(::Output& output, std::string_view path) {
	if (path == "name") {
		return Value::makeScalar(output.name);
	}
	if (output.type == OutputType::CV) {
		auto& cv = static_cast<::CVInstrument&>(output);
		if (path == "channel") {
			return Value::makeNumber(cv.getChannel());
		}
		if (path == "cv2Source") {
			return Value::makeNumber(std::to_underlying(cv.getCV2Mode()));
		}
		return std::nullopt;
	}
	if (output.type != OutputType::MIDI_OUT) {
		return std::nullopt;
	}
	auto& midi = static_cast<::MIDIInstrument&>(output);
	if (path == "channel") {
		return Value::makeNumber(midi.getChannel());
	}
	if (path == "channelSuffix") {
		return Value::makeNumber(midi.channelSuffix);
	}
	if (path == "deviceDefinitionFileName") {
		return Value::makeScalar(midi.deviceDefinitionFileName);
	}
	if (path == "outputMpeY") {
		return Value::makeNumber(static_cast<int32_t>(midi.outputMPEY));
	}
	if (path == "collapseAftertouch") {
		return Value::makeNumber(midi.collapseAftertouch ? 1 : 0);
	}
	if (path == "collapseMpe") {
		return Value::makeNumber(midi.collapseMPE ? 1 : 0);
	}
	return std::nullopt;
}

/// @brief @p clip's engine copy at audio clip path @p path, in the record's form. The attack and the voice priority
///        have no copy: the clip reads its record for them.
std::optional<Value> engineAudioClipValue(::AudioClip& clip, std::string_view path) {
	auto number = [](int64_t v) { return std::optional<Value>{Value::makeNumber(v)}; };
	if (path == "startSamplePos") {
		return number(static_cast<int64_t>(clip.sampleHolder.startPos));
	}
	if (path == "endSamplePos") {
		return number(static_cast<int64_t>(clip.sampleHolder.endPos));
	}
	if (path == "transpose") {
		return number(clip.sampleHolder.transpose);
	}
	if (path == "cents") {
		return number(clip.sampleHolder.cents);
	}
	if (path == "linearInterpolation") {
		return number(clip.sampleControls.interpolationMode == InterpolationMode::LINEAR ? 1 : 0);
	}
	if (path == "pitchSpeedIndependent") {
		return number(clip.sampleControls.pitchAndSpeedAreIndependent ? 1 : 0);
	}
	if (path == "reversed") {
		return number(clip.sampleControls.reversed ? 1 : 0);
	}
	if (path == "attack") {
		return number(clip.attack());
	}
	if (path == "priority") {
		return number(std::to_underlying(clip.voicePriority()));
	}
	if (path == "overdubsShouldCloneAudioTrack") {
		return number(clip.overdubsShouldCloneOutput ? 1 : 0);
	}
	return std::nullopt;
}

std::optional<Value> engineSettingValue(::Song& song, const project::SettingAddress& address, std::string_view path) {
	if (address.group == SettingGroupId::TRACK || address.group == SettingGroupId::INSTRUMENT_DEVICE) {
		::Output* output = findOutput(song, std::get<project::TrackAddress>(address.owner).output_id);
		return output != nullptr ? engineTrackValue(*output, path) : std::nullopt;
	}
	if (address.group == SettingGroupId::AUDIO_INPUT) {
		::Output* output = findOutput(song, std::get<project::TrackAddress>(address.owner).output_id);
		if (output == nullptr || output->type != OutputType::AUDIO) {
			return std::nullopt;
		}
		auto& audio = static_cast<::AudioOutput&>(*output);
		if (path == "mode") {
			return Value::makeScalar(std::string{project::kAudioModeWords[util::to_underlying(audio.mode)]});
		}
		if (path == "inputChannel") {
			const int32_t channel = std::max(0, util::to_underlying(audio.inputChannel));
			return channel < std::ssize(project::kInputChannelWords)
			           ? std::optional<Value>{Value::makeScalar(std::string{project::kInputChannelWords[channel]})}
			           : std::nullopt;
		}
		if (path == "outputRecordingFrom") {
			::Output* source = audio.getOutputRecordingFrom();
			return Value::makeNumber(source != nullptr && source->id() ? int64_t{*source->id()} : -1);
		}
		return std::nullopt;
	}
	if (address.group == SettingGroupId::CLIP_VALUES) {
		auto* clip = static_cast<::InstrumentClip*>(
		    findClip(song, std::get<project::ClipAddress>(address.owner).clip_id, ClipType::INSTRUMENT));
		if (clip == nullptr) {
			return std::nullopt;
		}
		if (path == "midiBank") {
			return Value::makeNumber(clip->midiBank);
		}
		if (path == "midiSub") {
			return Value::makeNumber(clip->midiSub);
		}
		if (path == "midiPGM") {
			return Value::makeNumber(clip->midiPGM);
		}
		if (path == "bendRange" || path == "bendRangeMPE") {
			const bool main = path == "bendRange";
			const ExpressionParamSet* expression = clip->paramManager.getExpressionParamSet();
			return expression != nullptr
			           ? Value::makeNumber(expression->bendRanges[main ? BEND_RANGE_MAIN : BEND_RANGE_FINGER_LEVEL])
			           : Value::makeNumber(main ? 2 : 48); // the factory ranges, as the record's
		}
		return std::nullopt;
	}
	if (address.group == SettingGroupId::CLIP) {
		::Clip* clip = findAnyClip(song, std::get<project::ClipAddress>(address.owner).clip_id);
		if (clip == nullptr) {
			return std::nullopt;
		}
		if (path == "name") {
			return Value::makeScalar(clip->name);
		}
		return path == "sequenceDirection" ? std::optional<Value>{Value::makeScalar(
		                                         std::string{project::to_string(clip->sequenceDirectionMode)})}
		                                   : std::nullopt;
	}
	if (address.group == SettingGroupId::AUDIO_CLIP) {
		auto* clip = static_cast<::AudioClip*>(
		    findClip(song, std::get<project::ClipAddress>(address.owner).clip_id, ClipType::AUDIO));
		return clip != nullptr ? engineAudioClipValue(*clip, path) : std::nullopt;
	}
	if (address.group == SettingGroupId::LANE) {
		const auto& lane = std::get<project::LaneAddress>(address.owner);
		::Drum* drum = findDrum(song, lane.kit_id, lane.drum_id);
		return drum != nullptr && path == "name" ? std::optional<Value>{Value::makeScalar(drum->name())} : std::nullopt;
	}
	if (address.group == SettingGroupId::NOTE_ROW) {
		const auto& row_address = std::get<project::NoteRowAddress>(address.owner);
		auto* clip = static_cast<::InstrumentClip*>(findClip(song, row_address.clip_id, ClipType::INSTRUMENT));
		::NoteRow* row = clip != nullptr ? findNoteRow(*clip, row_address) : nullptr;
		if (row != nullptr && path == "bendRange") {
			const ExpressionParamSet* expression = row->paramManager.getExpressionParamSet();
			return Value::makeNumber(expression != nullptr ? expression->bendRanges[BEND_RANGE_FINGER_LEVEL] : 48);
		}
		return row != nullptr && path == "sequenceDirection" ? std::optional<Value>{Value::makeScalar(std::string{
		                                                           project::to_string(row->sequenceDirectionMode())})}
		                                                     : std::nullopt;
	}
	if (address.group == SettingGroupId::SONG) {
		if (path == "swingInterval") {
			return Value::makeNumber(deluge::engine::index_of(song.project.groove.swing_interval));
		}
		if (path == "syncScalingClip") {
			::Clip* clip = song.getSyncScalingClip();
			return Value::makeNumber(clip != nullptr ? int64_t{clip->id()} : -1);
		}
		if (path.starts_with("reverb/")) {
			return engineReverbValue(path);
		}
		if (path.starts_with("scales/disabled/")) {
			return project::read_setting(song.project, address, path); // the engine reads the record
		}
	}
	return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Addresses

std::optional<project::TrackAddress> trackAddressOf(::Output& output, ::Song& song) {
	const std::optional<uint32_t> id = output.id();
	if (!id || findOutput(song, *id) != &output || song.project.output_structure.find(*id) == nullptr) {
		return std::nullopt;
	}
	return project::TrackAddress{.output_id = *id};
}

std::optional<project::TrackAddress> trackAddressOf(::AudioOutput& output, ::Song& song) {
	return trackAddressOf(static_cast<::Output&>(output), song);
}

std::optional<project::LaneAddress> laneAddressOf(::Drum& drum, ::Song& song) {
	const std::optional<uint32_t> drum_id = drum.id();
	if (!drum_id || song.project.drum_structure.find(*drum_id) == nullptr) {
		return std::nullopt;
	}
	for (::Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->type == OutputType::KIT && output->id() && findDrum(song, *output->id(), *drum_id) == &drum) {
			return project::LaneAddress{.kit_id = *output->id(), .drum_id = *drum_id};
		}
	}
	return std::nullopt;
}

std::optional<project::NoteRowAddress> noteRowAddressOf(::NoteRow& row, ::InstrumentClip& clip, ::Song& song) {
	if (findClip(song, clip.id(), ClipType::INSTRUMENT) != &clip || clip.output == nullptr) {
		return std::nullopt;
	}
	project::NoteRowAddress address{.clip_id = clip.id()};
	if (clip.output->type == OutputType::KIT) {
		if (row.drum == nullptr || row.drum->type != DrumType::SOUND || !row.drum->id()) {
			return std::nullopt;
		}
		address.is_kit = true;
		address.row_key = static_cast<int32_t>(*row.drum->id());
	}
	else {
		address.row_key = row.y();
	}
	if (findNoteRow(clip, address) != &row) {
		return std::nullopt;
	}
	return address;
}

std::optional<project::ClipAddress> clipAddressOf(::Clip& clip, ::Song& song) {
	if (findClip(song, clip.id(), clip.type) != &clip || song.project.clip_structure.find(clip.id()) == nullptr) {
		return std::nullopt;
	}
	return project::ClipAddress{.clip_id = clip.id()};
}

std::optional<project::ClipAddress> clipAddressOf(::InstrumentClip& clip, ::Song& song) {
	return clipAddressOf(static_cast<::Clip&>(clip), song);
}

std::optional<project::ClipAddress> audioClipAddressOf(::AudioClip& clip, ::Song& song) {
	if (findClip(song, clip.id(), ClipType::AUDIO) != &clip
	    || song.project.audio_clip_structure.find(clip.id()) == nullptr) {
		return std::nullopt;
	}
	return project::ClipAddress{.clip_id = clip.id()};
}

} // namespace deluge::bridge
