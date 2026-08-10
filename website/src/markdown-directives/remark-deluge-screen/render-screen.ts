import { gunzipSync } from "zlib"

export const SCREEN_WIDTH = 128
export const SCREEN_HEIGHT = 48

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
  const width = px_width * (SCREEN_WIDTH + PADDING * 2)
  const height = px_height * (SCREEN_HEIGHT + PADDING * 2)
  const rects: string[] = [
    `<rect x="0" y="0" width="${width}" height="${height}" fill="${BACKGROUND_COLOR}" />`,
  ]

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
          const x =
            j * px_width + (px_width * PIXEL_GAP) / 2 + px_width * PADDING
          const yPos =
            y * px_height + (px_height * PIXEL_GAP) / 2 + px_height * PADDING
          const w = px_width * (1 - PIXEL_GAP)
          const h = px_height * (1 - PIXEL_GAP)
          rects.push(
            `<rect x="${x}" y="${yPos}" width="${w}" height="${h}" fill="${FOREGROUND_COLOR}" />`,
          )
        }
      }
    }
  }

  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">${rects.join("")}</svg>`
  const base64Svg = Buffer.from(svg, "utf8").toString("base64")
  return `data:image/svg+xml;base64,${base64Svg}`
}
