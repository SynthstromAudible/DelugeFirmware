import { strict as assert } from "node:assert"
import { test } from "node:test"
import { conceptTerms, menuTerms, type MenuData } from "./terms.ts"

const data: MenuData = {
  trees: {
    synthSoundMenu: {
      varName: "root",
      oled: "Sound",
      children: [
        {
          varName: "voiceMenu",
          oled: "Voice",
          children: [
            { varName: "unisonMenu", oled: "Unison" },
            { varName: "synthModeMenu", oled: "Mode" },
          ],
        },
        { varName: "interpolationMenu", oled: "Interpolation" },
        { varName: "midiMenu", oled: "MIDI" },
      ],
    },
    kitFxMenu: {
      varName: "root2",
      oled: "Kit FX",
      children: [
        { varName: "unisonMenu", oled: "Unison" }, // same item, second tree
        { varName: "lpfModeMenu", oled: "Mode" },
        { varName: "autoInterpolation", oled: "Interpolation" },
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
    "/reference/menu_hierarchies#menu-synthSoundMenu-unisonMenu",
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
          varName: "interpolationMenu",
        },
      ],
    }),
  )
  assert.equal(
    terms["INTErpolation"],
    "/reference/menu_hierarchies#menu-synthSoundMenu-interpolationMenu",
  )
})

test("option values (virtual selectors) are not menu items", () => {
  const withValue: MenuData = {
    trees: {
      synthSoundMenu: {
        varName: "root",
        oled: "Sound",
        children: [
          {
            varName: "polyphonyMenu",
            oled: "Polyphony",
            children: [
              { varName: "virtualSelector_polyphonyMenu_1", oled: "Legato" },
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
