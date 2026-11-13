/**
 * The real index: src/data/guidebook-index.json against the Guidebook's pages. Headings are read
 * from the MDX source here (the page itself takes them from Astro, with their slugs).
 */
import { strict as assert } from "node:assert"
import { existsSync, readdirSync, readFileSync } from "node:fs"
import { join } from "node:path"
import { test } from "node:test"
import vocabJson from "../../../data/guidebook-index.json"
import {
  buildIndex,
  uncovered,
  type IndexPage,
  type IndexVocab,
} from "./build-index.ts"

const DOCS = join(import.meta.dirname, "../../../content/docs")
const ROOT = join(DOCS, "guidebook")
const vocab = vocabJson as IndexVocab
const LEGACY = "appendix/e-community-guide-os-41.mdx"

/** Headings that aren't topics: examples, notes, reference tables. */
export const NOT_TOPICS = [
  /^Example( \d+)?:?$/,
  /^Example: /,
  /^Notes? (when|on) /,
  /^Additional .* notes$/i,
  /^Overview$/,
  /^Acknowledgments$/,
  /^The Deluge official guidebook/,
  /^How to Use This Manual$/,
  /^What’s in the Box$/,
  /^Deluge Legacy Shortcuts/,
]

export function readPages(): IndexPage[] {
  return readdirSync(ROOT, { withFileTypes: true })
    .filter((d) => d.isDirectory())
    .flatMap((d) =>
      readdirSync(join(ROOT, d.name))
        // Appendix E is the OS 4.1 community guide, not indexed
        .filter((f) => f.endsWith(".mdx") && `${d.name}/${f}` !== LEGACY)
        .map((f) => {
          const src = readFileSync(join(ROOT, d.name, f), "utf8")
          const title = /^title:\s*"?(.*?)"?$/m.exec(src)?.[1] ?? f
          const section = /section:\s*"([^"]+)"/.exec(src)?.[1] ?? ""
          const headings = [...src.matchAll(/^(#{2,3}) (.+)$/gm)].map((m) => ({
            depth: m[1].length,
            slug: "",
            text: m[2].replace(/:key\[([^\]]*)\]/g, "$1").replace(/[*_`]/g, ""),
          }))
          return {
            section,
            title: title.replace(/^(?:[\d.]+\s+|Appendix [A-Z]: )/, ""),
            href: `/guidebook/${d.name}/${f.replace(/\.mdx$/, "")}`,
            headings,
          }
        }),
    )
}

test("every Guidebook title and heading is under some headword", () => {
  const missing = uncovered(readPages(), vocab).filter(
    (i) => !NOT_TOPICS.some((r) => r.test(i.text)),
  )
  assert.deepEqual(
    missing.map((i) => `${i.section} ${i.text}`),
    [],
  )
})

test("a head is a headword or an external entry, not both", () => {
  const heads = new Set(vocab.headwords.map((h) => h.head.toLowerCase()))
  assert.deepEqual(
    vocab.external.filter((e) => heads.has(e.head.toLowerCase())),
    [],
  )
})

test("see and also point at headwords; links point at pages", () => {
  const heads = new Set(vocab.headwords.map((h) => h.head))
  for (const s of vocab.see)
    for (const t of s.see) assert.ok(heads.has(t), `${s.term} → ${t}`)
  for (const h of vocab.headwords)
    for (const t of h.also ?? []) assert.ok(heads.has(t), `${h.head} also ${t}`)
  const pages = (href: string) => href.replace(/#.*$/, "").replace(/^\//, "")
  for (const h of vocab.headwords)
    for (const r of h.refs ?? [])
      assert.ok(existsSync(join(DOCS, `${pages(r.href)}.mdx`)), r.href)
  for (const x of vocab.external)
    assert.ok(existsSync(join(DOCS, `${pages(x.href)}.mdx`)), x.href)
})

test("every headword lists something", () => {
  const empty = buildIndex(readPages(), vocab)
    .flatMap((g) => g.entries)
    .filter((e) => !e.subs.length && !e.see && !e.external)
    .map((e) => e.head)
  assert.deepEqual(empty, [])
})
