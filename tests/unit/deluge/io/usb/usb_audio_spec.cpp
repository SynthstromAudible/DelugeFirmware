// tests/unit/deluge/io/usb/usb_audio_spec.cpp
// The engine's side of USB audio (io/usb/usb_audio.cpp) over a render block's USB port.
#include "io/usb/usb_audio.h"
#include "libdeluge/audio_ports.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>

// What usb_audio.cpp reaches outside itself, as these specs need it.
namespace FlashStorage {
bool usbAudioEnabled = true;
}
bool usbCurrentlyInitialized = false;
extern "C" void deluge_usb_device_start(bool) noexcept {
}
extern "C" bool deluge_usb_audio_present(void) noexcept {
	return true;
}

namespace {

using deluge::usb_audio::beginBlock;
using deluge::usb_audio::fillInputRing;
using deluge::usb_audio::getInputRingStart;
using deluge::usb_audio::receivingFromHost;
using deluge::usb_audio::sendingToHost;
using deluge::usb_audio::sendToHost;

constexpr size_t kFrames = 8;

/// A USB port with `pairs` pairs each way over its own blocks, the host there as `receiving` and `sending`.
struct Port {
	std::array<DelugeStereoSample, kFrames> in{};
	std::array<DelugeStereoSample, kFrames> out{};
	const DelugeStereoSample* inPairs[1] = {in.data()};
	DelugeStereoSample* outPairs[1] = {out.data()};
	DelugeAudioPort port{};

	Port(uint8_t pairs, bool receiving, bool sending) {
		for (size_t i = 0; i < kFrames; i++) {
			in[i] = {static_cast<int32_t>(i + 1), -static_cast<int32_t>(i + 1)};
		}
		port = DelugeAudioPort{
		    .in = pairs != 0 ? inPairs : nullptr,
		    .out = pairs != 0 ? outPairs : nullptr,
		    .in_pairs = pairs,
		    .out_pairs = pairs,
		    .flags = DELUGE_PORT_MAIN_MIX_OUT | DELUGE_PORT_MONITORABLE,
		    .far_receiving = receiving,
		    .far_sending = sending,
		    .out_silent = pairs != 0 ? 1u : 0u,
		};
	}
};

std::array<StereoSample, kFrames> block() {
	std::array<StereoSample, kFrames> b{};
	for (size_t i = 0; i < kFrames; i++) {
		b[i].l = deluge::dsp::Q<31>::from_raw(static_cast<int32_t>(100 + i));
		b[i].r = deluge::dsp::Q<31>::from_raw(static_cast<int32_t>(200 + i));
	}
	return b;
}

/// The first `kFrames` frames of the USB input ring after a block's fill.
int32_t ringSample(size_t frame, size_t channel) {
	return getInputRingStart()[frame * 2 + channel];
}

} // namespace

// clang-format off
describe usb_audio("deluge::usb_audio over the block's USB port", $ {
	it("with USB audio off (no pairs), reads silence and sends nothing", _ {
		Port p(0, true, true);
		beginBlock(&p.port);
		expect(receivingFromHost()).to_equal(false);
		expect(sendingToHost()).to_equal(false);
		auto b = block();
		sendToHost(b);
		expect(p.port.out_silent).to_equal(0u);
		beginBlock(nullptr);
	});

	it("a host that collects and doesn't play: our output goes out, and its input reads silence", _ {
		Port p(1, true, false);
		beginBlock(&p.port);
		expect(sendingToHost()).to_equal(true);
		expect(receivingFromHost()).to_equal(false);
		fillInputRing(getInputRingStart(), kFrames);
		expect(ringSample(0, 0)).to_equal(0);
		auto b = block();
		sendToHost(b);
		expect(p.port.out_silent & 1u).to_equal(0u);
		expect(p.out[3].l).to_equal(103);
		expect(p.out[3].r).to_equal(203);
		beginBlock(nullptr);
	});

	it("a host that plays and doesn't collect: its input is read, and our output stays silent", _ {
		Port p(1, false, true);
		beginBlock(&p.port);
		expect(sendingToHost()).to_equal(false);
		expect(receivingFromHost()).to_equal(true);
		fillInputRing(getInputRingStart(), kFrames);
		expect(ringSample(0, 0)).to_equal(1);
		expect(ringSample(7, 1)).to_equal(-8);
		expect(p.port.out_silent & 1u).to_equal(1u);
		beginBlock(nullptr);
	});

	it("a block with no port (an offline export) reads silence and takes nothing", _ {
		Port p(1, true, true);
		beginBlock(&p.port);
		fillInputRing(getInputRingStart(), kFrames);
		beginBlock(nullptr);
		expect(receivingFromHost()).to_equal(false);
		expect(sendingToHost()).to_equal(false);
		fillInputRing(getInputRingStart(), kFrames);
		expect(ringSample(0, 0)).to_equal(0);
		auto b = block();
		sendToHost(b);
		expect(p.out[0].l).to_equal(0);
	});
});

CPPSPEC_SPEC(usb_audio)
