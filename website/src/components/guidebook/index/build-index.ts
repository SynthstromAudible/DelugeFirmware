/**
 * The Guidebook's back-of-book index, built from its page titles and headings (depth 2 and 3)
 * and a list of headwords (src/data/guidebook-index.json). A heading is listed under every
 * headword it names, as written, linked to its section; the same heading on several pages is one
 * subentry with a link per section. Headwords can also carry hand-added references (for topics
 * with no heading of their own), "see" entries point from other words to headwords, and external
 * entries point to the community docs for what the Guidebook doesn't cover.
 */

export type IndexHeading = { depth: number; slug: string; text: string }
export type IndexPage = {
  section: string
  title: string
  href: string
  headings: IndexHeading[]
}

export type IndexVocab = {
  headwords: {
    head: string
    /** Words or phrases that put a heading under this headword (whole words, any case). */
    match: string[]
    /** Phrases that don't count, e.g. "clip view" for Clip. */
    not?: string[]
    /** Sections whose title and headings all belong here, e.g. a parameter list. */
    sections?: string[]
    refs?: { label: string; href: string; section: string }[]
    /** Other headwords to look at. */
    also?: string[]
  }[]
  see: { term: string; see: string[] }[]
  external: { head: string; label: string; href: string }[]
}

export type IndexLink = { text: string; href: string }
export type IndexEntry = {
  head: string
  subs: { label: string; links: IndexLink[] }[]
  see?: string[]
  also?: string[]
  external?: { label: string; href: string }
}
export type IndexGroup = { letter: string; entries: IndexEntry[] }

type Item = { section: string; text: string; href: string }

const escape = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
const word = (phrase: string) =>
  new RegExp(`(^|[^\\p{L}\\p{N}])${escape(phrase)}($|[^\\p{L}\\p{N}])`, "iu")
const clean = (text: string) =>
  text
    .replace(/<Badge\b[^>]*\/>/g, "")
    .replace(/\s+/g, " ")
    .replace(/[\s:.]+$/, "")
    .trim()

/** Every page title and depth-2/3 heading, in page order. */
function items(pages: IndexPage[]): Item[] {
  return pages.flatMap((p) => [
    { section: p.section, text: clean(p.title), href: p.href },
    ...p.headings
      .filter((h) => h.depth === 2 || h.depth === 3)
      .map((h) => ({
        section: p.section,
        text: clean(h.text),
        href: `${p.href}#${h.slug}`,
      })),
  ])
}

function names(item: Item, hw: IndexVocab["headwords"][number]): boolean {
  if (hw.sections?.includes(item.section)) return true
  let text = item.text
  for (const n of hw.not ?? [])
    text = text.replace(new RegExp(word(n).source, "giu"), "$1$2")
  return hw.match.some((m) => word(m).test(text))
}

// "3.2" < "21.4" < "A" (appendices, lettered, come after every chapter)
const sectionOrder = (a: string, b: string) => {
  const key = (s: string) =>
    /^\d/.test(s) ? s.split(".").map(Number) : [Infinity, s.charCodeAt(0)]
  const [x, y] = [key(a), key(b)]
  return x[0] - y[0] || (x[1] ?? 0) - (y[1] ?? 0)
}
const alpha = (a: string, b: string) =>
  a.localeCompare(b, "en", { sensitivity: "base" })

export function buildIndex(
  pages: IndexPage[],
  vocab: IndexVocab,
): IndexGroup[] {
  const all = items(pages)
  const entries: IndexEntry[] = []
  for (const hw of vocab.headwords) {
    // keyed by the label in lower case: a page title and its own first heading are one subentry
    const subs = new Map<string, { label: string; links: IndexLink[] }>()
    const add = (label: string, link: IndexLink) => {
      const sub = subs.get(label.toLowerCase()) ?? { label, links: [] }
      // one link per section; a heading's anchor beats its page's top
      const same = sub.links.findIndex((l) => l.text === link.text)
      if (same < 0) sub.links.push(link)
      else if (link.href.includes("#")) sub.links[same] = link
      subs.set(label.toLowerCase(), sub)
    }
    for (const item of all.filter((i) => names(i, hw)))
      add(item.text, { text: item.section, href: item.href })
    for (const r of hw.refs ?? [])
      add(r.label, { text: r.section, href: r.href })
    entries.push({
      head: hw.head,
      subs: [...subs.values()]
        .map(({ label, links }) => ({
          label,
          links: links.sort((a, b) => sectionOrder(a.text, b.text)),
        }))
        .sort((a, b) => alpha(a.label, b.label)),
      ...(hw.also?.length ? { also: hw.also } : {}),
    })
  }
  for (const s of vocab.see)
    entries.push({ head: s.term, subs: [], see: s.see })
  for (const x of vocab.external)
    entries.push({
      head: x.head,
      subs: [],
      external: { label: x.label, href: x.href },
    })

  const groups = new Map<string, IndexEntry[]>()
  for (const e of entries.sort((a, b) => alpha(a.head, b.head))) {
    const letter = e.head[0].toUpperCase()
    groups.set(letter, [...(groups.get(letter) ?? []), e])
  }
  return [...groups].map(([letter, list]) => ({ letter, entries: list }))
}

/** Titles and headings no headword names: what the index is missing. */
export function uncovered(pages: IndexPage[], vocab: IndexVocab): Item[] {
  return items(pages).filter((i) => !vocab.headwords.some((hw) => names(i, hw)))
}
