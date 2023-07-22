# `fetch-artifacts`

This is a small tool (well, ~100 megs worth of node_modules) that fetches all the workflow runs and retrieves the build ones from Github Actions.
In principle it would work against anything that's sort of shaped like the main `SynthstromAudible/Deluge` repository but in practice that repo is currently hardcoded.

There's a few handy scripts set up for development:
  - `build`, which builds the project and dumps a sketchy `package.json` in to the `dist/` folder to make the whole thing look like a module (required so we can use top-level `await` syntax)
  - `run`, which runs the tool
  - `run-cached`, which runs the tool and caches the retrieved binaries in `.df_cache`.
  - `dev`, which runs `tsc` in watch mode for rapid iteration.

The tool expects `GITHUB_TOKEN` to be set to a valid github token (personal access or otherwise) in the environment (like it would be on an Actions runner).
It might work without doing that but you'll pretty rapidly hit GH API limits.
