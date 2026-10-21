#include "engines/deluge/codec.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::instrument;
using deluge::serialization::Value;

namespace {

/// The top-level keys encode writes, in file order (codec.cpp's kStateKeys).
const std::vector<std::string_view> kKeys{
    "polyphonic",     "voicePriority", "sideChainSend", "mode",       "transpose",
    "cents",          "modFXType",     "lpfMode",       "hpfMode",    "filterRoute",
    "clippingAmount", "maxVoices",     "osc1",          "osc2",       "lfo1",
    "lfo2",           "lfo3",          "lfo4",          "modulator1", "modulator2",
    "unison",         "midiOutput",    "delay",         "sidechain",  "audioCompressor",
    "stutter"};

/// States across what each entry's encoding depends on.
std::vector<std::pair<std::string, SoundState>> matrix() {
	std::vector<std::pair<std::string, SoundState>> states;
	states.emplace_back("new", new_synth_state());

	SoundState fm = new_synth_state();
	fm.synth_mode = SynthMode::FM;
	fm.modulator_transpose = {3, -2};
	fm.modulator1_to_modulator0 = true;
	states.emplace_back("fm", fm);

	SoundState ring = new_synth_state();
	ring.synth_mode = SynthMode::RINGMOD;
	ring.sources[1].osc_type = OscType::SAW;
	states.emplace_back("ringmod", ring);

	SoundState sample = new_synth_state();
	sample.sources[0].osc_type = OscType::SAMPLE;
	sample.sources[0].repeat_mode = SampleRepeatMode::ONCE;
	sample.sources[0].ranges = {RangeState{.top_note = 60, .file_path = "SAMPLES/a.wav", .end_pos = 100},
	                            RangeState{.top_note = kOpenTopNote, .file_path = "SAMPLES/b.wav", .end_pos = 200}};
	states.emplace_back("ranged sample", sample);

	SoundState dx = new_synth_state();
	dx.sources[1].osc_type = OscType::DX7;
	dx.sources[1].dx_patch = DxPatchState{.engine_mode = 2, .random_detune = 5};
	dx.sources[1].dx_patch->params[7] = 42;
	states.emplace_back("dx7", dx);

	SoundState wavetable = new_synth_state();
	wavetable.sources[0].osc_type = OscType::WAVETABLE;
	wavetable.sources[0].ranges = {RangeState{.top_note = kOpenTopNote, .file_path = "WAVETABLES/w.wav"}};
	wavetable.oscillator_sync = true;
	states.emplace_back("wavetable", wavetable);

	SoundState synced = new_synth_state();
	synced.lfo_sync_level[1] = deluge::engine::SyncLevel::EIGHTH;
	synced.effects.delay.sync_level = deluge::engine::SyncLevel::QUARTER;
	synced.effects.sidechain.sync_level = deluge::engine::SyncLevel::SIXTEENTH;
	states.emplace_back("synced", synced);

	SoundState nonzero = new_synth_state();
	nonzero.transpose = 5;
	nonzero.cents = -7;
	nonzero.side_chain_send_level = 1234;
	nonzero.effects.saturation.clipping_amount = 4;
	nonzero.effects.stutter.use_song_stutter = false;
	nonzero.effects.filter.lpf_mode = deluge::effects::FilterMode::OFF;
	nonzero.effects.mod_fx.type = deluge::effects::ModFXType::CHORUS;
	nonzero.effects.compressor.ratio = 7 << 24;
	states.emplace_back("non-zero", nonzero);
	return states;
}

/// A slot around the instrument's entries: the sound's params before them and its arpeggiator after.
Value slot_of(const SoundState& state, const deluge::engine::CodecContext& codec) {
	Value base = Value::makeMap();
	Value params = Value::makeMap();
	params.set("volume", Value::makeScalar("0x00000000"));
	base.set("defaultParams", std::move(params));
	Value arp = Value::makeMap();
	arp.set("mode", Value::makeScalar("off"));
	base.set("arpeggiator", std::move(arp));
	return with_state(base, state, codec);
}

/// The keys whose encoding differs between @p a and @p b, present in one alone included, in file order.
std::vector<std::string_view> differing(const SoundState& a, const SoundState& b,
                                        const deluge::engine::CodecContext& codec) {
	const Value ea = encode(a, codec);
	const Value eb = encode(b, codec);
	std::vector<std::string_view> keys;
	for (const std::string_view key : kKeys) {
		const Value* va = ea.find(key);
		const Value* vb = eb.find(key);
		if ((va == nullptr) != (vb == nullptr) || (va != nullptr && !(*va == *vb))) {
			keys.push_back(key);
		}
	}
	return keys;
}

} // namespace

// clang-format off
describe codec_entry("one instrument entry's encoding, and a slot's entries replaced in place", ${
	it("encodes each entry as the whole encoding holds it", _ {
		std::vector<std::string> wrong;
		for (const int32_t magnitude : {1, 3}) {
			const deluge::engine::CodecContext codec{.input_tick_magnitude = magnitude};
			for (const auto& [name, state] : matrix()) {
				const Value whole = encode(state, codec);
				for (const std::string_view key : kKeys) {
					const auto entry = encode_entry(state, key, codec);
					const Value* held = whole.find(key);
					if (entry.has_value() != (held != nullptr) || (held != nullptr && !(*entry == *held))) {
						wrong.push_back(name + "/" + std::string(key) + "@" + std::to_string(magnitude));
					}
				}
			}
		}
		expect(wrong).to_equal(std::vector<std::string>{});
	});

	it("replaces only the entries asked, as with_state replaces them all", _ {
		std::vector<std::string> wrong;
		for (const int32_t magnitude : {1, 3}) {
			const deluge::engine::CodecContext codec{.input_tick_magnitude = magnitude};
			const auto states = matrix();
			for (const auto& [from_name, from] : states) {
				for (const auto& [to_name, to] : states) {
					Value slot = slot_of(from, codec);
					with_entries(slot, to, differing(from, to, codec), codec);
					if (!(slot == with_state(slot_of(from, codec), to, codec))) {
						wrong.push_back(from_name + "->" + to_name + "@" + std::to_string(magnitude));
					}
				}
			}
		}
		expect(wrong).to_equal(std::vector<std::string>{});
	});

	it("replaces every entry as with_state does when asked for all", _ {
		const deluge::engine::CodecContext codec{.input_tick_magnitude = 1};
		for (const auto& [name, state] : matrix()) {
			Value slot = slot_of(new_synth_state(), codec);
			with_entries(slot, state, kKeys, codec);
			expect(slot == with_state(slot_of(new_synth_state(), codec), state, codec)).to_be_true();
		}
	});

	it("encodes nothing for a key the instrument does not own", _ {
		const deluge::engine::CodecContext codec{.input_tick_magnitude = 1};
		expect(encode_entry(new_synth_state(), "defaultParams", codec).has_value()).to_be_false();
		expect(encode_entry(new_synth_state(), "compressor", codec).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(codec_entry)
