/*
 * Copyright (c) 2026 Deluge Community Firmware contributors
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */
#pragma once

#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/integer.h"
#include "gui/ui_timer_manager.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace deluge::gui::menu_item::cpu {

class VoiceUsage final : public HorizontalMenu {
public:
	class VoiceCount final : public Integer {
	public:
		enum class Kind { LIVE, AVERAGE, MAXIMUM };

		VoiceCount(VoiceUsage& usage, Kind kind, l10n::String name, l10n::String columnLabel)
		    : Integer(name), usage_(usage), kind_(kind), column_label_(columnLabel) {}

		void beginSession(MenuItem*) override {
			usage_.sample();
			Integer::beginSession(nullptr);
			usage_.startTimer();
		}

		ActionResult timerCallback() override { return usage_.timerCallback(); }
		void selectEncoderAction(int32_t) override {}
		bool selectEncoderActionIsPermitted() override { return false; }

		void getColumnLabel(StringBuf& label) override { label.append(l10n::getView(column_label_)); }

		void readCurrentValue() override { setValue(metricValue()); }

	private:
		int32_t metricValue() const {
			switch (kind_) {
			case Kind::LIVE:
				return usage_.live_voice_count_;
			case Kind::AVERAGE:
				return usage_.averageVoiceCount();
			case Kind::MAXIMUM:
				return usage_.maximum_voice_count_;
			}
			return 0;
		}

		[[nodiscard]] int32_t getMaxValue() const override { return std::numeric_limits<int32_t>::max(); }
		[[nodiscard]] deluge::gui::menu_item::RenderingStyle getRenderingStyle() const override {
			return deluge::gui::menu_item::RenderingStyle::NUMBER;
		}

		VoiceUsage& usage_;
		Kind kind_;
		l10n::String column_label_;
	};

	VoiceUsage(l10n::String name, l10n::String title)
	    : HorizontalMenu(name, title, {&live_, &average_, &maximum_}, HorizontalMenu::Layout::FIXED),
	      live_(*this, VoiceCount::Kind::LIVE, l10n::String::STRING_FOR_LIVE_VOICE_COUNT,
	            l10n::String::STRING_FOR_LIVE),
	      average_(*this, VoiceCount::Kind::AVERAGE, l10n::String::STRING_FOR_AVERAGE_VOICE_COUNT,
	               l10n::String::STRING_FOR_AVG),
	      maximum_(*this, VoiceCount::Kind::MAXIMUM, l10n::String::STRING_FOR_MAXIMUM_VOICE_COUNT,
	               l10n::String::STRING_FOR_MAX) {}

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		total_voice_count_ = 0;
		sample_count_ = 0;
		maximum_voice_count_ = 0;
		sample();
		HorizontalMenu::beginSession(navigatedBackwardFrom);
		startTimer();
	}

	ActionResult timerCallback() override {
		sample();
		if (renderingStyle() == RenderingStyle::HORIZONTAL) {
			renderUIsForOled();
		}
		else {
			getCurrentItem()->readValueAgain();
		}
		startTimer();
		return ActionResult::DEALT_WITH;
	}

private:
	static constexpr uint32_t kRefreshIntervalMs = 500;

	void startTimer() { uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kRefreshIntervalMs); }

	void sample() {
		live_voice_count_ =
		    AudioEngine::getNumVoices() + (currentSong != nullptr ? currentSong->countAudioVoices() : 0);
		total_voice_count_ += live_voice_count_;
		++sample_count_;
		maximum_voice_count_ = std::max(maximum_voice_count_, live_voice_count_);
	}

	int32_t averageVoiceCount() const {
		return sample_count_ == 0 ? 0 : static_cast<int32_t>(total_voice_count_ / sample_count_);
	}

	int32_t live_voice_count_{0};
	int32_t maximum_voice_count_{0};
	uint64_t total_voice_count_{0};
	uint32_t sample_count_{0};

	VoiceCount live_;
	VoiceCount average_;
	VoiceCount maximum_;
};

} // namespace deluge::gui::menu_item::cpu
