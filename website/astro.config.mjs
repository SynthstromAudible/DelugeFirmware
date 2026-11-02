// @ts-check
import { defineConfig, passthroughImageService } from "astro/config"
import starlight from "@astrojs/starlight"
import svelte from "@astrojs/svelte"
import rehypeMermaid from "rehype-mermaid"
import starlightLinksValidator from "starlight-links-validator"
import remarkGfm from "remark-gfm"
import remarkGithub from "remark-github"
// @ts-expect-error remark-link-rewrite has no types
import RemarkLinkRewrite from "remark-link-rewrite"
import tailwindcss from "@tailwindcss/vite"
import guidebookRedirects from "./src/data/guidebook-redirects.json" with { type: "json" }
import { remarkDelugeKey } from "./src/markdown-directives/remark-deluge-key/remark-deluge-key.ts"
import { remarkDelugeScreen } from "./src/markdown-directives/remark-deluge-screen/remark-deluge-screen.ts"
import remarkDirective from "remark-directive"
import starlightSidebarTopics from "starlight-sidebar-topics"
import { readFileSync } from "node:fs"
import { fileURLToPath } from "node:url"
import { remarkCrossLinks } from "./src/markdown-directives/remark-cross-links/remark-cross-links.ts"
import { allTerms } from "./src/markdown-directives/remark-cross-links/terms.ts"
import { remarkPanelSteps } from "./src/markdown-directives/remark-panel-steps/remark-panel-steps.ts"

const readJson = (/** @type {string} */ path) =>
  JSON.parse(readFileSync(new URL(path, import.meta.url), "utf8"))
const crossLinkTerms = allTerms(
  readJson("./src/data/guidebook-terms.json"),
  readJson("./src/data/generated/menu-hierarchies.json"),
)

const siteBasePath = process.env.SITE_BASE_PATH || ""
const isCI = process.env.CI === "true"

// https://astro.build/config
const config = defineConfig({
  site: process.env.SITE_URL,
  base: siteBasePath,
  trailingSlash: "never",
  // CI runners occasionally miss Sharp native bindings during image generation.
  // Use passthrough in CI to keep builds deterministic.
  image: isCI ? { service: passthroughImageService() } : undefined,
  // Guidebook pages moved by the reorganization (online-manual tools/guidebook/reorg.py)
  // Astro doesn't add `base` to redirect destinations; page links get it from RemarkLinkRewrite.
  redirects: Object.fromEntries(
    Object.entries(guidebookRedirects).map(([from, to]) => [
      from,
      `${siteBasePath}${to}`,
    ]),
  ),
  integrations: [
    svelte(),
    starlight({
      title: "Deluge Community",
      logo: {
        light: "./src/assets/deluge_community_firmware_logo_inverted.png",
        dark: "./src/assets/banner.png",
        replacesTitle: true,
      },
      social: {
        github: "https://github.com/SynthstromAudible/DelugeFirmware",
        discord: "https://discord.gg/s2MnkFqZgj",
        patreon: "https://patreon.com/Synthstrom",
      },
      editLink: {
        baseUrl:
          "https://github.dev/SynthstromAudible/DelugeFirmware/blob/main/website",
      },
      pagefind: {
        // See: https://pagefind.app/docs/ranking/
        ranking: {
          // termSimilarity: 1.0,
          // termFrequency: 1.0,
          // pageLength: 0.75,
          // termSaturation: 1.4,
        },
      },
      plugins: [
        starlightLinksValidator({
          exclude: [
            "/",
            "/DelugeFirmware/", // TODO: This is a basePath issue, fix!
            // Menu item anchors live in FirmwareMenuSection's HTML, which this validator can't see;
            // scripts/check-cross-links.mjs checks them against the built pages instead.
            "/reference/menu_hierarchies#menu-*",
          ],
        }),
        starlightSidebarTopics([
          {
            label: "Documentation",
            icon: "open-book",
            link: "/features/community_features",
            items: [
              {
                label: "Downloads",
                slug: "downloads",
              },
              {
                label: "Companion",
                slug: "reference/deluge_companion",
              },
              {
                label: "Change Log",
                slug: "changelogs/changelog",
              },
              {
                label: "Features",
                autogenerate: { directory: "features" },
              },
              {
                label: "Manual",
                items: [
                  {
                    label: "Introduction",
                    autogenerate: { directory: "manual/introduction" },
                  },
                  {
                    label: "Device Overview",
                    collapsed: true,
                    autogenerate: { directory: "manual/device_overview" },
                  },
                  {
                    label: "User Interfaces",
                    collapsed: true,
                    autogenerate: { directory: "manual/user_interfaces" },
                  },
                  {
                    label: "Engines",
                    collapsed: true,
                    autogenerate: { directory: "manual/engines" },
                  },
                ],
              },
              {
                label: "Reference",
                autogenerate: { directory: "reference" },
              },
            ],
          },
          {
            label: "Guidebook (C1.0)",
            icon: "document",
            link: "/guidebook",
            items: [
              { label: "About", link: "/guidebook" },
              { label: "Index", link: "/guidebook/index-a-z" },
              {
                label: "Part 1: Getting Started",
                items: [
                  {
                    label: "1 Overview",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/01-overview" },
                  },
                  {
                    label: "2 Basics",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/02-basics" },
                  },
                ],
              },
              {
                label: "Part 2: Clips & Sequencing",
                items: [
                  {
                    label: "3 Clips",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/03-clips" },
                  },
                  {
                    label: "4 Sequencing",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/04-sequencing" },
                  },
                  {
                    label: "5 Keyboard Views",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/05-keyboard-views" },
                  },
                  {
                    label: "6 Automation",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/06-automation" },
                  },
                ],
              },
              {
                label: "Part 3: Sounds",
                items: [
                  {
                    label: "7 Synthesizers",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/07-synthesizers" },
                  },
                  {
                    label: "8 Arpeggiator",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/08-arpeggiator" },
                  },
                  {
                    label: "9 Kits",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/09-kits" },
                  },
                  {
                    label: "10 Modulation",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/10-modulation" },
                  },
                  {
                    label: "11 Effects",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/11-effects" },
                  },
                ],
              },
              {
                label: "Part 4: Arranging & Performing",
                items: [
                  {
                    label: "12 Session View",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/12-session-view" },
                  },
                  {
                    label: "14 Arranger View",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/14-arranger-view" },
                  },
                  {
                    label: "15 Songs & Files",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/15-songs-and-files" },
                  },
                ],
              },
              {
                label: "Part 5: Audio & Looping",
                items: [
                  {
                    label: "16 Audio",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/16-audio" },
                  },
                  {
                    label: "17 Looping",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/17-looping" },
                  },
                ],
              },
              {
                label: "Part 6: Connecting",
                items: [
                  {
                    label: "18 MIDI",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/18-midi" },
                  },
                  {
                    label: "19 CV",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/19-cv" },
                  },
                  {
                    label: "20 USB Host",
                    collapsed: true,
                    autogenerate: { directory: "guidebook/20-usb-host" },
                  },
                ],
              },
              {
                label: "Part 7: System",
                items: [
                  {
                    label: "21 Settings & System",
                    collapsed: true,
                    autogenerate: {
                      directory: "guidebook/21-settings-and-system",
                    },
                  },
                ],
              },
              {
                label: "Appendices",
                collapsed: true,
                autogenerate: { directory: "guidebook/appendix" },
              },
            ],
          },
          {
            label: "Resources",
            icon: "star",
            link: "/resources/applications/overview",
            items: [
              {
                label: "Community Presets",
                autogenerate: { directory: "resources/presets" },
              },
              {
                label: "Community Applications",
                autogenerate: { directory: "resources/applications" },
              },
              {
                label: "MIDI Devices",
                autogenerate: { directory: "resources/midi_devices" },
              },
              {
                label: "Tutorials",
                link: "/resources/tutorials",
              },
            ],
          },
          {
            label: "Blog",
            icon: "pen",
            link: "/blog",
            items: [
              { label: "All Posts", link: "/blog" },
              {
                label: "Posts",
                autogenerate: { directory: "blog" },
              },
            ],
          },
          {
            label: "Development",
            icon: "seti:json",
            link: "/development/deluge/additional_info",
            items: [
              {
                label: "Deluge Development",
                autogenerate: { directory: "development/deluge" },
              },
              { label: "Doxygen Generated Docs", link: "/doxygen" },
              {
                label: "Website Development",
                autogenerate: { directory: "development/website" },
              },
            ],
          },
        ]),
      ],
      customCss: ["./src/styles/global.css"],
      credits: true,
      components: {
        SocialIcons: "./src/components/SocialIcons.astro",
        Head: "./src/components/Head.astro",
        Search: "./src/components/Search.astro",
      },
    }),
  ],
  markdown: {
    remarkPlugins: [
      remarkGfm,
      [
        remarkGithub,
        {
          repository: "SynthstromAudible/DelugeFirmware",
        },
      ],
      remarkDirective,
      remarkDelugeKey,
      [
        remarkPanelSteps,
        {
          panel: readJson("./src/data/guidebook-panel.json"),
          component: fileURLToPath(
            new URL(
              "./src/components/guidebook/PanelFigure.astro",
              import.meta.url,
            ),
          ),
        },
      ],
      remarkDelugeScreen,
      [remarkCrossLinks, { terms: crossLinkTerms }],
      [
        RemarkLinkRewrite,
        {
          replacer: (/** @type {string} */ link) => {
            if (link.startsWith("/")) {
              return `${siteBasePath}${link}`
            }
            return link
          },
        },
      ],
    ],
    rehypePlugins: [[rehypeMermaid, { strategy: "pre-mermaid" }]],
  },
  vite: {
    plugins: [tailwindcss()],
  },
  experimental: {
    contentIntellisense: true,
  },
})

export default config
