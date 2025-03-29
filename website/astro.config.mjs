// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

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
			sidebar: [
        {
          label: 'Downloads',
          slug: 'downloads'
        },
				{
					label: 'Guides',
					items: [
						// Each item here is one entry in the navigation menu.
						{ label: 'Example Guide', slug: 'guides/example' },
					],
				},
				{
					label: 'Reference',
					autogenerate: { directory: 'reference' },
				},
			],
		}),
	],
});
