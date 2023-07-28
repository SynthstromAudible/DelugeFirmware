# Deluge Firmware website

This site is built on NextJS with Tailwind for styling, using the NextJS static HTML export mode.
The build process is slightly more complex than your standard NextJS site, because we need to fetch the list of existing firmware binaries and dump them in the NextJS `public/` folder.
That work is accomplished by `../fetch-artifacts/`, a Node script which queries the Github API to retreive the workflow runs and download the associated artifacts.
See the `fetch-artifacts` directory for details, but the cliff notes is that to build the site, you need to do this:

```shell
# make sure you have GITHUB_TOKEN set to a valid PAT or other token
# assuming you're currently in this directory
pushd ../fetch-artifacts
npm run run                 # or npm run run-cached, if you're going to be
                            # doing local development and don't want to wait to
                            # re-download artifacts repeatedly
popd

npm run build               # or npm run dev, for the NextJS dev experience
```

You should then have all the files required to deploy the site, or otherwise hack on it.
