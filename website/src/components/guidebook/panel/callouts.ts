import type { Box, PanelControl, PanelMap } from "./panel-map.ts"

/** A label pointing at a named control, placed on one side of the figure. */
export type Callout = {
  control: string
  text: string
  side: "left" | "right" | "top" | "bottom"
}

/** Where a callout's label sits and its leader line runs, in panel coordinates. */
export type PlacedCallout = Callout & {
  /** The label's anchor on the window edge (its leader starts here). */
  label: { x: number; y: number }
  leader: { x1: number; y1: number; x2: number; y2: number }
}

/** Minimum spacing between stacked labels, as a fraction of the window's height (or width). */
export const LABEL_PITCH = 0.07

/** The point on a control's outline in the direction of (x, y). */
function edgeToward(c: PanelControl, x: number, y: number) {
  const dx = x - c.cx
  const dy = y - c.cy
  if (c.shape === "circle") {
    const d = Math.hypot(dx, dy) || 1
    return { x: c.cx + (c.r * dx) / d, y: c.cy + (c.r * dy) / d }
  }
  const t = Math.min(
    dx ? c.w / 2 / Math.abs(dx) : Infinity,
    dy ? c.h / 2 / Math.abs(dy) : Infinity,
  )
  return { x: c.cx + dx * t, y: c.cy + dy * t }
}

/**
 * Lay out callouts around a window onto the panel: labels on their side's window edge, level with
 * their control and pushed apart so none overlap; leaders from the label to the control's outline.
 */
export function layoutCallouts(
  callouts: Callout[],
  panel: PanelMap,
  window: Box,
): PlacedCallout[] {
  const [wx, wy, ww, wh] = window
  const control = (id: string) => {
    const c = panel.controls[id]
    if (!c) throw new Error(`callout: no control ${id} on the panel`)
    return c
  }
  const placed = new Map<Callout, { x: number; y: number }>()
  for (const side of ["left", "right", "top", "bottom"] as const) {
    const vertical = side === "left" || side === "right"
    const group = callouts
      .filter((c) => c.side === side)
      .map((c) => ({ c, at: control(c.control) }))
      .sort((a, b) => (vertical ? a.at.cy - b.at.cy : a.at.cx - b.at.cx))
    let last = -Infinity
    for (const { c, at } of group) {
      const pos = Math.max(
        vertical ? at.cy : at.cx,
        last + LABEL_PITCH * (vertical ? wh : ww),
      )
      last = pos
      placed.set(
        c,
        vertical
          ? { x: side === "left" ? wx : wx + ww, y: pos }
          : { x: pos, y: side === "top" ? wy : wy + wh },
      )
    }
  }
  return callouts.map((c) => {
    const label = placed.get(c) ?? { x: wx, y: wy }
    const end = edgeToward(control(c.control), label.x, label.y)
    return {
      ...c,
      label,
      leader: { x1: label.x, y1: label.y, x2: end.x, y2: end.y },
    }
  })
}

/**
 * Tops for labels of the given heights, each centred on its control where it can be, pushed
 * apart (in order) so none overlap, and kept within 0..limit. All values in the same units (px).
 */
export function stackLabels(
  centres: number[],
  heights: number[],
  gap: number,
  limit: number,
): number[] {
  const tops: number[] = []
  centres.forEach((c, i) => {
    const below = i ? tops[i - 1] + heights[i - 1] + gap : -Infinity
    tops.push(Math.max(c - heights[i] / 2, below))
  })
  for (let i = tops.length - 1; i >= 0; i--) {
    const ceiling = i === tops.length - 1 ? limit : tops[i + 1] - gap
    tops[i] = Math.max(0, Math.min(tops[i], ceiling - heights[i]))
  }
  return tops
}

/** Whether labels of these heights, with gaps between them, fit in `limit`. */
export function labelsFit(heights: number[], gap: number, limit: number) {
  const total =
    heights.reduce((a, h) => a + h, 0) + gap * Math.max(0, heights.length - 1)
  return total <= limit
}
