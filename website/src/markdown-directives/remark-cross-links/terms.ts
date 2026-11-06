import type { XrefTerm } from "./link-terms.ts"

export const MENU_PAGE = "/reference/menu_hierarchies"

type MenuNode = { varName: string; oled: string; children?: MenuNode[] }
export type MenuData = { trees: Record<string, MenuNode> }

/** Anchor id of a menu item on the menu hierarchy page (see FirmwareMenuSection.astro). */
export const menuAnchor = (treeKey: string, varName: string) =>
  `menu-${treeKey}-${varName}`

export type TermsFile = {
  /** Concept term -> guidebook page (optionally #heading). Matched in any case. */
  concepts: {
    term: string
    aliases?: string[]
    href: string
    /** Match only as written, e.g. DELAY but not the everyday word "delay". */
    caseSensitive?: boolean
  }[]
  menu: {
    /** Names never auto-linked as menus: hardware labels and acronyms. */
    exclude: string[]
    /** Explicit menu links for names that are ambiguous in the menu data. */
    pin: { phrases: string[]; tree: string; varName: string }[]
  }
}

/** Choices and placeholders in the generated menus (`virtualSelector...`, `virtualLaunchMode0`, ...). */
const isOptionValue = (varName: string) => varName.startsWith("virtual")

/** 7SEG-style spellings the book uses for menu names: INTErpolation, OCTAves. */
function sevenSegForms(name: string): string[] {
  if (name.includes(" ") || name.length <= 4) return []
  return [2, 3, 4].map((k) => name.slice(0, k) + name.slice(k).toLowerCase())
}

/**
 * Menu names that identify exactly one menu item, written as the book writes them (capitals,
 * or 7SEG-style). Ambiguous names (MODE, TYPE, ...) are left out unless pinned.
 */
export function menuTerms(data: MenuData, file: TermsFile["menu"]): XrefTerm[] {
  const firstTree = new Map<string, string>() // varName -> tree it first appears in
  const itemsByName = new Map<string, Set<string>>() // NAME -> varNames
  const walk = (tree: string, node: MenuNode) => {
    if (!firstTree.has(node.varName)) firstTree.set(node.varName, tree)
    const name = node.oled.trim().toUpperCase()
    const items = itemsByName.get(name) ?? new Set<string>()
    items.add(node.varName)
    itemsByName.set(name, items)
    node.children?.forEach((c) => walk(tree, c))
  }
  Object.entries(data.trees).forEach(([tree, root]) =>
    root.children?.forEach((c) => walk(tree, c)),
  )

  const exclude = new Set(file.exclude.map((s) => s.toUpperCase()))
  const terms: XrefTerm[] = []
  for (const [name, items] of itemsByName) {
    if (
      items.size !== 1 ||
      name.length < 4 ||
      exclude.has(name) ||
      !/\p{L}/u.test(name)
    )
      continue
    const varName = [...items][0]
    // Option values (POLY, LEGATO, FORWARD...) still count towards ambiguity, but are not linked:
    // the links are for menu and setting names.
    const tree = firstTree.get(varName)
    if (isOptionValue(varName) || !tree) continue
    terms.push({
      phrases: [name, ...sevenSegForms(name)],
      href: `${MENU_PAGE}#${menuAnchor(tree, varName)}`,
      kind: "menu",
      caseSensitive: true,
    })
  }
  for (const pin of file.pin) {
    terms.push({
      phrases: pin.phrases,
      href: `${MENU_PAGE}#${menuAnchor(pin.tree, pin.varName)}`,
      kind: "menu",
      caseSensitive: true,
    })
  }
  return terms
}

export function conceptTerms(file: TermsFile["concepts"]): XrefTerm[] {
  return file.map((c) => ({
    phrases: [c.term, ...(c.aliases ?? [])],
    href: c.href,
    kind: "concept" as const,
    ...(c.caseSensitive ? { caseSensitive: true } : {}),
  }))
}

export const allTerms = (file: TermsFile, menus: MenuData): XrefTerm[] => [
  ...conceptTerms(file.concepts),
  ...menuTerms(menus, file.menu),
]
