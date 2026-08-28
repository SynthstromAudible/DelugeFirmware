#pragma once
/// Legacy-transcription oracles for the pad_leds.cpp tweens.
///
/// Every function below is a line-by-line transcription of the routine named in its comment, taken
/// from `src/deluge/hid/led/pad_leds.cpp` on the commit this port was written against. They exist
/// so `gfx/grid/tween.cpp` can be written idiomatically (runtime `PadGrid` dimensions, hoisted
/// invariants, helpers factored out) while still being byte-compared against the legacy arithmetic
/// it claims parity with. Deliberately NOT written in terms of the toolkit: fixed 8x18 `RGB`
/// arrays indexed `image[y][x]`, the legacy variable names, the legacy control flow, and local
/// copies of the fixed-point helpers.
///
/// Occupancy stays a real input here -- `renderExplodeAnimation` and
/// `renderInstrumentClipCollapseAnimation` take the `occupancyMaskStore` legacy reads, so the
/// byte-compares can falsify the port's optional `OccupancyMask` rather than assuming it. Specs
/// that exercise the port's *default* (empty mask, "non-black is occupied") pass the oracle a mask
/// derived the same way; the spec that exercises a black-but-occupied pad passes a mask that
/// differs from the colours, which is exactly the case the substitution gets wrong.
///
/// The one substitution the `PadGrid` shape still forces, applied identically here and in the port:
/// `renderZoomedSquare`'s `(*(uint32_t*)&inputImageRow[xSource * 3]) & 16777215` black test becomes
/// `r | g | b` (legacy has no occupancy plane for the zoom source images either).
///
/// One blind spot, deliberate and unavoidable: the zoom oracle calls the *port's* copies of the
/// lookup tables and helpers (`deluge::gfx::grid::kExpTableSmall`, `interpolate_table`,
/// `sine_q15`), because a spec that links the leaf library cannot pull `util/**` in. A
/// transcription error in `gfx/grid/exp_table.{h,cpp}` or `sine_table.{h,cpp}` would therefore be
/// invisible to the byte-compares below; those files have to be checked against
/// `util/lookuptables` and `util/waves.h` by eye (they were, entry for entry).
#include "gfx/grid/exp_table.h"
#include "gfx/grid/pad_grid.h"
#include "gfx/grid/sine_table.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

namespace legacy {

// definitions_cxx.hpp
constexpr int32_t kDisplayWidth = 16;
constexpr int32_t kDisplayWidthMagnitude = 4;
constexpr int32_t kSideBarWidth = 2;
constexpr int32_t kDisplayHeight = 8;
constexpr int32_t kTotalWidth = kDisplayWidth + kSideBarWidth;

using Image = RGB[kDisplayHeight][kTotalWidth];
using OccupancyMask = uint8_t[kDisplayHeight][kTotalWidth];

/// util/const_functions.h
inline uint32_t rshift_round(uint32_t value, uint32_t rshift) {
	return (value + (1 << (rshift - 1))) >> rshift;
}

/// util/functions.h:337 -- verbatim, `occupancyFromWhichColourCame` and all.
inline RGB drawSquare(const RGB& squareColour, int32_t intensity, const RGB& square, uint8_t* occupancyMask,
                      int32_t occupancyFromWhichColourCame) {
	(void)occupancyFromWhichColourCame;
	int32_t modifiedIntensity = intensity;
	int32_t colourRemainingAmount = 65536;
	int32_t maxOldOccupancy = (65536 - modifiedIntensity) >> 10;
	if (*occupancyMask > maxOldOccupancy) {
		colourRemainingAmount = (maxOldOccupancy << 16) / *occupancyMask;
	}
	int32_t newOccupancyMaskValue =
	    static_cast<int32_t>(rshift_round(static_cast<uint32_t>(*occupancyMask * colourRemainingAmount), 16))
	    + static_cast<int32_t>(rshift_round(static_cast<uint32_t>(modifiedIntensity), 10));
	*occupancyMask = static_cast<uint8_t>(std::min<int32_t>(64, newOccupancyMaskValue));
	return RGB::blend2(square, squareColour, static_cast<uint32_t>(colourRemainingAmount),
	                   static_cast<uint32_t>(modifiedIntensity));
}

inline bool anythingHere(const RGB& c) {
	return (c.r | c.g | c.b) != 0;
}

// -------------------------------------------------------------------------------------------
// renderExplodeAnimation -- pad_leds.cpp:645-732.
//
// `imageStore` here is the legacy 10-row store: row 0 is the off-screen row above the display,
// rows 1..8 are the display, row 9 the off-screen row below (session_view.cpp:2604-2606). The
// tween's single-`PadGrid` source has no such margin, so the two margin rows are always black --
// transcribed anyway, so the loop bounds stay legacy's.
inline void renderExplodeAnimation(const RGB imageStore[kDisplayHeight + 2][kTotalWidth],
                                   const uint8_t occupancyMaskStore[kDisplayHeight + 2][kTotalWidth],
                                   int32_t explodedness, int32_t explodeAnimationXStartBig,
                                   int32_t explodeAnimationXWidthBig, int32_t explodeAnimationYOriginBig, Image image) {
	std::memset(image, 0, sizeof(RGB) * kDisplayHeight * kTotalWidth);
	OccupancyMask occupancyMask;
	std::memset(occupancyMask, 0, sizeof(occupancyMask));

	int32_t xDestArray[kDisplayWidth];
	uint16_t xIntensityArray[kDisplayWidth][2];
	std::memset(xIntensityArray, 0, sizeof(xIntensityArray));

	int32_t xStart = 0;
	int32_t xEnd = kDisplayWidth;

	for (int32_t xSource = 0; xSource < kDisplayWidth; xSource++) {
		int32_t xSourceBig = xSource << 16;
		int32_t xOriginBig = explodeAnimationXStartBig
		                     + static_cast<int32_t>((static_cast<int64_t>(explodeAnimationXWidthBig) * xSourceBig)
		                                            >> (kDisplayWidthMagnitude + 16));
		xOriginBig &= ~static_cast<int32_t>(65535);

		int32_t xSourceBigRelativeToOrigin = xSourceBig - xOriginBig;
		int32_t xDestBig =
		    xOriginBig + static_cast<int32_t>((static_cast<int64_t>(xSourceBigRelativeToOrigin) * explodedness) >> 16);

		xDestArray[xSource] = xDestBig >> 16;

		if (xDestArray[xSource] < -1) {
			xStart = xSource + 1;
			continue;
		}
		else if (xDestArray[xSource] >= kDisplayWidth) {
			xEnd = xSource;
			break;
		}
		xIntensityArray[xSource][1] = static_cast<uint16_t>(xDestBig);
		xIntensityArray[xSource][0] = static_cast<uint16_t>(65535 - xIntensityArray[xSource][1]);
	}

	for (int32_t ySource = -1; ySource < kDisplayHeight + 1; ySource++) {
		int32_t ySourceBig = ySource << 16;
		int32_t ySourceBigRelativeToOrigin = ySourceBig - explodeAnimationYOriginBig;
		int32_t yDestBig =
		    explodeAnimationYOriginBig
		    + static_cast<int32_t>((static_cast<int64_t>(ySourceBigRelativeToOrigin) * explodedness) >> 16);
		int32_t yDest = yDestBig >> 16;

		uint32_t yIntensity[2];
		yIntensity[1] = static_cast<uint32_t>(yDestBig & 65535);
		yIntensity[0] = 65535 - yIntensity[1];

		for (int32_t xSource = xStart; xSource < xEnd; xSource++) {
			if (occupancyMaskStore[ySource + 1][xSource]) {
				for (int32_t xOffset = 0; xOffset < 2; xOffset++) {
					int32_t xNow = xDestArray[xSource] + xOffset;
					if (xNow < 0) {
						continue;
					}
					if (xNow >= kDisplayWidth) {
						break;
					}
					for (int32_t yOffset = 0; yOffset < 2; yOffset++) {
						int32_t yNow = yDest + yOffset;
						if (yNow < 0) {
							continue;
						}
						if (yNow >= kDisplayHeight) {
							break;
						}
						uint32_t intensityNow = (yIntensity[yOffset] * xIntensityArray[xSource][xOffset]) >> 16;
						image[yNow][xNow] =
						    drawSquare(imageStore[ySource + 1][xSource], static_cast<int32_t>(intensityNow),
						               image[yNow][xNow], &occupancyMask[yNow][xNow], 64);
					}
				}
			}
		}
	}
}

// -------------------------------------------------------------------------------------------
// renderInstrumentClipCollapseAnimation -- pad_leds.cpp:344-517, reduced to the part that does not
// read a global. The greyed-out region past the clip length (:390-418), the triplet columns
// (:435-441), the expanding mute column (:438-444) and the keyboard sidebar morph (:456-501) all
// depend on `currentSong` / `instrumentClipView` / the UI mode, so `transitioning` is false here
// and those branches are skipped -- exactly the shape the toolkit core exposes.
//
// `image` arrives pre-filled with the destination frame instead of legacy's `memset` + Session
// pads (:347-356); `occupancyMask` is 64 wherever it is lit, as :354 sets it.
inline void renderInstrumentClipCollapseAnimation(const RGB imageStore[kDisplayHeight][kTotalWidth],
                                                  const uint8_t occupancyMaskStore[kDisplayHeight][kTotalWidth],
                                                  const uint8_t destinationOccupied[kDisplayHeight][kTotalWidth],
                                                  const int16_t animatedRowGoingFrom[kDisplayHeight],
                                                  const int16_t animatedRowGoingTo[kDisplayHeight],
                                                  int32_t numAnimatedRows, int32_t progress, Image image) {
	OccupancyMask occupancyMask;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kTotalWidth; x++) {
			occupancyMask[y][x] = destinationOccupied[y][x] ? 64 : 0;
		}
	}

	uint16_t intensity1Array[kDisplayHeight];
	uint16_t intensity2Array[kDisplayHeight];
	int8_t newRowPosition1Array[kDisplayHeight];

	for (int32_t i = 0; i < numAnimatedRows; i++) {
		int32_t newRowPosition =
		    static_cast<int32_t>(animatedRowGoingFrom[i]) * 65536
		    + (static_cast<int32_t>(animatedRowGoingTo[i]) - animatedRowGoingFrom[i]) * (65536 - progress);
		newRowPosition1Array[i] = static_cast<int8_t>(newRowPosition >> 16);
		intensity2Array[i] = static_cast<uint16_t>(newRowPosition);
		intensity1Array[i] = static_cast<uint16_t>(65535 - intensity2Array[i]);
	}

	for (int32_t col = 0; col < kTotalWidth; col++) {
		for (int32_t i = 0; i < numAnimatedRows; i++) {
			if (!occupancyMaskStore[i][col]) {
				continue;
			}
			RGB squareColours = imageStore[i][col];
			int32_t intensity1 = intensity1Array[i];
			int32_t intensity2 = intensity2Array[i];

			if (newRowPosition1Array[i] >= 0 && newRowPosition1Array[i] < kDisplayHeight) {
				image[newRowPosition1Array[i]][col] =
				    drawSquare(squareColours, intensity1, image[newRowPosition1Array[i]][col],
				               &occupancyMask[newRowPosition1Array[i]][col], occupancyMaskStore[i][col]);
			}
			if (newRowPosition1Array[i] >= -1 && newRowPosition1Array[i] < kDisplayHeight - 1) {
				image[newRowPosition1Array[i] + 1][col] =
				    drawSquare(squareColours, intensity2, image[newRowPosition1Array[i] + 1][col],
				               &occupancyMask[newRowPosition1Array[i] + 1][col], occupancyMaskStore[i][col]);
			}
		}
	}
}

// -------------------------------------------------------------------------------------------
// renderZoomedSquare -- pad_leds.cpp:1338-1370.
inline void renderZoomedSquare(int32_t outputSquareStartOnSourceImage, int32_t outputSquareEndOnSourceImage,
                               uint32_t sourceImageTimesBiggerThanNormal, uint32_t sourceImageFade, uint32_t* output,
                               const RGB* inputImageRow, int32_t inputImageWidth, bool* drawingAnything) {
	int32_t outImageStartSquareLeftEdge =
	    static_cast<int32_t>(static_cast<uint32_t>(outputSquareStartOnSourceImage) & ~static_cast<uint32_t>(65535));
	for (int32_t sourceSquareLeftEdge = std::max<int32_t>(0, outImageStartSquareLeftEdge); true;
	     sourceSquareLeftEdge += 65536) {
		if (sourceSquareLeftEdge >= outputSquareEndOnSourceImage) {
			break;
		}
		int32_t xSource = sourceSquareLeftEdge >> 16;
		if (xSource >= inputImageWidth) {
			break;
		}
		if (!anythingHere(inputImageRow[xSource])) {
			continue;
		}
		*drawingAnything = true;

		int32_t sourceSquareRightEdge = sourceSquareLeftEdge + 65536;
		uint32_t intensity = static_cast<uint32_t>(std::min(sourceSquareRightEdge, outputSquareEndOnSourceImage)
		                                           - std::max(sourceSquareLeftEdge, outputSquareStartOnSourceImage));
		intensity = static_cast<uint32_t>(
		    (static_cast<uint64_t>(intensity) * sourceImageFade * sourceImageTimesBiggerThanNormal) >> 32);

		output[0] += static_cast<uint32_t>(inputImageRow[xSource].r) * intensity;
		output[1] += static_cast<uint32_t>(inputImageRow[xSource].g) * intensity;
		output[2] += static_cast<uint32_t>(inputImageRow[xSource].b) * intensity;
	}
}

// renderZoomWithProgress -- pad_leds.cpp:1216-1336, with `zoomPinSquareInner`/`zoomPinSquareOuter`
// left as the single `zoomPinSquare` array the two #defines at :1238-1239 alias them to, and
// `transitionTakingPlaceOnRow[]` all true. Only main columns are written; `image`'s sidebar is
// whatever the caller left in it.
inline void renderZoomWithProgress(int32_t inImageTimesBiggerThanNative, uint32_t inImageFadeAmount,
                                   int8_t zoomMagnitude, const RGB innerImage[kDisplayHeight][kTotalWidth],
                                   const RGB outerImage[kDisplayHeight][kTotalWidth], int32_t zoomPinSquare,
                                   Image image) {
	uint32_t outImageTimesBiggerThanNative = static_cast<uint32_t>(inImageTimesBiggerThanNative) << zoomMagnitude;
	uint32_t inImageTimesSmallerThanNative = 4294967295u / static_cast<uint32_t>(inImageTimesBiggerThanNative);
	uint32_t outImageTimesSmallerThanNative = 4294967295u / outImageTimesBiggerThanNative;

	const int32_t innerImageLeftEdge = 0;
	const int32_t innerImageRightEdge = kDisplayWidth;
	const int32_t outerImageRightEdge = kDisplayWidth;

	int32_t outputSquareStartOnInImage[kDisplayWidth];
	int32_t outputSquareEndOnInImage[kDisplayWidth];
	int32_t outputSquareStartOnOutImage[kDisplayWidth];
	int32_t outputSquareEndOnOutImage[kDisplayWidth];
	uint16_t inImageFadePerCol[kDisplayWidth];

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (yDisplay == 0) { // legacy: recompute only when zoomPinSquare[y] changes; it never does
			int32_t inImagePos0Onscreen = zoomPinSquare - (zoomPinSquare >> 8) * (inImageTimesBiggerThanNative >> 8);
			int32_t inImageLeftEdgeOnscreen = inImagePos0Onscreen + inImageTimesBiggerThanNative * innerImageLeftEdge;
			int32_t inImageRightEdgeOnscreen = inImagePos0Onscreen + inImageTimesBiggerThanNative * innerImageRightEdge;

			for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
				int32_t outputSquareLeftEdge = xDisplay * 65536;
				int32_t outputSquareRightEdge = outputSquareLeftEdge + 65536;

				int32_t inImageOverlap = std::min(outputSquareRightEdge, inImageRightEdgeOnscreen)
				                         - std::max(outputSquareLeftEdge, inImageLeftEdgeOnscreen);
				if (inImageOverlap < 0) {
					inImageOverlap = 0;
				}
				inImageFadePerCol[xDisplay] =
				    static_cast<uint16_t>((static_cast<uint32_t>(inImageOverlap) * inImageFadeAmount) >> 16);

				int32_t outputSquareLeftEdgePositionRelativeToPinSquare = zoomPinSquare - outputSquareLeftEdge;

				int32_t outputSquareLeftEdgePositionOnInImageRelativeToPinSquare =
				    static_cast<int32_t>((static_cast<int64_t>(outputSquareLeftEdgePositionRelativeToPinSquare)
				                          * inImageTimesSmallerThanNative)
				                         >> 16);
				int32_t outputSquareLeftEdgePositionOnOutImageRelativeToPinSquare =
				    static_cast<int32_t>((static_cast<int64_t>(outputSquareLeftEdgePositionRelativeToPinSquare)
				                          * outImageTimesSmallerThanNative)
				                         >> 16);

				outputSquareStartOnOutImage[xDisplay] =
				    zoomPinSquare - outputSquareLeftEdgePositionOnOutImageRelativeToPinSquare;
				outputSquareStartOnInImage[xDisplay] =
				    zoomPinSquare - outputSquareLeftEdgePositionOnInImageRelativeToPinSquare;

				outputSquareEndOnInImage[xDisplay] =
				    outputSquareStartOnInImage[xDisplay] + static_cast<int32_t>(inImageTimesSmallerThanNative);
				outputSquareEndOnOutImage[xDisplay] =
				    outputSquareStartOnOutImage[xDisplay] + static_cast<int32_t>(outImageTimesSmallerThanNative);
			}
		}

		for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			uint32_t outValue[3];
			std::memset(outValue, 0, sizeof(outValue));
			bool drawingAnything = false;

			if (inImageFadePerCol[xDisplay]) {
				renderZoomedSquare(outputSquareStartOnInImage[xDisplay], outputSquareEndOnInImage[xDisplay],
				                   static_cast<uint32_t>(inImageTimesBiggerThanNative), inImageFadePerCol[xDisplay],
				                   outValue, innerImage[yDisplay], innerImageRightEdge, &drawingAnything);
			}
			renderZoomedSquare(outputSquareStartOnOutImage[xDisplay], outputSquareEndOnOutImage[xDisplay],
			                   outImageTimesBiggerThanNative, 65535u - inImageFadePerCol[xDisplay], outValue,
			                   outerImage[yDisplay], outerImageRightEdge, &drawingAnything);

			if (drawingAnything) {
				for (int32_t colour = 0; colour < 3; colour++) {
					int32_t result = static_cast<int32_t>(rshift_round(outValue[colour], 16));
					image[yDisplay][xDisplay][static_cast<size_t>(colour)] =
					    static_cast<uint8_t>(std::min<int32_t>(std::numeric_limits<uint8_t>::max(), result));
				}
			}
			else {
				image[yDisplay][xDisplay] = RGB{0, 0, 0};
			}
		}
	}
}

// renderZoom -- pad_leds.cpp:1173-1210 (the progress-to-scale/fade arithmetic only; the UI-mode
// bookkeeping and the send-out at either end are the caller's).
inline void renderZoom(const RGB innerImage[kDisplayHeight][kTotalWidth],
                       const RGB outerImage[kDisplayHeight][kTotalWidth], bool zoomingIn, int8_t zoomMagnitude,
                       int32_t zoomPinSquare, int32_t transitionProgress, Image image) {
	if (!zoomingIn) {
		transitionProgress = 65536 - transitionProgress;
	}

	uint32_t sineValue = static_cast<uint32_t>(
	    (deluge::gfx::grid::sine_q15(static_cast<uint32_t>(transitionProgress + 98304) & 131071, 17) >> 16) + 32768);

	int32_t powersOfTwo = (static_cast<int32_t>(transitionProgress >> 7) - 512) << zoomMagnitude;
	int32_t fine = powersOfTwo & 1023;
	int32_t coarse = powersOfTwo >> 10;

	int32_t inImageTimesBiggerThanNormal =
	    deluge::gfx::grid::interpolate_table(static_cast<uint32_t>(fine), 10, deluge::gfx::grid::kExpTableSmall);
	inImageTimesBiggerThanNormal = deluge::gfx::grid::increase_magnitude(inImageTimesBiggerThanNormal, coarse - 14);

	renderZoomWithProgress(inImageTimesBiggerThanNormal, sineValue, zoomMagnitude, innerImage, outerImage,
	                       zoomPinSquare, image);
}

} // namespace legacy
