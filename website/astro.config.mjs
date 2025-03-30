// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import rehypeMermaid from 'rehype-mermaid'
import starlightLinksValidator from 'starlight-links-validator'
import remarkGfm from 'remark-gfm'
import remarkGithub from 'remark-github'
// @ts-expect-error no types
import RemarkLinkRewrite from 'remark-link-rewrite'
import { withBase } from './src/utils'

// https://astro.build/config
const config = defineConfig({
  site: process.env.SITE_URL,
  base: process.env.SITE_BASE_PATH,
  trailingSlash: 'never',
  integrations: [
    starlight({
      title: 'Deluge Community',
      logo: {
        light: './src/assets/deluge_community_firmware_logo_inverted.png',
        dark: './src/assets/deluge_community_firmware_logo.png',
        replacesTitle: true,
      },
      social: {
        github: 'https://github.com/SynthstromAudible/DelugeFirmware',
        discord: 'https://discord.gg/s2MnkFqZgj',
        patreon: 'https://www.patreon.com/Synthstrom'
      },
      plugins: [starlightLinksValidator()],
      sidebar: [
        {
          label: 'Downloads',
          slug: 'downloads'
        },
        {
          label: 'Change Log',
          slug: 'changelogs/changelog',
        },
        {
          label: 'Features',
          autogenerate: { directory: 'features' },
        },
        // {
        // 	label: 'Guides',
        // 	items: [
        // 		// Each item here is one entry in the navigation menu.
        // 		{ label: 'Example Guide', slug: 'guides/example' },
        // 	],
        // },
        {
          label: 'Reference',
          autogenerate: { directory: 'reference' },
        },
        {
          label: 'Development',
          autogenerate: { directory: 'development' },
        },
        { label: 'Doxygen', link: '/doxygen' },
        {
          label: 'Other',
          autogenerate: { directory: 'poc' },
          badge: 'Testing Docs Features'
        }
      ],
    }),
  ],
  markdown: {
    remarkPlugins: [
      remarkGfm,
      [remarkGithub, {
        repository: 'SynthstromAudible/DelugeFirmware'
      }],
      [RemarkLinkRewrite, {
        replacer: (/** @type {string} */ link) => {
          if (link.startsWith('/')) {
            return withBase(link)
          }
          return link
        }
      }]
    ],
    rehypePlugins: [
      [rehypeMermaid, { strategy: 'img-svg', dark: true }],
    ],
  },
});

export default config;
