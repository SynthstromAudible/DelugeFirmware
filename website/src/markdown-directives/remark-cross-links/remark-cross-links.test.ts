import { strict as assert } from "node:assert"
import { test } from "node:test"
import { guidebookHref } from "./remark-cross-links.ts"

const docs = "/home/x/website/src/content/docs"

test("guidebook pages map to their site path", () => {
  assert.equal(
    guidebookHref(`${docs}/guidebook/03-sequencing/02-playing-a-sequence.mdx`),
    "/guidebook/03-sequencing/02-playing-a-sequence",
  )
})

test("the guidebook index maps to /guidebook", () => {
  assert.equal(guidebookHref(`${docs}/guidebook/index.mdx`), "/guidebook")
})

test("other pages are not linked", () => {
  assert.equal(guidebookHref(`${docs}/manual/introduction/intro.md`), null)
  assert.equal(guidebookHref(undefined), null)
})
