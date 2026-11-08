import type { Frame, Led } from "./frames.ts"
import type { StepControl } from "./key-controls.ts"
import type { Box, PanelMap } from "./panel-map.ts"

/** A window onto the panel: a region name from the panel map, or an explicit box. */
export function windowBox(window: string | Box, panel: PanelMap): Box {
  if (typeof window !== "string") return window
  const box = panel.regions[window]
  if (!box)
    throw new Error(
      `no panel region "${window}" (have: ${Object.keys(panel.regions).join(", ")})`,
    )
  return box
}

/**
 * `window` grown, if needed, to include every listed control (with a margin), within the
 * panel. A step's controls are always visible even if the chosen window missed one.
 */
export function fitWindow(window: Box, ids: string[], panel: PanelMap): Box {
  let [x0, y0, x1, y1] = [
    window[0],
    window[1],
    window[0] + window[2],
    window[1] + window[3],
  ]
  const margin = 0.02 * panel.viewBox[2]
  for (const id of ids) {
    const c = panel.controls[id]
    if (!c) continue
    const [cx0, cy0, cx1, cy1] =
      c.shape === "circle"
        ? [c.cx - c.r, c.cy - c.r, c.cx + c.r, c.cy + c.r]
        : [c.x, c.y, c.x + c.w, c.y + c.h]
    if (cx0 < x0 || cy0 < y0 || cx1 > x1 || cy1 > y1) {
      x0 = Math.min(x0, cx0 - margin)
      y0 = Math.min(y0, cy0 - margin)
      x1 = Math.max(x1, cx1 + margin)
      y1 = Math.max(y1, cy1 + margin)
    }
  }
  const [, , pw, ph] = panel.viewBox
  x0 = Math.max(0, x0)
  y0 = Math.max(0, y0)
  x1 = Math.min(pw, x1)
  y1 = Math.min(ph, y1)
  return [
    Math.round(x0),
    Math.round(y0),
    Math.round(x1 - x0),
    Math.round(y1 - y0),
  ]
}

/**
 * The window for a demo with no authored frames: the smallest panel region that already holds
 * every control its steps use (with fitWindow's margin), else the whole panel.
 */
export function demoWindow(ids: string[], panel: PanelMap): Box {
  const area = (b: Box) => b[2] * b[3]
  const fits = Object.values(panel.regions)
    .filter((r) => fitWindow(r, ids, panel).every((v, i) => v === r[i]))
    .sort((a, b) => area(a) - area(b))
  return ids.length && fits.length ? fits[0] : panel.viewBox
}

const ledClasses = (led: Led): string[] => {
  const { colour, blink } =
    typeof led === "string" ? { colour: led, blink: false } : led
  if (colour === "off") return []
  return ["led", `led-${colour}`, ...(blink ? ["led-blink"] : [])]
}

/** What one control looks like in a frame: its classes and/or inline fill. */
export type ControlAttributes = { id: string; class?: string; style?: string }

/**
 * The classes (lit LEDs, step highlights) and fills (pad colours) a frame gives each control.
 * Shared by the build (renderFrame) and the page script, which applies a step's frame to the
 * figure's one SVG.
 */
export function frameAttributes(
  frame: Frame,
  highlights: StepControl[],
): ControlAttributes[] {
  const classes = new Map<string, string[]>()
  const styles = new Map<string, string>()
  const addClass = (id: string, cls: string[]) =>
    classes.set(id, [...(classes.get(id) ?? []), ...cls])

  for (const [id, led] of Object.entries(frame.leds ?? {}))
    addClass(id, ledClasses(led))
  for (const { id, action } of highlights) addClass(id, ["hl", `hl-${action}`])
  for (const [id, colour] of Object.entries(frame.pads ?? {}))
    styles.set(id, `fill:${colour}`)

  const ids = [...new Set([...classes.keys(), ...styles.keys()])]
  return ids
    .map((id) => {
      const cls = [...new Set(classes.get(id) ?? [])]
      return {
        id,
        ...(cls.length ? { class: cls.join(" ") } : {}),
        ...(styles.has(id) ? { style: styles.get(id) } : {}),
      }
    })
    .filter((a) => a.class || a.style)
}

/**
 * The panel SVG showing one frame: cropped to `window`, pad colours filled, lit LEDs and the
 * step's controls marked with classes (styled in guidebook-panel.css). Pure string work on the
 * panel tool's output, where each control is one element carrying `data-c="<id>"`.
 */
export function renderFrame(
  panelSvg: string,
  frame: Frame,
  highlights: StepControl[],
  window: Box,
): string {
  const attrs = new Map(
    frameAttributes(frame, highlights).map((a) => [a.id, a]),
  )
  const withControls = panelSvg.replace(
    /<(circle|rect)\b([^>]*?)\sdata-c="([^"]+)"/g,
    (whole, tag: string, before: string, id: string) => {
      const a = attrs.get(id)
      if (!a) return whole
      const extra = [
        a.class ? ` class="${a.class}"` : "",
        a.style ? ` style="${a.style}"` : "",
      ].join("")
      return `<${tag}${before} data-c="${id}"${extra}`
    },
  )
  return withControls.replace(
    /viewBox="[^"]*"/,
    `viewBox="${window.join(" ")}"`,
  )
}
