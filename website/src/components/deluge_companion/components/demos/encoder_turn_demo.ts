import { Action } from "../../data/actions.js"

function prefersReducedMotion(): boolean {
  return (
    typeof window !== "undefined" &&
    window.matchMedia("(prefers-reduced-motion: reduce)").matches
  )
}

export function shouldBlinkTurnControl(
  actions: Set<Action> | undefined,
): boolean {
  if (!actions) return false
  return actions.has(Action.PRESS) || actions.has(Action.HOLD)
}

export function clearTurnIndicators(
  svgElement: SVGSVGElement | undefined,
): void {
  svgElement?.querySelectorAll(".dc-turn-indicator").forEach((el) => {
    el.remove()
  })
}

export function appendTurnIndicator(
  svgElement: SVGSVGElement | undefined,
  target: Element,
  options?: { angleDeg?: number },
): void {
  if (!svgElement || !(target instanceof SVGGraphicsElement)) return

  const ctm = svgElement.getScreenCTM()
  if (!ctm) return

  const rect = target.getBoundingClientRect()
  if (rect.width <= 0 || rect.height <= 0) return

  const centerClientX = rect.left + rect.width / 2
  const centerClientY = rect.top + rect.height / 2
  const radiusPx = Math.min(rect.width, rect.height) * 0.42

  const centerPoint = svgElement.createSVGPoint()
  centerPoint.x = centerClientX
  centerPoint.y = centerClientY
  const center = centerPoint.matrixTransform(ctm.inverse())

  const outerPoint = svgElement.createSVGPoint()
  outerPoint.x = centerClientX
  outerPoint.y = centerClientY - radiusPx * 0.76
  const outer = outerPoint.matrixTransform(ctm.inverse())

  const innerPoint = svgElement.createSVGPoint()
  innerPoint.x = centerClientX
  innerPoint.y = centerClientY - radiusPx * 0.18
  const inner = innerPoint.matrixTransform(ctm.inverse())

  const line = document.createElementNS("http://www.w3.org/2000/svg", "line")
  line.setAttribute("class", "dc-turn-indicator")
  line.setAttribute("x1", `${outer.x}`)
  line.setAttribute("y1", `${outer.y}`)
  line.setAttribute("x2", `${inner.x}`)
  line.setAttribute("y2", `${inner.y}`)

  if (typeof options?.angleDeg === "number") {
    line.setAttribute(
      "transform",
      `rotate(${options.angleDeg} ${center.x} ${center.y})`,
    )
    svgElement.appendChild(line)
    return
  }

  if (!prefersReducedMotion()) {
    const animate = document.createElementNS(
      "http://www.w3.org/2000/svg",
      "animateTransform",
    )
    animate.setAttribute("attributeName", "transform")
    animate.setAttribute("type", "rotate")
    animate.setAttribute(
      "values",
      `-28 ${center.x} ${center.y};28 ${center.x} ${center.y};-28 ${center.x} ${center.y}`,
    )
    animate.setAttribute("dur", "0.9s")
    animate.setAttribute("repeatCount", "indefinite")
    line.appendChild(animate)
  }

  svgElement.appendChild(line)
}
