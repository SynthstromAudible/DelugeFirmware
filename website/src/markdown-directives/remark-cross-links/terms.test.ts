import { strict as assert } from "node:assert"
import { test } from "node:test"
import { conceptTerms, menuTerms, type MenuData } from "./terms.ts"

const data: MenuData = {
  trees: {
    synthSoundMenu: {
      key: "synthSoundMenu",
      oled: "Sound",
      title: "Sound",
      children: [
        {
          key: "voice",
          oled: "Voice",
          title: "Voice",
          children: [
            { key: "voice-unison", oled: "Unison", title: "Unison" },
            { key: "voice-mode", oled: "Mode", title: "Synth mode" },
          ],
        },
        {
          key: "interpolation",
          oled: "Interpolation",
          title: "Samp* interp",
        },
        { key: "midi", oled: "MIDI", title: "MIDI" },
      ],
    },
    kitFxMenu: {
      key: "kitFxMenu",
      oled: "Kit FX",
      title: "Kit FX",
      children: [
        { key: "unison", oled: "Unison", title: "Unison" }, // same item, second tree
        { key: "mode", oled: "Mode", title: "LPF mode" },
        {
          key: "auto-interpolation",
          oled: "Interpolation",
          title: "Interpolation",
        },
      ],
    },
  },
}

const byPhrase = (terms: ReturnType<typeof menuTerms>) =>
  Object.fromEntries(terms.flatMap((t) => t.phrases.map((p) => [p, t.href])))

test("a name used by one item links to its first tree", () => {
  const terms = byPhrase(menuTerms(data, { exclude: [], pin: [] }))
  assert.equal(
    terms["UNISON"],
    "/reference/menu_hierarchies#menu-synthSoundMenu-voice-unison",
  )
})

test("ambiguous names (two different items) are left out", () => {
  const terms = byPhrase(menuTerms(data, { exclude: [], pin: [] }))
  assert.equal(terms["MODE"], undefined)
  assert.equal(terms["INTERPOLATION"], undefined)
})

test("7SEG-style spellings of single-word names link too", () => {
  const terms = byPhrase(menuTerms(data, { exclude: [], pin: [] }))
  assert.ok(terms["UNIson"] && terms["UNISon"] && terms["UNison"])
})

test("excluded names (hardware labels, acronyms) are left out", () => {
  const terms = byPhrase(
    menuTerms(data, { exclude: ["midi", "UNISON"], pin: [] }),
  )
  assert.equal(terms["UNISON"], undefined)
  assert.equal(terms["MIDI"], undefined)
})

test("tree roots are not menu items", () => {
  const terms = byPhrase(menuTerms(data, { exclude: [], pin: [] }))
  assert.equal(terms["SOUND"], undefined)
  assert.equal(terms["KIT FX"], undefined)
})

test("pins link ambiguous names to the item the book means", () => {
  const terms = byPhrase(
    menuTerms(data, {
      exclude: [],
      pin: [
        {
          phrases: ["INTErpolation"],
          tree: "synthSoundMenu",
          key: "interpolation",
        },
      ],
    }),
  )
  assert.equal(
    terms["INTErpolation"],
    "/reference/menu_hierarchies#menu-synthSoundMenu-interpolation",
  )
})

test("one setting listed for two kinds of owner is one item", () => {
  const twice: MenuData = {
    trees: {
      synthSoundMenu: {
        key: "synthSoundMenu",
        oled: "Sound",
        title: "Sound",
        children: [
          { key: "rhythm", oled: "Rhythm", title: "Arp rhythm" },
          { key: "rhythm-2", oled: "Rhythm", title: "Arp rhythm" },
        ],
      },
    },
  }
  const terms = byPhrase(menuTerms(twice, { exclude: [], pin: [] }))
  assert.equal(
    terms["RHYTHM"],
    "/reference/menu_hierarchies#menu-synthSoundMenu-rhythm",
  )
})

test("option values are not menu items", () => {
  const withValue: MenuData = {
    trees: {
      synthSoundMenu: {
        key: "synthSoundMenu",
        oled: "Sound",
        title: "Sound",
        children: [
          {
            key: "polyphony",
            oled: "Polyphony",
            title: "Polyphony",
            children: [
              {
                key: "polyphony-legato",
                oled: "Legato",
                title: "Legato",
                option: true,
              },
            ],
          },
        ],
      },
    },
  }
  const terms = byPhrase(menuTerms(withValue, { exclude: [], pin: [] }))
  assert.ok(terms["POLYPHONY"])
  assert.equal(terms["LEGATO"], undefined)
})

test("menu terms are case-sensitive", () => {
  assert.ok(
    menuTerms(data, { exclude: [], pin: [] }).every((t) => t.caseSensitive),
  )
})

test("concepts match any case unless marked case-sensitive", () => {
  const [anyCase, capsOnly] = conceptTerms([
    { term: "ping-pong", href: "/delay" },
    { term: "DELAY", href: "/delay", caseSensitive: true },
  ])
  assert.equal(anyCase.caseSensitive, undefined)
  assert.equal(capsOnly.caseSensitive, true)
})
