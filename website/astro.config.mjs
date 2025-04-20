// @ts-check
import { defineConfig } from "astro/config"
import starlight from "@astrojs/starlight"
import rehypeMermaid from "rehype-mermaid"
import starlightLinksValidator from "starlight-links-validator"
import remarkGfm from "remark-gfm"
import remarkGithub from "remark-github"
// @ts-expect-error remark-link-rewrite has no types
import RemarkLinkRewrite from "remark-link-rewrite"
import { withBase } from "./src/utils"
import tailwindcss from "@tailwindcss/vite"
import { remarkDelugeShortcut } from "./src/markdown-directives/remark-deluge-key/remark-deluge-shortcut.ts"
import remarkDirective from "remark-directive"

// https://astro.build/config
const config = defineConfig({
  site: process.env.SITE_URL,
  base: process.env.SITE_BASE_PATH || "",
  trailingSlash: "never",
  integrations: [
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
        patreon: "https://www.patreon.com/Synthstrom",
      },
      editLink: {
        baseUrl:
          "https://github.dev/SynthstromAudible/DelugeFirmware/blob/community/website",
      },
      sidebar: [
        {
          label: "Downloads",
          slug: "downloads",
        },
        {
          label: "Change Log",
          slug: "changelogs/changelog",
        },
        {
          label: "Features",
          autogenerate: { directory: "features" },
        },
        // {
        // 	label: 'Guides',
        // 	items: [
        // 		// Each item here is one entry in the navigation menu.
        // 		{ label: 'Example Guide', slug: 'guides/example' },
        // 	],
        // },
        {
          label: "Reference",
          autogenerate: { directory: "reference" },
        },
        {
          label: "Development",
          autogenerate: { directory: "development" },
        },
        { label: "Doxygen Generated Docs", link: "/doxygen" },
      ],
      pagefind: {
        // See: https://pagefind.app/docs/ranking/
        ranking: {
          // termSimilarity: 1.0,
          // termFrequency: 1.0,
          // pageLength: 0.75,
          // termSaturation: 1.4,
        },
      },
      plugins: [starlightLinksValidator()],
      customCss: ["./src/styles/global.css"],
      credits: true,
      components: {
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
      remarkDelugeShortcut,
      [
        RemarkLinkRewrite,
        {
          replacer: (/** @type {string} */ link) => {
            if (link.startsWith("/")) {
              return withBase(link)
            }
            return link
          },
        },
      ],
    ],
    rehypePlugins: [
      // Known issue: diagrams follow the browser preferred dark mode, not the one selected in the header.
      // See: https://github.com/remcohaszing/rehype-mermaid/issues/16
      [rehypeMermaid, { strategy: "img-svg", dark: true }],
    ],
  },
  vite: {
    plugins: [tailwindcss()],
  },
})

export default config
