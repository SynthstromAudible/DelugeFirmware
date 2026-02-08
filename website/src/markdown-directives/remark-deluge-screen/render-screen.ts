import { createCanvas } from "canvas"
import { gunzipSync } from "zlib"

export const SCREEN_WIDTH = 128
export const SCREEN_HEIGHT = 48

const DPI_SCALE = 2
const PIXEL_GAP = 0.1 // Ratio of pixel size to pixel gap
const PADDING = 2 // Padding in deluge pixels

/**
 * Render the screen via canvas to base64 png
 * @param screenDataBase64 - gzipped base64 encoding of the Uint8Array screen data
 */
export const renderScreen = (
  screenDataBase64: string,
  scale = 1,
): {
  screenData: string
  size: { width: number; height: number }
} => {
  const compressed = Buffer.from(screenDataBase64, "base64")
  const decompressed = gunzipSync(compressed as unknown as ArrayBuffer, {})
  const screenData = new Uint8Array(decompressed)

  if (screenData.length !== (SCREEN_WIDTH * SCREEN_HEIGHT) / 8) {
    throw new Error(`Invalid screen data length ${screenData.length}`)
  }

  return {
    screenData: drawOleddata(screenData, scale, scale),
    size: {
      width: SCREEN_WIDTH * scale + PADDING * 2,
      height: SCREEN_HEIGHT * scale + PADDING * 2,
    },
  }
}

const BACKGROUND_COLOR = "#000"
const FOREGROUND_COLOR = "#FFF"

// Code adapted from https://github.com/silicakes/deluge-extensions/blob/102e596bb3fafee8823e067bcfba76a045a334db/scripts/app.js#L576
const drawOleddata = (
  data: Uint8Array,
  px_width: number,
  px_height: number,
) => {
  const canvas = createCanvas(
    px_width * (SCREEN_WIDTH + PADDING * 2) * DPI_SCALE,
    px_height * (SCREEN_HEIGHT + PADDING * 2) * DPI_SCALE,
  )
  const ctx = canvas.getContext("2d")
  if (!ctx) throw new Error("Failed to create canvas context")
  ctx.scale(DPI_SCALE, DPI_SCALE)

  ctx.fillStyle = BACKGROUND_COLOR
  ctx.fillRect(
    0,
    0,
    px_width * (SCREEN_WIDTH + PADDING * 2),
    px_height * (SCREEN_HEIGHT + PADDING * 2),
  )

  ctx.fillStyle = FOREGROUND_COLOR
  ctx.translate(px_width * PADDING, px_height * PADDING)

  for (let blk = 0; blk < 6; blk++) {
    for (let rstride = 0; rstride < 8; rstride++) {
      const mask = 1 << rstride
      for (let j = 0; j < SCREEN_WIDTH; j++) {
        if (blk * SCREEN_WIDTH + j > data.length) {
          break
        }
        const idata = data[blk * SCREEN_WIDTH + j] & mask

        const y = blk * 8 + rstride

        if (idata > 0) {
          ctx.fillRect(
            j * px_width + (px_width * PIXEL_GAP) / 2,
            y * px_height + (px_height * PIXEL_GAP) / 2,
            px_width * (1 - PIXEL_GAP),
            px_height * (1 - PIXEL_GAP),
          )
        }
      }
    }
  }

  return canvas.toDataURL("image/png")
}
