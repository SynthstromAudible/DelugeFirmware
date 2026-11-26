/*
 * Copyright © 2026 Synthstrom Audible Limited
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

// Mic velocity's settings, in SETTINGS/MicVelocity.XML:
//
// <micVelocity firmwareVersion=".." earliestCompatibleFirmware="..">
//   <enabled>1</enabled>
//   <minVelocity>1</minVelocity>
//   <maxVelocity>127</maxVelocity>
//   <shape>0</shape>
//   <calibration>
//     <point><x>7</x><y>4</y><soft>-5500</soft><hard>-3800</hard></point>   (levels in hundredths of a dB)
//     ...
//   </calibration>
// </micVelocity>

#include "hid/mic_velocity/mic_velocity.h"
#include "storage/storage_manager.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace deluge::mic_velocity {

namespace {

using dsp::mic_velocity::Calibration;
using dsp::mic_velocity::kCalibrationPoints;
using dsp::mic_velocity::kNumCalibrationPoints;
using dsp::mic_velocity::VelocityCurve;

constexpr char const* kFile = "SETTINGS/MicVelocity.XML";
constexpr char const* kRootTag = "micVelocity";

bool changed = false;

int32_t toCentiDb(float db) {
	return static_cast<int32_t>(std::lround(db * 100));
}

/// One <point>: its pad and levels
struct ReadPoint {
	int32_t x = -1;
	int32_t y = -1;
	int32_t soft = 0;
	int32_t hard = 0;
	bool haveSoft = false;
	bool haveHard = false;
};

ReadPoint readPoint(Deserializer& reader) {
	ReadPoint point;
	char const* tag;
	while (*(tag = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tag, "x")) {
			point.x = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tag, "y")) {
			point.y = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tag, "soft")) {
			point.soft = reader.readTagOrAttributeValueInt();
			point.haveSoft = true;
		}
		else if (!strcmp(tag, "hard")) {
			point.hard = reader.readTagOrAttributeValueInt();
			point.haveHard = true;
		}
		reader.exitTag();
	}
	return point;
}

/// The <calibration>: used only if it has every calibration point (a file from a firmware calibrating different pads
/// is ignored)
void readCalibration(Deserializer& reader) {
	Calibration::PointLevels soft{};
	Calibration::PointLevels hard{};
	std::array<bool, kNumCalibrationPoints> found{};
	char const* tag;
	while (*(tag = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tag, "point")) {
			ReadPoint const point = readPoint(reader);
			for (int32_t p = 0; p < kNumCalibrationPoints; p++) {
				if (point.x == kCalibrationPoints[p].x && point.y == kCalibrationPoints[p].y && point.haveSoft
				    && point.haveHard) {
					soft[p] = static_cast<float>(point.soft) / 100;
					hard[p] = static_cast<float>(point.hard) / 100;
					found[p] = true;
				}
			}
		}
		reader.exitTag();
	}
	if (std::all_of(found.begin(), found.end(), [](bool f) { return f; })) {
		setCalibration(Calibration{soft, hard});
	}
}

} // namespace

void settingsChanged() {
	changed = true;
}

void readSettings() {
	FilePointer fp;
	if (!StorageManager::fileExists(kFile, &fp)) {
		return;
	}
	if (StorageManager::openXMLFile(&fp, smDeserializer, kRootTag) != Error::NONE) {
		return;
	}
	Deserializer& reader = smDeserializer;
	char const* tag;
	while (*(tag = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tag, "enabled")) {
			enabled = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tag, "minVelocity")) {
			curve.minVelocity = std::clamp<int32_t>(reader.readTagOrAttributeValueInt(), 1, 127);
		}
		else if (!strcmp(tag, "maxVelocity")) {
			curve.maxVelocity = std::clamp<int32_t>(reader.readTagOrAttributeValueInt(), 1, 127);
		}
		else if (!strcmp(tag, "shape")) {
			curve.shape = std::clamp<int32_t>(reader.readTagOrAttributeValueInt(), VelocityCurve::kMinShape,
			                                  VelocityCurve::kMaxShape);
		}
		else if (!strcmp(tag, "calibration")) {
			readCalibration(reader);
		}
		reader.exitTag();
	}
	smDeserializer.closeWriter();
	changed = false; // Reading set the calibration, which counts as a change
	update();
}

void writeSettingsIfChanged() {
	if (!changed) {
		return;
	}
	if (StorageManager::createXMLFile(kFile, smSerializer, true) != Error::NONE) {
		return; // Stays changed, so it's tried again next time
	}
	Serializer& writer = GetSerializer();
	writer.writeOpeningTagBeginning(kRootTag);
	writer.writeFirmwareVersion();
	writer.writeEarliestCompatibleFirmwareVersion("4.1.3");
	writer.writeOpeningTagEnd();

	writer.writeTag("enabled", enabled ? 1 : 0);
	writer.writeTag("minVelocity", curve.minVelocity);
	writer.writeTag("maxVelocity", curve.maxVelocity);
	writer.writeTag("shape", curve.shape);

	if (calibrated()) {
		Calibration const& c = calibration();
		writer.writeOpeningTag("calibration");
		for (int32_t p = 0; p < kNumCalibrationPoints; p++) {
			writer.writeOpeningTag("point");
			writer.writeTag("x", kCalibrationPoints[p].x);
			writer.writeTag("y", kCalibrationPoints[p].y);
			writer.writeTag("soft", toCentiDb(c.softDb()[p]));
			writer.writeTag("hard", toCentiDb(c.hardDb()[p]));
			writer.writeClosingTag("point");
		}
		writer.writeClosingTag("calibration");
	}

	writer.writeClosingTag(kRootTag);
	if (writer.closeFileAfterWriting() == Error::NONE) {
		changed = false;
	}
}

} // namespace deluge::mic_velocity
