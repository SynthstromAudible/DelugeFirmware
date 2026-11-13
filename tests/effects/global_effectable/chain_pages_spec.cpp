// clang-format off
#include "effects/global_effectable/pages.h"
#include "effects/global_effectable/params.h"
#include "effects/setting_params.h"
#include <cppspec.hpp>

using namespace deluge;
namespace ge = deluge::global_effectable::param;
namespace st = deluge::effects::setting;
using deluge::plugin::PressAction;
using deluge::global_effectable::section_pages;

describe chain_pages("the host chains' pages", ${
	it("puts the volume over the pan on the first mod button", _{
		const auto pages = section_pages(0, {});
		expect(pages.size()).to_equal(size_t{1});
		expect(*pages[0].slots[0].param == ge::kPan).to_be_true();
		expect(*pages[0].slots[1].param == ge::kVolume).to_be_true();
	});
	it("cycles low-pass, high-pass, EQ from the upper knob, the lower stepping the mode", _{
		const auto pages = section_pages(1, {});
		expect(pages.size()).to_equal(size_t{3});
		expect(pages[0].slots[1].press == PressAction::NEXT_PAGE).to_be_true();
		expect(*pages[0].slots[0].press_param == st::kLpfMode).to_be_true();
		expect(*pages[1].slots[0].press_param == st::kHpfMode).to_be_true();
		expect(pages[2].slots[0].press == PressAction::NONE).to_be_true();
		expect(*pages[2].slots[1].param == ge::kTreble).to_be_true();
	});
	it("has no page on the envelopes' button", _{
		expect(section_pages(2, {}).empty()).to_be_true();
	});
	it("steps the delay's settings, the alternative knobs the sync ones", _{
		auto pages = section_pages(3, {});
		expect(*pages[0].slots[1].press_param == st::kDelayPingPong).to_be_true();
		expect(*pages[0].slots[0].press_param == st::kDelayType).to_be_true();
		pages = section_pages(3, {.alt_delay = true});
		expect(*pages[0].slots[1].press_param == st::kDelaySyncType).to_be_true();
		expect(*pages[0].slots[0].press_param == st::kDelaySyncLevel).to_be_true();
	});
	it("gives a clip's reverb button the reverb preset and the sidechain", _{
		const auto pages = section_pages(4, {});
		expect(pages.size()).to_equal(size_t{1});
		expect(*pages[0].slots[0].param == ge::kReverbAmount).to_be_true();
		expect(pages[0].slots[0].press == PressAction::REVERB_PRESET).to_be_true();
		expect(*pages[0].slots[1].param == ge::kSidechainCompressorVolume).to_be_true();
		expect(*pages[0].slots[1].press_param == st::kSidechainSyncLevel).to_be_true();
	});
	it("gives the song a reverb group and a five-page compressor group", _{
		const auto pages = section_pages(4, {.song = true});
		expect(pages.size()).to_equal(size_t{6});
		expect(pages[0].group).to_equal(uint8_t{0});
		expect(*pages[0].slots[1].param == st::kCompThreshold).to_be_true();
		expect(pages[0].slots[1].press == PressAction::TOGGLE_GROUP).to_be_true();
		expect(pages[0].slots[0].press == PressAction::REVERB_PRESET).to_be_true();
		expect(pages[1].group).to_equal(uint8_t{1});
		expect(*pages[1].slots[0].param == st::kCompRatio).to_be_true();
		expect(*pages[5].slots[0].param == st::kCompBlend).to_be_true();
		expect(pages[5].slots[0].press == PressAction::NEXT_PAGE).to_be_true();
		expect(pages[5].slots[1].press == PressAction::TOGGLE_GROUP).to_be_true();
	});
	it("gives the mod FX button the params its type has", _{
		auto pages = section_pages(5, {.mod_fx_type = effects::ModFXType::FLANGER});
		expect(pages.size()).to_equal(size_t{1});
		expect(pages[0].page_id).to_equal(1u);
		expect(*pages[0].slots[0].param == ge::kModFXFeedback).to_be_true();
		expect(*pages[0].slots[1].param == ge::kModFXRate).to_be_true();
		expect(*pages[0].slots[1].press_param == st::kModFXType).to_be_true();
		pages = section_pages(5, {.mod_fx_type = effects::ModFXType::CHORUS});
		expect(pages.size()).to_equal(size_t{2});
		expect(pages[1].page_id).to_equal(2u);
		expect(global_effectable::mod_fx_page_valid(effects::ModFXType::GRAIN, 2)).to_be_true();
		expect(global_effectable::mod_fx_page_valid(effects::ModFXType::FLANGER, 0)).to_be_false();
	});
	it("holds the stutter from the upper knob, and the clip's chain has the pitch below it", _{
		expect(section_pages(6, {})[0].slots[1].press == PressAction::HOLD_STUTTER).to_be_true();
		expect(*section_pages(6, {})[0].slots[0].param == ge::kPitchAdjust).to_be_true();
		expect(section_pages(6, {.song = true})[0].slots[0].param.has_value()).to_be_false();
	});
	it("puts the sample rate reduction over the bitcrush", _{
		const auto pages = section_pages(7, {});
		expect(*pages[0].slots[0].param == ge::kBitcrushAmount).to_be_true();
		expect(*pages[0].slots[1].param == ge::kSampleRateReduction).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(chain_pages)
