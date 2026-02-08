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
import { remarkDelugeKey } from "./src/markdown-directives/remark-deluge-key/remark-deluge-key.ts"
import { remarkDelugeScreen } from "./src/markdown-directives/remark-deluge-screen/remark-deluge-screen.ts"
import remarkDirective from "remark-directive"
import starlightSidebarTopics from "starlight-sidebar-topics"

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
      remarkDelugeScreen,
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
  experimental: {
    contentIntellisense: true,
  },
})

export default config
