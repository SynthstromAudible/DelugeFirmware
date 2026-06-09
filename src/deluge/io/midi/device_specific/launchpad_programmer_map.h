/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include <cstdint>

namespace launchpad_programmer_map {

// Programmer-mode CC numbers (Launchpad X / Mini MK3 / Pro MK3 family).
namespace cc {
static constexpr uint8_t kUp = 91;
static constexpr uint8_t kDown = 92;
static constexpr uint8_t kLeft = 93;
static constexpr uint8_t kRight = 94;
static constexpr uint8_t kSession = 95;
static constexpr uint8_t kNote = 96;
static constexpr uint8_t kCustom = 97;      // Deluge: play / stop
static constexpr uint8_t kCaptureMidi = 98; // Deluge: record toggle
} // namespace cc

// Right-hand scene column, top (Volume) → bottom (Record Arm) in Ableton mixer labels.
static constexpr uint8_t kSceneCcs[8] = {89, 79, 69, 59, 49, 39, 29, 19};

// Scene CC → Deluge session grid row (7 = top visible section, 0 = bottom).
bool sceneRowFromCc(uint8_t cc, int32_t& delugeY);

// Programmer LED index for scene button at Deluge grid row (7 = top, 0 = bottom).
uint8_t sceneLedIndexForDelugeRow(int32_t delugeY);

// Launchpad X / Mini MK3 / Pro MK3 Programmer mode 8×8 grid (y=0 top, x=0 left).
uint8_t gridLedIndex(int32_t x, int32_t y);
uint8_t gridNote(int32_t x, int32_t y);

// Deluge session grid y (0 top) → Launchpad Programmer row (0 top).
uint8_t gridLedIndexFromDeluge(int32_t x, int32_t delugeY);

// Grid note (11–88) → Deluge session grid cell (0–7, 0–7). Returns false if not a grid note.
bool delugeGridCoordsFromNote(uint8_t note, int32_t& x, int32_t& y);

// All aux LEDs cleared on connect (top row, transport, logo — not scene column).
constexpr uint8_t kNonGridLedIndexCount = 10;
uint8_t const* nonGridLedIndices();

// Top row + logo only; transport (97/98) is driven separately.
constexpr uint8_t kFixedOffLedIndexCount = 8;
uint8_t const* fixedOffLedIndices();

} // namespace launchpad_programmer_map
