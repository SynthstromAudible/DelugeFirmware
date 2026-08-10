/**
 * Hardware Coordinates Configuration
 *
 * Maps grid and grid-column SVG element IDs to physical X,Y coordinates.
 * X: 0 (far left) → 17 (far right)
 * Y: 0 (bottom) → 7 (top)
 *
 * Button Naming Convention: Button-[Y][XX]
 *   Y = Y coordinate (0=bottom, 7=top)
 *   XX = X coordinate (00-15 for main grid, 16 for launch, 17 for audition)
 */

export type Coordinate = {
  x: number
  y: number
}

/**
 * SVG Element ID to Hardware Coordinate mappings
 */
export const hwCoordinates: Record<string, Coordinate> = {
  // Main Grid (16 × 8) + Right pads (launch at X=16, audition at X=17)
  // All buttons follow pattern: Button-[Y][XX]

  // Row 7 (top)
  "Button-700": { x: 0, y: 7 },
  "Button-701": { x: 1, y: 7 },
  "Button-702": { x: 2, y: 7 },
  "Button-703": { x: 3, y: 7 },
  "Button-704": { x: 4, y: 7 },
  "Button-705": { x: 5, y: 7 },
  "Button-706": { x: 6, y: 7 },
  "Button-707": { x: 7, y: 7 },
  "Button-708": { x: 8, y: 7 },
  "Button-709": { x: 9, y: 7 },
  "Button-710": { x: 10, y: 7 },
  "Button-711": { x: 11, y: 7 },
  "Button-712": { x: 12, y: 7 },
  "Button-713": { x: 13, y: 7 },
  "Button-714": { x: 14, y: 7 },
  "Button-715": { x: 15, y: 7 },
  "Button-716": { x: 16, y: 7 },
  "Button-717": { x: 17, y: 7 },

  // Row 6
  "Button-600": { x: 0, y: 6 },
  "Button-601": { x: 1, y: 6 },
  "Button-602": { x: 2, y: 6 },
  "Button-603": { x: 3, y: 6 },
  "Button-604": { x: 4, y: 6 },
  "Button-605": { x: 5, y: 6 },
  "Button-606": { x: 6, y: 6 },
  "Button-607": { x: 7, y: 6 },
  "Button-608": { x: 8, y: 6 },
  "Button-609": { x: 9, y: 6 },
  "Button-610": { x: 10, y: 6 },
  "Button-611": { x: 11, y: 6 },
  "Button-612": { x: 12, y: 6 },
  "Button-613": { x: 13, y: 6 },
  "Button-614": { x: 14, y: 6 },
  "Button-615": { x: 15, y: 6 },
  "Button-616": { x: 16, y: 6 },
  "Button-617": { x: 17, y: 6 },

  // Row 5
  "Button-500": { x: 0, y: 5 },
  "Button-501": { x: 1, y: 5 },
  "Button-502": { x: 2, y: 5 },
  "Button-503": { x: 3, y: 5 },
  "Button-504": { x: 4, y: 5 },
  "Button-505": { x: 5, y: 5 },
  "Button-506": { x: 6, y: 5 },
  "Button-507": { x: 7, y: 5 },
  "Button-508": { x: 8, y: 5 },
  "Button-509": { x: 9, y: 5 },
  "Button-510": { x: 10, y: 5 },
  "Button-511": { x: 11, y: 5 },
  "Button-512": { x: 12, y: 5 },
  "Button-513": { x: 13, y: 5 },
  "Button-514": { x: 14, y: 5 },
  "Button-515": { x: 15, y: 5 },
  "Button-516": { x: 16, y: 5 },
  "Button-517": { x: 17, y: 5 },

  // Row 4
  "Button-400": { x: 0, y: 4 },
  "Button-401": { x: 1, y: 4 },
  "Button-402": { x: 2, y: 4 },
  "Button-403": { x: 3, y: 4 },
  "Button-404": { x: 4, y: 4 },
  "Button-405": { x: 5, y: 4 },
  "Button-406": { x: 6, y: 4 },
  "Button-407": { x: 7, y: 4 },
  "Button-408": { x: 8, y: 4 },
  "Button-409": { x: 9, y: 4 },
  "Button-410": { x: 10, y: 4 },
  "Button-411": { x: 11, y: 4 },
  "Button-412": { x: 12, y: 4 },
  "Button-413": { x: 13, y: 4 },
  "Button-414": { x: 14, y: 4 },
  "Button-415": { x: 15, y: 4 },
  "Button-416": { x: 16, y: 4 },
  "Button-417": { x: 17, y: 4 },

  // Row 3 (Note patch source at 15)
  "Button-300": { x: 0, y: 3 },
  "Button-301": { x: 1, y: 3 },
  "Button-302": { x: 2, y: 3 },
  "Button-303": { x: 3, y: 3 },
  "Button-304": { x: 4, y: 3 },
  "Button-305": { x: 5, y: 3 },
  "Button-306": { x: 6, y: 3 },
  "Button-307": { x: 7, y: 3 },
  "Button-308": { x: 8, y: 3 },
  "Button-309": { x: 9, y: 3 },
  "Button-310": { x: 10, y: 3 },
  "Button-311": { x: 11, y: 3 },
  "Button-312": { x: 12, y: 3 },
  "Button-313": { x: 13, y: 3 },
  "Button-314": { x: 14, y: 3 },
  "Button-315": { x: 15, y: 3 },
  "Button-316": { x: 16, y: 3 },
  "Button-317": { x: 17, y: 3 },

  // Row 2 (Random patch source at 15)
  "Button-200": { x: 0, y: 2 },
  "Button-201": { x: 1, y: 2 },
  "Button-202": { x: 2, y: 2 },
  "Button-203": { x: 3, y: 2 },
  "Button-204": { x: 4, y: 2 },
  "Button-205": { x: 5, y: 2 },
  "Button-206": { x: 6, y: 2 },
  "Button-207": { x: 7, y: 2 },
  "Button-208": { x: 8, y: 2 },
  "Button-209": { x: 9, y: 2 },
  "Button-210": { x: 10, y: 2 },
  "Button-211": { x: 11, y: 2 },
  "Button-212": { x: 12, y: 2 },
  "Button-213": { x: 13, y: 2 },
  "Button-214": { x: 14, y: 2 },
  "Button-215": { x: 15, y: 2 },
  "Button-216": { x: 16, y: 2 },
  "Button-217": { x: 17, y: 2 },

  // Row 1 (Velocity patch source at 15)
  "Button-100": { x: 0, y: 1 },
  "Button-101": { x: 1, y: 1 },
  "Button-102": { x: 2, y: 1 },
  "Button-103": { x: 3, y: 1 },
  "Button-104": { x: 4, y: 1 },
  "Button-105": { x: 5, y: 1 },
  "Button-106": { x: 6, y: 1 },
  "Button-107": { x: 7, y: 1 },
  "Button-108": { x: 8, y: 1 },
  "Button-109": { x: 9, y: 1 },
  "Button-110": { x: 10, y: 1 },
  "Button-111": { x: 11, y: 1 },
  "Button-112": { x: 12, y: 1 },
  "Button-113": { x: 13, y: 1 },
  "Button-114": { x: 14, y: 1 },
  "Button-115": { x: 15, y: 1 },
  "Button-116": { x: 16, y: 1 },
  "Button-117": { x: 17, y: 1 },

  // Row 0 (bottom)
  "Button-000": { x: 0, y: 0 },
  "Button-001": { x: 1, y: 0 },
  "Button-002": { x: 2, y: 0 },
  "Button-003": { x: 3, y: 0 },
  "Button-004": { x: 4, y: 0 },
  "Button-005": { x: 5, y: 0 },
  "Button-006": { x: 6, y: 0 },
  "Button-007": { x: 7, y: 0 },
  "Button-008": { x: 8, y: 0 },
  "Button-009": { x: 9, y: 0 },
  "Button-010": { x: 10, y: 0 },
  "Button-011": { x: 11, y: 0 },
  "Button-012": { x: 12, y: 0 },
  "Button-013": { x: 13, y: 0 },
  "Button-014": { x: 14, y: 0 },
  "Button-015": { x: 15, y: 0 },
  "Button-016": { x: 16, y: 0 },
  "Button-017": { x: 17, y: 0 },
}

export function getCoordinate(elementId: string): Coordinate | undefined {
  return hwCoordinates[elementId]
}

export function getGridPads(): { id: string; coord: Coordinate }[] {
  return Object.entries(hwCoordinates)
    .filter(([id]) => id.match(/^Button-\d{3}$/))
    .map(([id, coord]) => ({ id, coord }))
}
