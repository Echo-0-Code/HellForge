# How HellForge implements mods

<!-- diataxis: explanation -->

This page is for people working on HellForge itself. It explains how the parts behind the [mod SDK](modding/README.md)
work, what they check before they change the game, and where the research behind them is. Mod authors do not need it:
the [modding guide](modding/README.md) covers everything a mod can do.

The mod loader is in `native/mod-loader/`, and the rest of the engine is described in [engine.md](engine.md). The
research notes and scripts named under `artifacts/` are not published. What they found is written here and in the
source.

This page covers staging, load order, source builds, the load chain, the registries, named fields, data spec builds
and the research behind the examples. Two more pages
hold the rest:

- [How Difficulty 11 is implemented](internals-difficulty.md): the level `add_difficulty` adds after Super Helldive.
- [Stats, live editing and events](internals-live.md): the stats pass, live editing, event sources and native edits.

## Contents

- [The mods folder and staging](#the-mods-folder-and-staging)
- [Load order](#load-order)
- [Source mod builds](#source-mod-builds)
- [The load chain](#the-load-chain)
- [Registries](#registries): [weapons](#weapons), [the game state's arrays](#the-game-states-arrays),
  [projectiles and damage](#projectiles-and-damage), [explosions, beams and arcs](#explosions-beams-and-arcs),
  [stratagems](#stratagems), [entities and behaviours](#entities-and-behaviours), [abilities](#abilities)
- [Difficulty 11](#difficulty-11) (its own page)
- [World calls](#world-calls) (their own page)
- [Named fields](#named-fields)
- [Data spec builds](#data-spec-builds)
- [Stats, live editing and events](#stats-live-editing-and-events) (their own page)
- [Research behind the examples](#research-behind-the-examples)
- [Where to go next](#where-to-go-next)

## The mods folder and staging

An installation loads these mods:

- every `<installation>/mods/<folder>/` that has a valid `hellforge-mod.json`, or holds such folders;
- every `<installation>/mods/<name>.zip` that holds one ([where it may sit](mods.md#where-the-mod-may-sit)).

Zips are unpacked into `mods/.unpacked/<name>-<content hash>` whenever mods are loaded, by the launcher's Mods page or a
session start. The mod is loaded from there. Deleting or replacing a zip removes its unpacked copy at the next load, so a
zipped mod is managed through its zip alone. Zips with absolute or `..` paths are refused: their files would land
outside the mod's folder. There is no limit on size or file count.

A zip and a folder with the same files give the same modlist code. A zipped mod counts by what its zip holds. Files it
writes into its unpacked folder as it runs (key bindings, a cache) are each player's own and never split friends. A mod
kept as a loose folder counts by every file in it, so share mods as zips.

Each session stages the data mods into the copied game, loads the code mods into it and applies the declarative native
edits while the game runs. Nothing is changed in the normal game.

Staging places only what changed since the last session: a patch staged from the same file (same size and time) stays.
For the same reason, `mods/.unpacked` keeps the zips' hashes and the files they ship (`.zip-hashes.json`,
`.zip-files.json`). Staged patches are hard links where possible ([disk space](mods.md#disk-space)).

The session's `result.json` lists what loaded (`mods`) and the status of network edits (`nativeMods`).
`native-mods.log` records network edits, and `mod-<id>.log` records stats and legacy component edits.

## Load order

The bottom mod on the Mods page loads first and the top one last, as in the community's mod managers. Where two mods
change the same thing, the higher one wins: its data patches get the higher numbers, and its stats and its code's edits
apply later. Sessions load and stage mods in this order (`ModCatalog.LoadOrder`).

On the Mods page, rows are dragged from anywhere on them within their group, or moved with ⋮ Move up / Move down. The
page follows the mods folder: a mod copied in or deleted there shows at once. Mods new to the page go to the top of
their group. The editor's Local Stats stays at the top, so what you edit in game applies last.

The modlist code compares the gameplay mods, not their order, and leaves out the cosmetic group. So friends who order
the same mods differently can still play together. That is theirs to sort out: a conflict goes to a different mod on
each machine.

What mods add is numbered alike whatever the order: stratagems, weapons, entities, projectiles, damage types, abilities
and behaviours. Code mods are set up in the order of their ids, the same on every machine, and added entities are
numbered by their resource ids.

## Source mod builds

A code mod that ships its sources (C or C++ under `src/`) is built by HellForge, not by its author. Before every session
that has it enabled, `HellForge.Cli mods-stage` gets the build into `<installation>/temp/mod-builds/<mod id>/`. It uses
the SDK's pinned Zig and the same flags on every machine, and the loader loads that build. A DLL in the mod's own folder
(an IDE's build, say) is not used. The code is in `src/HellForge.Core/ModBuilds.cs`.

On Linux the session runs under Wine, where the pinned `zig.exe` does not work (issue 81). So the wrapper runs a native
build agent before it enters the namespace: `app/cli-linux/HellForge.Cli build-source-mods --install <installation>`,
which calls the same `ModBuilds`. The session finds the build ready and does not compile. A source mod without a build
there is reported and stays off, never compiled with `zig.exe` under Wine.

A session after a mod sync (`--mod-sync`) plays the host's mods from the synced store. The wrapper passes `--sync yes`
and the session's `--sandbox`, and `ModBuilds.ForSession` adds those mods, resolved as `mods-stage` resolves them.

### The cache

A build is kept under its key, a hash of:

- the mod's manifest and every file under `src/`;
- the SDK's headers;
- the compiler's identity (`zig 0.17.0 / x86_64-windows-gnu`, not its archive) and flags;
- the game build.

A change to any of them builds the mod again. So a mod follows a game update by itself, as long as the names it uses
still exist. The "hf-source-key-v2" salt marks the identity scheme: a native Linux build and a Windows session of the
same sources and pin share one key.

### The check

`record.json` beside the build holds its key, the sources' hash and the DLL's SHA-256. An optional compiler and host say
which build made it. A session uses the build only when all three still match. A stale, corrupted or swapped DLL is built
again and never loaded. This guards against accidents and casual swaps. It does not guard against someone with access to
the installation, who could edit the sources or the record too.

### Errors

A mod that does not compile stays off for that session, and the session still runs. The record keeps the compiler's
message (file and line), which the launcher's Mods page shows on the mod's card. The same sources are not compiled again.
Build now in the mod's ⋮ menu (or `HellForge.Cli build-mod --mod <mods>/<mod>`) compiles at once, even unchanged
sources. On Linux it runs the native agent. Zig's own failures are never kept
([damaged mod tools](modding/tools.md#how-hellforge-builds-mods)).

### Tools and friends

The first source mod fetches the pinned Zig into `<installation>/tools` (checksummed) if it is not there. That is
`zig.exe` on Windows and `zig-linux/zig` natively on Linux, both pinned in `app/sdk/tools.json`. The modlist digest takes
a source mod's sources, not a DLL, so friends who play together build identical mods.

### Prebuilt DLLs

A mod can ship its DLL instead: closed source, or built with other tools and libraries. If it ships sources as well, it
sets `"prebuilt": true` in `code` so that HellForge loads the DLL rather than building it. A prebuilt mod names the game
build it was built for, `"game": "25480438"`. One built for another build is refused with a message saying so. Without
`"game"`, a prebuilt DLL still loads (mods from before this rule).

## The load chain

Every session runs `HellForge.Cli mods-stage`, which writes the plan `modloader-plan.txt` into the session folder: build
pins, entity profile, and the mods in load order. The session then sets `HELLFORGE_MODLOADER` and
`HELLFORGE_MODLOADER_PLAN` for the game. This happens with or without code mods, because the loader also collects the
read-only stats snapshot (see [The stats pass](internals-live.md#the-stats-pass)).

In the game's process:

1. The `steam_api64.dll` stand-in, the first HellForge code there, starts `hellforge_modloader.dll` on its own thread.
2. The loader loads each mod and calls its exported `hellforge_mod_init(const HfApi*, HfMod*)`. It sets mods up in the
   order of their ids (the same on every machine), and their edits apply in the Mods page's load order.
3. It watches the game start and delivers the load events on its watch thread (`native/mod-sdk/hellforge_mod.h`). They
   come in the order mods registered for them, never two at once.

[modding/concepts.md](modding/concepts.md) says when each load event comes, and [modding/runtime.md](modding/runtime.md)
covers the events mods can subscribe to.

## Registries

When a mod adds a weapon, projectile, damage type, stratagem, entity, behaviour or ability, HellForge changes the game's
data. This section explains what it does, what it checks first, and how each one behaves in co-op. The mod's side of all
this is in [modding/content.md](modding/content.md).

### Weapons

HellForge adds the weapon to:

- the game's entity data when that loads (the file is never changed);
- the engine's network types, so it can be equipped and synced;
- the armory;
- the backend's item catalogue and the player's inventory (`registered-items.json` in the session folder, or its `game`
  folder when sandboxed).

The loader's `modloader.log` and the mod's log record each step, and `native/mod-loader/registry.c` explains how.

**Mastery.** The primary a player carries gains the mission's XP, won or lost, the same number as the player's own. A
retried `Mission/end` pays the weapon nothing, as it pays nothing else.

- `Mission/end` names the primary's package in each participant's `progressedPackages`, one
  `{id32, factor: 1}` entry.
- The reply's `progressionPackageUpdates` gives the package's level and progress after the mission, at most 15 entries
  of `{platformAccountId, id32, level, progress}`.
- The client refuses the whole reply for an account that is a number, for a missing `id32`, `level` or `progress`, and
  for a `rewards` that is not an object. So no `rewards` is sent.
- A weapon has 25 levels. Each level's `requirement` counts on its own, and `progress` is the XP towards the next level.
- The first level has another kind of requirement, which the client never reads, so it passes at the first mission. It
  gives the weapon's default attachments.

A mod weapon's package is its own (`WeaponMastery.cs`, `RegisteredItems.cs`). It sells at its base's prices, against its
own level, and what it gives is its own child items, saved with its progress. That is
`progression.modPackageProgress`, which the client is never served: the backend turns it into `packageProgress` and
inventory entries for the weapons this session has.

A new copy starts at level 0 holding no attachments. Its ids follow from its unit name, so a mod added back finds its
level.

In multiplayer, the weapon is part of the mod's code, which the modlist gate already requires friends to share.

**Network types past 2,048.** Every weapon and entity a mod adds takes a network type, a copy of its base's. The game
sends a type in 11 bits, so 2,048 types fit, 1,855 of them vanilla. Two things hold that width: the network
configuration's width byte, and network field type 431, which names a corpse's source type. At boot the entity map
limits them.

For a mod set with more types than fit, the loader widens both to 12 bits (4,096 types) when it adds the types. It does
so only then: a set that fits keeps the game's own wire format. Every player in a game runs the same mods, so every one
widens or none does. If the widths are not the ones reviewed, or the set needs more than 4,096, the first types that fit
get one and the rest are logged (`registry_load.c` `AddNetworkTypes`).

For many sets other limits come first, each refused on its own: a component table's compiled slots (below) and the
game's 4,096 catalogue items.

**Growing component tables.** A component table is an index of S rows `{entity, record, 0}`, probed from `entity % S`,
then its records. Its slot count S is compiled into every lookup of it:

- the probe bound;
- the wrap;
- the modulus (a multiply by a magic number and a shift, or a mask);
- where the records start;
- the default record's address.

Before the weapons are added, the loader counts the rows they will take in each table. A table they would fill past three
quarters is built again at about twice the rows (an S its constants can hold). Every constant that encodes its S is
rewritten with the game's other threads paused. That is the same pause in which the game is pointed at the new table
(`table_growth.c`, after a design by CowboyBingus). Each instruction is checked against its reviewed bytes first. If one
differs, nothing is written and the table stays as it was.

Only tables whose every site is known grow: the 16 that weapons and stratagems use, from a static scan of game.dll build
25480438 (`table_growth_sites.h`, generated by `scripts/ExportTableGrowth.py`). Among them is the loadout index, table
29, which also grows only while HellForge's armory builder replaces the game's. A table the entity loader makes again at
its vanilla size is built again at the grown size before the game reads it.

Tables 233 (Equipment) and 321 (ProjectileWeapon) also appear, as data, in helldivers2.exe's protected code, which was
not analysed. The log says so when they grow. Any other table that is full refuses the entities that do not fit, as
before.

### The game state's arrays

The progression arrays sit inside one large state object, sized for what the game ships and reached by displacements
compiled into every access. `native/mod-loader/state_moves.c` allocates the state larger, puts the arrays past its old
end (a fixed distance from P) and rewrites every displacement, listed in `state_move_sites.h`.

- **Package progress slots.** 256 slots of 0x18 bytes at P+0x4D4 become 1,024. One memset of P+0x4B4..P+0x1CE0 resets
  them, and a stub clears the new slots after it.
- **The item catalogue.** Count, definitions, hierarchy, category starts, index and statuses, P+0x1CE0..P+0xD5D50, move
  as one block, each field at its place for 40,960 items.
- **Capacity.** The caps, parse buffers and reset follow in the same writes, all or none. The backend serves more than
  4,096 items only when told the move happened (`capacity` in `registered-items.json`).
- **game.dll still decoding.** WinLicense decodes upward past the pin, so a site above it can briefly read as encoded.
  The move then retries each tick and holds setup_game (+0x4EE160, which allocates the state) until it
  moves or refuses. `HELLFORGE_MOVE_UNDECODED=n` fakes n such tries.
- **The guard.** With `HELLFORGE_MOVE_GUARD=1` the old slots hold a canary and the old catalogue's pages are no-access:
  a vectored handler logs each access once, by its address. Every such line is a site the list missed.

### Projectiles and damage

A weapon gets its own projectile through `HfWeapon.projectile`. This gives the weapon its own
`ProjectileWeaponComponentData` record naming the type (+0), and removes the default ammunition from its own
customization record. Ammunition attachments carry an entity delta that sets a weapon's projectile when it is assembled.
The Liberator's default Full Metal Jacket sets 276. Without one, the weapon fires what its record names, as vanilla
weapons without ammunition do.

A new projectile is a copy of the base's 272-byte settings row under a type after the game's list. The game's types are
0-350, and all mods together can add 512 new types. The list has room for only 3 more where it is. So the loader moves it
to a larger copy within 2 GB of game.dll and rewrites the 66 instructions that address it, checking each one first
(`native/mod-loader/projectile_sites.h`, generated by `artifacts/projectile-registry/projectile_sites.py`). Then it
raises the game's two type bounds.

The projectile's damage is the DamageInfo id at row +0x3C. `projectile_row(type)` returns the row after
`HF_EVENT_PROJECTILES_READY`, and +0 is the type and must stay. `set_field` / `hf_set` change a mod's own row as they
change an entity's record. For example, `hf_set(mod, type, ProjectileSettings, impact_explosion, HF_U32(82))` makes its
hits burst into the Orbital Gas Strike's cloud. HellForge loads an explosion's packages itself.

The game's DamageInfo list (ids 0-649) has no spare room. So the loader moves it to a larger copy within 2 GB of game.dll
and rewrites the 489 instructions that address it, checking each one first (`native/mod-loader/damage_sites.h`,
generated by `artifacts/projectile-registry/damage_sites.py`). New rows are copies of their base with the damage scaled.
`damage_row(id)` returns one after `HF_EVENT_PROJECTILES_READY` for further changes: +4 standard, +8 durable, +0x0C four
armour penetration values.

Every assumption is checked against the reviewed build once the lists have loaded: the lists, their loaders, the bounds
and every rewritten instruction. Any difference adds nothing and logs why, and weapons then fire their base's projectile.
The research is in `artifacts/projectile-registry/FINDINGS.md`, and the code in `native/mod-loader/projectiles.c` and
`damage.c`.

In multiplayer, every machine fires its own copy of a hand-held weapon's shots from its own data. So friends with the
same mods (the modlist gate) fire the same projectile. Types and ids are numbered in the mods' set-up order (by mod id),
the same on every machine whatever its load order.

`HfWeapon.sound_from` gives the weapon a vanilla weapon's firing sounds, named by its loadout id, such as
`HF_ITEM_LAS16_SICKLE`. The weapon's own `ProjectileWeaponComponentData` record takes the donor's sound events: shot,
loops, fire modes, secondary, silenced. The donor's sound banks must load with the weapon, so the mod's data puts them
in the weapon's archive patch. `examples/mods/talon-liberator` does this with the Sickle's two banks.

No row member is a colour. A projectile's look is its particle effects: `spawn_effect` (row +0x48) draws the LAS-58
Talon's bolt, and `flight_effect` (row +0x70) its light and trail. An effect's colours are curves over the particle's
life (10 key times, then 10 RGB keys on a 0..255 scale), not its materials or textures. A recoloured effect therefore
has to come as data, in an archive the weapon loads, as the gold Liberator's model does. `examples/mods/talon-liberator`
turns the Talon's yellow curves blue.

### Explosions, beams and arcs

| List | game.dll (build 25480438) | Slots | Row | Loader | Instructions | Bounds |
| --- | --- | --- | --- | --- | --- | --- |
| Explosions | +0x37CC920 | 423 | 152 bytes | 0x11DC320, clears 0xD38 bytes | 47 (one, 0x11DC554, has no `.pdata`) | 38 |
| Beams | +0x37C8250 | 31 | 112 bytes | 0x11D7E00 | 22 | none |
| Arcs | +0x37C5F90 | 17 | 104 bytes | 0x11D7AE0 | 60 | none |

These move as the damage list does (`explosion_sites.h`, `beam_arc_sites.h`). An explosion bound left behind gives the
default row, so all rise. Beams and arcs have no bounds: an unfilled slot crashes, so only installed types reach a record.

An expiry explosion travels in network field type 248, 9 bits. Up to 89 new types keep that format. A larger set widens
it to 10 bits (the field row's +13) on every machine, which all run the same mods.

## Stats, live editing and events

[Stats, live editing and events](internals-live.md) has the rest of the machinery:

- [The stats pass](internals-live.md#the-stats-pass): how stats files are validated and written at load.
- [Live editing](internals-live.md#live-editing): how fields change while the game runs, the in-game editor and the
  live pipe.
- [Event sources](internals-live.md#event-sources): where the events mods subscribe to come from.
- [Native edits](internals-live.md#native-edits): network field types and legacy component writes.

## Research behind the examples

`examples/mods/machine-gun-10k` gives the M-1000 Maxigun's backpack 10,000 rounds. HD2Runtime's research
(`local_research/events/maxigun/deposit-limit-notes.md`) found why no Lua mod can. A backpack's live ammo is the network
field `remaining` of type `deposit_value` {int, 10 bits, min 0}. So the engine clamps it to 1,023 at the first shot after
any value above that.

The mod widens `deposit_value` to 14 bits (up to 16,383). It sets the backpack's `DepositComponentData` (record 23 of
resource `0x056DE1C5E21E723E`) capacity and start amount to 10,000 and refill to 5,000. The HUD maximum is the start
amount, so it shows 10,000.

`examples/mods/golden-liberator` is a gold-plated copy of the AR-23 Liberator as its own arsenal weapon. Its
`hellforge-data.json` describes the model: the Liberator's unit under the mod's own name, pointing to a copy of the body
material. That copy's colour lookup texture is gold metal in every zone: every zone takes the bare-metal zone's values.
HellForge builds it from each player's game copy.

`examples/mods/talon-liberator` does the same in blue. It adds the blue copies of the Talon's bolt and flight effects,
everything they use, and the Sickle's sound banks. Both build byte for byte what the research scripts
`artifacts/registry/build_gold_liberator.py` and `build_talon_liberator.py` built.

## Where to go next

- [internals-difficulty.md](internals-difficulty.md): how the Difficulty 11 level is added.
- [internals-live.md](internals-live.md): the stats pass, live editing, event sources and native edits.
- [engine.md](engine.md): the engine's parts, building and tests.
- [sdk-roadmap.md](sdk-roadmap.md): what is planned for the SDK.
- [engine-roadmap.md](engine-roadmap.md): network isolation and co-op sync plans.
- [multiplayer.md](multiplayer.md): how co-op works.
- [sdk-reference.md](sdk-reference.md): every SDK call, component, table and constant.
