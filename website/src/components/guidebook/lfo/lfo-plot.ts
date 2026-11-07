/**
 * One LFO shape's plot in the book's style (figs 6.3-2 to 6.3-4): the level axis, a dashed
 * centre line, AMPLITUDE and TIME, and the signal as a thin accent line. The newest value is at
 * the right; `shift` (0..1 of one value's spacing) slides the trace left between values, so it
 * advances smoothly. `clipId` must be unique on the page.
 */
export const TILE = { w: 200, h: 112 }
const AX = 16 // level axis
const RIGHT = 4
const TOP = 6
const BOT = 86
const MID = (TOP + BOT) / 2

const n = (v: number) => Number(v.toFixed(1))

export function renderTile(
  values: number[],
  opts: { clipId: string; width?: number; shift?: number; scale?: number },
): string {
  const width = opts.width ?? TILE.w
  const shift = opts.shift ?? 0
  const scale = opts.scale ?? 1
  const x1 = width - RIGHT
  const dx = (x1 - AX) / Math.max(1, values.length - 2)
  const y = (v: number) =>
    MID - Math.max(-1, Math.min(1, v * scale)) * (MID - TOP)
  const d = values
    .map((v, i) => `${i ? "L" : "M"}${n(AX + (i - shift) * dx)} ${n(y(v))}`)
    .join("")
  return [
    `<clipPath id="${opts.clipId}"><rect x="${AX}" y="0" width="${n(x1 - AX)}" height="${TILE.h}"/></clipPath>`,
    `<line class="gb-lfo-centre" x1="${AX}" y1="${MID}" x2="${n(x1)}" y2="${MID}"/>`,
    `<line class="gb-lfo-axis" x1="${AX}" y1="${TOP - 2}" x2="${AX}" y2="${BOT + 2}"/>`,
    `<text class="gb-lfo-label" transform="translate(9 ${MID}) rotate(-90)" text-anchor="middle">AMPLITUDE</text>`,
    `<text class="gb-lfo-label" x="${n((AX + x1) / 2)}" y="${TILE.h - 6}" text-anchor="middle">TIME</text>`,
    scale !== 1
      ? `<text class="gb-lfo-scale" x="${n(x1)}" y="${TOP + 6}" text-anchor="end">drawn ×${scale}</text>`
      : "",
    `<path class="gb-lfo-curve" clip-path="url(#${opts.clipId})" d="${d}"/>`,
  ].join("")
}
