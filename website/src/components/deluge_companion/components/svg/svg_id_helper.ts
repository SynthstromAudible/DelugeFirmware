import type { Coordinate } from "../../data/hardware_coordinates.js"

// Builds reverse lookup from hardware coordinates to one-or-more SVG IDs.
export function buildCoordinateToSvgIdsMap(
  coordinates: Record<string, Coordinate>,
): Map<string, string[]> {
  const coordinateToSvgIds = new Map<string, string[]>()

  for (const [svgId, coord] of Object.entries(coordinates)) {
    const key = `${coord.x},${coord.y}`
    if (!coordinateToSvgIds.has(key)) {
      coordinateToSvgIds.set(key, [])
    }

    const ids = coordinateToSvgIds.get(key)
    if (ids) {
      ids.push(svgId)
    }
  }

  return coordinateToSvgIds
}

// Resolves all possible SVG IDs for one grid coordinate.
export function getSvgIdsForCoordinate(
  coordinateToSvgIds: Map<string, string[]>,
  x: number,
  y: number,
): string[] {
  const key = `${x},${y}`
  const mapped = coordinateToSvgIds.get(key) ?? []

  // Deluge.svg uses mixed formats like Button-23 and Button-203.
  // For first-column pads (x=0), some assets also use Button-7..Button-0.
  const firstColumnId = x === 0 ? `Button-${y}` : undefined
  const compactId = `Button-${y}${x}`
  const paddedId = `Button-${y}${String(x).padStart(2, "0")}`

  return [
    ...new Set(
      [...mapped, compactId, paddedId, firstColumnId].filter(
        Boolean,
      ) as string[],
    ),
  ]
}
