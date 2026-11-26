#pragma once

/// @file
/// @brief A test-only transcription of the legacy waveform arithmetic as this tree held it at `ba0d31ef8`, statement
///        for statement, with its types, truncations, sentinels and control flow kept. It is a record to compare the
///        waveform view types with, not product code; a sim spec proved it equal to that code while the code was live.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iterator>

// The transcription keeps legacy's narrowings, uninitialised locals and goto on purpose, so the lints that would
// rewrite them are off for it.
// NOLINTBEGIN
namespace deluge::ui::views::waveform::test::oracle {

inline constexpr int32_t kDisplayWidth = 16;
inline constexpr int32_t kDisplayWidthMagnitude = 4;
inline constexpr int32_t kMaxSampleValue = 16777216;

inline constexpr uint8_t kNotInvestigated = 0;
inline constexpr uint8_t kInvestigated = 1;
inline constexpr uint8_t kBeyond = 2;

/// `WaveformRenderData` (`ba0d31ef8:src/deluge/model/sample/waveform_render_data.h`).
struct RenderData {
	int64_t xScroll{};
	int64_t xZoom{};
	int64_t validLengthSamples{};
	std::array<int32_t, 16> maxPerCol{};
	std::array<int32_t, 16> minPerCol{};
	std::array<uint8_t, 16> colStatus{};
};

/// `deluge::sample::col_bar_positions` (`ba0d31ef8:src/deluge/model/sample/waveform_scan.cpp`).
inline void col_bar_positions(int32_t xDisplay, const RenderData& data, int32_t& min24, int32_t& max24,
                              int32_t valueCentrePoint, int32_t valueSpan) {
	min24 = ((int64_t)(data.minPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;
	max24 = ((int64_t)(data.maxPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;

	if (max24 - min24 < kMaxSampleValue) {
		int32_t midPoint = (max24 >> 1) + (min24 >> 1);
		max24 = midPoint + 8388608;
		min24 = midPoint - 8388608;
	}
}

/// `deluge::sample::col_brightness_for_single_row` (`ba0d31ef8:src/deluge/model/sample/waveform_scan.cpp`).
inline int32_t col_brightness_for_single_row(int32_t xDisplay, int32_t maxPeakFromZero, const RenderData& data) {
	int32_t peak1 = std::abs(data.minPerCol[xDisplay]);
	int32_t peak2 = std::abs(data.maxPerCol[xDisplay]);

	int32_t peakHere = std::max(peak1, peak2);

	uint32_t peak16 = ((int64_t)peakHere << 16) / maxPeakFromZero;

	return std::min<int32_t>(peak16 >> 8, 256);
}

/// `MarkerColumn` (`ba0d31ef8:src/deluge/model/sample/waveform_navigator.h`).
struct MarkerColumn {
	int32_t pos;
	int32_t colOnScreen;
};

/// `SampleMarkerEditor::kInvalidColumn`: a marker that is not set.
inline constexpr int32_t kInvalidColumn = INT32_MIN;

/// `MarkerType`'s numbering: -1 for NONE, then the zone's four markers.
inline constexpr int32_t kMarkerNone = -1;
inline constexpr int32_t kMarkerLoopEnd = 2;
inline constexpr int32_t kMarkerEnd = 3;
inline constexpr int32_t kNumMarkerTypes = 4;

/// `deluge::sample::WaveformNav` (`ba0d31ef8:src/deluge/model/sample/waveform_navigator.cpp`), over a length rather
/// than a sample. `opened` leaves out the render data's invalidation, which is the cache's.
struct Nav {
	uint64_t length{};
	int64_t xZoom{};
	int64_t xScroll{};

	void opened(int64_t scroll, int64_t zoom) {
		if (zoom != 0) {
			xScroll = scroll;
			xZoom = zoom;

			adjust_scroll_position();
		}
		else {
			xScroll = 0;
			xZoom = max_zoom();
		}
	}

	[[nodiscard]] int32_t max_zoom() const { return ((length - 1) >> kDisplayWidthMagnitude) + 1; }

	[[nodiscard]] bool is_zoomed_in() const { return (xZoom != max_zoom()); }

	bool zoom(int32_t offset, bool shouldAllowExtraScrollRight, const MarkerColumn* cols, int32_t markerType) {
		int32_t newXZoom;

		if (offset >= 0) {
			if (xZoom < 2) {
				return false;
			}

			bool isSquareNumber = false;
			uint32_t nextSquareNumber;
			for (int32_t i = 0; i < 31; i++) {
				uint32_t squareNumber = (uint32_t)1 << i;
				if (squareNumber == xZoom) {
					isSquareNumber = true;
					break;
				}
				else if (squareNumber > xZoom) {
					nextSquareNumber = squareNumber;
					break;
				}
			}

			if (!isSquareNumber) {
				if (xZoom >= nextSquareNumber * 0.707) {
					newXZoom = nextSquareNumber >> 1;
				}
				else {
					newXZoom = nextSquareNumber >> 2;
				}
			}

			else {
				newXZoom = xZoom >> 1;
			}
		}

		else {
			int32_t limit = max_zoom();
			if (xZoom >= limit) {
				return false;
			}
			newXZoom = xZoom << 1;
			if (newXZoom >= limit || (newXZoom * 2) * 0.707 >= limit) {
				newXZoom = limit;
			}
		}

		int32_t pinMarkerCol = -1;
		int32_t pinMarkerPos = xScroll + xZoom * (kDisplayWidth >> 1);
		int32_t pinnedToMarkerType = kMarkerNone;

		if (markerType != kMarkerNone) {

			for (int32_t m = 0; m < kNumMarkerTypes; m++) {
				int32_t col = cols[m].colOnScreen;

				if (col >= 0 && col < kDisplayWidth) {

					if (m == markerType) {
bestYet:
						pinMarkerCol = col;
						if (m >= kMarkerLoopEnd) {
							pinMarkerCol++;
						}
						pinMarkerPos = cols[m].pos;
						pinnedToMarkerType = m;
					}
					else {
						int32_t pinMarkerDistanceFromCentre = pinMarkerCol - (kDisplayWidth >> 1);
						if (pinMarkerDistanceFromCentre < 0) {
							pinMarkerDistanceFromCentre = -pinMarkerDistanceFromCentre;
						}

						int32_t thisMarkerDistanceFromCentre = col - (kDisplayWidth >> 1);
						if (thisMarkerDistanceFromCentre < 0) {
							thisMarkerDistanceFromCentre = -thisMarkerDistanceFromCentre;
						}

						if (thisMarkerDistanceFromCentre < pinMarkerDistanceFromCentre) {
							goto bestYet;
						}
					}

					if (m == markerType) {
						break;
					}
				}
			}
		}

		if (pinMarkerCol == -1) {
			pinMarkerCol = (kDisplayWidth >> 1);
		}

		xScroll = pinMarkerPos - newXZoom * pinMarkerCol;

		xZoom = newXZoom;

		if (pinnedToMarkerType >= kMarkerLoopEnd) {
			xScroll = ((xScroll - 1) / xZoom + 1) * xZoom;
		}
		else {
			xScroll = xScroll / xZoom * xZoom;
		}

		adjust_scroll_position(shouldAllowExtraScrollRight);

		return true;
	}

	bool scroll(int32_t offset, bool shouldAllowExtraScrollRight, const MarkerColumn* cols) {
		if (offset >= 0) {

			if (shouldAllowExtraScrollRight) {
				int32_t newScroll = xScroll + xZoom;
				if ((int32_t)(xScroll + xZoom * kDisplayWidth) < xScroll) {
					return false;
				}
				xScroll = newScroll;
			}
			else {
				if (xScroll + xZoom * kDisplayWidth >= length
				    && (!cols || cols[kMarkerEnd].colOnScreen < kDisplayWidth)) {
					return false;
				}
				xScroll += xZoom;
			}
		}

		else {
			if (xScroll <= 0) {
				return false;
			}
			else if (xScroll < xZoom) {
				xScroll = 0;
			}
			xScroll -= xZoom;
		}

		return true;
	}

	void adjust_scroll_position(bool shouldAllowExtraScrollRight = false) {
		if (xScroll < 0) {
			xScroll = 0;
		}
		else {
			if (!shouldAllowExtraScrollRight) {
				uint32_t lengthInSamples = length;
				int32_t scrollLimit = ((lengthInSamples - 1) / xZoom + 1 - kDisplayWidth) * xZoom;
				if (xScroll > scrollLimit) {
					xScroll = scrollLimit;
				}
			}
		}
	}
};

} // namespace deluge::ui::views::waveform::test::oracle
// NOLINTEND
