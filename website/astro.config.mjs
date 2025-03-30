// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import rehypeMermaid from 'rehype-mermaid'
import starlightLinksValidator from 'starlight-links-validator'

// https://astro.build/config
export default defineConfig({
	integrations: [
		starlight({
			title: 'Deluge Community',
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
					label: 'Changelog',
					autogenerate: { directory: 'changelogs' },
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
				{
					label: 'Other',
					autogenerate: { directory: 'poc' },
					badge: 'Testing Docs Features'
				}
			],
		}),
	],
	markdown: {
		rehypePlugins: [
			[rehypeMermaid, { strategy: 'img-svg', dark: true }],
		],
	},
});
