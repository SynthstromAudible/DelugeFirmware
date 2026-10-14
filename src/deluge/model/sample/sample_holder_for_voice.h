/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <utility>

#include "model/sample/sample_holder.h"
#include "util/phase_increment_fine_tuner.h"

class Source;

class SampleHolderForVoice final : public SampleHolder {
public:
	SampleHolderForVoice();

	SampleHolderForVoice(SampleHolderForVoice&& other) noexcept
	    : SampleHolder(std::move(other)), loopStartPos(other.loopStartPos), loopEndPos(other.loopEndPos),
	      transpose(other.transpose), cents(other.cents), loopLocked(other.loopLocked), fineTuner(other.fineTuner),
	      clustersForLoopStart_(std::exchange(other.clustersForLoopStart_, nullptr)) {}
	SampleHolderForVoice& operator=(SampleHolderForVoice&& other) noexcept {
		SampleHolder::operator=(std::move(other));
		loopStartPos = other.loopStartPos;
		loopEndPos = other.loopEndPos;
		transpose = other.transpose;
		cents = other.cents;
		loopLocked = other.loopLocked;
		fineTuner = other.fineTuner;
		clustersForLoopStart_ = std::exchange(other.clustersForLoopStart_, nullptr);
		return *this;
	}
	~SampleHolderForVoice() override;
	void unassignAllClusterReasons(bool beingDestructed = false) override;
	void setCents(int32_t newCents);
	void recalculateFineTuner();
	void claimClusterReasons(bool reversed, int32_t clusterLoadInstruction = CLUSTER_ENQUEUE) override;
	/// @brief Set the transpose from the sample's midiNote, which its detection has already settled; an unset or failed
	///        note leaves the transpose alone. @p thatOneNote is the MIDI key a range covering just one note covers.
	void setTransposeAccordingToSamplePitch(bool minimizeOctaves = false, bool rangeCoversJustOneNote = false,
	                                        int32_t thatOneNote = 0);
	uint32_t getMSecLimit(Source* source);

	/// In samples. 0 means not set.
	uint32_t loopStartPos;
	/// In samples, 0 means no looping active.
	///
	/// Unlike endPos, this may not be beyond the waveform ever!
	uint32_t loopEndPos;

	int16_t transpose;
	int8_t cents;
	/// Whether the loop length should be kept constant when updating the start/end position.
	bool loopLocked;
	PhaseIncrementFineTuner fineTuner;

	/// Passive lookahead reservation anchored at this voice's loop-start marker; pins a small window
	/// of cluster residency around the loop point, mirroring SampleHolder::clustersForStart_.
	/// `nullptr` when not yet opened (or after being released).
	DelugeSampleReservation* clustersForLoopStart_ = nullptr;

	[[nodiscard]] uint32_t loopLength() const { return loopEndPos - loopStartPos; }

protected:
	void sampleBeenSet(bool reversed, bool manuallySelected) override;
};
