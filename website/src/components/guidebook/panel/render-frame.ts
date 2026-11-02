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
  const classes = new Map<string, string[]>()
  const styles = new Map<string, string>()
  const addClass = (id: string, cls: string[]) =>
    classes.set(id, [...(classes.get(id) ?? []), ...cls])

  for (const [id, led] of Object.entries(frame.leds ?? {}))
    addClass(id, ledClasses(led))
  for (const { id, action } of highlights) addClass(id, ["hl", `hl-${action}`])
  for (const [id, colour] of Object.entries(frame.pads ?? {}))
    styles.set(id, `fill:${colour}`)

  const withControls = panelSvg.replace(
    /<(circle|rect)\b([^>]*?)\sdata-c="([^"]+)"/g,
    (whole, tag: string, before: string, id: string) => {
      const cls = [...new Set(classes.get(id) ?? [])]
      const extra = [
        cls.length ? ` class="${cls.join(" ")}"` : "",
        styles.has(id) ? ` style="${styles.get(id)}"` : "",
      ].join("")
      return extra ? `<${tag}${before} data-c="${id}"${extra}` : whole
    },
  )
  return withControls.replace(
    /viewBox="[^"]*"/,
    `viewBox="${window.join(" ")}"`,
  )
}
