# How the world calls work

<!-- diataxis: explanation -->

This page is for people working on HellForge itself. It explains what the SDK 21 calls read and call in the engine.
The mod's side is in [modding/world.md](modding/world.md), and the rest of the mod machinery in
[internals.md](internals.md).

## What each call uses

The SDK 21 calls (`world_calls.c`) call the engine's own C API, the table at game.dll+0x3326308, as the game's Lua
bindings do. Each slot and the first bytes of its function are checked before the first call.

| Call | Reads or calls |
| --- | --- |
| `play_effect` | the World table (+0x8): create particles (+0x278), link them to a unit (+0x338); the Unit table (+0x18): node, pose, alive |
| `stop_effect` | the World table: stop spawning (+0x2A8) or destroy (+0x298). An id that already ended is ignored |
| `find_enemies` | the health manager's records (game.dll+0x3326688) and the faction manager's masks (game.dll+0x3326CB8) |
| `armory_weapon` | the menu object (game.dll+0x347CE38): its state at +0, the customize screen at +0x158, its weapon at +0x10 |

- Effects play in the gameplay world, the Game's +0x10E8. A particles resource must pass the Application table's
  can-get check (+0x388) first, as an unloaded one crashes the engine.
- An effect is created at the node's world pose and linked with the "destroy" orphan policy. The unit's death then
  ends it.
- The link call does not check its unit, so the unit's alive check comes first.
- A shared effect travels on the mod channel under the id `@hellforge.effects`, which no mod id can take. It names
  the entity by network id; the receiver finds it with `entity_of_network` and plays the effect on the game thread.

### Stratagems

The game's stratagem list (`game.dll + 0x37CB600`) holds exactly the 150 StratagemTypes its enum names. HellForge moves
it to a larger copy once the game has loaded its stratagems, as it does the damage list. The list is addressed by 339
instructions, and 69 bounds on the type grow by the number of new stratagems. The names the mission report reads by type
move with it. Every instruction and bound is checked against the reviewed game build first, and anything else adds no
stratagem. The review is in `artifacts/stratagem-registry/BOUNDS.md` (not committed).

All mods together can add 1,024 new stratagems; past 256 types their co-op field widens to 16 bits. The picker shows
256 cards, about 160 new ones. A new stratagem:

- does not load its base's ship-upgrade (level) packages, since the loadout keeps one per vanilla type;
- never counts in the mission statistics;
- is never a mission's random pick.

To make it owned, the local backend lists each new stratagem for the session as a copy of its base's catalogue item
(level, price, traits) and its child under the new stable id. It adds it to the inventory too. Nothing is saved to the
profile, and a saved loadout that names a removed stratagem drops it.

A stratagem's code must be neither another stratagem's code nor a prefix of one, and must not have one as a prefix.
`artifacts/stratagem-registry/codes.py <code>` lists the vanilla ones it collides with.

Its icon is a data spec `icon` resource in the stratagem icons archive `007e093ca718ca1a` (see
[Data specs](modding/data.md#data-specs)), as in `examples/mods/stratagem-compendium`. A missing icon material crashes the
game, which is why the spec always builds both. The image follows the stratagem pack's format: red where the icon takes
its category's colour, green where it is white. The in-game editor draws it that way too.

In multiplayer, peers exchange stratagem types as numbers, so every player must run the same mods. The modlist gate
ensures it, and types are numbered in the mods' set-up order (by mod id), the same on every machine.

`payload` / `payload_count` name what a call spawns, and `package` what it loads for the mission. A Pelican drop (a base
of delivery kind 3, such as `HF_STRATAGEM_EXO45_PATRIOT`) spawns `payload[1]` carrying `payload[0]`. `{0, pelican}`
sends an empty Pelican.

The game names a stratagem "\<designation> \<title>" on the permit. The designation is its payload entity's, since a
stratagem row has none. So a copy of the A/MG-43 sentry whose sentry is a copy too reads "A/MG-43 \<title>". That holds
until the sentry entity is given text of its own (`HfEntity` / `HfEntityDef` `designation` and `title`, SDK 7). The
compendium's HMG sentry names its sentry "A/MG-101" and its stratagem "Heavy MG Sentry".

In the in-game editor (SDK 7), every custom stratagem gets a page under its category. It has its own icon, the PNG its
data spec builds the icon from. The page is a vanilla stratagem's (`editor_like`; 0: its base's) pointed at the
stratagem's own data. That means:

- its own settings row;
- for each vanilla entity on that page, the mod's entity standing for it;
- that entity's projectile and damage, where the mod copied them from the page's. This takes one copy of each base: two
  of the same base are a guess, so their fields are left out.

`editor_entities` names the entities, as `{entity, like}` pairs ending in `{0}`. By default, each payload entity a mod
added stands for the vanilla entity it copies. Fields with nothing of the mod's to point at are left out, so the page
never edits the base. A shared record is refused as for any live edit (give the entity `own_all` to edit everything).

The page's edits are saved in the editor's own `mod-stats.json`, not in Local Stats (the stats pass knows no mod
content). The editor applies them as live edits at every session's start. In co-op the host's go with its Local Stats. A
joiner's editor takes its own off and applies the host's (`host-mod-stats.json`, which its backend writes), and puts its
own back when the host goes. Tools read the same through `content` (`HfContent`): every stratagem, entity, projectile
and damage type mods added, with what it copies.

For selection headings, [stratagem-categories.md](stratagem-categories.md) has the implementation notes and acceptance
checks. Controller selection and saved loadouts still need in-game validation.

### Entities and behaviours

`own_all = 1` makes a copy share nothing. It is set on `HfEntity`, or on `HfEntityDef` with `size = sizeof(HfEntityDef)`
(SDK 7). The copy gets a record of its own in every component of its base, each a copy of the base's. An edit of the base
(a stats mod, the in-game editor) never changes the copy, and the copy's edits never change the base.

HellForge sizes each record from the profile's component, or from the table's shape among every table of the build's
entity data. The shape is the table's index slots and its size, from HD2Runtime's component catalogue
(`native/mod-loader/component_sizes.h`). A table it cannot size for certain stays shared, and the mod's log names it.
Every component of the A/MG-43 sentry sizes, and `examples/mods/stratagem-compendium` builds every stratagem this way.

`add_behaviour` adds a behaviour: a vanilla behaviour that HellForge extends, under an id of its own (694 and up). An
entity runs it through `HfEntity.behaviour`, and its own `BehaviorComponentData` record names it. The game's behaviour
system dispatches it like its own. The behaviour system has six per-behaviour switches: update, stage change, event,
stage count, and the two network event mappings. Each sends ids above 693 to HellForge, which runs its behaviours, and
sends every other id where the game always did.

The one base so far is the transport Pelican's (`HF_BEHAVIOUR_PELICAN_TRANSPORT`, 667). It flies to the beacon and
hovers. Its HellForge version holds there for `hold_seconds` instead of 6. Meanwhile it circles the beacon
(`orbit_radius`, `orbit_height`) and turns toward its mounted turret's target (`face_target`), then departs. Everything
else is 667's own code. If HellForge cannot install its behaviours (on another game build), entities given one run their
base's. The review is in `artifacts/pelican-registry/behaviour/DISPATCH.md` (not committed).

`examples/mods/stratagem-compendium` (`src/pelicans.c`) uses all of it, as definition blocks
([modding/content.md](modding/content.md)). It has two Pelicans with turrets of their own on that behaviour, called in by
two new stratagems. The Gatling's turret fires the MG-206's armour-piercing rounds at the Gatling Sentry's 1600 RPM.

### Abilities

A silo countdown (`flags = HF_ABILITY_HOLDS_LINKED_LAUNCH`, no base) is for a copy of a silo's targeting remote, named in
its fire modes. Firing the remote holds the launch of the missile it is linked to for `hold_seconds`. The countdown runs
on the missile, so it goes on once the remote is tossed. Then the missile launches at the point the laser marked last.
While the laser stays on, the missile follows it, as the silo's does. The compendium's Shredder Silo
(`src/shredder_silo.c`) is the example.

HellForge does what the remote's link does at a launch through the game's own functions, each checked against the
reviewed build. These are the engine, the explosive set off, the owner, the guidance target and the rack letting go. If
one differs, the remote launches at once, as the silo's does. This is experimental (2026-10-18). If the laser launches
the missile before the remote's fire runs, there is no countdown, and `modloader.log` says so.

The game's tick dispatcher (game.dll 0x11509E0) runs an ability 60 times a second from tick 0 until it reports it is
done. Its ids end at 2867. HellForge points the dispatcher's default branch at a stub. The stub hands HellForge's own ids
to the loader (`native/mod-loader/abilities.c`) and every other id on, as it does for behaviours. A HellForge ability
reports "running" through its hold, then calls its base with the tick less the hold.

Sounds play in the world at the entity, on its unit's sound source, as the game's weapon sounds do. A silo countdown's
sounds play at the silo. With no unit, they play on the world's default source, heard everywhere. On machines where the
countdown's start is replicated it plays its sounds too (not yet seen in a two-PC session). A sound whose bank is not
loaded plays nothing, and `package` loads it (a package of HellForge's catalogue). `modloader.log` says when an ability
is set off and when its base runs.

In multiplayer, the silo, its missile and its remote belong to the machine that called the silo in. The countdown runs
on every machine its start reaches. Since engine 0.1.21, only the machine with authority over the missile at the last
tick launches it, as with the game's own silo. The launch reaches the others with the missile's state, and they keep the
link marked until it arrives. Engine 0.1.19 launched on every machine, which set the explosive off once per machine.

The remote's own fire runs on the machine with authority over the remote. The comments in
`native/mod-loader/abilities.c` have the details. They include the guard for a remote left attached to nothing when the
missile lands.

## Difficulty 11

[How Difficulty 11 is implemented](internals-difficulty.md) explains how `add_difficulty` adds a level after Super
Helldive.

## Named fields

SDK 6 names the fields of component records, so a mod never writes an offset. How they grow is in
[sdk-roadmap.md](sdk-roadmap.md#mappings). One source names them:

- `mappings/components.json`: per component, its index, record size, the game build whose layout it is, and fields.
  Each field has an offset, type, count, meaning, source and whether the game confirmed it.
- `mappings/catalog.json`: vanilla content by name.

`HellForge.Cli sdk-headers --repo .` generates these files from them:

| File | What it holds |
|---|---|
| `native/mod-sdk/hellforge_components.h` | a packed struct per component record (`HfProjectileWeapon`, `HfWeaponData`, `HfDeposit`...): named fields at their offsets, the rest as `_unknown_XX` padding, size and offsets asserted. Also field groups (`HF_GROUP_...`), and structs for the settings tables' rows (`HfDifficultySettings`, `HfStratagemSettings`, `HfProjectileSettings`, `HfDamageInfo`) |
| `native/mod-sdk/hellforge_content.h` | vanilla content constants, each commented with its catalog name: every armory item and its entity (`HF_ITEM_...`, `HF_ENTITY_...`), every stratagem (`HF_STRATAGEM_...`), projectiles, behaviours, damage, packages, tags, stratagem traits (`HF_TRAIT_...`), categories and beams. Also the kinds `find` takes (`HF_KIND_...`) |
| `native/mod-loader/sdk_fields.h`, `sdk_content.h` | the loader's tables of the same fields (which a mod's fields must match) and content (which `find` resolves) |
| `docs/sdk-reference.md` | the SDK reference: the API from `hellforge_mod.h`'s comments, and every component, table and constant |

`hellforge_mod.h` includes both SDK headers. Edit the mappings, never the generated files: the integration checks fail
when they differ. Naming a researched range is one entry in the mappings. The padding around it splits, nothing else
moves, and mods built before keep working.

## Data spec builds

A mod's data spec is checked and built from each player's game copy. Everything is checked before anything is written:

- every resource the spec names must be in the game data (all missing ones are listed);
- every edit must find what it expects;
- new names must be new.

When a session stages mods, the data is built from the installation's game copy into
`<installation>/temp/mod-data/<mod id>/<key>/`. It is reused while the key holds. The key covers the data builder's
version, the supported game build, the game's `bundles.nxa`, the spec and its input files. A mod whose data cannot be
built is not loaded, and the session's result lists why.

The modlist code counts the spec and the builder version, not the generated files. With the same game build they make
the same bytes on every machine.

To build or check one by hand, run `HellForge.Cli build-data --mod <folder>`, which checks the spec. Add
`--install <folder>` to build into that installation's cache, or `--game-data <game/data> --out <folder>`.
`scripts/Build-Mod.ps1` runs it for mods that have a spec (`-Install` or `HELLFORGE_INSTALL` to build). The code is in
`src/HellForge.Core/GameData/`.

## Where to go next

- [internals.md](internals.md): the rest of how HellForge implements mods.
- [multiplayer.md](multiplayer.md): how co-op works, including the mod channel.
- [sdk-reference.md](sdk-reference.md): every SDK call, component, table and constant.
