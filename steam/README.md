# Steam release setup

## Store page copy

`store-listing.md` — paste-ready description/tags/system requirements for
the Steamworks store page wizard.

## Store/library art

`assets/` — auto-generated capsule/icon images sized to Steam's current
requirements, see `assets/README.md` for what's ready vs. what still
needs real design work (Library Hero, Library Logo, screenshots, trailer).

## Automated build upload (CI)

`.github/workflows/steam-deploy.yml` uploads Windows/macOS/Linux builds to
Steam automatically after a GitHub release publishes and `release.yml`
finishes attaching the zips. It defaults to the `prerelease` Steam branch
(not the public `default` branch), so nothing goes live to buyers without
a manual promote step in Steamworks — change `releaseBranch` in the
workflow (or pass one via the manual `workflow_dispatch` trigger) once
you're ready to ship straight to default.

Every Steam release is meant to be driven by this workflow — there's no
manual/local upload path kept in this repo. `game-ci/steam-deploy` builds
its own VDF internally from the `appId`/`depotNPath` workflow inputs, so
there's nothing to hand-edit; the App ID and depot IDs only need to exist
as repo secrets/workflow inputs, not as checked-in files.

### One-time setup before the workflow can run

1. Finish Steamworks onboarding (legal/tax/banking) and create the app —
   this part is unavoidably manual.
2. In Steamworks, create a **build account** (Users & Permissions) instead
   of using your main account for CI — least-privilege for an automated
   credential.
3. Note the App ID Steamworks assigns (App Admin > SteamPipe > Depots).
4. Generate the Steam Guard config for the build account, once, locally:
   ```sh
   steamcmd +login <builduser> <password> +quit   # enter Steam Guard code from email
   steamcmd +login <builduser> +quit               # confirm it's remembered
   cat config/config.vdf | base64 > config_base64.txt
   ```
   (macOS steamcmd keeps this at `~/Library/Application Support/Steam/config/config.vdf`.)
5. Add repo secrets: `STEAM_USERNAME`, `STEAM_CONFIG_VDF` (contents of
   `config_base64.txt`), `STEAM_APP_ID`.
6. Rotate `STEAM_CONFIG_VDF` if it's ever exposed — it's a live login session.
