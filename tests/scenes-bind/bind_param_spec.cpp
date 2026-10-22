#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/param_display.h"
#include "scenes/bind/target.h"
#include "support/model_fixture.h"

#include <array>
#include <cppspec.hpp>
#include <limits>

using namespace deluge::scenes::bind;
using deluge::engine::ParamDisplay;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
using namespace deluge::scenes::bind::test;

namespace {

constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();

constexpr std::array<int32_t, 1> kFm{1}; // SynthMode::FM, as the "mode" row stores it
constexpr std::array kOnFm{deluge::engine::setting_is("mode", kFm)};
constexpr std::array<int32_t, 1> kLpfOff{5}; // "Off" in the "lpfMode" row
constexpr std::array kLpfOn{deluge::engine::setting_is_not("lpfMode", kLpfOff)};

constexpr Target kSoundLpf{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kLpfFrequency};
constexpr Target kChainLpf{.kind = Target::Kind::PARAM,
                           .param = deluge::global_effectable::param::kLpfFrequency,
                           .param_table = ParamTable::CHAIN};
constexpr Target kSoundBass{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kBass};
constexpr Target kPortamento{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kPortamento};
constexpr Target kChainTempo{
    .kind = Target::Kind::PARAM, .param = deluge::global_effectable::param::kTempo, .param_table = ParamTable::CHAIN};
constexpr Target kSoundPan{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kPan};
constexpr Target kPulseWidth{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kOscAPhaseWidth};
constexpr Target kFmOnly{
    .kind = Target::Kind::PARAM, .param = deluge::instrument::param::kLpfFrequency, .applies_when = kOnFm};
constexpr Target kWithFm{.kind = Target::Kind::PARAM, .param = deluge::instrument::param::kPan, .shown_with = &kFmOnly};
constexpr Target kChainLpfOn{.kind = Target::Kind::PARAM,
                             .param = deluge::global_effectable::param::kLpfFrequency,
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
		expect(menu_of(ParamDisplay::STANDARD, kMin)).to_equal(0);
		expect(menu_of(ParamDisplay::STANDARD, 0)).to_equal(25);
		expect(menu_of(ParamDisplay::STANDARD, kMax)).to_equal(50);
		expect(stored_of(ParamDisplay::STANDARD, 0)).to_equal(kMin);
		expect(stored_of(ParamDisplay::STANDARD, 25)).to_equal(-23);
		expect(stored_of(ParamDisplay::STANDARD, 50)).to_equal(kMax);
		expect(menu_of(ParamDisplay::HALF_PRECISION, 0)).to_equal(0);
		expect(menu_of(ParamDisplay::HALF_PRECISION, kMax)).to_equal(50);
		expect(stored_of(ParamDisplay::HALF_PRECISION, 0)).to_equal(0);
		expect(stored_of(ParamDisplay::HALF_PRECISION, 25)).to_equal(1073741812);
		expect(stored_of(ParamDisplay::HALF_PRECISION, 50)).to_equal(kMax);
		expect(menu_of(ParamDisplay::PAN, kMin)).to_equal(-25);
		expect(menu_of(ParamDisplay::PAN, 0)).to_equal(0);
		expect(menu_of(ParamDisplay::PAN, kMax)).to_equal(25);
		expect(stored_of(ParamDisplay::PAN, -25)).to_equal(kMin);
		expect(stored_of(ParamDisplay::PAN, 10)).to_equal(858993440);
		expect(stored_of(ParamDisplay::PAN, 25)).to_equal(kMax);
		expect(menu_range(ParamDisplay::STANDARD)).to_equal(std::pair{0, 50});
		expect(menu_range(ParamDisplay::HALF_PRECISION)).to_equal(std::pair{0, 50});
		expect(menu_range(ParamDisplay::PAN)).to_equal(std::pair{-25, 25});
	});

	it("addresses the param on the owner each root names", _ {
		const auto owner_of = [](const Target& t, const Addresses& where) {
			const auto address = param_address(t, where);
			return address ? std::optional{std::pair{address->owner, address->clip_id}} : std::nullopt;
		};
		expect(owner_of(kSoundLpf, kOnSynth) == std::optional{std::pair{OwnerId::instrument(kSynth.id),
		                                                                 std::optional{kSynthClip.clip_id}}})
		    .to_be_true();
		expect(owner_of(kSoundLpf, kOnKitRow)
		       == std::optional{std::pair{OwnerId::drum(kDrum.id), std::optional{kKitClip.clip_id}}})
		    .to_be_true();
		expect(owner_of(kChainLpf, kOnKitFx)
		       == std::optional{std::pair{OwnerId::instrument(kKitChain.id), std::optional{kKitClip.clip_id}}})
		    .to_be_true();
		expect(owner_of(kChainLpf, kOnAudio)
		       == std::optional{std::pair{OwnerId::clip(kAudioClip.clip_id), std::optional<uint32_t>{}}})
		    .to_be_true();
		expect(owner_of(kChainLpf, kOnSong) == std::optional{std::pair{OwnerId::song(), std::optional<uint32_t>{}}})
		    .to_be_true();
		expect(param_address(kSoundLpf, Addresses{}).has_value()).to_be_false();
		expect(param_address(kSoundLpf, kOnSynth)->id == deluge::instrument::param::kLpfFrequency).to_be_true();
	});

	it("names a param on the other kind of owner only where that owner's table holds it by the same name", _ {
		// A sound's and a chain's ids are their own tables': a sound's portamento has the id a chain's mod FX rate does.
		expect(deluge::instrument::param::kPortamento == deluge::global_effectable::param::kModFXRate).to_be_true();
		expect(param_address(kPortamento, kOnKitFx).has_value()).to_be_false();
		ModelFixture f;
		expect(view(kPortamento, f.song, kOnKitFx, f.facts).has_value()).to_be_false();
		expect(request_for(kPortamento, f.song, kOnKitFx, f.facts, 10).has_value()).to_be_false();
		expect(param_address(kChainTempo, kOnSynth).has_value()).to_be_false();
		expect(param_address(kSoundBass, kOnSong)->id == deluge::global_effectable::param::kBass).to_be_true();
		expect(param_address(kChainLpf, kOnSynth)->id == deluge::instrument::param::kLpfFrequency).to_be_true();
	});

	it("shows the value the param plays, in its display's range", _ {
		ModelFixture f;
		f.facts.param_values.push_back({*param_address(kSoundPan, kOnSynth), kMax});
		f.facts.param_values.push_back({*param_address(kPulseWidth, kOnSynth), kMax});
		f.facts.param_values.push_back({*param_address(kChainLpf, kOnSong), kMin});
		const auto pan = view(kSoundPan, f.song, kOnSynth, f.facts);
		expect(pan.has_value() && pan->applies && pan->value == 25 && pan->least == -25 && pan->greatest == 25)
		    .to_be_true();
		const auto width = view(kPulseWidth, f.song, kOnSynth, f.facts);
		expect(width.has_value() && width->value == 50 && width->least == 0 && width->greatest == 50).to_be_true();
		const auto lpf = view(kChainLpf, f.song, kOnSong, f.facts);
		expect(lpf.has_value() && lpf->value == 0).to_be_true();
		// A param the runtime does not hold shows the value a new owner starts with.
		const auto unheld = view(kSoundLpf, f.song, kOnSynth, f.facts);
		const auto* lpf_row = deluge::engine::find_param(deluge::instrument::kParams, kSoundLpf.param);
		expect(unheld.has_value() && unheld->value == menu_of(lpf_row->display, lpf_row->default_value)).to_be_true();
	});

	it("applies where its conditions hold over the owner's rows, and where its partner shows", _ {
		ModelFixture f;
		for (const Target* t : {&kFmOnly, &kWithFm}) {
			f.facts.param_values.push_back({*param_address(*t, kOnSynth), 0});
		}
		f.facts.param_values.push_back({*param_address(kChainLpfOn, kOnKitFx), 0});
		const auto applies = [&](const Target& t, const Addresses& where) {
			const auto shown = view(t, f.song, where, f.facts);
			return shown.has_value() && shown->applies;
		};
		f.facts.states[kSynth.id].synth_mode = deluge::instrument::SynthMode::SUBTRACTIVE;
		expect(applies(kFmOnly, kOnSynth)).to_be_false();
		expect(applies(kWithFm, kOnSynth)).to_be_false();
		f.facts.states[kSynth.id].synth_mode = deluge::instrument::SynthMode::FM;
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
		const auto request = request_for(kSoundPan, f.song, kOnSynth, f.facts, 10);
		expect(request.has_value() && request->menu_value == 10 && request->param_value == 858993440).to_be_true();
		expect(request_for(kSoundPan, f.song, kOnSynth, f.facts, 26).has_value()).to_be_false();
	});
});

CPPSPEC_SPEC(bind_param)
