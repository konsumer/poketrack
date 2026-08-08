# Steam release setup

## Store page copy

`store-listing.md` — paste-ready description/tags/system requirements for
the Steamworks store page wizard.

## Store/library art

`assets/` — capsules, icons, library hero/logo and screenshots, all
generated and sized to Steam's current requirements. See
`assets/README.md` for how each is built and how to regenerate it. Only
the **trailer** still needs a real capture.

## Automated build upload (CI)

`.github/workflows/steam-deploy.yml` uploads Windows/macOS/Linux builds to
Steam automatically after a GitHub release publishes and `release.yml`
finishes attaching the zips. It can also be run by hand
(`workflow_dispatch`) against any past tag.

Every Steam release is meant to be driven by this workflow — there's no
manual/local upload path kept in this repo. `game-ci/steam-deploy` builds
its own VDF internally from the `appId`/`depotNPath` workflow inputs, so
there's nothing to hand-edit; the App ID and depot IDs only need to exist
as repo secrets/workflow inputs, not as checked-in files.

The workflow runs two jobs, `deploy` (the app) then `deploy-examples` (the
Example Beats app, App ID 5043220). They're **sequential on purpose**: both
log in as the same Steam build account, and Steam only keeps one steamcmd
session per account, so running them together made them knock each other
over. A repo-level `concurrency: steam-deploy` group stops two releases
overlapping for the same reason.

## Publishing: prerelease → promote

Steam separates *uploading a build* from *making it live*. This is the part
that trips people up, so concretely:

1. **Publish a GitHub release.** `release.yml` builds and attaches the
   zips; `steam-deploy.yml` then uploads them to Steam and sets the build
   live on the **`prerelease`** beta branch.
2. **Test it.** In the Steam client, right-click the game → Properties →
   Betas → pick `prerelease`. Steam downloads that build. Nobody on the
   default branch sees any change.
3. **Promote it.** In Steamworks: App Admin → **Builds**, find the build,
   set its branch to `default` in the dropdown, then **Publish** (this
   needs the separate "publish app changes" confirmation).

### Why promoting is manual

It has to be. Steam's own SteamPipe docs:

> Note that the 'default' branch can not be set live automatically. That
> must be done through the App Admin panel.

So a workflow can *upload* a build and set it live on any **beta** branch,
but it can never flip the default branch — that's deliberately a human
action in the web UI. The workflow rejects `releaseBranch: default` up
front with an explanatory error rather than letting steamcmd upload a
whole build and then die at the set-live step.

That failure mode is worth recognising, because it's silent: the upload
succeeds, the log shows no error, and the job just goes red at the end.
`deploy-examples` used to pass `releaseBranch: default` and failed exactly
this way on every run. It now uploads with `releaseBranch: ''` (upload
only, set nothing live) and is promoted by hand like the main app.

### Deploying somewhere other than prerelease

Run the workflow manually (Actions → Deploy to Steam → Run workflow) and
set `releaseBranch`:

- a beta branch name (e.g. `prerelease`, `beta`) — uploaded and set live there
- empty — uploaded, nothing set live; promote entirely by hand
- `default` — rejected, see above

**The beta branch must already exist in Steamworks** (App Admin → Builds →
Betas). steamcmd will not create one, and pointing at a branch that doesn't
exist is another "uploaded fine, then failed" case.

### One-time setup before the workflow can run

1. Finish Steamworks onboarding (legal/tax/banking) and create the app —
   this part is unavoidably manual.
2. In Steamworks, create a **build account** (Users & Permissions) instead
   of using your main account for CI — least-privilege for an automated
   credential. It needs "Edit App Metadata" and "Publish App Changes".
3. Note the App ID Steamworks assigns (App Admin > SteamPipe > Depots).
   The workflow maps `depot1Path`/`depot2Path`/`depot3Path` to the depot
   IDs `appId+1`, `appId+2`, `appId+3` — Steamworks' default numbering. If
   your depots are numbered differently, the upload fails.
4. Create the `prerelease` beta branch (App Admin → Builds → Betas), or the
   default `releaseBranch` has nothing to set live on.
5. Generate the Steam Guard config for the build account, once, locally:
   ```sh
   steamcmd +login <builduser> <password> +quit   # enter Steam Guard code from email
   steamcmd +login <builduser> +quit               # confirm it's remembered
   cat config/config.vdf | base64 > config_base64.txt
   ```
   (macOS steamcmd keeps this at `~/Library/Application Support/Steam/config/config.vdf`.)
6. Add repo secrets: `STEAM_USERNAME`, `STEAM_CONFIG_VDF` (contents of
   `config_base64.txt`), `STEAM_APP_ID`. Optionally
   `STEAM_EXAMPLES_APP_ID` to override the Example Beats App ID.
7. Rotate `STEAM_CONFIG_VDF` if it's ever exposed — it's a live login session.

### When a deploy fails

Both jobs dump `build_output.log` and any other steamcmd logs on failure,
which say far more than the action's exit code does. Common causes, in
rough order of likelihood:

| Symptom | Cause |
|---|---|
| Upload succeeds, job fails at the end | Set-live target is `default`, or a beta branch that doesn't exist |
| `Failed to commit build ... : Failure` | Same as above, or the build account lacks "Publish App Changes" |
| Two jobs fail together, no clear error | Overlapping steamcmd sessions — should be prevented now by `needs:` + the concurrency group |
| `Build for depot NNNN failed` | Depot IDs don't match `appId+1/2/3` |
| Login fails | `STEAM_CONFIG_VDF` expired or was rotated — regenerate it (step 5) |
