export type ContextMenuDefinition = {
  label?: string
  title: string
  options: string[]
  selectedIndex?: number
  renderMethod?: MenuRenderMethod
}

export const MENU_RENDER_METHODS = [
  "NO_ROUNDING",
  "ROUNDED_INVERSION",
  "NO_INVERSION",
] as const

export type MenuRenderMethod = (typeof MENU_RENDER_METHODS)[number]

export const DEFAULT_MENU_RENDER_METHOD: MenuRenderMethod = "NO_ROUNDING"

export const DEFAULT_MENU_CONTEXT = "NONE"

const contextMenuDefinitions: Record<string, ContextMenuDefinition> = {
  NONE: {
    title: "Menu",
    options: ["..."],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  CLONE: {
    label: "Load Preset > Clone",
    title: "Load Preset",
    options: ["Clone"],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  BASIC: {
    label: "Load File(s) > Basic",
    title: "Load File(s)",
    options: ["Multisamples", "Basic"],
    selectedIndex: 1,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  MULTISAMPLES: {
    label: "Load File(s) > Multisamples",
    title: "Load File(s)",
    options: ["Multisamples", "Basic"],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  SINGLE_CYCLE: {
    label: "Load File(s) > Single-Cycle",
    title: "Load File(s)",
    options: ["Single-Cycle", "Wavetable"],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  OSCILLATOR_INPUT: {
    label: "Sound > Oscillator > Type > Input",
    title: "Osc. Type",
    options: ["Input"],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  LOAD_ALL: {
    label: "Sample(s) > Load All",
    title: "Sample(s)",
    options: ["Load All"],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  SLICE: {
    label: "Sample(s) > Slice",
    title: "Sample(s)",
    options: ["Load All", "Slice"],
    selectedIndex: 1,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  CHOKE: {
    label: "Sound > Voice > Polyphony > Choke",
    title: "Polyphony",
    options: ["Choke"],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
  COUNT_IN: {
    label: "Settings > Recording > Count-In Bars",
    title: "Recording",
    options: ["Count-In Bars"],
    selectedIndex: 0,
    renderMethod: DEFAULT_MENU_RENDER_METHOD,
  },
}

// Returns canonical context-menu key used by markdown parsing and rendering.
export function normalizeMenuContext(value?: string): string {
  const normalized = value?.trim().toUpperCase()
  return normalized && normalized.length > 0 ? normalized : DEFAULT_MENU_CONTEXT
}

// Returns context-menu definition or the default fallback when unknown.
export function getMenuContextDefinition(
  context?: string,
): ContextMenuDefinition {
  const normalized = normalizeMenuContext(context)
  return (
    contextMenuDefinitions[normalized] ??
    contextMenuDefinitions[DEFAULT_MENU_CONTEXT]
  )
}

// Returns true when value maps to a known menu render method token.
export function isMenuRenderMethod(value: string): value is MenuRenderMethod {
  return MENU_RENDER_METHODS.includes(value as MenuRenderMethod)
}

// Returns canonical render method, defaulting when omitted.
export function normalizeMenuRenderMethod(value?: string): MenuRenderMethod {
  const normalized = value?.trim().toUpperCase()
  if (normalized && isMenuRenderMethod(normalized)) {
    return normalized
  }
  return DEFAULT_MENU_RENDER_METHOD
}

// Returns a compact, human-readable menu label for step chips.
export function getMenuContextLabel(context?: string): string {
  const normalized = normalizeMenuContext(context)
  const contextDefinition = contextMenuDefinitions[normalized]

  if (contextDefinition?.label && contextDefinition.label.trim().length > 0) {
    return contextDefinition.label
  }

  if (normalized === DEFAULT_MENU_CONTEXT) {
    return "menu"
  }

  return normalized
    .split("_")
    .filter(Boolean)
    .map((segment) => segment.charAt(0) + segment.slice(1).toLowerCase())
    .join(" ")
}
