/// @file
/// @brief The DX7 cases' rig, setup and blocks.
/// @details Each block is a DX7 voice's block as a sound renders it: the patch's LFO, then the voice adapter's render.

#include "bench/cases.h"

#include "instruments/deluge/dx7_init_voice.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include "processing/dx7/dx_voice_adapter.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <tuple>

namespace deluge::bench {

namespace {

/// @brief The voice source's phase increment the cases render at: middle C at 44.1 kHz.
constexpr uint32_t kPhaseIncrement = 0x00f00000;
/// @brief The note and velocity each case holds.
constexpr int kNote = 60;
constexpr int kVelocity = 100;

/// @brief The patch bytes the cases set, per op (21 bytes each) and global.
constexpr std::size_t kOpBytes = 21;
constexpr std::size_t kAlgorithm = 134;
constexpr std::size_t kFeedback = 135;
constexpr std::size_t kLfoRate = 137;
constexpr std::size_t kLfoPmd = 139;
constexpr std::size_t kLfoAmd = 140;
constexpr std::size_t kLfoWave = 142;
constexpr std::size_t kLfoPms = 143;

struct Dx7Rig {
	dsp::DxVoiceAdapter adapter{};
	DxPatch* patch = nullptr;
};

Dx7Rig rig{};

/// @brief Writes @p config's patch into @p p: the init voice with every op sounding, and the case's globals.
void build_patch(const Dx7Config& config, DxPatch& p) {
	std::ranges::copy(instrument::kDx7InitVoice, p.params.begin());
	for (std::size_t op = 0; op < 6; ++op) {
		uint8_t* o = &p.params[op * kOpBytes];
		o[0] = 99;
		o[1] = 50;
		o[2] = 40;
		o[3] = 30;
		o[4] = 99;
		o[5] = 80;
		o[6] = 60;
		o[7] = 0;
		o[16] = 90;                           // output level
		o[18] = static_cast<uint8_t>(op + 1); // coarse
		if (config.lfo) {
			o[14] = 3; // amplitude-modulation sensitivity
		}
	}
	p.params[kAlgorithm] = config.algorithm;
	p.params[kFeedback] = config.feedback;
	if (config.lfo) {
		p.params[kLfoWave] = 4;
		p.params[kLfoRate] = 35;
		p.params[kLfoPmd] = 50;
		p.params[kLfoAmd] = 99;
		p.params[kLfoPms] = 7;
	}
	p.setEngineMode(config.engine);
}

} // namespace

void Dx7Case::setup(std::size_t /*n*/) {
	if (rig.patch == nullptr) {
		rig.patch = getDxEngine()->newPatch();
	}
	build_patch(config_, *rig.patch);
	rig.adapter.unassign();
	rig.adapter.note_on(rig.patch, kNote, kVelocity);
}

void Dx7Case::block(std::size_t n) {
	rig.patch->computeLfo(static_cast<int>(n));
	std::ignore = rig.adapter.render(static_cast<int32_t>(n), kPhaseIncrement, 0, false, kNote);
}

} // namespace deluge::bench
