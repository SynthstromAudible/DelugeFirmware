/// @file
/// @brief The knob layouts the engine changes on its own, pinned as it changes them: the FM swap, a sample row's
///        pitch-adjust knob, and the empty layout of a CV track, a MIDI drum and a gate drum.
///
/// @par The oracle
///
/// Each expected layout is a literal recorded from the engine (`instruments::classic::remap_for_mode`,
/// `Sound::setupAsSample`), with the knobs named as a preset file names them: the param, and a cable's sources after
/// `<`. Every check reads the layout four ways, and all four must equal the literal:
///   * the engine's array (`Sound::modKnobs`), which sound logic reads;
///   * the preset form, `<modKnobs>` as `Sound::writeToFile` writes it from the engine's array;
///   * the record (`DocOutput::controls` / `DocDrum::controls`), which must equal `bridge::controlsOf` of the engine;
///   * the song form, `<modKnobs>` as `encode_controls` writes it from the record.
///
/// @par The empty layouts
///
/// A CV track, a MIDI drum and a gate drum have no knobs: what is pinned is what a user sees, never how the focus
/// names them. A gold turn writes no param and leaves the knob mode alone, a mod button changes no mode and shows no
/// popup, the knob indicators read zero, and a non-sound drum's own edit (`NonAudioDrum::modEncoderAction`, the
/// channel, and `MIDIDrum::modEncoderAction`'s note) still happens while its row is auditioned.
///
/// The wavetable claim's mode-7 knobs, and the slicer's and a folder import's sample rows, are pinned by the
/// sample_claim scenario, which runs those claims against a card. For those rows it pins mod button 6's lower knob and
/// the record, not that the pitch adjust survives: that is pinned here, on the clip view's new row.

#include "gui/ui/root_knobs.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"

#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "instruments/classic/naming.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_controls.h"
#include "model/edits/set_kit_setting.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"
#include "modulation/knob_target.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/controller_mapping.h"
#include "project/controls_codec.h"
#include "serialization/value.h"
#include "storage/storage_manager.h"
#include "storage/value_serializer.h"
#include "ui/scenes/menu/nodes/fan_out_rules.h"
#include "util/functions.h"

#include "support/popup_capture.h"
#include "support/printers/controller_mapping_printer.h"
#include "support/printers/knob_pair_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// The host BSP's shadow of the knob indicator columns, four segment brightnesses each; declared rather than included
/// because `host_script.h` is not on this target's include path.
extern "C" const uint8_t* host_script_ring_shadow(uint8_t which);

namespace {

namespace project = deluge::project;
namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;

using deluge::serialization::Value;

/// A sound's 16 knobs, button by button, lower knob first, each named as a preset file names it.
using Layout = std::array<std::string_view, 16>;

/// @brief @p target named as a preset file names it: the param, then a cable's sources after `<`.
std::string knob_name(const deluge::modulation::KnobTarget& target) {
	const deluge::synth::CableTarget word = deluge::modulation::knobWord(target);
	std::string name = params::paramNameForFile(params::Kind::UNPATCHED_SOUND, word.param_index());
	if (!word.is_param()) {
		name += std::string{"<"} + sourceToString(word.top_source());
		if (word.has_second_source()) {
			name += std::string{"+"} + sourceToString(word.second_source_from_top());
		}
	}
	return name;
}

/// @brief @p layout as one line, so a failure prints the whole of it as a literal.
std::string joined(const std::vector<std::string>& layout) {
	std::string out = "{";
	for (size_t i = 0; i < layout.size(); i++) {
		out += (i == 0 ? "\"" : ", \"") + layout[i] + "\"";
	}
	return out + "}";
}

/// @brief @p layout as one line.
std::string joined(const Layout& layout) {
	return joined(std::vector<std::string>{layout.begin(), layout.end()});
}

/// @brief The engine's array, as names.
std::vector<std::string> engine_layout(const Sound& sound) {
	std::vector<std::string> out;
	for (int32_t button = 0; button < kNumModButtons; button++) {
		for (int32_t knob = 0; knob < kNumPhysicalModKnobs; knob++) {
			out.push_back(knob_name(sound.modKnobs[button][knob].target));
		}
	}
	return out;
}

/// @brief The first `modKnobs` in @p value, searched depth first.
const Value* find_mod_knobs(const Value& value) {
	if (value.isMap()) {
		if (const Value* found = value.find("modKnobs")) {
			return found;
		}
		for (const auto& entry : value.entries()) {
			if (const Value* found = find_mod_knobs(entry.second)) {
				return found;
			}
		}
	}
	if (value.isArray()) {
		for (const Value& item : value.items()) {
			if (const Value* found = find_mod_knobs(item)) {
				return found;
			}
		}
	}
	return nullptr;
}

/// @brief The scalar text at @p key of @p element, or empty.
std::string text_at(const Value& element, std::string_view key) {
	const Value* value = element.find(key);
	return value != nullptr && value->isScalar() ? value->str() : std::string{};
}

/// @brief A file's `modKnobs` list, as names; a missing list reads as no knobs.
std::vector<std::string> file_layout(const Value* modKnobs) {
	std::vector<std::string> out;
	if (modKnobs == nullptr || !modKnobs->isArray()) {
		return out;
	}
	for (const Value& boxed : modKnobs->items()) {
		const Value* knob = boxed.find("modKnob");
		if (knob == nullptr) {
			out.emplace_back("?");
			continue;
		}
		std::string name = text_at(*knob, "controlsParam");
		const std::string first = text_at(*knob, "patchAmountFromSource");
		if (!first.empty()) {
			name += "<" + first;
			const std::string second = text_at(*knob, "patchAmountFromSecondSource");
			if (!second.empty()) {
				name += "+" + second;
			}
		}
		out.push_back(name);
	}
	return out;
}

/// @brief The preset form: the `<modKnobs>` that @p write puts out through the shared serialiser.
std::vector<std::string> preset_layout(const std::function<void()>& write) {
	ValueSerializer writer(ValueSerializer::Writes::ITEMS);
	{
		ActiveWriterScope scope(writer);
		write();
	}
	const std::optional<Value> written = writer.finish();
	return file_layout(written ? find_mod_knobs(*written) : nullptr);
}

/// @brief The song form: `<modKnobs>` as `encode_controls` writes @p record.
std::vector<std::string> song_layout(const std::vector<project::ControllerMapping>& record) {
	Value element = Value::makeMap();
	project::encode_controls(record, deluge::instruments::classic::kNaming, element);
	return file_layout(element.find("modKnobs"));
}

/// @brief @p sound's record in @p song, or null for a sound with none.
std::vector<project::ControllerMapping>* record_of(Sound& sound, Song& song) {
	const std::optional<project::ControlsAddress> address = deluge::bridge::controlsAddressOf(sound);
	return address ? song.project.resolveControls(*address) : nullptr;
}

/// @brief @p sound's layout read every way it is kept equals @p want: the engine's array, its preset form (what
///        @p write_preset writes), and with a @p song, its record (equal to the engine's) and the record's song form.
void expect_layout(auto& self, Sound& sound, Song* song, const std::function<void()>& write_preset,
                   const Layout& want) {
	const std::string wanted = joined(want);
	expect(joined(engine_layout(sound))).to_equal(wanted);
	expect(joined(preset_layout(write_preset))).to_equal(wanted);
	if (song == nullptr) {
		return;
	}
	std::vector<project::ControllerMapping>* record = record_of(sound, *song);
	expect(record).not_().to_be_null("the sound's controls record");
	if (record != nullptr) {
		expect(*record).to_equal(deluge::bridge::controlsOf(sound), "the sound's controls record");
		expect(joined(song_layout(*record))).to_equal(wanted);
	}
}

/// @brief A synth fixture's layout, its preset written as `SaveInstrumentPresetUI::serializeForSave` writes it.
void expect_synth_layout(auto& self, spec::SongFixture& f, const Layout& want) {
	expect_layout(
	    self, *static_cast<SoundInstrument*>(f.instrument), f.song, [&] { f.instrument->writeToFile(f.clip, f.song); },
	    want);
}

/// @brief A kit row's layout, its preset written as a row saved on its own (`SoundDrum::writeToFileAsInstrument`).
void expect_row_layout(auto& self, spec::SongFixture& f, SoundDrum& drum, const Layout& want) {
	NoteRow* row = f.clip->getNoteRowForDrum(&drum);
	expect_layout(
	    self, drum, f.song, [&] { drum.writeToFileAsInstrument(false, row != nullptr ? &row->paramManager : nullptr); },
	    want);
}

/// @brief The menu's write of the synth mode on @p sound: `edits::settingFor`, an Edit on its slot when it has one.
void set_mode(Sound& sound, const char* mode) {
	deluge::edits::settingFor(sound, "mode", Value::makeScalar(mode));
}

/// @brief A plain param's knob target.
deluge::modulation::KnobTarget param_knob(int32_t p) {
	return deluge::modulation::knobParam(p);
}

/// @brief An unpatched param's knob target.
deluge::modulation::KnobTarget unpatched_knob(int32_t p) {
	return deluge::modulation::knobParam(params::UNPATCHED_START + p);
}

/// @brief Point @p sound's knobs on @p button at @p lower and @p upper as a learn does: a SetControls on its record.
void set_pair(auto& self, Sound& sound, Song& song, uint8_t button, deluge::modulation::KnobTarget lower,
              deluge::modulation::KnobTarget upper) {
	std::vector<project::ControllerMapping> controls = deluge::bridge::controlsOf(sound);
	for (project::ControllerMapping& mapping : controls) {
		if (mapping.control.button == button) {
			mapping.target = deluge::bridge::controlTargetOf(mapping.control.knob == 0 ? lower : upper,
			                                                 params::Kind::UNPATCHED_SOUND);
		}
	}
	deluge::edits::Context ctx{song, song.project};
	deluge::edits::SetControls edit{*deluge::bridge::controlsAddressOf(sound), controls};
	expect(int(deluge::edits::apply(ctx, edit))).to_equal(int(Error::NONE), "the controls edit");
}

// The layouts, recorded from `instruments::classic::remap_for_mode` (the FM swap) and `Sound::setupAsSample` (a sample
// row's mode 6).

/// A new synth's knobs (`Sound::Sound`), recorded; `instruments::classic::remap_for_mode` brings them back after FM and
/// out of it.
constexpr Layout kDefaultSynth{"pan",
                               "volumePostFX",
                               "lpfResonance",
                               "lpfFrequency",
                               "env1Release",
                               "env1Attack",
                               "delayFeedback",
                               "delayRate",
                               "reverbAmount",
                               "volumePostReverbSend<compressor",
                               "pitch<lfo1",
                               "lfo1Rate",
                               "portamento",
                               "stutterRate",
                               "bitcrushAmount",
                               "sampleRateReduction"};

/// @brief @p base with mod button 1's (the filter button's) knobs named @p lower and @p upper.
constexpr Layout with_button1(const Layout& base, std::string_view lower, std::string_view upper) {
	Layout out = base;
	out[2] = lower;
	out[3] = upper;
	return out;
}

/// @brief @p base with knob @p knob of mod button @p button named @p name.
constexpr Layout with_knob(const Layout& base, size_t button, size_t knob, std::string_view name) {
	Layout out = base;
	out[button * 2 + knob] = name;
	return out;
}

/// The FM swap of the filter button: the modulators' volumes (`instruments::classic::remap_for_mode`, into FM).
constexpr Layout kDefaultFm = with_button1(kDefaultSynth, "modulator2Volume", "modulator1Volume");

/// @brief A param snapshot of @p pm: for each collection it holds, every value of a param set, how many CCs a MIDI
///        collection holds and how many cables a cable set holds.
std::vector<int32_t> param_snapshot(ParamManager* pm) {
	std::vector<int32_t> out;
	if (pm == nullptr) {
		return out;
	}
	for (const ParamCollectionSummary& summary : pm->summaries) {
		ParamCollection* collection = summary.paramCollection;
		if (collection == nullptr) {
			continue;
		}
		switch (collection->getParamKind()) {
		case params::Kind::MIDI:
			out.push_back(static_cast<int32_t>(std::ssize(static_cast<MIDIParamCollection*>(collection)->params)));
			break;
		case params::Kind::PATCH_CABLE:
			out.push_back(static_cast<PatchCableSet*>(collection)->numPatchCables);
			break;
		default: {
			auto* set = static_cast<ParamSet*>(collection);
			for (int32_t i = 0; i < set->getNumParams(); i++) {
				out.push_back(set->getValue(i));
			}
		}
		}
	}
	return out;
}

/// @brief Knob @p knob's indicator column as the host's LEDs show it, four segment brightnesses.
std::array<uint8_t, 4> ring(uint8_t knob) {
	const uint8_t* shadow = host_script_ring_shadow(knob);
	return {shadow[0], shadow[1], shadow[2], shadow[3]};
}

/// @brief The knob mode @p output's record holds.
int32_t record_mode(Song& song, Output& output) {
	return song.project.output_structure.find(*output.id())->mod_knob_mode;
}

/// @brief What a user sees of an owner with no knobs, @p f's clip in the clip view with the knobs on it: both knobs
///        turned through the clip view (`InstrumentClipView::modEncoderAction`), then mod button 2 pressed, write no
///        param of the clip, the song or @p row_params, change no mode (the record's nor the engine's), show no popup,
///        and leave the indicators at zero.
void expect_no_knobs(auto& self, spec::SongFixture& f, ParamManager* row_params) {
	deluge::ui::scenes::spec::PopupCapture popups;
	deluge::ui::scenes::spec::make_current(f.song);
	setRootUILowLevel(&instrumentClipView);
	// Lit first, so zero is what the knobs' owner leaves them at.
	indicator_leds::setKnobIndicatorLevel(0, 100, false);
	indicator_leds::setKnobIndicatorLevel(1, 100, false);
	view.setActiveModControllableTimelineCounter(f.clip, false);
	(void)deluge::edits::modKnobModeFor(project::TrackAddress{*f.instrument->id()}, 3);
	const int32_t mode = record_mode(*f.song, *f.instrument);
	expect(mode).to_equal(3);
	expect(static_cast<int32_t>(f.instrument->modKnobMode)).to_equal(mode);
	const std::vector<int32_t> clip_before = param_snapshot(&f.clip->paramManager);
	const std::vector<int32_t> song_before = param_snapshot(&f.song->paramManager);
	const std::vector<int32_t> row_before = param_snapshot(row_params);

	for (int32_t knob = 0; knob < 2; knob++) {
		instrumentClipView.modEncoderAction(knob, 5);
		instrumentClipView.modEncoderAction(knob, -3);
	}
	deluge::gui::ui::root_knobs::mod_button(2, true);
	deluge::gui::ui::root_knobs::mod_button(2, false);
	view.setKnobIndicatorLevels();

	expect(param_snapshot(&f.clip->paramManager)).to_equal(clip_before, "the clip's params");
	expect(param_snapshot(&f.song->paramManager)).to_equal(song_before, "the song's params");
	expect(param_snapshot(row_params)).to_equal(row_before, "the row's params");
	expect(record_mode(*f.song, *f.instrument)).to_equal(mode);
	expect(static_cast<int32_t>(f.instrument->modKnobMode)).to_equal(mode); // the engine's copy too
	{
		using Kind = deluge::ui::scenes::spec::PopupEvent::Kind;
		expect(popups.count(Kind::POPUP) + popups.count(Kind::PERSISTENT) + popups.count(Kind::TEMPORARY)
		       + popups.count(Kind::NOTIFICATION))
		    .to_equal(size_t{0}, "the popups shown");
	}
	expect(ring(0)).to_equal(std::array<uint8_t, 4>{}, "the lower knob's ring");
	expect(ring(1)).to_equal(std::array<uint8_t, 4>{}, "the upper knob's ring");

	nullifyUIs();
	view.releaseActiveModControllable();
	deluge::ui::scenes::spec::make_current(nullptr);
}

/// @brief Make @p f's clip its output's active clip, as the clip a user has open is.
void make_active(spec::SongFixture& f) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	(void)f.instrument->setActiveClip(setupModelStackWithTimelineCounter(memory, f.song, f.clip), PgmChangeSend::NEVER);
}

/// @brief A kit fixture with one drum of type @p T selected, the clip on that row.
template <typename T>
T* kit_with_drum(spec::SongFixture& f) {
	f = spec::make_kit_song();
	T* drum = spec::add_drum<T>(f);
	f.clip->affectEntire = false;
	make_active(f);
	return drum;
}

/// @brief Turn knob @p knob of the clip view one value's worth up (`NonAudioDrum::modChange` steps once per four).
void turn_one_step(int32_t knob) {
	for (int32_t i = 0; i < 4; i++) {
		instrumentClipView.modEncoderAction(knob, 1);
	}
}

} // namespace

// clang-format off
describe layout_characterisation("the knob layouts the engine changes on its own, pinned as it changes them", ${
	context("the FM swap (instruments::classic::remap_for_mode), through the menu's mode write", _ {
		it("swaps a new synth's filter button for the modulators' volumes, and back", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& sound = *static_cast<SoundInstrument*>(f.instrument);
			expect_synth_layout(self, f, kDefaultSynth);
			set_mode(sound, "fm");
			expect_synth_layout(self, f, kDefaultFm);
			set_mode(sound, "subtractive");
			expect_synth_layout(self, f, kDefaultSynth);
		});

		it("brings a high-pass, an EQ and a mixed pair on the filter button back as the low-pass pair", _ {
			const std::array<std::pair<deluge::modulation::KnobTarget, deluge::modulation::KnobTarget>, 3> pairs{{
			    {param_knob(params::LOCAL_HPF_RESONANCE), param_knob(params::LOCAL_HPF_FREQ)},
			    {unpatched_knob(params::UNPATCHED_BASS), unpatched_knob(params::UNPATCHED_TREBLE)},
			    {param_knob(params::LOCAL_HPF_RESONANCE), param_knob(params::LOCAL_LPF_FREQ)},
			}};
			// The pairs `instruments::classic::remap_for_mode` takes into FM: a resonance or the bass on the lower knob, any filter's upper.
			constexpr std::array<Layout, 3> kBefore{with_button1(kDefaultSynth, "hpfResonance", "hpfFrequency"),
			                                        with_button1(kDefaultSynth, "bass", "treble"),
			                                        with_button1(kDefaultSynth, "hpfResonance", "lpfFrequency")};
			for (size_t i = 0; i < pairs.size(); i++) {
				spec::SongFixture f = spec::make_synth_song();
				auto& sound = *static_cast<SoundInstrument*>(f.instrument);
				set_pair(self, sound, *f.song, 1, pairs[i].first, pairs[i].second);
				expect_synth_layout(self, f, kBefore[i]);
				set_mode(sound, "fm");
				expect_synth_layout(self, f, kDefaultFm);
				set_mode(sound, "subtractive");
				expect_synth_layout(self, f, kDefaultSynth);
			}
		});

		it("leaves a pair with the cutoff on the lower knob, as the filter cycle's group 1 has it", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& sound = *static_cast<SoundInstrument*>(f.instrument);
			set_pair(self, sound, *f.song, 1, param_knob(params::LOCAL_LPF_FREQ), param_knob(params::LOCAL_LPF_RESONANCE));
			// `instruments::classic::remap_for_mode` only takes a resonance (or the bass) on the lower knob into FM.
			constexpr Layout kCutoffLower = with_button1(kDefaultSynth, "lpfFrequency", "lpfResonance");
			expect_synth_layout(self, f, kCutoffLower);
			set_mode(sound, "fm");
			expect_synth_layout(self, f, kCutoffLower);
		});

		it("swaps a low-pass pair on any button, mod button 7 included", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& sound = *static_cast<SoundInstrument*>(f.instrument);
			set_pair(self, sound, *f.song, 7, param_knob(params::LOCAL_LPF_RESONANCE), param_knob(params::LOCAL_LPF_FREQ));
			constexpr Layout kOn7 = with_knob(with_knob(kDefaultSynth, 7, 0, "lpfResonance"), 7, 1, "lpfFrequency");
			expect_synth_layout(self, f, kOn7);
			set_mode(sound, "fm");
			// `instruments::classic::remap_for_mode` swaps the pair on every button.
			expect_synth_layout(
			    self, f, with_knob(with_knob(kDefaultFm, 7, 0, "modulator2Volume"), 7, 1, "modulator1Volume"));
		});

		it("passes over a button with a cable's depth on it, though the cable's param would match", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& sound = *static_cast<SoundInstrument*>(f.instrument);
			set_pair(self, sound, *f.song, 1,
			         deluge::modulation::knobTargetOf(
			             deluge::synth::CableTarget::depth_of(params::LOCAL_LPF_RESONANCE, PatchSource::LFO_LOCAL_1)),
			         param_knob(params::LOCAL_LPF_FREQ));
			// `instruments::classic::remap_for_mode` swaps a knob only when it is on the plain param, never on a cable's depth.
			constexpr Layout kCable = with_button1(kDefaultSynth, "lpfResonance<lfo2", "lpfFrequency");
			expect_synth_layout(self, f, kCable);
			set_mode(sound, "fm");
			expect_synth_layout(self, f, kCable);
		});

		it("turns a modulator-volume pair learned in FM into the low-pass pair on the way out", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& sound = *static_cast<SoundInstrument*>(f.instrument);
			set_mode(sound, "fm");
			set_pair(self, sound, *f.song, 3, param_knob(params::LOCAL_MODULATOR_1_VOLUME),
			         param_knob(params::LOCAL_MODULATOR_0_VOLUME));
			expect_synth_layout(
			    self, f, with_knob(with_knob(kDefaultFm, 3, 0, "modulator2Volume"), 3, 1, "modulator1Volume"));
			// Out of FM, `instruments::classic::remap_for_mode` brings the modulators' volumes back as the low-pass pair.
			set_mode(sound, "subtractive");
			expect_synth_layout(self, f,
			                    with_knob(with_knob(kDefaultSynth, 3, 0, "lpfResonance"), 3, 1, "lpfFrequency"));
		});

		it("writes the record and the engine both when the record resolves but its owner does not", _ {
			spec::SongFixture f = spec::make_synth_song();
			auto& sound = *static_cast<SoundInstrument*>(f.instrument);
			// The record stays in the project while the song's output list no longer holds the sound, so the
			// SetControls Edit finds no owner to apply to and refuses.
			Output* const outputs = f.song->live.media.first_output;
			f.song->live.media.first_output = nullptr;
			const bool changed = deluge::bridge::writeKnobPairs(
			    sound, *f.song, [](const deluge::instruments::classic::KnobPairs& knobs) {
				    deluge::instruments::classic::KnobPairs out = knobs;
				    out[2] = {.lower = deluge::instruments::classic::param::kModulator2Volume,
				              .upper = deluge::instruments::classic::param::kModulator1Volume};
				    return out;
			    });
			f.song->live.media.first_output = outputs;
			expect(changed).to_be_true();
			const auto* record = f.song->project.resolveControls(*deluge::bridge::controlsAddressOf(sound));
			expect(record).not_().to_be_null("the sound's controls record");
			const deluge::instruments::classic::KnobPair want{.lower = deluge::instruments::classic::param::kModulator2Volume,
			                                        .upper = deluge::instruments::classic::param::kModulator1Volume};
			if (record != nullptr) {
				expect(deluge::bridge::knobPairsOf(*record)[2]).to_equal(want, "the record's mod button 2");
			}
			expect(deluge::bridge::knobPairsOf(deluge::bridge::controlsOf(sound))[2])
			    .to_equal(want, "the engine's mod button 2");
		});

		it("swaps the drums of a kit the mode's fan-out reaches, and passes over a sample row", _ {
			spec::SongFixture f = spec::make_kit_song();
			deluge::ui::scenes::spec::make_current(f.song);
			SoundDrum* sample = spec::add_drum<SoundDrum>(f);
			SoundDrum* synth = spec::add_drum<SoundDrum>(f);
			// The menu's fan-out for the mode (`fan_out::kRingable`) takes a drum whose oscillators are both
			// ringable: the second row's are made saws, the first keeps its samples.
			deluge::edits::settingFor(*synth, "osc1/type", Value::makeScalar("saw"));
			deluge::edits::settingFor(*synth, "osc2/type", Value::makeScalar("saw"));
			// A sample row's knobs (`Sound::setupAsSample`): mod button 6's lower knob is the pitch adjust.
			constexpr Layout kRow = with_knob(kDefaultSynth, 6, 0, "pitch");
			constexpr Layout kRowFm = with_knob(kDefaultFm, 6, 0, "pitch");
			expect_row_layout(self, f, *sample, kRow);
			expect_row_layout(self, f, *synth, kRow);
			deluge::edits::Context ctx{*f.song, f.song->project};
			deluge::edits::SetKitSetting fm{*f.instrument->id(), "mode", Value::makeScalar("fm"),
			                                deluge::ui::scenes::menu::fan_out::kRingable};
			expect(int(deluge::edits::apply(ctx, fm))).to_equal(int(Error::NONE), "the kit's FM mode edit");
			expect(fm.drums_written()).to_equal(1u);
			expect_row_layout(self, f, *sample, kRow);
			expect_row_layout(self, f, *synth, kRowFm);
			deluge::edits::SetKitSetting back{*f.instrument->id(), "mode", Value::makeScalar("subtractive"),
			                                  deluge::ui::scenes::menu::fan_out::kRingable};
			expect(int(deluge::edits::apply(ctx, back))).to_equal(int(Error::NONE), "the kit's subtractive mode edit");
			expect(back.drums_written()).to_equal(1u);
			expect_row_layout(self, f, *sample, kRow);
			expect_row_layout(self, f, *synth, kRow);
			deluge::ui::scenes::spec::make_current(nullptr);
		});

		it("swaps a sound with no slot in the song, which has no record", _ {
			spec::SongFixture f = spec::make_synth_song();
			deluge::ui::scenes::spec::make_current(f.song);
			ParamManagerForTimeline params;
			auto* loose = static_cast<SoundInstrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
			loose->setupAsDefaultSynth(&params);
			expect(deluge::bridge::controlsAddressOf(*loose).has_value()).to_be_false();
			const auto write_preset = [&] {
				Serializer& writer = GetSerializer();
				writer.writeOpeningTagBeginning("sound", true);
				loose->Sound::writeToFile(writer, false, &params, nullptr, nullptr, nullptr);
				writer.writeClosingTag("sound", true, true);
			};
			expect_layout(self, *loose, nullptr, write_preset, kDefaultSynth);
			set_mode(*loose, "fm");
			expect_layout(self, *loose, nullptr, write_preset, kDefaultFm);
			set_mode(*loose, "subtractive");
			expect_layout(self, *loose, nullptr, write_preset, kDefaultSynth);
			deluge::ui::scenes::spec::make_current(nullptr);
		});
	});

	context("a kit row made from a sample (Sound::setupAsSample)", _ {
		it("gives a new row from the clip view the pitch adjust on mod button 6, which keeps it from being zeroed", _ {
			deluge::ui::scenes::spec::PopupCapture popups;
			spec::SongFixture f = spec::make_kit_song();
			deluge::ui::scenes::spec::make_current(f.song);
			auto* kit = static_cast<Kit*>(f.instrument);
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, f.song, f.clip);
			int32_t index = 0;
			NoteRow* row = f.clip->createNewNoteRowForKit(stack, /*atStart=*/false, &index);
			SoundDrum* drum = instrumentClipView.createDrumForRow(stack->addNoteRow(index, row), false);
			expect(drum).not_().to_be_null("the row's new drum");
			if (drum == nullptr) {
				return;
			}
			expect_row_layout(self, f, *drum, with_knob(kDefaultSynth, 6, 0, "pitch"));

			// `Kit::setupPatching` with the clip asks each row's cable set for the pitch adjusts, which holds none of
			// them, so it zeroes nothing; without a clip it runs
			// `Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails`, which zeroes a pitch
			// adjust no knob names. The pitch adjust on mod button 6 is what keeps a sample row's from being zeroed.
			PatchedParamSet* patched = row->paramManager.getPatchedParamSet();
			const auto seed = [&] {
				for (const int32_t p : {params::LOCAL_PITCH_ADJUST, params::LOCAL_OSC_A_PITCH_ADJUST}) {
					patched->params[p].setBase(*patched->docForParam(p), 1 << 26);
				}
			};
			seed();
			alignas(ModelStackWithAutoParam) char patch_memory[MODEL_STACK_MAX_SIZE];
			// This pins only that the clip arm zeroes nothing; the minimal-details pass below is the assertion that tells
			// a knobbed pitch adjust from one without a knob.
			kit->setupPatching(setupModelStackWithTimelineCounter(patch_memory, f.song, f.clip));
			expect(patched->getValue(params::LOCAL_PITCH_ADJUST)).to_equal(1 << 26);
			expect(patched->getValue(params::LOCAL_OSC_A_PITCH_ADJUST)).to_equal(1 << 26);
			drum->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&row->paramManager, f.song);
			expect(patched->getValue(params::LOCAL_PITCH_ADJUST)).to_equal(1 << 26);
			expect(patched->getValue(params::LOCAL_OSC_A_PITCH_ADJUST)).to_equal(0);

			// With the knob moved back to the portamento, the same pass zeroes the pitch adjust too.
			set_pair(self, *drum, *f.song, 6, unpatched_knob(params::UNPATCHED_PORTAMENTO),
			         unpatched_knob(params::UNPATCHED_STUTTER_RATE));
			seed();
			drum->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&row->paramManager, f.song);
			expect(patched->getValue(params::LOCAL_PITCH_ADJUST)).to_equal(0);
			deluge::ui::scenes::spec::make_current(nullptr);
		});
	});

	context("an owner with no knobs", _ {
		it("moves nothing on a CV track", _ {
			spec::SongFixture f = spec::make_cv_song();
			make_active(f);
			expect_no_knobs(self, f, nullptr);
		});

		it("moves nothing on a kit's MIDI drum, and the drum's own channel and note still turn while it auditions", _ {
			spec::SongFixture f;
			MIDIDrum* drum = kit_with_drum<MIDIDrum>(f);
			NoteRow* row = f.clip->getNoteRowForDrum(drum);
			expect_no_knobs(self, f, row != nullptr ? &row->paramManager : nullptr);

			deluge::ui::scenes::spec::PopupCapture popups;
			deluge::ui::scenes::spec::make_current(f.song);
			setRootUILowLevel(&instrumentClipView);
			view.setActiveModControllableTimelineCounter(f.clip, false);
			const uint8_t channel = drum->channel();
			const uint8_t note = drum->note();
			const std::vector<int32_t> clip_before = param_snapshot(&f.clip->paramManager);
			currentUIMode = UI_MODE_AUDITIONING;
			turn_one_step(0);
			turn_one_step(1);
			currentUIMode = UI_MODE_NONE;
			expect(static_cast<int32_t>(drum->channel())).to_equal((channel + 1) % 16);
			expect(static_cast<int32_t>(drum->note())).to_equal((note + 1) % 128);
			expect(param_snapshot(&f.clip->paramManager)).to_equal(clip_before, "the clip's params");
			nullifyUIs();
			view.releaseActiveModControllable();
			deluge::ui::scenes::spec::make_current(nullptr);
		});

		it("moves nothing on a kit's gate drum, and the drum's own channel still turns while it auditions", _ {
			spec::SongFixture f;
			GateDrum* drum = kit_with_drum<GateDrum>(f);
			NoteRow* row = f.clip->getNoteRowForDrum(drum);
			expect_no_knobs(self, f, row != nullptr ? &row->paramManager : nullptr);

			deluge::ui::scenes::spec::PopupCapture popups;
			deluge::ui::scenes::spec::make_current(f.song);
			setRootUILowLevel(&instrumentClipView);
			view.setActiveModControllableTimelineCounter(f.clip, false);
			const uint8_t channel = drum->channel();
			const std::vector<int32_t> clip_before = param_snapshot(&f.clip->paramManager);
			currentUIMode = UI_MODE_AUDITIONING;
			turn_one_step(0);
			turn_one_step(1);
			currentUIMode = UI_MODE_NONE;
			expect(static_cast<int32_t>(drum->channel())).to_equal((channel + 1) % NUM_GATE_CHANNELS);
			expect(param_snapshot(&f.clip->paramManager)).to_equal(clip_before, "the clip's params");
			nullifyUIs();
			view.releaseActiveModControllable();
			deluge::ui::scenes::spec::make_current(nullptr);
		});
	});
});
// clang-format on

CPPSPEC_SPEC(layout_characterisation)
