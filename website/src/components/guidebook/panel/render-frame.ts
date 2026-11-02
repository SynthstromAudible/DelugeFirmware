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
