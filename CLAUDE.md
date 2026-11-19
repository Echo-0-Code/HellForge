# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

HellForge runs a player's own copy of HELLDIVERS 2 against a local backend instead of the official servers, and loads
mods into it (C mod SDK, stats/data mods, in-game ImGui editor on F8). This checkout is a fork of
`SkyeShade/HellForge`; upstream PRs target `dev`, and `main` only changes through `release/X.Y` branches.

## Commands

Windows (full toolchain: .NET 10 SDK, VS 2022 Build Tools C++ workload; engine builds also need HD2 installed via Steam):

```powershell
./scripts/Test.ps1                         # build solution + tests that need no game (what CI runs)
./scripts/Build-Engine.ps1                 # engine package + native/smoke tests (needs the game's steam_api64.dll)
./scripts/Build-Engine.ps1 -NoSteamApi     # same with a synthetic export table, no game needed (CI)
./scripts/Build-Editor.ps1 -Test -SkipOverlayTest   # in-game editor build + local_stats_test, no GPU (CI)
./scripts/Build-Mod.ps1 <mod folder> [-Install <installation>]   # compile a C/C++ mod with pinned Zig / check data & stats specs
./scripts/Publish-HellForge.ps1 -InstallRoot <folder>             # push a dev build into a real installation's app/
./scripts/Package-HellForge.ps1            # artifacts/package/HellForge-Setup.exe
Launch.cmd                                 # build and open the launcher from the repo
```

Linux (only the cross-platform parts; engine, editor and package need Windows):

```bash
./scripts/test.sh    # builds Launcher, Cli, tests in Release; runs both test projects, the lint, bash -n linux/*.sh
```

Individual pieces:

```bash
dotnet run --project tests/HellForge.Tests -c Release            # integration checks
dotnet run --project tests/HellForge.LocalBackend.Tests -c Release   # backend checks (on Linux use a short TMPDIR: unix socket path limit)
python3 scripts/lint/lint.py check --warnings-as-errors          # structure lint, as CI runs it (`py -3` on Windows)
python3 scripts/lint/lint.py scan --list --only <unit>           # violations for one unit (e.g. mod-loader, HellForge.Core)
python3 -m unittest discover -s scripts/lint/tests -v            # also scripts/engine-version/tests, scripts/nightly/tests
python3 scripts/engine-version/engine_version.py --base origin/dev   # which engine sources changed since EngineVersion was set
```

The two test projects are plain console programs (`Program.cs` + `Test("name", () => ...)` / per-file `Register(...)`
classes), not xUnit/NUnit: there is no filter flag. To run one check, temporarily narrow `Program.cs` or read the
`PASS`/`FAIL <name>` lines. Every game file in tests is synthetic. Tests needing Windows (DPAPI seals, NTFS junctions)
skip themselves on Linux. Some test-project files are compiled by linking sources from `src/` (see
`tests/HellForge.Tests/HellForge.Tests.csproj`), so moving those files breaks the tests.

## Rules that CI enforces

- `TreatWarningsAsErrors` is on for every project (`Directory.Build.props`), and Zig mod builds use `-Wall -Werror`.
- Structure lint (`scripts/lint/README.md`): cognitive complexity > 15, functions > 150 code lines, > 7 parameters,
  > 16 KiB fixed stack arrays in C/C++, ≥ 100 duplicated tokens, files > 1000 code lines. CI fails on warnings too.
  Known violations live in `lint-baseline.json`; suppress a reviewed one with `// lint-ok: R<n> <reason>` on the
  reported line. Tests, `bin/obj` and generated headers are not linted.
- Engine version: changing engine sources (`native/` except editor and launcher-stub, `src/HellForge.Core`,
  `src/HellForge.Session`, `src/HellForge.LocalBackend`, `src/HellForge.SdkShimGen`, `mappings/`, shipped scripts;
  not tests or `.md`) warns on a normal PR. Don't bump `EngineVersion`; write "engine bump with the next build" in the PR.
- `CHANGELOG.md`: player/modder/launcher-visible changes add a line under Unreleased (Added/Changed/Fixed, SDK changes
  under "For mod makers", CLI/settings under "For launcher developers"), written in players' words. Fixed is only for
  bugs the latest release has.
- Changing a `HellForge.Cli` command, option or JSON output requires updating `docs/external-launchers.md`.
- PR template: `.github/pull_request_template.md`.

## Hard content rules (CONTRIBUTING.md)

Never commit the game's files or anything derived from them (textures exported to PNG, data patches/diffs carrying game
bytes, decompiled code), Arrowhead/Sony artwork, or anything that bypasses the Steam ownership check, grants premium
content/Super Credits, or connects to official servers. Facts are allowed: addresses, byte patterns, struct layouts,
resource ids and field names (kept in `mappings/` and `native/mod-sdk/`), preferably produced by the `scripts/Export*.py`
exporters. `examples/mods/*/data/` and `build/` are game-derived and gitignored.

## Architecture

- **Launcher / Setup / CLI** (`src/HellForge.Launcher`, `src/HellForge.Setup`, `src/HellForge.Cli`) are built on
  **`src/HellForge.Core`**, the shared library for mods, staging, stats, game data, integrity and Steam checks.
  `HellForge.Cli` is also the build tool (`sdk-headers`, `build-data`, `supported-build`, `new-mod`) and the API that
  external mod managers use. `src/HellForge.SteamDownload` fetches the supported game build via SteamKit2.
- **Engine** (versioned separately as `EngineVersion`, zipped by `Build-Engine.ps1` with an `engine.json` of hashes):
  - `src/HellForge.Session` (NativeAOT): the session runner. It does the memory-only init/routing edits, stand-in DLL
    swaps, and starts the backend and the game. `EarlyConstructor.cs` was generated from
    `scripts/EarlyConstructorExperiment.cs` and then hand-edited, so don't regenerate it.
  - `src/HellForge.LocalBackend` (NativeAOT): serves the game's online services locally, plus co-op lobbies, relays and
    mod sync between HellForge players over Steam. `Test-BackendAot.ps1` requires the AOT build to answer byte-for-byte
    like the JIT build.
  - `native/mod-loader` (C + MinHook, a single translation unit: `hellforge_modloader.c` `#include`s the other `.c`
    files): loads code mods and applies registrations, the stats pass, native edits and live edits.
  - `native/sdk-shims` (PlayFab Party/Multiplayer stand-ins, generated by `src/HellForge.SdkShimGen`) and
    `native/steam-cloud` (the `steam_api64.dll` stand-in: cloud saves in the profile, starts the mod loader, network
    gate). `native/network-block` and the `scripts/*NetworkBlock*.ps1` hold the WFP block. Shipped PowerShell stays
    readable on purpose.
  - `native/common/hf_privacy.h`: native logs must not name the PC. Packaging fails if output contains the user
    folder or git email.
- **Mapping → SDK pipeline:** `mappings/*.json` (field offsets, catalog names, functions, globals) are the source of
  truth. `HellForge.Cli sdk-headers --repo .` generates `native/mod-sdk/hellforge_components.h`,
  `hellforge_content.h`, the loader's tables and `docs/sdk-reference.md`. Edit mappings and regenerate, never the
  generated files. Everything is pinned to one game build (`src/HellForge.Core/supported-build.json`, Steam build
  25480438). Engine hashes and the entity profile are checked against it in `ModTests`.
- **Mods** (`docs/internals.md`, `docs/modding/`): a folder with `hellforge-mod.json`, and optionally C/C++ sources
  under `src/` (built on every player's PC with pinned Zig, `x86_64-windows-gnu`, against `native/mod-sdk` only),
  `hellforge-data.json` (data spec, built from the local game copy into a cache), `hellforge-stats.json` (validated
  against the pinned stats schema), or manifest `native` edits. `native/mod-sdk/hellforge_mod.h` documents every SDK
  call. `templates/c-mod` is the `new-mod` template, and `examples/mods/` are reference mods
  (`machine-gun-10k-code` is the minimal one). SDK, template and examples are MIT-0, and the rest is GPL-3.0.
- **In-game editor** (`native/editor`): a bundled code mod (ImGui), built by `Build-Editor.ps1`, outside the engine
  package. `native/launcher-stub` is the top-level `HellForge.exe`.
- **Linux/Proton** support: `linux/*.sh` plus `native/linux-sandbox` and `native/linux-capture` (`docs/linux.md`).

Docs index: `docs/README.md`. Start contributor reading at `docs/engine.md` (parts, build, tests) and
`docs/internals.md` (load order, registries, stats pass, live editing, co-op).
