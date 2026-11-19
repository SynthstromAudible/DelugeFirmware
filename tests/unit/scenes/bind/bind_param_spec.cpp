#include "effects/host_chain/params.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/params.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/param_display.h"
#include "scenes/bind/target.h"
#include "support/model_fixture.h"

#include <array>
#include <cppspec.hpp>
#include <limits>

#include "support/printers/owner_id_printer.h"
#include "support/printers/param_id_printer.h"

using namespace deluge::scenes::bind;
using deluge::plugin::MappingKind;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using namespace deluge::scenes::bind::test;

namespace {

constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();

constexpr std::array<int32_t, 1> kFm{1}; // SynthMode::FM, as the "mode" row stores it
constexpr std::array kOnFm{deluge::plugin::setting_is("mode", kFm)};
constexpr std::array<int32_t, 1> kLpfOff{5}; // "Off" in the "lpfMode" row
constexpr std::array kLpfOn{deluge::plugin::setting_is_not("lpfMode", kLpfOff)};

constexpr Target kSoundLpf{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kLpfFrequency};
constexpr Target kChainLpf{
    .kind = Target::Kind::PARAM, .param = deluge::host_chain::param::kLpfFrequency, .param_table = ParamTable::CHAIN};
constexpr Target kSoundBass{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kBass};
constexpr Target kPortamento{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kPortamento};
constexpr Target kChainTempo{
    .kind = Target::Kind::PARAM, .param = deluge::host_chain::param::kTempo, .param_table = ParamTable::CHAIN};
constexpr Target kSoundPan{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kPan};
constexpr Target kPulseWidth{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kOscAPhaseWidth};
constexpr Target kFmOnly{
    .kind = Target::Kind::PARAM, .param = deluge::instrument::param::kLpfFrequency, .applies_when = kOnFm};
constexpr Target kWithFm{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kPan, .shown_with = &kFmOnly};
constexpr Target kChainLpfOn{.kind = Target::Kind::PARAM,
                             .param = deluge::host_chain::param::kLpfFrequency,
                             .param_table = ParamTable::CHAIN,
                             .applies_when = kLpfOn};

const Addresses kOnSynth{.slot = kSynth, .clip = kSynthClip};
const Addresses kOnKitRow{.slot = kDrum, .clip = kKitClip};
const Addresses kOnKitFx{.chain = kKitChain, .chain_owner = ChainOwner::KIT, .clip = kKitClip};
const Addresses kOnAudio{.chain = kAudioChain, .chain_owner = ChainOwner::AUDIO_TRACK, .clip = kAudioClip};
const Addresses kOnSong{.chain = kMasterChain, .chain_owner = ChainOwner::SONG};

} // namespace

// clang-format off
describe bind_param("a param target", ${
	it("converts each display's stored words and menu values as legacy's value scaling does", _ {
		expect(menu_of(MappingKind::STANDARD, kMin)).to_equal(0);
		expect(menu_of(MappingKind::STANDARD, 0)).to_equal(25);
		expect(menu_of(MappingKind::STANDARD, kMax)).to_equal(50);
		expect(stored_of(MappingKind::STANDARD, 0)).to_equal(kMin);
		expect(stored_of(MappingKind::STANDARD, 25)).to_equal(-23);
		expect(stored_of(MappingKind::STANDARD, 50)).to_equal(kMax);
		expect(menu_of(MappingKind::UNIPOLAR, 0)).to_equal(0);
		expect(menu_of(MappingKind::UNIPOLAR, kMax)).to_equal(50);
		expect(stored_of(MappingKind::UNIPOLAR, 0)).to_equal(0);
		expect(stored_of(MappingKind::UNIPOLAR, 25)).to_equal(1073741812);
		expect(stored_of(MappingKind::UNIPOLAR, 50)).to_equal(kMax);
		expect(menu_of(MappingKind::PAN, kMin)).to_equal(-25);
		expect(menu_of(MappingKind::PAN, 0)).to_equal(0);
		expect(menu_of(MappingKind::PAN, kMax)).to_equal(25);
		expect(stored_of(MappingKind::PAN, -25)).to_equal(kMin);
		expect(stored_of(MappingKind::PAN, 10)).to_equal(858993440);
		expect(stored_of(MappingKind::PAN, 25)).to_equal(kMax);
		expect(menu_range(MappingKind::STANDARD)).to_equal(std::pair{0, 50});
		expect(menu_range(MappingKind::UNIPOLAR)).to_equal(std::pair{0, 50});
		expect(menu_range(MappingKind::PAN)).to_equal(std::pair{-25, 25});
	});

	it("addresses the param on the owner each root names", _ {
		const auto owner_of = [](const Target& t, const Addresses& where) {
			const auto address = param_address(t, where);
			return address ? std::optional{std::pair{address->owner, address->clip_id}} : std::nullopt;
		};
		expect(owner_of(kSoundLpf, kOnSynth))
		    .to_equal(std::optional{std::pair{OwnerId::instrument(kSynth.id), std::optional{kSynthClip.clip_id}}});
		expect(owner_of(kSoundLpf, kOnKitRow))
		    .to_equal(std::optional{std::pair{OwnerId::drum(kDrum.id), std::optional{kKitClip.clip_id}}});
		expect(owner_of(kChainLpf, kOnKitFx))
		    .to_equal(std::optional{std::pair{OwnerId::instrument(kKitChain.id), std::optional{kKitClip.clip_id}}});
		expect(owner_of(kChainLpf, kOnAudio))
		    .to_equal(std::optional{std::pair{OwnerId::clip(kAudioClip.clip_id), std::optional<uint32_t>{}}});
		expect(owner_of(kChainLpf, kOnSong))
		    .to_equal(std::optional{std::pair{OwnerId::song(), std::optional<uint32_t>{}}});
		expect(param_address(kSoundLpf, Addresses{}).has_value()).to_be_false();
		expect(param_address(kSoundLpf, kOnSynth)->id).to_equal(deluge::instrument::param::kLpfFrequency);
	});

	it("names a param on the other kind of owner only where that owner's table holds it by the same name", _ {
		// A sound's and a chain's ids are their own tables': a sound's portamento has the id a chain's mod FX rate does.
		expect(deluge::instrument::param::kPortamento).to_equal(deluge::host_chain::param::kModFXRate);
		expect(param_address(kPortamento, kOnKitFx).has_value()).to_be_false();
		ModelFixture f;
		expect(view(kPortamento, f.song, kOnKitFx, f.facts, f.audio, f.sound).has_value()).to_be_false();
		expect(request_for(kPortamento, f.song, kOnKitFx, f.facts, f.audio, f.sound, 10).has_value()).to_be_false();
		expect(param_address(kChainTempo, kOnSynth).has_value()).to_be_false();
		expect(param_address(kSoundBass, kOnSong)->id).to_equal(deluge::host_chain::param::kBass);
		expect(param_address(kChainLpf, kOnSynth)->id).to_equal(deluge::instrument::param::kLpfFrequency);
	});

	it("addresses a cable on a sound's slot and nowhere on a chain", _ {
		constexpr deluge::plugin::ParamId depth{deluge::instrument::kCableDepthFlag | 24u | (1u << 8)};
		const Target cable{.kind = Target::Kind::CABLE, .param = depth};
		const auto on_synth = param_address(cable, kOnSynth);
		expect(on_synth.has_value()).to_be_true();
		if (on_synth.has_value()) {
			expect(on_synth->id).to_equal(depth);
			expect(on_synth->owner).to_equal(OwnerId::instrument(kSynth.id));
		}
		expect(param_address(cable, kOnKitRow).has_value()).to_be_true();
		expect(param_address(cable, kOnKitFx).has_value()).to_be_false();
		expect(param_address(cable, kOnSong).has_value()).to_be_false();
	});

	it("shows the value the param plays, in its display's range", _ {
		ModelFixture f;
		f.facts.param_values.push_back({*param_address(kSoundPan, kOnSynth), kMax});
		f.facts.param_values.push_back({*param_address(kPulseWidth, kOnSynth), kMax});
		f.facts.param_values.push_back({*param_address(kChainLpf, kOnSong), kMin});
		const auto pan = view(kSoundPan, f.song, kOnSynth, f.facts, f.audio, f.sound);
		expect(pan.has_value()).to_be_true();
		if (pan.has_value()) {
			expect(pan->applies).to_be_true();
			expect(pan->value).to_equal(25);
			expect(pan->least).to_equal(-25);
			expect(pan->greatest).to_equal(25);
		}
		const auto width = view(kPulseWidth, f.song, kOnSynth, f.facts, f.audio, f.sound);
		expect(width.has_value()).to_be_true();
		if (width.has_value()) {
			expect(width->value).to_equal(50);
			expect(width->least).to_equal(0);
			expect(width->greatest).to_equal(50);
		}
		const auto lpf = view(kChainLpf, f.song, kOnSong, f.facts, f.audio, f.sound);
		expect(lpf.has_value()).to_be_true();
		if (lpf.has_value()) {
			expect(lpf->value).to_equal(0);
		}
		// A param the runtime does not hold shows the value a new owner starts with.
		const auto unheld = view(kSoundLpf, f.song, kOnSynth, f.facts, f.audio, f.sound);
		const auto* lpf_row = deluge::plugin::find_param(deluge::instrument::kParams, kSoundLpf.param);
		expect(unheld.has_value()).to_be_true();
		if (unheld.has_value()) {
			expect(unheld->value).to_equal(menu_of(lpf_row->mapping, lpf_row->default_value));
		}
	});

	it("applies where its conditions hold over the owner's rows, and where its partner shows", _ {
		ModelFixture f;
		for (const Target* t : {&kFmOnly, &kWithFm}) {
			f.facts.param_values.push_back({*param_address(*t, kOnSynth), 0});
		}
		f.facts.param_values.push_back({*param_address(kChainLpfOn, kOnKitFx), 0});
		const auto applies = [&](const Target& t, const Addresses& where) {
			const auto shown = view(t, f.song, where, f.facts, f.audio, f.sound);
			return shown.has_value() && shown->applies;
		};
		f.sound.states[kSynth.id].synth_mode = deluge::instrument::SynthMode::SUBTRACTIVE;
		expect(applies(kFmOnly, kOnSynth)).to_be_false();
		expect(applies(kWithFm, kOnSynth)).to_be_false();
		f.sound.states[kSynth.id].synth_mode = deluge::instrument::SynthMode::FM;
		expect(applies(kFmOnly, kOnSynth)).to_be_true();
		expect(applies(kWithFm, kOnSynth)).to_be_true();
		f.facts.chains[FakeFacts::chain_key(kKitChain)].filter.lpf_mode = deluge::effects::FilterMode::OFF;
		expect(applies(kChainLpfOn, kOnKitFx)).to_be_false();
		f.facts.chains[FakeFacts::chain_key(kKitChain)].filter.lpf_mode = deluge::effects::FilterMode::SVF_BAND;
		expect(applies(kChainLpfOn, kOnKitFx)).to_be_true();
	});

	it("asks for the stored word of a menu value, within the display's range", _ {
		ModelFixture f;
		f.facts.param_values.push_back({*param_address(kSoundPan, kOnSynth), 0});
		const auto request = request_for(kSoundPan, f.song, kOnSynth, f.facts, f.audio, f.sound, 10);
		expect(request.has_value()).to_be_true();
		if (request.has_value()) {
			expect(request->menu_value).to_equal(10);
			expect(request->param_value).to_equal(858993440);
		}
		expect(request_for(kSoundPan, f.song, kOnSynth, f.facts, f.audio, f.sound, 26).has_value()).to_be_false();
	});
});

CPPSPEC_SPEC(bind_param)
