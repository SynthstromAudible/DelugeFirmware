import {
  type EnvelopeData,
  type Settings,
  curve,
  endTime,
  levelAt,
  formatTime,
  niceTicks,
  timing,
} from "./envelope.ts"

/** What the envelope modulates: volume (ENV1's default), or another parameter, bipolar about its value. */
export type Destination = "volume" | "other"

/*
 * The geometry and look of the book's figure (fig 6.3-1), at the book's scale (1 unit = 4/3 px):
 * thin grey guides at note on / off and at each stage's end, grey dimension arrows naming the
 * stages, a thin accent-red curve, and for other destinations the dashed destination value with
 * dotted ± mod depth bounds and a mod depth arrow.
 */
export const VIEW = { w: 560, h: 218 }
const X0 = 75 // note on
const PAD_R = 12
/** Room right of the curve for the mod depth arrow. */
const DEPTH_W = 64
const Y_NOTE = 21 // note on / off labels
const Y_GUIDE = 27 // guides start
const Y_TOP = 40 // level 1 (+ mod depth)
const Y_BOT = 155 // level 0 (− mod depth)
const Y_MID = (Y_TOP + Y_BOT) / 2
const Y_DIM = 161 // stage dimension arrows
const Y_NAME = 178
const Y_KIND = 190
const Y_GUIDE_END = 198
/** Room a stage name needs along the axis. */
const LABEL_W = 54
const HEAD = 7.7 // arrowhead length

const esc = (s: string) =>
  s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
const n = (v: number) => Number(v.toFixed(1))
/** A round tick's time, without trailing zeros: 2 s, 500 ms, 1.5 s. */
const tickLabel = (u: number) =>
  formatTime(u)
    .replace(/(\.\d*?)0+ /, "$1 ")
    .replace(/\. /, " ")

/** An open arrowhead (the book's: paper-filled, grey outline) with its tip at (x, y). */
function head(x: number, y: number, dir: "l" | "r" | "u" | "d"): string {
  const w = HEAD / 2
  const pts = {
    l: [x + HEAD, y - w, x + HEAD, y + w],
    r: [x - HEAD, y - w, x - HEAD, y + w],
    u: [x - w, y + HEAD, x + w, y + HEAD],
    d: [x - w, y - HEAD, x + w, y - HEAD],
  }[dir].map(n)
  return `<path class="gb-env-head" d="M${n(x)} ${n(y)}L${pts[0]} ${pts[1]}L${pts[2]} ${pts[3]}Z"/>`
}

/** A dimension line from a to b, arrowheads at both ends where there is room. */
function dimension(
  a: [number, number],
  b: [number, number],
  vertical: boolean,
): string {
  const len = vertical ? b[1] - a[1] : b[0] - a[0]
  const line = `<line class="gb-env-dim" x1="${n(a[0])}" y1="${n(a[1])}" x2="${n(b[0])}" y2="${n(b[1])}"/>`
  if (len < HEAD * 2.5) return line
  return (
    line +
    (vertical
      ? head(a[0], a[1], "u") + head(b[0], b[1], "d")
      : head(a[0], a[1], "l") + head(b[0], b[1], "r"))
  )
}

/**
 * The envelope plot's SVG content (for a `viewBox="0 0 <width> 218"` svg, `width` 560 by
 * default), in the book's figure's style. The time axis fits the whole envelope, so stages keep
 * their true proportions. `now` (seconds since note on) places the playhead of a note being
 * played; while `holding`, the note has not been let go yet (`gate` is then ignored: the curve
 * runs to `now`).
 */
export function renderPlot(
  data: EnvelopeData,
  s: Settings,
  opts: {
    gate: number
    destination: Destination
    now?: number
    holding?: boolean
    tMax?: number
    /** The plot's width in px: draw at the size shown so text keeps its size. */
    width?: number
  },
): string {
  const { destination, now, holding = false } = opts
  const other = destination === "other"
  const width = opts.width ?? VIEW.w
  const X1 = width - PAD_R - (other ? DEPTH_W : 0)
  const gate = holding ? Math.max(opts.gate, now ?? 0) + 1e-9 : opts.gate
  const t = timing(data, s)
  const end = holding ? (now ?? 0) : endTime(data, s, gate)
  const tMax = Math.max(opts.tMax ?? 0, end, 1e-4)
  const x = (u: number) => X0 + (Math.min(u, tMax) / tMax) * (X1 - X0)
  const y = (level: number) => Y_BOT - level * (Y_BOT - Y_TOP)
  const pts = curve(data, s, gate)
  const shown =
    now === undefined
      ? pts
      : [
          ...pts.filter((p) => p.t < now),
          { t: now, ...levelAt(data, s, now, gate) },
        ]
  const d = shown
    .map((p, i) => `${i ? "L" : "M"}${n(x(p.t))} ${n(y(p.level))}`)
    .join("")
  const out: string[] = []
  const held = Math.min(gate, end)
  const relEnd = holding ? held : end

  // the level range: dotted bounds; for other destinations the dashed destination value between
  const boundEnd = other ? X1 + DEPTH_W - 8 : X1
  out.push(
    `<line class="gb-env-bound" x1="${X0}" y1="${Y_TOP}" x2="${n(boundEnd)}" y2="${Y_TOP}"/>`,
    `<line class="gb-env-bound" x1="${X0}" y1="${Y_BOT}" x2="${n(boundEnd)}" y2="${Y_BOT}"/>`,
  )
  if (other) {
    const depthX = n(X1 + DEPTH_W / 2)
    out.push(
      `<line class="gb-env-dest" x1="${X0 - 20}" y1="${Y_MID}" x2="${n(boundEnd)}" y2="${Y_MID}"/>`,
      `<circle class="gb-env-dest-dot" cx="${X0 - 20}" cy="${Y_MID}" r="3.3"/>`,
      `<text class="gb-env-axis-label" x="${X0 - 49}" y="${Y_MID - 10}" text-anchor="middle">Mod</text>`,
      `<text class="gb-env-axis-label" x="${X0 - 49}" y="${Y_MID}" text-anchor="middle" dominant-baseline="middle">destination</text>`,
      `<text class="gb-env-axis-label" x="${X0 - 49}" y="${Y_MID + 13}" text-anchor="middle">Value</text>`,
      dimension([depthX, Y_TOP], [depthX, Y_BOT], true),
      `<ellipse class="gb-env-depth-mark" cx="${depthX}" cy="${Y_MID}" rx="4.7" ry="3"/>`,
      `<text class="gb-env-axis-label" x="${depthX}" y="${Y_NOTE}" text-anchor="middle">Mod depth</text>`,
    )
  } else {
    out.push(
      `<text class="gb-env-axis-label" x="${X0 - 8}" y="${Y_TOP}" text-anchor="end" dominant-baseline="middle">Full</text>`,
      `<text class="gb-env-axis-label" x="${X0 - 8}" y="${Y_BOT}" text-anchor="end" dominant-baseline="middle">Silent</text>`,
    )
  }

  // guides: note on and note off, and the end of each stage
  const guide = (gx: number, cls: string) =>
    `<line class="${cls}" x1="${n(gx)}" y1="${Y_GUIDE}" x2="${n(gx)}" y2="${Y_GUIDE_END}"/>`
  out.push(
    guide(x(0), "gb-env-guide"),
    `<text class="gb-env-note" x="${X0}" y="${Y_NOTE}" text-anchor="middle">Note ON</text>`,
  )
  for (const b of [t.attack, t.attack + t.decay])
    if (b < held) out.push(guide(x(b), "gb-env-stage-end"))
  if (!holding) {
    out.push(
      guide(x(gate), "gb-env-guide"),
      `<text class="gb-env-note" x="${n(x(gate))}" y="${Y_NOTE}" text-anchor="middle">Note OFF</text>`,
    )
    if (relEnd > held) out.push(guide(x(relEnd), "gb-env-stage-end"))
  }

  // each stage: a dimension arrow across its span, named underneath
  const spans: [string, string, number, number][] = [
    ["ATTACK", "(time)", 0, Math.min(t.attack, held)],
    ["DECAY", "(time)", t.attack, Math.min(t.attack + t.decay, held)],
    ["SUSTAIN", "(level)", t.attack + t.decay, held],
    ["RELEASE", "(time)", held, relEnd],
  ]
  // A stage shorter than half a pixel goes unnamed; the others keep a label's width apart,
  // pushed along the axis, with a leader where a label had to move off its span.
  const shownSpans = spans
    .map(([name, kind, a, b]) => ({ name, kind, xa: x(a), xb: x(b) }))
    .filter(({ xa, xb }) => xb - xa >= 0.5)
  const centres = shownSpans.map(({ xa, xb }) => (xa + xb) / 2)
  for (let i = 0; i < centres.length; i++)
    centres[i] = Math.max(
      centres[i],
      i ? centres[i - 1] + LABEL_W : X0 + LABEL_W / 2 - 20,
    )
  for (let i = centres.length - 1; i >= 0; i--)
    centres[i] = Math.min(
      centres[i],
      i < centres.length - 1 ? centres[i + 1] - LABEL_W : width - LABEL_W / 2,
    )
  shownSpans.forEach(({ name, kind, xa, xb }, i) => {
    const mid = (xa + xb) / 2
    const c = n(centres[i])
    out.push(
      dimension([xa + 1, Y_DIM], [xb - 1, Y_DIM], false),
      `<text class="gb-env-stage" x="${c}" y="${Y_NAME}" text-anchor="middle">${name}</text>`,
      `<text class="gb-env-kind" x="${c}" y="${Y_KIND}" text-anchor="middle">${kind}</text>`,
    )
    if (Math.abs(c - mid) > 8)
      out.push(
        `<line class="gb-env-dim" x1="${n(mid)}" y1="${Y_DIM}" x2="${c}" y2="${Y_NAME - 10}"/>`,
      )
  })

  // the sustain level: an arrow from the floor up to it, the knob value boxed on the way
  const susA = x(t.attack + t.decay)
  const susB = x(held)
  if (t.sustain > 0 && susB - susA >= 24 && t.attack + t.decay < held) {
    const sx = n((susA + susB) / 2)
    const top = y(t.sustain) + 2
    out.push(dimension([sx, top], [sx, Y_BOT - 2], true))
    const by = n((top + Y_BOT) / 2)
    out.push(
      `<rect class="gb-env-value-box" x="${n(sx - 12.7)}" y="${n(by - 7.6)}" width="25.4" height="15.2"/>`,
      `<text class="gb-env-value" x="${sx}" y="${by}" text-anchor="middle" dominant-baseline="central">${s.sustain}</text>`,
    )
  }

  // a light time ruler, for the true lengths
  for (const tick of niceTicks(tMax, width < 420 ? 4 : 6)) {
    const tx = n(x(tick))
    out.push(
      `<text class="gb-env-tick" x="${tx}" y="${VIEW.h - 4}" text-anchor="middle">${tick === 0 ? "0" : esc(tickLabel(tick))}</text>`,
    )
  }

  out.push(`<path class="gb-env-curve" d="${d}"/>`)
  const last = shown.at(-1)
  if (now !== undefined && last) {
    out.push(
      `<circle class="gb-env-dot" cx="${n(x(last.t))}" cy="${n(y(last.level))}" r="3.3"/>`,
    )
  }
  return out.join("")
}
