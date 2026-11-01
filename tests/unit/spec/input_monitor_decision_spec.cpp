// The engine's input monitoring decision against a verbatim copy of the setMonitoringMode it replaces, over every
// source, mode and socket and input shape; and its warm-up gain against the getInputMonitoringGain it replaces, across
// the warm-up, the fade and after the latch.

#include "processing/engines/input_monitor.h"

#include "cppspec.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <utility>

using deluge::audio::InputMonitor;
using deluge::audio::MonitorDecision;
using deluge::audio::MonitorInputs;
using deluge::dsp::fixed::OutputFold;

namespace {

enum class MonitoringAction { NONE, REMOVE_RIGHT_CHANNEL, SUBTRACT_RIGHT_CHANNEL };

/// AudioEngine::setMonitoringMode, its globals as @p in's fields: (doMonitoring, monitoringAction).
std::pair<bool, MonitoringAction> legacy(const MonitorInputs& in) {
	bool doMonitoring = false;
	MonitoringAction monitoringAction;
	if (in.source == AudioInputChannel::USB) {
		doMonitoring = (in.mode != InputMonitoringMode::OFF && !in.usb_direct_monitoring);
		monitoringAction = MonitoringAction::NONE;
		return {doMonitoring, monitoringAction};
	}
	if (in.source == AudioInputChannel::STEREO || in.source == AudioInputChannel::LEFT) {
		if (in.mode == InputMonitoringMode::SMART) {
			doMonitoring = (in.line_in || in.headphones);
		}
		else {
			doMonitoring = (in.mode == InputMonitoringMode::ON);
		}
	}

	monitoringAction = MonitoringAction::NONE;
	if (doMonitoring && in.recorder) {
		if (in.line_in) {
			if (in.looks_differential) {
				monitoringAction = MonitoringAction::SUBTRACT_RIGHT_CHANNEL;
			}
			else if (in.no_right_channel) {
				monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
			}
		}

		else if (in.mic) {
			if (in.no_right_channel) {
				monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
			}
		}

		else {
			monitoringAction = MonitoringAction::REMOVE_RIGHT_CHANNEL;
		}
	}
	return {doMonitoring, monitoringAction};
}

/// The fold the output stage took for @p action.
OutputFold fold_of(MonitoringAction action) {
	switch (action) {
	case MonitoringAction::SUBTRACT_RIGHT_CHANNEL:
		return OutputFold::DIFFERENTIAL;
	case MonitoringAction::REMOVE_RIGHT_CHANNEL:
		return OutputFold::LEFT_ONLY;
	default:
		return OutputFold::STEREO;
	}
}

/// AudioEngine::getInputMonitoringGain, its warm-up latch as @p warmedUp.
int32_t legacy_gain(bool warmedUp, uint32_t time) {
	constexpr uint32_t kInputMonitoringWarmupSamples = kSampleRate / 2;
	constexpr uint32_t kInputMonitoringFadeInSamples = kSampleRate / 20;
	if (warmedUp) {
		return std::numeric_limits<int32_t>::max();
	}
	if (time <= kInputMonitoringWarmupSamples) {
		return 0;
	}
	uint32_t elapsed = time - kInputMonitoringWarmupSamples;
	if (elapsed >= kInputMonitoringFadeInSamples) {
		return std::numeric_limits<int32_t>::max();
	}
	return static_cast<int32_t>((static_cast<uint64_t>(elapsed) * std::numeric_limits<int32_t>::max())
	                            / kInputMonitoringFadeInSamples);
}

constexpr std::array<AudioInputChannel, 6> kSources{AudioInputChannel::USB,  AudioInputChannel::STEREO,
                                                    AudioInputChannel::LEFT, AudioInputChannel::RIGHT,
                                                    AudioInputChannel::NONE, AudioInputChannel::BALANCED};
constexpr std::array<InputMonitoringMode, 3> kModes{InputMonitoringMode::SMART, InputMonitoringMode::ON,
                                                    InputMonitoringMode::OFF};

} // namespace

// clang-format off
describe input_monitor_decision("the engine's input monitoring", ${
	it("decides as setMonitoringMode did, for every source, mode, socket and input shape", _ {
		int checked = 0;
		for (const AudioInputChannel source : kSources) {
			for (const InputMonitoringMode mode : kModes) {
				for (uint32_t bits = 0; bits < (1u << 7); ++bits) {
					const MonitorInputs in{.source = source,
					                       .mode = mode,
					                       .usb_direct_monitoring = (bits & 1u) != 0,
					                       .line_in = (bits & 2u) != 0,
					                       .headphones = (bits & 4u) != 0,
					                       .mic = (bits & 8u) != 0,
					                       .recorder = (bits & 16u) != 0,
					                       .looks_differential = (bits & 32u) != 0,
					                       .no_right_channel = (bits & 64u) != 0};
					const auto [on, action] = legacy(in);
					const MonitorDecision d = deluge::audio::decide(in);
					expect(d.on).to_equal(on);
					if (on) {
						expect(d.fold == fold_of(action)).to_be_true();
					}
					InputMonitor monitor;
					monitor.decide(in);
					expect(monitor.on()).to_equal(on);
					checked++;
				}
			}
		}
		expect(checked).to_equal(6 * 3 * 128);
	});

	it("gains as getInputMonitoringGain did, through the warm-up, the fade and after the latch", _ {
		constexpr uint32_t kEnd = InputMonitor::kWarmupSamples + InputMonitor::kFadeInSamples;
		const std::array<uint32_t, 9> times{0,    1,        InputMonitor::kWarmupSamples, InputMonitor::kWarmupSamples + 1,
		                                    InputMonitor::kWarmupSamples + InputMonitor::kFadeInSamples / 2,
		                                    kEnd - 1, kEnd, kEnd + 1, std::numeric_limits<uint32_t>::max()};
		InputMonitor monitor;
		for (const uint32_t t : times) {
			expect(monitor.gain(t)).to_equal(legacy_gain(false, t));
		}
		monitor.advance_time(kEnd - 1);
		expect(monitor.warmed_up()).to_be_false();
		monitor.advance_time(kEnd);
		expect(monitor.warmed_up()).to_be_true();
		monitor.advance_time(0);
		expect(monitor.warmed_up()).to_be_true();
		for (const uint32_t t : times) {
			expect(monitor.gain(t)).to_equal(legacy_gain(true, t));
		}
	});
});
// clang-format on

CPPSPEC_SPEC(input_monitor_decision)
