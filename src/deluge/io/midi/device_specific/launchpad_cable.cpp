/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "io/midi/device_specific/launchpad_cable.h"

#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_specific/novation_launchpad_mk3.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/root_complex/usb_hosted.h"
#include "processing/engines/audio_engine.h"

namespace launchpad_cable {

namespace {

MIDICableUSBHosted* cachedPort2 = nullptr;
uint32_t lastDisconnectedScanTime = 0;
static constexpr uint32_t kDisconnectedScanIntervalSamples = kSampleRate;

MIDICableUSBHosted* scanForPort2() {
	using namespace MIDIDeviceManager;

	if (root_usb == nullptr || root_usb->getType() != RootComplexType::RC_USB_HOST) {
		return nullptr;
	}

	for (auto& cable : root_usb->getCables()) {
		auto& hosted = static_cast<MIDICableUSBHosted&>(cable);
		if (!hosted.connectionFlags) {
			continue;
		}
		if (!novation_launchpad_mk3::matchesVendorProduct(hosted.vendorId, hosted.productId)) {
			continue;
		}
		if (hosted.portNumber != 1) {
			continue;
		}
		return &hosted;
	}
	return nullptr;
}

} // namespace

void registerPort2(MIDICableUSBHosted* cable) {
	cachedPort2 = cable;
}

MIDICableUSBHosted* getPort2() {
	if (cachedPort2 != nullptr) {
		if (cachedPort2->connectionFlags) {
			return cachedPort2;
		}
		cachedPort2 = nullptr;
	}

	uint32_t now = AudioEngine::audioSampleTimer;
	if (now - lastDisconnectedScanTime < kDisconnectedScanIntervalSamples) {
		return nullptr;
	}

	lastDisconnectedScanTime = now;
	cachedPort2 = scanForPort2();
	return cachedPort2;
}

bool isPort2(MIDICable& cable) {
	MIDICableUSBHosted* port2 = getPort2();
	return port2 != nullptr && &cable == static_cast<MIDICable*>(port2);
}

bool isLaunchpadCable(MIDICable& cable) {
	using namespace MIDIDeviceManager;

	if (root_usb == nullptr || root_usb->getType() != RootComplexType::RC_USB_HOST) {
		return false;
	}

	for (auto& hostedCable : root_usb->getCables()) {
		if (static_cast<MIDICable*>(&hostedCable) != &cable) {
			continue;
		}
		auto& hosted = static_cast<MIDICableUSBHosted&>(hostedCable);
		return hosted.connectionFlags
		       && novation_launchpad_mk3::matchesVendorProduct(hosted.vendorId, hosted.productId);
	}
	return false;
}

} // namespace launchpad_cable
